/*============================================================================

  Copyright (c) 2015-2016 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/
#include <sys/mman.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <dlfcn.h>

#include <linux/ion.h>
#include <linux/msm_ion.h>
#include <media/msmb_camera.h>

extern "C" {
#include "mct_controller.h"
#include "mct_pipeline.h"
#include "mct_module.h"
#include "mct_stream.h"
#include "modules.h"
}

#include "camera_dbg.h"
#include "test_manager.h"

#undef LOG_TAG
#define LOG_TAG "mm-camera-test-manager"

//#define TM_DEBUG_XML

#define TM_FILL_SUB_ENTRY_INT(stkt, field)    \
  (stkt.field = getInt(pNode, (char *)""#field""))
#define TM_FILL_SUB_ENTRY_FLOAT(stkt, field)  \
  (stkt.field = getFloat(pNode, (char *)""#field""))
#define TM_FILL_SUB_ENTRY_STRING(stkt, field) \
  (getString(pNode, (char *)""#field"", stkt.field))
#define TM_FILL_SUB_ENTRY_TYPE(stkt, field, typecast)    \
  (stkt.field = (typecast)getInt(pNode, (char *)""#field""))
#define TM_FILL_SUB_ENTRY_INT_ARRAY(stkt, field, size) \
  (getIntArray(pNode, (char *)""#field"", (int *)stkt.field, size))


#define TM_FILL_ENTRY_INT(stkt, field)    (stkt->field = getInt(pNode, (char *)""#field""))
#define TM_FILL_ENTRY_FLOAT(stkt, field)  (stkt->field = getFloat(pNode, (char *)""#field""))
#define TM_FILL_ENTRY_STRING(stkt, field) (getString(pNode, (char *)""#field"", stkt->field))
#define TM_FILL_ENTRY_TYPE(stkt, field, typecast)    (stkt->field = (typecast)getInt(pNode, (char *)""#field""))

#define TM_FILL_ENTRY_TYPE_ARRAY(stkt, field, typecast, size) \
  (getIntArray(pNode, (char *)""#field"", (typecast)stkt->field, size))

#define TM_FILL_ENTRY_CHAR_ARRAY(stkt, field, size, size2) \
  (getCharArray(pNode, (char *)""#field"", &stkt->field[0][0], size, size2))

#define TM_FILL_ENTRY_INT_ARRAY(stkt, field, size) \
  (getIntArray(pNode, (char *)""#field"", (int *)stkt->field, size))

const str_types_t str_types_lut[] = {
  { "preview",  CAM_STREAM_TYPE_PREVIEW },
  { "postview", CAM_STREAM_TYPE_POSTVIEW },
  { "video",    CAM_STREAM_TYPE_VIDEO },
  { "snapshot", CAM_STREAM_TYPE_SNAPSHOT },
  { "offline",  CAM_STREAM_TYPE_OFFLINE_PROC },
  { "analysis",  CAM_STREAM_TYPE_ANALYSIS }
};



static mct_module_init_name_t modules_inits_lut[] = {
  {"sensor", module_sensor_init, module_sensor_deinit, NULL},
  {"iface",  module_iface_init,  module_iface_deinit, NULL},
  {"isp",    module_isp_init,    module_isp_deinit, NULL},
  {"stats",  stats_module_init,    stats_module_deinit, NULL},
  {"pproc",  pproc_module_init,  pproc_module_deinit, NULL},
  {"imglib", module_imglib_init, module_imglib_deinit, NULL},
};

using namespace tm_interface;

#ifdef TM_DEBUG_XML
typedef void (*tm_xml_nodes_traverse)(xmlNodePtr pNode, char *fmt_str);
#endif

Test_Manager::Test_Manager()
{
  streams.num = 0;
}

boolean Test_Manager::executeTest(tm_intf_input_t *pInput)
{
  boolean                  rc = FALSE;
  boolean                  main_ret = FALSE;
  TM_Interface            tmi;
  TestSuite               *tst_suite;
  superParam              *sup;
  uint32_t                cnt;
  uint32_t                session_id;

  memcpy(&m_input, pInput, sizeof(tm_intf_input_t));
  tmi.putInput(&m_input);
  p_tmi = &tmi;

  session_id = m_input.session_id;

  fillSessionStreamIdentity(session_id, MCT_SESSION_STREAM_ID);

  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    fillStreamIdentity(session_id, cnt + 1);
  }
  tmi.setParmStreamIdentity(parm_stream_identity);

  sup = new superParam(m_input.is_first_module_src);

  /* Create a test suite instance */
  tst_suite = new TestSuite(tmi, m_input.lib_name);
  if (!tst_suite) {
    delete sup;
    goto EXIT1;
  }
  if(m_input.print_usecases) {
    tst_suite->listUCNames();
    main_ret = TRUE;
    goto EXIT2;
  }

  num_modules = getNumOfModules();
  if (0 == num_modules) {
    goto EXIT2;
  }

  rc = modulesAssignType();
  if (FALSE == rc) {
    goto EXIT2;
  }

  rc = modulesInit();
  if(FALSE == rc) {
    TMDBG_ERROR("modules init fails");
    goto EXIT2;
  }
  tmi.setModulesList(modules);

  // if there is no sensor test application should prepare chromatix
  // and send it to modules
  if (NULL == tmi.getModule("sensor")) {
    tmi.loadChromatixLibraries();
  }
  /* open ion device */
  rc = tmi.openIonFD();
  if (FALSE == rc) {
    TMDBG_ERROR(" Ion device open failed\n");
    goto EXIT3;
  }

  // if need to load data from input file
  if (m_input.read_input_from_file == TRUE) {

    TMDBG("read data from file\n");

    rc = tmi.allocateAndReadInputBufXml();
    if( FALSE == rc) {
      goto EXIT4;
    }
  }
  /* Load reference image for validation purposes */
  rc = tmi.allocateAndReadRefImage();
  if( FALSE == rc) {
    goto EXIT5;
  }
  rc = tmi.allocateFillMetadata(&streams);
  if( FALSE == rc) {
    goto EXIT6;
  }

  if (m_input.use_out_buffer == TRUE) {
    // if tested module doesn't support write in HAL buffers
    // will write in native buffer
    for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
      /* Create output buffers */
      rc = tmi.allocateOutputBufs(cnt);
      if( FALSE == rc) {
        goto EXIT7;
      }
    }
  }
  rc = tst_suite->createDummyPorts();
  if (rc == FALSE) {
    TMDBG_ERROR(" creating dummy ports\n");
    goto EXIT9;
  }

  /* Create pipeline */
  rc = createPipeline();
  if (FALSE == rc) {
    TMDBG_ERROR("creating pipeline");
    goto EXIT12;
  }
  tmi.setPipelineInstance(pipeline);

  rc = createSessionStream();
  if (rc == FALSE) {
    TMDBG_ERROR(" error creating session stream\n");
    goto EXIT13;
  }

  // start modules
  rc = modulesStart();
  if (rc == FALSE) {
    TMDBG_ERROR("starting session in modules\n");
    goto EXIT13b;
  }

  /* Create stream info */
  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    rc = createStream(streams.ids[cnt].identity, m_input.stream_types[cnt]);
    if (rc == FALSE) {
      TMDBG_ERROR("in creating stream type %d info\n", m_input.stream_types[cnt]);
      if (cnt == 0) {
        goto EXIT15;
      } else {
        goto EXIT16;
      }
    }
  }
  rc = modulesSetMod();
  if (rc == FALSE) {
    TMDBG_ERROR("during set modules mod\n");
    goto EXIT16;
  }

  rc = modulesLink(CAM_STREAM_TYPE_PARM);
  if (rc == FALSE) {
    TMDBG_ERROR("during link session stream modules\n");
    goto EXIT16;
  }
{
  uint32_t hal_version = CAM_HAL_V1;
  rc = sup->addParamToBatch(CAM_INTF_PARM_HAL_VERSION,
    sizeof(hal_version), &hal_version);
}
  rc = sup->sendSuperParams(&tmi, 0, parm_stream_identity);
  if (rc == FALSE) {
    TMDBG_ERROR("sending superparams\n");
    goto EXIT16;
  }

  rc = tmi.configureStream(session_stream_info->pp_config.feature_mask);
  if (rc == FALSE) {
    TMDBG_ERROR("stream sending stream info\n");
    goto EXIT16;
  }
  // link modules
  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    rc = modulesLink(m_input.stream_types[cnt]);
    if (rc == FALSE) {
      TMDBG_ERROR("during link modules\n");
      goto EXIT16;
    }
  }
  // process
  switch (m_input.execute_params.method) {
  case TM_EXECUTE_ALL:
    rc = tst_suite->executeSuiteTests();
    break;
  case TM_EXECUTE_BY_NAME:
    rc = tst_suite->executeSuiteTests(m_input.execute_params.name);
    break;
  case TM_EXECUTE_BY_INDEX:
    rc = tst_suite->executeSuiteTests(m_input.execute_params.index);
    break;
  case TM_EXECUTE_BATCH:
    rc = tst_suite->executeSuiteTests(m_input.execute_params.batch.indexes,
      m_input.execute_params.batch.num_indexes);
    break;
  default:
    /*Default behavior: Execute All */
    rc = tst_suite->executeSuiteTests();
    break;
  }

  main_ret = rc;
  if (rc == FALSE) {
    TMDBG_ERROR("process error!!!\n");
  }

  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    modulesUnlink(m_input.stream_types[cnt]);
  }

EXIT16:
  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    destroyStream(m_input.stream_types[cnt]);
  }

EXIT15:
  modulesStop();

EXIT14:

EXIT13b:
  destroySessionStream();

EXIT13:
  destroyPipeline();

EXIT12:
  tst_suite->deleteDummyPorts();

EXIT9:

EXIT8:
  tmi.freeOutputBuffer();

EXIT7:
  tmi.freeScratchBuffer();

EXIT6a:
  tmi.freeMetadataBuffer();

EXIT6:
  tmi.freeReferenceBuffer();

EXIT5:
  tmi.freeInputBuffer(NULL);

EXIT4:
  tmi.closeIonFD();

EXIT3:
  tmi.closeChromatixLibraries();

  modulesDeinit();

EXIT2:
  delete tst_suite;
  delete sup;

EXIT1:
  return main_ret;
}

/*************************************************************/
/************ functions for new stream creation ************/

void Test_Manager::fillStreamIdentity(uint32_t session_idx, uint32_t stream_idx)
{
  streams.ids[streams.num].id = stream_idx;
  streams.ids[streams.num++].identity = pack_identity(session_idx, stream_idx);
}

void Test_Manager::fillSessionStreamIdentity(uint32_t session_idx,
  uint32_t stream_idx)
{
  session_id = session_idx;
  session_stream_id = stream_idx;
  parm_stream_identity = pack_identity(session_idx, stream_idx);
}

void Test_Manager::fillStreamFeatures(tm_intf_input_t *test_case,
  cam_pp_feature_config_t *pp_feature_config, mct_stream_info_t *stream_info)
{
  uint32_t idx = (stream_info->identity & 0xFFFF) - 1;

  if (idx >= TM_MAX_NUM_STREAMS) {
    TMDBG_ERROR("Invalid stream index in stream info %d\n", idx);
    return;
  }

  if (test_case->cpp.denoise_params.denoise_enable) {
    memcpy(&pp_feature_config->denoise2d, &test_case->cpp.denoise_params,
      sizeof(cam_denoise_param_t));
    if (TM_DENOISE_TNR == test_case->cpp.denoise_type) {
      pp_feature_config->feature_mask |= CAM_QCOM_FEATURE_CPP_TNR;
    } else {
      pp_feature_config->feature_mask |= CAM_QCOM_FEATURE_DENOISE2D;
    }
  }

  if (test_case->cpp.sharpness_ratio) {
    pp_feature_config->feature_mask |= CAM_QCOM_FEATURE_SHARPNESS;
    pp_feature_config->sharpness = (uint32_t) test_case->cpp.sharpness_ratio;
  }

  if (test_case->sizes.rotation) {

    pp_feature_config->feature_mask |= CAM_QCOM_FEATURE_ROTATION;
    if (test_case->sizes.rotation == 90) {
      pp_feature_config->rotation = ROTATE_90;
    } else if (test_case->sizes.rotation == 180) {
      pp_feature_config->rotation = ROTATE_180;
    } else if (test_case->sizes.rotation == 270) {
      pp_feature_config->rotation = ROTATE_270;
    } else {
      pp_feature_config->rotation = ROTATE_0;
    }
    if (test_case->sizes.rotation == 90 || test_case->sizes.rotation == 270) {
      stream_info->dim.width = test_case->sizes.output_height[idx];
      stream_info->dim.height = test_case->sizes.output_width[idx];
      stream_info->buf_planes.plane_info.mp[0].stride =
        test_case->sizes.output_height[idx];
      stream_info->buf_planes.plane_info.mp[0].scanline =
        test_case->sizes.output_width[idx];
    }
  }

  if (test_case->sizes.flip) {
    pp_feature_config->feature_mask |= CAM_QCOM_FEATURE_FLIP;
    pp_feature_config->flip = test_case->sizes.flip;
  }
  if ((m_input.sizes.output_width[idx] > m_input.sizes.input_width) ||
    (m_input.sizes.output_height[idx] > m_input.sizes.input_height)) {
    pp_feature_config->feature_mask |= CAM_QCOM_FEATURE_SCALE;
    pp_feature_config->scale_param.output_width  = m_input.sizes.output_width[idx];
    pp_feature_config->scale_param.output_height = m_input.sizes.output_height[idx];
  }
  if (test_case->cpp.use_crop) {
    pp_feature_config->feature_mask |= CAM_QCOM_FEATURE_CROP;
    pp_feature_config->input_crop.left   = test_case->sizes.win_first_pixel;
    pp_feature_config->input_crop.top    = test_case->sizes.win_first_line;
    pp_feature_config->input_crop.width  = test_case->sizes.win_width;
    pp_feature_config->input_crop.height = test_case->sizes.win_height;
    if ((m_input.sizes.output_width[idx] > m_input.sizes.win_width) ||
      (m_input.sizes.output_height[idx] > m_input.sizes.win_height)) {
      pp_feature_config->feature_mask |= CAM_QCOM_FEATURE_SCALE;
      pp_feature_config->scale_param.output_width  = m_input.sizes.output_width[idx];
      pp_feature_config->scale_param.output_height = m_input.sizes.output_height[idx];
    }

  }
}

boolean Test_Manager::createStream(uint32_t stream_identity,
  cam_stream_type_t stream_type)
{
  boolean rc;
  cam_pp_offline_src_config_t *offline_src_cfg;
  cam_pp_online_src_config_t *online_src_cfg;
  cam_pp_feature_config_t     *pp_feature_config;
  mct_stream_t                *stream;
  uint32_t                    idx;

  stream = mct_stream_new(stream_identity & 0x0000FFFF);
  if (NULL == stream) {
    TMDBG_ERROR("on mct stream new!");
    return FALSE;
  }
  stream_info = &stream->streaminfo;

  memset(stream_info, 0, sizeof(mct_stream_info_t));

  stream_info->stream = stream;

  idx = stream->streamid - 1;
  if (idx >= TM_MAX_NUM_STREAMS) {
    TMDBG_ERROR("Invalid stream index %d\n", stream->streamid);
    return FALSE;
  }

  stream_info->identity = stream_identity;
  stream_info->dim.width = m_input.sizes.output_width[idx];
  stream_info->dim.height = m_input.sizes.output_height[idx];
  stream_info->fmt = p_tmi->getFormat(m_input.out_format[idx]);
  if (CAM_FORMAT_MAX == stream_info->fmt) {
    TMDBG_ERROR("Unknown format");
    return FALSE;
  }
  stream_info->stream_type = stream_type;
  stream_info->streaming_mode = m_input.streaming_mode[idx];
  stream_info->num_burst = m_input.num_burst[idx];
  stream_info->img_buffer_list = p_tmi->getImgBufsList(stream->streamid);
  stream->buffers.img_buf = stream_info->img_buffer_list;
  stream->buffers.stream_info = stream_info;

  TMDBG_HIGH("create stream id: %d, w: %d, h::%d, format: %s, " \
    "streaming_mode: %d, num_burst: %d",
    stream->streamid, m_input.sizes.output_width[idx],
    m_input.sizes.output_height[idx], m_input.out_format[idx],
    stream_info->streaming_mode, stream_info->num_burst);

  stream_info->buf_planes.plane_info.num_planes =
    p_tmi->getNumPlanes(m_input.out_format[idx]);
  stream_info->buf_planes.plane_info.mp[0].stride =
    m_input.sizes.output_width[idx];
  stream_info->buf_planes.plane_info.mp[0].scanline =
    m_input.sizes.output_height[idx];
  stream_info->buf_planes.plane_info.mp[0].len =
    stream_info->buf_planes.plane_info.mp[0].stride *
    stream_info->buf_planes.plane_info.mp[0].scanline;
  stream_info->buf_planes.plane_info.mp[1].len =
    stream_info->buf_planes.plane_info.mp[0].stride *
    stream_info->buf_planes.plane_info.mp[0].scanline/2;
  stream_info->buf_planes.plane_info.frame_len =
    m_input.sizes.output_width[idx] * m_input.sizes.output_height[idx] *
    p_tmi->getPixelSize(stream_info->fmt);

  if(CAM_STREAM_TYPE_OFFLINE_PROC == stream_type) {
    stream_info->reprocess_config.pp_type = CAM_OFFLINE_REPROCESS_TYPE;
    offline_src_cfg = &stream_info->reprocess_config.offline;
    offline_src_cfg->num_of_bufs = 1;
    offline_src_cfg->input_fmt = p_tmi->getFormat(m_input.input_format);
    offline_src_cfg->input_dim.width = m_input.sizes.input_width;
    offline_src_cfg->input_dim.height = m_input.sizes.input_height;
    offline_src_cfg->input_buf_planes.plane_info.num_planes = 2;
    for (uint32_t i = 0; i <
       offline_src_cfg->input_buf_planes.plane_info.num_planes; i++) {
       offline_src_cfg->input_buf_planes.plane_info.mp[i].stride =
        m_input.sizes.input_width;
       offline_src_cfg->input_buf_planes.plane_info.mp[i].scanline =
           (m_input.sizes.input_height/(i+1));
       offline_src_cfg->input_buf_planes.plane_info.mp[i].offset = 0;
       offline_src_cfg->input_buf_planes.plane_info.mp[i].offset_x = 0;
       offline_src_cfg->input_buf_planes.plane_info.mp[i].offset_y = 0;
       offline_src_cfg->input_buf_planes.plane_info.mp[i].len =
       offline_src_cfg->input_buf_planes.plane_info.mp[i].stride *
       offline_src_cfg->input_buf_planes.plane_info.mp[i].scanline;
       TMDBG_HIGH("%s:stride %d,scanline %d,offset %d,offsetx %d,offsety %d,len %d",
           __func__, offline_src_cfg->input_buf_planes.plane_info.mp[i].stride,
           offline_src_cfg->input_buf_planes.plane_info.mp[i].scanline,
           offline_src_cfg->input_buf_planes.plane_info.mp[i].offset,
           offline_src_cfg->input_buf_planes.plane_info.mp[i].offset_x,
           offline_src_cfg->input_buf_planes.plane_info.mp[i].offset_y,
           offline_src_cfg->input_buf_planes.plane_info.mp[i].len);
     }

    pp_feature_config = &stream_info->reprocess_config.pp_feature_config;
  } else {
    stream_info->reprocess_config.pp_type = CAM_ONLINE_REPROCESS_TYPE;
    online_src_cfg = &stream_info->reprocess_config.online;
    online_src_cfg->input_stream_id = stream_identity & 0x0000FFFF;
    online_src_cfg->input_stream_type = stream_type;
    pp_feature_config = &stream_info->pp_config;
  }
  fillStreamFeatures(&m_input, pp_feature_config, stream_info);

  /* Add stream to pipeline */
  rc = mct_object_set_parent(MCT_OBJECT_CAST(stream_info->stream),
    MCT_OBJECT_CAST(pipeline));
  if (rc == FALSE) {
    TMDBG_ERROR("adding stream to pipeline\n");
    destroyStream(stream_type);
    return FALSE;
  }

  TMDBG(">%s: feature mask: 0x%llx<\n", __func__, pp_feature_config->feature_mask);

  return TRUE;
}

void Test_Manager::destroyStream(cam_stream_type_t stream_type)
{
  mct_pipeline_get_stream_info_t info;
  mct_stream_t *stream = NULL;

  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);

  if (NULL == stream) {
    TMDBG_ERROR("Cannot find stream type: %d", stream_type);
    return;
  }

  MCT_PIPELINE_CHILDREN(pipeline) =
    mct_list_remove(MCT_PIPELINE_CHILDREN(pipeline), stream);
  (MCT_PIPELINE_NUM_CHILDREN(pipeline))--;

  free(stream);
}

boolean Test_Manager::createSessionStream(void)
{
  cam_pp_feature_config_t     *pp_feature_config;
  tm_intf_input_t *test_case = &m_input;
  boolean rc;
  mct_stream_t *stream;

  stream = mct_stream_new(MCT_SESSION_STREAM_ID);

  if (!stream) {
    TMDBG_ERROR("failed to create session stream!");
    return FALSE;
  }

  session_stream_info = &stream->streaminfo;

  memset(session_stream_info, 0, sizeof(mct_stream_info_t));

  session_stream_info->stream = stream;

  session_stream_info->stream_type = CAM_STREAM_TYPE_PARM;
  session_stream_info->fmt = CAM_FORMAT_YUV_420_NV12;
  session_stream_info->dim.width = 0;
  session_stream_info->dim.height = 0;
  session_stream_info->streaming_mode = CAM_STREAMING_MODE_CONTINUOUS;
  session_stream_info->buf_planes.plane_info.num_planes= 0;
  session_stream_info->num_bufs = 0;
  session_stream_info->identity = parm_stream_identity;
  pp_feature_config = &session_stream_info->pp_config;


  fillStreamFeatures(test_case, pp_feature_config, session_stream_info);

  MCT_OBJECT_CHILDREN(pipeline) = NULL;

  /* Add stream to pipeline */
  rc = mct_object_set_parent(MCT_OBJECT_CAST(session_stream_info->stream),
    MCT_OBJECT_CAST(pipeline));
  if (rc == FALSE) {
    TMDBG_ERROR("adding session stream to pipeline\n");
    return FALSE;
  }

  return TRUE;
}

boolean Test_Manager::destroySessionStream(void)
{
  MCT_PIPELINE_CHILDREN(pipeline) =
    mct_list_remove(MCT_PIPELINE_CHILDREN(pipeline), session_stream_info->stream);
  (MCT_PIPELINE_NUM_CHILDREN(pipeline))--;
  free(session_stream_info->stream);
  return TRUE;
}

/*************************************************************/
/********* End of functions for new stream creation *********/

boolean Test_Manager::createPipeline()
{
  tm_bus_thread_data_t *thread_data;

  mct_controller_t *pController =
    (mct_controller_t *) malloc (sizeof (mct_controller_t *));
  if (!pController) {
    TMDBG_ERROR("creating mct_controller object");
    return FALSE;
  }
  /* Create pipeline */
  pipeline = mct_pipeline_new(session_id, pController);
  if (!pipeline) {
    TMDBG_ERROR("creating pipeline");
    return FALSE;
  }
  /* Create bus synchronization to handle bus messages */
  pthread_mutex_init(&bus_handle_mutex, NULL);
  pthread_cond_init(&bus_handle_cond, NULL);
  pipeline->bus->mct_mutex = &bus_handle_mutex;
  pipeline->bus->mct_cond  = &bus_handle_cond;
  p_tmi->saveBusHandle(pipeline->bus);
  thread_data = (tm_bus_thread_data_t *)malloc(sizeof(tm_bus_thread_data_t));
  TMASSERT(!thread_data, TRUE, "cannot allocate thread data!");
  thread_data->tmi = p_tmi;
  thread_data->pipeline = pipeline;
  pthread_mutex_lock(&bus_handle_mutex);
  pthread_create(&bus_hdl_tid, NULL, p_tmi->busHandlerThread,
    thread_data);
  pthread_mutex_unlock(&bus_handle_mutex);
  return TRUE;
}

void Test_Manager::destroyPipeline(void)
{
  mct_bus_msg_t *bus_msg_destroy;

  if (!pipeline) {
    return;
  }
  bus_msg_destroy = (mct_bus_msg_t*)malloc(sizeof(mct_bus_msg_t));
  if (bus_msg_destroy) {
    memset(bus_msg_destroy, 0, sizeof(mct_bus_msg_t));
    bus_msg_destroy->type = MCT_BUS_MSG_CLOSE_CAM;
    pthread_mutex_lock(&pipeline->bus->priority_q_lock);
    mct_queue_push_tail(pipeline->bus->priority_queue, bus_msg_destroy);
    pthread_mutex_unlock(&pipeline->bus->priority_q_lock);
    /*Signal bus_handle thread to terminate */
    pthread_mutex_lock(&bus_handle_mutex);
    pthread_cond_signal(&bus_handle_cond);
    pthread_mutex_unlock(&bus_handle_mutex);
    pthread_join(bus_hdl_tid, NULL);
//  pthread_join(bus_hdl_tid, NULL);

    pthread_mutex_destroy(&bus_handle_mutex);
    pthread_cond_destroy(&bus_handle_cond);
    free (pipeline->controller);
    mct_pipeline_destroy(pipeline);
  }
}

static boolean tm_intf_link_list_modules(void *data1, void *data2)
{
  mct_module_t *p_mod =  (mct_module_t *)data1;
  tm_intf_link_data_t *ld = (tm_intf_link_data_t *)data2;
  TM_Interface *tm = ld->tmi;

  ld->status = TRUE;

  if(ld->prev_module == NULL) {
    ld->prev_module = p_mod;
    return TRUE;
  }
  if (FALSE == tm->linkTwoModules(ld->prev_module, p_mod, &ld->stream->streaminfo)) {
    TMDBG_ERROR("[in link two modules\n");
    ld->status = FALSE;
    return FALSE;
  }

  ld->prev_module = p_mod;
  ld->prev_port = tm->getSrcPort(p_mod);

  return TRUE;
}

boolean tm_intf_ports_reserve_and_link(void *data1, void *data2)
{
  mct_module_t      *module =  (mct_module_t *)data1;
  mct_port_t        *port;
  boolean           rc = FALSE;
  tm_intf_link_data_t *ld = (tm_intf_link_data_t *)data2;
  TM_Interface      *tm = ld->tmi;
  mct_port_t        *src_port = ld->prev_port;
  mct_stream_t      *stream = ld->stream;

  TMDBG("%s: Enter for module name: %s", __func__, module->object.name);

  port = tm->getSinkPort(module);
  if (!port) {
    TMDBG_ERROR("- no module port\n");
    goto EXIT;
  }

  rc = tm->linkTwoPorts(src_port, port, &ld->stream->streaminfo);
  if (rc == FALSE) {
    TMDBG_ERROR("link port!");
    goto EXIT;
  }

  rc = mct_object_set_parent(MCT_OBJECT_CAST(module),
    MCT_OBJECT_CAST(stream));
  if (rc == FALSE) {
    TMDBG_ERROR("adding module to stream\n");
    goto EXIT_UNLINK;
  }
  ld->prev_port = tm->getSrcPort(module);
  if (!ld->prev_port) {
    TMDBG_ERROR("No free src port for module: %s", module->object.name);
  }

  TMDBG("%s: Exit", __func__);

  ld->status = TRUE;
  return TRUE;

EXIT_UNLINK:
  tm->unlinkTwoPorts(src_port, port, ld->stream->streaminfo.identity);
EXIT:
  ld->status = FALSE;
  return FALSE;
}

boolean tm_intf_unlink_list_modules(void *data1, void *data2)
{
  mct_module_t *p_mod =  (mct_module_t *)data1;
  tm_intf_link_data_t *ld = (tm_intf_link_data_t *)data2;
  TM_Interface *tm = ld->tmi;

  if(ld->prev_module == NULL) {
    ld->prev_module = p_mod;
    return TRUE;
  }

  tm->unlinkTwoModules(ld->prev_module, p_mod, &ld->stream->streaminfo);

  ld->prev_module = p_mod;

  return TRUE;
}

boolean tm_intf_ports_unlink(void *data1, void *data2)
{
  mct_module_t      *module =  (mct_module_t *)data1;
  mct_port_t        *port;
  boolean           rc = FALSE;
  tm_intf_link_data_t *ld = (tm_intf_link_data_t *)data2;
  TM_Interface *tm = ld->tmi;
  mct_port_t        *src_port = ld->prev_port;

  TMDBG("%s: Enter for module name: %s", __func__, module->object.name);

  port = tm->getSinkPort(module);

  if (!port) {
    TMDBG_ERROR("- no module port\n");
    goto EXIT;
  }

  rc = mct_object_unparent(MCT_OBJECT_CAST(module),
    MCT_OBJECT_CAST(ld->stream));
  if (rc == FALSE) {
    TMDBG_ERROR("unparent module from stream\n");
  }

  rc = tm->unlinkTwoPorts(src_port, port, ld->stream->streaminfo.identity);
  if (rc == FALSE) {
  }

  ld->prev_port = tm->getSrcPort(module);

  TMDBG("%s: Exit", __func__);

  return rc;

EXIT:
  return FALSE;
}

const mct_module_init_name_t *Test_Manager::findCurModuleInits(
  const mct_module_init_name_t *inits_lut, char *name)
{
  uint32_t cnt = 0;

  if (!inits_lut) {
    return NULL;
  }
  while (inits_lut[cnt].init_mod != NULL) {
    if (!strncmp(name, inits_lut[cnt].name, 100)) {
      return &inits_lut[cnt];
    }
    cnt++;
  }
  return NULL;
}

uint32_t Test_Manager::getNumOfModules(void)
{

  uint32_t cnt = 0;

  while(strlen(m_input.modules[cnt]) > 1) {
    modules_list[cnt].name = m_input.modules[cnt];
    TMDBG_HIGH(">>>> modules[%d]: %s", cnt, modules_list[cnt].name);
    cnt++;
  }
  return cnt;
}


boolean Test_Manager::modulesAssignType(void)
{
  uint32_t cnt = 0;

  for (cnt = 0; cnt <= num_modules-1; cnt++) {
    if (cnt == 0) {
      modules_list[cnt].module_type = MCT_MODULE_FLAG_SOURCE;
    }
    else if (cnt == num_modules-1) {
      modules_list[cnt].module_type = MCT_MODULE_FLAG_SINK;
    }
    else {
      modules_list[cnt].module_type = MCT_MODULE_FLAG_INDEXABLE;
    }
  }
  return TRUE;
}

boolean tm_module_start(void *data, void *user_data)
{
  mct_module_t *p_mod =  (mct_module_t *)data;
  uint32_t *session_id = (uint32_t *)user_data;
  boolean rc = FALSE;

  if (!data || !user_data)
    return FALSE;

  if(p_mod->start_session)
    rc = p_mod->start_session(p_mod, *session_id);

  return rc;
}

boolean Test_Manager::modulesStart(void)
{
  return mct_list_traverse(modules, tm_module_start, &session_id);
}

boolean tm_module_stop(void *data, void *user_data)
{
  mct_module_t *p_mod =  (mct_module_t *)data;
  uint32_t *session_id = (uint32_t *)user_data;
  boolean rc = FALSE;

  if (!data || !user_data)
    return FALSE;

  if(p_mod->stop_session)
    rc = p_mod->stop_session(p_mod, *session_id);

  if (rc == FALSE) {
    TMDBG_ERROR("Stop module: %s", p_mod->object.name);
  }

  return rc;
}

boolean Test_Manager::modulesStop(void)
{
  return mct_list_traverse(modules, tm_module_stop, &session_id);
}


static boolean tm_module_set_mod(void *data, void *user_data)
{
  mct_module_t *p_mod =  (mct_module_t *)data;
  tm_set_mod_t *p_set_mod = (tm_set_mod_t *)user_data;
  Test_Manager *tm = (Test_Manager *)p_set_mod->instance;
  boolean rc = FALSE;

  if (!data || !user_data)
    return FALSE;

  rc = tm->setModuleMod(p_mod, p_set_mod);

  return rc;
}

boolean Test_Manager::modulesSetMod(void)
{
  boolean rc = FALSE;
  uint32_t cnt;
  tm_set_mod_t  set_mode;

  set_mode.instance = this;

  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    set_mode.module_idx = 0;
    set_mode.stream_idx = cnt;
    rc = mct_list_traverse(modules, tm_module_set_mod, &set_mode);
    if (FALSE == rc) {
      break;
    }
  }
  return rc;
}

boolean Test_Manager::setModuleMod(mct_module_t *p_mod, tm_set_mod_t *p_set_mod)
{
  if (!p_mod) {
    return FALSE;
  }
  // TODO: remove mix between list and array
  if(p_mod->set_mod) {
    p_mod->set_mod(p_mod,
      modules_list[p_set_mod->module_idx].module_type,
      parm_stream_identity);
    p_mod->set_mod(p_mod,
      modules_list[p_set_mod->module_idx].module_type,
      streams.ids[p_set_mod->stream_idx].identity);
  }
  p_set_mod->module_idx++;
  return TRUE;
}

boolean Test_Manager::modulesInit(void)
{
  mct_module_t *curr_module = NULL;
  uint32_t     i;
  const mct_module_init_name_t *module_inits;
  char *mod_name;
  const mct_module_init_name_t *p_modules_inits_lut = modules_inits_lut;

  modules = NULL;

  if (!p_modules_inits_lut) {
    return FALSE;
  }

  TMDBG("[%d] init %d mods", __LINE__, num_modules);
  for (i = 0 ; i < num_modules ; i++) {
    mod_name = modules_list[i].name;
    TMDBG("going to init module name: %s", mod_name);
    module_inits = findCurModuleInits(p_modules_inits_lut, mod_name);
    if (!module_inits) {
      TMDBG_ERROR("cannot find module <%s> inits", mod_name);
      return FALSE;
    }
    curr_module = module_inits->init_mod(mod_name);
    if (curr_module) {
      TMDBG("append in list module name: %s", mod_name);
      modules = mct_list_append(modules, curr_module, NULL, NULL);
      if (modules == NULL) {
        TMDBG("NULL append in list module name: %s", mod_name);
        return FALSE;
      }
    } else {
      TMDBG_ERROR("Failed to init mod: %s", mod_name);
      module_inits->deinit_mod(curr_module);
      return FALSE;
    }
  }
  return TRUE;
}

void Test_Manager::modulesDeinit(void)
{
  mct_module_t *curr_module = NULL;
  uint32_t     i;
  char         *name;
  const mct_module_init_name_t *module_inits;
  const mct_module_init_name_t *p_modules_inits_lut = modules_inits_lut;

  if (!p_modules_inits_lut) {
    CDBG_ERROR("%s: Ptr to module lut corrupted", __func__);
    return;
  }
  TMDBG("[%d] deinit %d modules", __LINE__, num_modules);
  for (i = 0 ; i < num_modules ; i++) {
    name = modules_list[i].name;
    TMDBG("going to deinit module name: %s", name);
    curr_module = p_tmi->getModule(name);
    if (curr_module) {
      module_inits = findCurModuleInits(p_modules_inits_lut, name);
      if (module_inits) {
        module_inits->deinit_mod(curr_module);
      } else {
        TMDBG_ERROR("cannot find module_inits for module <%s> inits", name);
      }
      modules = mct_list_remove(modules, curr_module);
      p_tmi->setModulesList(modules);
    } else {
      TMDBG_ERROR("Module <%s> not found in list", modules_list[i].name);
    }
  } /* for */
}

boolean Test_Manager::modulesLink(cam_stream_type_t stream_type)
{
  boolean rc;
  tm_intf_link_data_t ld;
  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;
  mct_module_t *module;

  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  TMDBG_HIGH("%s: linking stream type: %d\n", __func__, stream_type);

  stream = mct_pipeline_get_stream(pipeline, &info);
  if (!stream) {
    TMDBG_ERROR("%s: Couldn't find stream type: %d\n", __func__, stream_type);
    return FALSE;
  }
  ld.stream = stream;  //stream_info->stream;
  ld.tmi = p_tmi;
  ld.prev_module = NULL;
  ld.prev_port = NULL;

  TMDBG("\n%s: link modules for stream id: %d, type: %d\n", __func__,
    ld.stream->streamid, ld.stream->streaminfo.stream_type);

  // if first module is source module than use test port
  if (m_input.is_first_module_src == TRUE) {
    ld.prev_module = NULL;
    rc = mct_list_traverse(modules, tm_intf_link_list_modules, &ld);
    if ((rc == TRUE) && (ld.status == TRUE)) {
      rc = p_tmi->linkTestPorts(&ld);
      if (rc == FALSE) {
        TMDBG_ERROR("%s: Test ports link fails!\n", __func__);
      }
    } else {
      rc = FALSE;
      TMDBG_ERROR("%s: Modules link fails!\n", __func__);
    }
  } else {
    // hack - because iface is using the same sink port
    // for all streams
    module = p_tmi->getFirstModule();
    ld.prev_port = p_tmi->getFirstInputPort();
    if (NULL == ld.prev_port) {
      TMDBG_ERROR(" No input port \n");
      return FALSE;
    }
    rc = mct_list_traverse(modules, tm_intf_ports_reserve_and_link, &ld);

    if ((rc == TRUE) && (ld.status == TRUE)) {
      rc = p_tmi->linkTestPorts(&ld);
      if (FALSE == rc) {
        modulesUnlink(ld.stream->streaminfo.stream_type);
      }
    } else if (ld.status == FALSE) {
      rc = ld.status;
    }
  }
  return rc;
}

boolean Test_Manager::modulesUnlink(cam_stream_type_t stream_type)
{
  boolean rc;
  tm_intf_link_data_t ld;
  mct_stream_t *stream = NULL;
  mct_pipeline_get_stream_info_t info;

  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  if (NULL == stream) {
    TMDBG_ERROR("Cannot find stream type: %d", stream_type);
    return FALSE;
  }

  ld.stream = stream;
  ld.tmi = p_tmi;
  ld.prev_module = NULL;
  ld.prev_port = NULL;

  // if first module is not source than use test port
  if(m_input.is_first_module_src == TRUE) {
    ld.prev_module = NULL;
    rc = mct_list_traverse(modules, tm_intf_unlink_list_modules, &ld);
  } else {
    ld.prev_port = p_tmi->getFirstInputPort();
    rc = mct_list_traverse(modules, tm_intf_ports_unlink, &ld);
  }
  return rc;
}

mct_module_init_name_t *tm_get_module_inits(mct_module_init_name_t *modules_inits,
  char *name)
{
  int cnt = 0;
  while (strcmp(name, modules_inits[cnt].name)) {
    cnt++;
    if (modules_inits[cnt].init_mod == NULL) {
      TMDBG_ERROR("Cannot find init function for module name: %s", name);
      return NULL;
    }
  }
  return &modules_inits[cnt];
}

boolean tm_is_module_present(char array[][32], char *name)
{
  uint32_t cnt = 0;

  while(strlen(array[cnt]) > 1) {
    if (!strcmp(array[cnt], name)) {
      return TRUE;
    }
    cnt++;
  }
  return FALSE;
}


#ifdef TM_DEBUG_XML
void tm_xml_print_node_value(xmlNodePtr pNode, char *fmt_str)
{
  xmlChar *content, *type;

//  TMDBG("--- %s", xmlGetNsProp(pNode, BAD_CAST("value"), BAD_CAST("usecase")));

  TMDBG_NEOL("%s%s", fmt_str, pNode->name);
  content = xmlGetProp(pNode, BAD_CAST("value"));
  if(content) {
    type = xmlGetProp(pNode, BAD_CAST("type"));
    if(type)
      TMDBG("\t - value %s, type: %s", content, type);
    else
      TMDBG("\t - value: %s", content);
  } else {
    type = xmlGetProp(pNode, BAD_CAST("type"));
    if(type)
      TMDBG("\t - type: %s", type);
    else
      TMDBG_NEOL("\n");
  }
}

boolean tm_xml_traverse_nodes(xmlNodePtr pNode, tm_xml_nodes_traverse traverse,
  char *fmt_str)
{
  xmlNodePtr pNextNode;
  boolean rc = FALSE;
  char *tmp_fmt_str;
  int len;

  if (!pNode)
    return FALSE;
  if (traverse)
    traverse(pNode, fmt_str);

  pNextNode = xmlFirstElementChild(pNode);
  if (pNextNode) {
    len = strlen(fmt_str) + 2;
    tmp_fmt_str = (char *)malloc(len);
    snprintf(tmp_fmt_str, len, "%s\t", fmt_str);
    rc = tm_xml_traverse_nodes(pNextNode, traverse, tmp_fmt_str);
    free(tmp_fmt_str);
  }
  if (rc == FALSE) {
    pNextNode = xmlNextElementSibling(pNode);
    if (pNextNode) {
      rc = tm_xml_traverse_nodes(pNextNode, traverse, fmt_str);
      if (rc == FALSE) {
        return FALSE;
      } else {
        return FALSE;
      }
    } else {
      return FALSE;
    }
  }
  return TRUE;
}
#endif //TM_DEBUG_XML

char *XML_Parser::getNodeValue(xmlNodePtr pNode, char *name)
{
  xmlChar *content;

  if (!pNode->name) {
    return NULL;
  }
  if (!name) {
    return NULL;
  }
  if(!strncmp((char *)pNode->name, name, 256)) {
    content = xmlGetProp(pNode, BAD_CAST("value"));
    if(content) {
      return (char *)content;
    }
  }
  return NULL;
}

char *XML_Parser::getNodeType(xmlNodePtr pNode, const char *name)
{
  xmlChar *type;

  if (!pNode->name) {
    return NULL;
  }
  if (!name) {
    return NULL;
  }
  if(!strncmp((char *)pNode->name, name, 256)) {
    type = xmlGetProp(pNode, BAD_CAST("type"));
    if(type) {
      return (char *)type;
    }
  }
  return NULL;
}

char *XML_Parser::getNodeName(xmlNodePtr pNode, const char *name)
{
  if (!pNode->name) {
    return NULL;
  }
  if (!name) {
    return NULL;
  }
  if(!strncmp((char *)pNode->name, name, 256)) {
    return (char *)name;
  }
  return NULL;
}

char *XML_Parser::getCurNodeType(xmlNodePtr pNode)
{
  xmlChar *type;

  type = xmlGetProp(pNode, BAD_CAST("type"));
  if(type) {
    return (char *)type;
  }
  return NULL;
}

char *XML_Parser::getCurNodeValue(xmlNodePtr pNode)
{
  xmlChar *val;

  val = xmlGetProp(pNode, BAD_CAST("value"));
  if(val) {
    return (char *)val;
  }
  return NULL;
}

xmlNodePtr XML_Parser::getChildNodeValue(xmlNodePtr pNode, char **value)
{
  xmlNodePtr pNextNode;
  xmlChar *content;

  if (!pNode)
    return NULL;

  pNextNode = xmlFirstElementChild(pNode);

  if (pNextNode) {
    content = xmlGetProp(pNextNode, BAD_CAST("value"));
    if(content) {
      *value = (char *)content;
    } else {
      *value = NULL;
    }
  } else {
    *value = NULL;
  }
  return pNextNode;
}

xmlNodePtr XML_Parser::getNextNodeValue(xmlNodePtr pNode, char **value)
{
  xmlNodePtr pNextNode;
  xmlChar *content;

  if (!pNode)
    return NULL;

  pNextNode = xmlNextElementSibling(pNode);

  if (pNextNode) {
    content = xmlGetProp(pNextNode, BAD_CAST("value"));
    if(content) {
      *value = (char *)content;
    } else {
      *value = NULL;
    }
  } else {
    *value = NULL;
  }
  return pNextNode;
}

xmlNodePtr XML_Parser::getNodeHandle(xmlNodePtr pNode, const char *name)
{
  xmlNodePtr pNextNode;
  char *rc = NULL;
  xmlNodePtr retNode = NULL;

  if (!pNode)
    return NULL;
  if (!name)
    return NULL;

  rc = getNodeName(pNode, name);
  if (rc) {
    return pNode;
  }

  pNextNode = xmlFirstElementChild(pNode);
  if (pNextNode) {
    retNode = getNodeHandle(pNextNode, name);
  }
  if (retNode == NULL) {
    pNextNode = xmlNextElementSibling(pNode);
    if (pNextNode) {
      retNode = getNodeHandle(pNextNode, name);
      if (retNode == NULL) {
        return NULL;
      }
    }
  }
  return retNode;
}

char *XML_Parser::findNodeProperty(xmlNodePtr pNode, char *name)
{
  xmlNodePtr pNextNode;
  char *rc = NULL;

  if (!pNode)
    return NULL;

  rc = getNodeValue(pNode, name);
  if (rc) {
    return rc;
  }

  pNextNode = xmlFirstElementChild(pNode);
  if (pNextNode) {
    rc = findNodeProperty(pNextNode, name);
  }
  if (rc == NULL) {
    pNextNode = xmlNextElementSibling(pNode);
    if (pNextNode) {
      rc = findNodeProperty(pNextNode, name);
    }
  }
  return rc;
}

void XML_Parser::initParamsTable(tm_params_type_lut_t *lut)
{
  lut->groups = 0;
  lut->type = (char *)"cam_intf_parm_type_t";
}

float *XML_Parser::fillPayloadBufFloat(float *pbuf, float val)
{
  *pbuf = val;
  return &pbuf[1];
}

uint8_t *XML_Parser::fillPayloadBuf(uint8_t *pbuf, uint32_t val, uint32_t size)
{
  uint32_t idx;

  idx = 0;
  while (size) {
    pbuf[idx] = (val >> ( 8 * idx)) & 0xFF;
    idx++;
    size--;
  }
  return &pbuf[idx];
}

boolean XML_Parser::fillPayloadValue(char *type, char *value, uint8_t **pbuf)
{
  uint32_t size;
  uint32_t cnt = 0;
  uint32_t val;
  float fval;
  boolean rc = FALSE;

  val = atoi(value);

  while (tm_payload_type_lut[cnt].type) {
    if (!strncmp(tm_payload_type_lut[cnt].type, type, 32)) {
      size = tm_payload_type_lut[cnt].size;
      if (!strncmp(type, "float", 32)) {
        fval = atof(value);
        *pbuf = (uint8_t *)fillPayloadBufFloat(*(float **)pbuf, fval);
      } else {
        *pbuf = fillPayloadBuf(*pbuf, val, size);
      }
      rc = TRUE;
    }
    cnt++;
  }
  if (rc == FALSE) {
    size = 4;
    *pbuf = fillPayloadBuf(*pbuf, val, size);
    rc = TRUE;
  }
  return rc;
}

boolean XML_Parser::fillParamPayload(xmlNodePtr pNode, uint8_t *payload)
{
  char *type;
  char *value =  NULL;
  xmlNodePtr pPayloadNode;
  boolean rc = FALSE;

  pPayloadNode = getChildNodeValue(pNode, &value);
  while (pPayloadNode) {
    type = getCurNodeType(pPayloadNode);
    if (!type) {
      TMDBG_ERROR("parameter %s has no type! Will be skipped!", pPayloadNode->name);
      continue;
    }
    CDBG_HIGH("%s%s - value: %s, type: %s", "---", pPayloadNode->name, value, type);
    if (value != NULL)
      rc = fillPayloadValue(type, value, &payload);
    if (rc == FALSE) {
      TMDBG_ERROR("parameter %s value is unknown type! Will be skipped!", pPayloadNode->name);
      break;
    }
    xmlFree(value);
    pPayloadNode = getNextNodeValue(pPayloadNode, &value);
  }
  if (value) {
    xmlFree(value);
  }
  return rc;
}

void XML_Parser::fillParamValue(xmlNodePtr pNode, tm_params_type_lut_t *lut)
{
  char *type;
  char *value;
  tm_params_t *params = lut->params_group[lut->groups].tbl;
  uint32_t cnt = lut->params_group[lut->groups].cnt;
  boolean rc = FALSE;

  params = &params[cnt];

  type = getCurNodeType(pNode);
  if (!type) {
    TMDBG_ERROR("parameter %s has no type! Will be skipped!", pNode->name);
    return;
  }
  if (!strcmp(lut->type, type)) {
    value = getCurNodeValue(pNode);
    if (value) {
      CDBG_HIGH("%s%s: value: %s, type: %s", "--", pNode->name, value, type);
      params->val = (cam_intf_parm_type_t)atoi(value);
    }
    rc = fillParamPayload(pNode, params->payload);
  }
  if (rc == TRUE) {
    lut->params_group[lut->groups].cnt++;
  }
}

void XML_Parser::parseParams(xmlNodePtr pNode, tm_params_type_lut_t *params_lut)
{
  xmlNodePtr pNextNode;
  xmlNodePtr pFirstLevelNode;
  char *value;
  char *type;

  pFirstLevelNode = xmlFirstElementChild(pNode);
  if (!pFirstLevelNode) {
    return;
  }
  CDBG_HIGH("%s%s", "-", pFirstLevelNode->name);

  params_lut->params_group[params_lut->groups].cnt = 0;

  do {
    pNextNode = getChildNodeValue(pFirstLevelNode, &value);
    type = getCurNodeType(pNextNode);
    while (pNextNode) {
      if (!type) {
        TMDBG_ERROR("parameter %s has no type! Will be skipped!", pNextNode->name);
        break;
      }
      fillParamValue(pNextNode, params_lut);
      xmlFree(value);
      pNextNode = getNextNodeValue(pNextNode, &value);
      type = getCurNodeType(pNextNode);
      xmlFree(value);
    }

    pFirstLevelNode = xmlNextElementSibling(pFirstLevelNode);
    if (pFirstLevelNode) {
      CDBG_HIGH("%s%s", "-", pFirstLevelNode->name);
    }
  } while (pFirstLevelNode);
}

void XML_Parser::parseParamGroups(xmlNodePtr pNode,
  tm_params_type_lut_t *params_lut)
{
  xmlNodePtr pFirstLevelNode;

  pFirstLevelNode = xmlFirstElementChild(pNode);
  if (!pFirstLevelNode) {
    return;
  }
  CDBG_HIGH("%s", pFirstLevelNode->name);
  initParamsTable(params_lut);

  do {
    parseParams(pFirstLevelNode, params_lut);

    pFirstLevelNode = xmlNextElementSibling(pFirstLevelNode);
    if (pFirstLevelNode) {
      CDBG_HIGH("%s", pFirstLevelNode->name);
    }
    params_lut->groups++;
  } while (pFirstLevelNode);
}

uint32_t XML_Parser::getCharArray(xmlNodePtr pNode, const char *name,
  char *array, uint32_t size, uint32_t size2)
{
  xmlNodePtr pCurNode;
  uint32_t cnt = 0;
  char *value;
  char *pArray = array;

  if (!name) {
    TMDBG_ERROR("%s: name is NULL", __func__);
    return size + 1;
  }

  pCurNode = getNodeHandle(pNode, name);

  if (pCurNode) {
    pCurNode = getChildNodeValue(pCurNode, &value);
    if (value) {
      pArray = array + (cnt++ * size2);
      strlcpy(pArray, value, size2);
//      xmlFree(value);
    }
  }
  while (pCurNode) {
    pCurNode = getNextNodeValue(pCurNode, &value);
    if (value) {
      if (cnt >= size) {
        return size + 1;  // Will handle if return size is great than max size
      }
      pArray = array + (cnt++ * size2);
      strlcpy(pArray, value, size2);
      xmlFree(value);
    }
  }

  return cnt;
}

uint32_t XML_Parser::getIntArray(xmlNodePtr pNode, const char *name, int *array,
  uint32_t size)
{
  xmlNodePtr pCurNode;
  uint32_t cnt = 0;
  char *value;

  TMASSERT(!name, TRUE, "%s: name is NULL", __func__);

  pCurNode = getNodeHandle(pNode, name);

  if (pCurNode) {
    pCurNode = getChildNodeValue(pCurNode, &value);
    if (value) {
      array[cnt++] = atoi(value);
      xmlFree(value);
    }
  }
  while (pCurNode) {
    pCurNode = getNextNodeValue(pCurNode, &value);
    if (value) {
      if (cnt >= size) {
        return size + 1;  //Will handle if return size is greater than max size
      }
      array[cnt++] = atoi(value);
      xmlFree(value);
    }
  }

  return cnt;
}

int XML_Parser::getInt(xmlNodePtr pNode, char *name)
{
  char *value;
  int ret;

  value = findNodeProperty(pNode, name);
  if (value) {
    TMDBG("%s = %s", name, value);
    ret = atoi(value);
    xmlFree(value);
    return ret;
  } else {
    TMDBG_ERROR("%s is not found", name);
  }
  return 0;
}

float XML_Parser::getFloat(xmlNodePtr pNode, char *name)
{
  char *value;
  int ret;
  value = findNodeProperty(pNode, name);
  if (value) {
    TMDBG("%s = %s", name, value);
    ret = atof(value);
    xmlFree(value);
    return ret;
  } else {
    TMDBG_ERROR("%s is not found", name);
  }
  return 0;
}

int XML_Parser::getString(xmlNodePtr pNode, char *name, char *out)
{
  char *value;
  int ret;
  value = findNodeProperty(pNode, name);
  if (value) {
    TMDBG("%s = %s", name, value);
    ret = strlcpy(out, value, 256);
    xmlFree(value);
    return ret;
  } else {
    TMDBG_ERROR("%s is not found", name);
  }
  return 0;
}

boolean XML_Parser::checkConfigParams(tm_intf_input_t *test_input)
{
  if (test_input->lib_name[0] == 0) {
    TMDBG_ERROR("not found library file name");
    return FALSE;
  }
  if (test_input->read_input_from_file) {
    if (test_input->input_filename[0][0] == 0) {
      TMDBG_ERROR("not found input file name");
      return FALSE;
    }
  }
  if (test_input->execute_params.batch.num_indexes > MAX_BATCH_INDEXES) {
    TMDBG_ERROR("batch number imdexes is bigger than MAX");
    return FALSE;
  }
  if (!test_input->num_streams) {
    TMDBG_ERROR("No stream types found!");
    return FALSE;
  }
  return TRUE;
}

void XML_Parser::fillStreamTypes(xmlNodePtr pNode, tm_intf_input_t *tm_input)
{
  uint32_t cnt, str_cnt;

  tm_input->num_streams =
    getCharArray(pNode, "stream_types", &str_array[0][0], STR_ARRAY_ENTRIES, 32);

  TMDBG_HIGH("num streams = %d", tm_input->num_streams);

  for (str_cnt = 0 ; str_cnt < tm_input->num_streams ; str_cnt++) {
    TMDBG_HIGH("stream_type[%d] = %s", str_cnt, str_array[str_cnt]);
    for (cnt = 0 ; cnt < STR_TYPES_LUT_SIZE ; cnt++) {
      if (!strncmp(str_array[str_cnt], str_types_lut[cnt].name, STR_TYPE_NAME_SIZE)) {
        tm_input->stream_types[str_cnt] = str_types_lut[cnt].stream_type;
        TMDBG_HIGH("stream type[%d] id: %d",
          str_cnt, tm_input->stream_types[str_cnt]);
        break;
      }
    }
  }
}

boolean XML_Parser::parseConfig(char *filename, tm_intf_input_t *test_case)
{
  uint32_t    cnt = 0;
  xmlDocPtr   pXmlDoc;
  xmlNodePtr  pNode;
  int         str_modes[TM_MAX_NUM_STREAMS];

#ifdef TM_DEBUG_XML
  char fmt_str[256] = "<<";
#endif

  if(!filename) {
    TMDBG_ERROR("Bad config file name");
    return FALSE;
  }
  if(!test_case) {
    TMDBG_ERROR("NULL test_case ptr");
    return FALSE;
  }
  pXmlDoc = xmlReadFile(filename, NULL, 0);
  if (pXmlDoc == NULL) {
    TMDBG_ERROR("Cannot read xml config file!");
    return FALSE;
  }

  pNode = xmlDocGetRootElement(pXmlDoc);
  if (pNode == NULL) {
    TMDBG_ERROR("Cannot find root xml element!");
    return FALSE;
  }

#ifdef TM_DEBUG_XML
  TMDBG("--- All entries in config xml file are ---");
  tm_xml_traverse_nodes(pNode, tm_xml_print_node_value, fmt_str);
  TMDBG("");
#endif //TM_DEBUG_XML

  TMDBG("=== Going to parse config xml file ===");

  TM_FILL_ENTRY_INT(test_case, read_input_from_file);
  if (test_case->read_input_from_file) {
    TM_FILL_ENTRY_INT(test_case, read_input_y_only);
  }
  TM_FILL_ENTRY_INT(test_case, frame_count);
  TM_FILL_ENTRY_INT(test_case, frames_skip);
  TM_FILL_ENTRY_INT(test_case, save_file);
  TM_FILL_ENTRY_INT(test_case, use_out_buffer);
  TM_FILL_ENTRY_INT(test_case, have_ref_image);
  if (test_case->have_ref_image) {
    TM_FILL_ENTRY_STRING(test_case, ref_imgname);
  }

  TM_FILL_ENTRY_STRING(test_case, input_format);
  TM_FILL_ENTRY_STRING(test_case, outfile_path);
  TM_FILL_ENTRY_STRING(test_case, chromatix_file);
  TM_FILL_ENTRY_STRING(test_case, lib_name);

  test_case->num_in_images = TM_FILL_ENTRY_CHAR_ARRAY(test_case,
    input_filename, MAX_FILENAMES, MAX_FILENAME_LEN);
  if (test_case->num_in_images > MAX_FILENAMES) {
    TMDBG_ERROR("input images are more than expected!");
    test_case->num_in_images = MAX_FILENAMES;
  }
  TM_FILL_ENTRY_CHAR_ARRAY(test_case, modules, MODULES_ARRAY_ENTRIES, 32);
  TM_FILL_ENTRY_STRING(test_case, uc_name);

  TM_FILL_ENTRY_INT(test_case, session_id);
  fillStreamTypes(pNode, test_case);

  getIntArray(pNode, "streaming_mode", str_modes, TM_MAX_NUM_STREAMS);
  for (cnt = 0 ; cnt < test_case->num_streams ; cnt++) {
    test_case->streaming_mode[cnt] = (cam_streaming_mode_t)str_modes[cnt];
  }

  TM_FILL_ENTRY_INT_ARRAY(test_case, num_burst, TM_MAX_NUM_STREAMS);
  TM_FILL_ENTRY_INT(test_case, is_first_module_src);

  TM_FILL_SUB_ENTRY_TYPE(test_case->execute_params, method, tm_intf_execute_method_t);
  if (test_case->execute_params.method != TM_EXECUTE_ALL) {
    if (test_case->execute_params.method == TM_EXECUTE_BY_INDEX) {
      TM_FILL_SUB_ENTRY_INT(test_case->execute_params, index);
    } else if (test_case->execute_params.method == TM_EXECUTE_BY_NAME) {
      TM_FILL_SUB_ENTRY_STRING(test_case->execute_params, name);
    } else if (test_case->execute_params.method == TM_EXECUTE_BATCH) {
      test_case->execute_params.batch.num_indexes =
        TM_FILL_SUB_ENTRY_INT_ARRAY(test_case->execute_params.batch, indexes,
          MAX_BATCH_INDEXES);
    } else {
      TMDBG_ERROR("Unexpected execute method");
    }
  }
  while(strlen(test_case->modules[cnt]) > 1) {
    TMDBG(">> modules[%d]: %s", cnt, test_case->modules[cnt]);
    cnt++;
  }
  if (!cnt) {
    TMDBG_ERROR("No modules found in config xml file!");
    return FALSE;
  }

  xmlFreeDoc(pXmlDoc);

  TMDBG("=== End parsing xml file ===\n");

  if (FALSE == checkConfigParams(test_case)) {
    return FALSE;
  }

  return TRUE;
}

boolean XML_Parser::checkUsecaseParams(tm_intf_input_t *test_input)
{
  uint32_t idx;
  if (!test_input) {
    TMDBG_ERROR("Invalid test_input\n");
    return FALSE;
  }

  if (test_input->num_streams >= TM_MAX_NUM_STREAMS) {
    TMDBG_ERROR("Invalid stream index %d\n", test_input->num_streams);
    return FALSE;
  }

  for (idx = 0 ; idx < test_input->num_streams ; idx++) {
    if (!test_input->sizes.output_width[idx]) {
      TMDBG_ERROR("output width[%d] is 0", idx);
      return FALSE;
    }
    if (!test_input->sizes.output_height[idx]) {
      TMDBG_ERROR("output height[%d] is 0", idx);
      return FALSE;
    }
  }
  if (test_input->read_input_from_file) {
    if (!test_input->sizes.input_width) {
      TMDBG_ERROR("input width is 0");
      return FALSE;
    }
    if (!test_input->sizes.input_height) {
      TMDBG_ERROR("input height is 0");
      return FALSE;
    }
  }
  return TRUE;
}

boolean XML_Parser::parseUcParams(char *filename, tm_intf_input_t *test_case,
  tm_params_type_lut_t *params_lut)
{
  xmlDocPtr pXmlDoc;
  xmlNodePtr pNode;

  if(!filename) {
    TMDBG_ERROR("Bad config file name");
    return FALSE;
  }
  if(!test_case) {
    TMDBG_ERROR("NULL test_case ptr");
    return FALSE;
  }
  pXmlDoc = xmlReadFile(filename, NULL, XML_PARSE_RECOVER);
  if (pXmlDoc == NULL) {
    TMDBG_ERROR("Cannot read xml use case file!");
    return FALSE;
  }
  pNode = xmlDocGetRootElement(pXmlDoc);
  if (pNode == NULL) {
    TMDBG_ERROR("Cannot find root xml element!");
    return FALSE;
  }

#ifdef TM_DEBUG_XML
  TMDBG("--- All entries in usecase xml file are ---");
  tm_xml_traverse_nodes(pNode, tm_xml_print_node_value, "--");
  TMDBG("");
#endif //TM_DEBUG_XML

  TMDBG("\n=== Going to parse usecase xml file ===\n");

  CDBG_HIGH("\n-- Going to parse superparams --\n");

  parseParamGroups(pNode, params_lut);

  TMDBG("\n-- Going to parse fixed params --\n");

  TM_FILL_SUB_ENTRY_INT(test_case->sizes, input_width    );
  TM_FILL_SUB_ENTRY_INT(test_case->sizes, input_height   );
  TM_FILL_SUB_ENTRY_INT_ARRAY(test_case->sizes, output_width,  TM_MAX_NUM_STREAMS);
  TM_FILL_SUB_ENTRY_INT_ARRAY(test_case->sizes, output_height, TM_MAX_NUM_STREAMS);
  if (test_case->have_ref_image) {
    TM_FILL_SUB_ENTRY_INT(test_case->sizes, ref_img_width  );
    TM_FILL_SUB_ENTRY_INT(test_case->sizes, ref_img_height );
  }
  TM_FILL_SUB_ENTRY_INT(test_case->sizes, rotation       );
  TM_FILL_SUB_ENTRY_INT(test_case->sizes, flip           );
  TM_FILL_SUB_ENTRY_INT(test_case->sizes, win_first_line );
  TM_FILL_SUB_ENTRY_INT(test_case->sizes, win_first_pixel);
  TM_FILL_SUB_ENTRY_INT(test_case->sizes, win_width      );
  TM_FILL_SUB_ENTRY_INT(test_case->sizes, win_height     );

  TM_FILL_ENTRY_CHAR_ARRAY(test_case, out_format, TM_MAX_NUM_STREAMS, 32);

  TM_FILL_SUB_ENTRY_INT(test_case->cpp.denoise_params, denoise_enable);
  TM_FILL_SUB_ENTRY_TYPE(test_case->cpp, denoise_type, tm_denoise_e);
  TM_FILL_SUB_ENTRY_TYPE(test_case->cpp.denoise_params, process_plates,
    cam_denoise_process_type_t);
  TM_FILL_SUB_ENTRY_INT(test_case->cpp, sharpness_ratio  );
  TM_FILL_SUB_ENTRY_INT(test_case->cpp, use_crop);

  TM_FILL_SUB_ENTRY_FLOAT(test_case->stats, lux_idx      );
  TM_FILL_SUB_ENTRY_FLOAT(test_case->stats, real_gain    );
  TM_FILL_SUB_ENTRY_INT(test_case->stats, line_count     );
  TM_FILL_SUB_ENTRY_FLOAT(test_case->stats, exp_time     );


  xmlFreeDoc(pXmlDoc);

  TMDBG("=== End parsing xml file ===\n");

  return checkUsecaseParams(test_case);
}


