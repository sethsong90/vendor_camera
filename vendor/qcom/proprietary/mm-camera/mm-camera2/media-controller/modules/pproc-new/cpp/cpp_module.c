/*============================================================================

  Copyright (c) 2013-2016 Qualcomm Technologies, Inc. All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

============================================================================*/
#include "eztune_diagnostics.h"
#include "cpp_module.h"
#include "cpp_port.h"
#include "cpp_log.h"
#include "cpp_module_events.h"
#include "server_debug.h"
#include <cutils/properties.h>

#define CPP_PORT_NAME_LEN     32
#define CPP_NUM_SINK_PORTS    8
#define CPP_NUM_SOURCE_PORTS  8
#define MINIMUM_PROCESS_TIME 10.0f
#define PADDING_FACTOR       1.2f
#define ROT_SYSTEM_OVERHEAD  1.2f
#define SYSTEM_OVERHEAD      1.05f
#define CPP_MININAL_CLK      266667000
#define CPP_CLIENT_ID        20
#define CPP_MAX_ENGINE       6

#ifndef MAX
#define MAX(x,y) (((x)>(y)) ? (x) : (y))
#endif

static const char cpp_sink_port_name[CPP_NUM_SINK_PORTS][CPP_PORT_NAME_LEN] = {
 "cpp_sink_0",
 "cpp_sink_1",
 "cpp_sink_2",
 "cpp_sink_3",
 "cpp_sink_4",
 "cpp_sink_5",
 "cpp_sink_6",
 "cpp_sink_7",
};

static const char cpp_src_port_name[CPP_NUM_SOURCE_PORTS][CPP_PORT_NAME_LEN] = {
 "cpp_src_0",
 "cpp_src_1",
 "cpp_src_2",
 "cpp_src_3",
 "cpp_src_4",
 "cpp_src_5",
 "cpp_src_6",
 "cpp_src_7",
};

volatile int32_t gcam_cpp_loglevel = 0;
static boolean cpp_module_set_session_data(mct_module_t *module,
  void *set_buf, uint32_t sessionid);

/** module_cpp_set_parent:
 *
 *  @ module: module name
 *  @ parent_mod: parent module
 *
 *  Update the parent of cpp module.
 *
 *  Return: Return 0 always
 *
 **/
void module_cpp_set_parent(mct_module_t *module, mct_module_t *parent_mod)
{
  cpp_module_ctrl_t *ctrl;
  if (!module) {
    CPP_ERR("module invalid");
    goto end;
  }

  ctrl = (cpp_module_ctrl_t *)MCT_OBJECT_PRIVATE(module);
  if (!ctrl) {
    CPP_ERR("ctrl invalid");
    goto end;
  }

  ctrl->parent_module = parent_mod;

end:
  return;
}

/** cpp_module_init:
 *  Args:
 *    @name: module name
 *  Return:
 *    - mct_module_t pointer corresponding to cpp on SUCCESS
 *    - NULL in case of FAILURE or if CPP hardware does not
 *      exist
 **/
mct_module_t *cpp_module_init(const char *name)
{
  mct_module_t *module;
  cpp_module_ctrl_t* ctrl;
  CPP_HIGH("name=%s", name);
  module = mct_module_create(name);
  if(!module) {
    CPP_ERR("failed");
    return NULL;
  }
  ctrl = cpp_module_create_cpp_ctrl();
  if(!ctrl) {
    CPP_ERR("failed");
    goto error_cleanup_module;
  }
  MCT_OBJECT_PRIVATE(module) = ctrl;
  ctrl->p_module = module;
  module->set_mod = cpp_module_set_mod;
  module->query_mod = cpp_module_query_mod;
  module->start_session = cpp_module_start_session;
  module->stop_session = cpp_module_stop_session;
  module->set_session_data = cpp_module_set_session_data;

  mct_port_t* port;
  uint32_t i;
  /* Create default ports */
  for(i=0; i < CPP_NUM_SOURCE_PORTS; i++) {
    port = cpp_port_create(cpp_src_port_name[i], MCT_PORT_SRC);
    if(!port) {
      CPP_ERR("failed");
      return NULL;
    }
    module->srcports = mct_list_append(module->srcports, port, NULL, NULL);
    MCT_PORT_PARENT(port) = mct_list_append(MCT_PORT_PARENT(port), module,
                              NULL, NULL);
  }
  for(i=0; i < CPP_NUM_SINK_PORTS; i++) {
    port = cpp_port_create(cpp_sink_port_name[i], MCT_PORT_SINK);
    if(!port) {
      CPP_ERR("failed");
      return NULL;
    }
    module->sinkports = mct_list_append(module->sinkports, port, NULL, NULL);
    MCT_PORT_PARENT(port) = mct_list_append(MCT_PORT_PARENT(port), module,
                              NULL, NULL);
  }
  CPP_INFO("info: CPP module_init successful");
  return module;

error_cleanup_module:
  mct_module_destroy(module);
  return NULL;
}

/** cpp_module_loglevel:
 *
 *  Args:
 *  Return:
 *    void
 **/
static void cpp_module_loglevel()
{
  char cpp_prop[PROPERTY_VALUE_MAX];
  memset(cpp_prop, 0, sizeof(cpp_prop));
  property_get("persist.camera.cpp.debug.mask", cpp_prop, "1");
  gcam_cpp_loglevel = atoi(cpp_prop);
}

/** cpp_module_deinit:
 *
 *  Args:
 *    @module: pointer to cpp mct module
 *  Return:
 *    void
 **/
void cpp_module_deinit(mct_module_t *module)
{
  cpp_module_ctrl_t *ctrl =
    (cpp_module_ctrl_t *) MCT_OBJECT_PRIVATE(module);
  cpp_module_destroy_cpp_ctrl(ctrl);
  /* TODO: free other dynamically allocated resources in module */
  mct_module_destroy(module);
}

static cpp_module_ctrl_t* cpp_module_create_cpp_ctrl(void)
{
  cpp_module_ctrl_t *ctrl = NULL;
  mct_queue_t *q;
  int32_t rc;
  ctrl = (cpp_module_ctrl_t *) malloc(sizeof(cpp_module_ctrl_t));
  if(!ctrl) {
    CPP_ERR("malloc failed");
    return NULL;
  }
  memset(ctrl, 0x00, sizeof(cpp_module_ctrl_t));
  ctrl->pfd[READ_FD] = -1;
  ctrl->pfd[WRITE_FD] = -1;

  /* create real-time queue */
  ctrl->realtime_queue = (mct_queue_t*) malloc(sizeof(mct_queue_t));
  if(!ctrl->realtime_queue) {
    CPP_ERR("malloc failed");
    goto error_queue;
  }
  memset(ctrl->realtime_queue, 0x00, sizeof(mct_queue_t));
  mct_queue_init(ctrl->realtime_queue);

  /* create partial_frame_queue*/
  ctrl->partial_frame_queue = (mct_queue_t*) malloc(sizeof(mct_queue_t));
  if(!ctrl->partial_frame_queue) {
    CPP_ERR("malloc failed");
    goto error_queue;
  }
  memset(ctrl->partial_frame_queue, 0x00, sizeof(mct_queue_t));
  mct_queue_init(ctrl->partial_frame_queue);

  /* create offline queue*/
  ctrl->offline_queue = (mct_queue_t*) malloc(sizeof(mct_queue_t));
  if(!ctrl->offline_queue) {
    CPP_ERR("malloc failed");
    goto error_queue;
  }
  memset(ctrl->offline_queue, 0x00, sizeof(mct_queue_t));
  mct_queue_init(ctrl->offline_queue);

  /* create ack list */
  ctrl->ack_list.list = NULL;
  ctrl->ack_list.size = 0;
  pthread_mutex_init(&(ctrl->ack_list.mutex), NULL);

  ctrl->clk_rate_list.list = NULL;
  ctrl->clk_rate_list.size = 0;
  pthread_mutex_init(&(ctrl->clk_rate_list.mutex), NULL);

  /* Create PIPE for communication with cpp_thread */
  rc = pipe(ctrl->pfd);
  if ((ctrl->pfd[0]) >= MAX_FD_PER_PROCESS) {
    dump_list_of_daemon_fd();
    ctrl->pfd[0] = -1;
    rc = -1;
  }
  if(rc < 0) {
    CPP_ERR("pipe() failed");
    goto error_pipe;
  }
  pthread_cond_init(&(ctrl->th_start_cond), NULL);
  ctrl->session_count = 0;

  /* initialize cpp_mutex */
  pthread_mutex_init(&(ctrl->cpp_mutex), NULL);

  /* Create the CPP hardware instance */
  ctrl->cpphw = cpp_hardware_create();
  if(ctrl->cpphw == NULL) {
    CPP_ERR("failed, cannnot create cpp hardware instance\n");
    goto error_hw;
  }

  /* Open ION device */
  pp_native_buf_mgr_init(&ctrl->pp_buf_mgr, CPP_CLIENT_ID);

  cpp_hardware_set_private_data(ctrl->cpphw, &ctrl->pp_buf_mgr);
  /* open the cpp hardware and load firmware at this time */
  rc = cpp_hardware_open_subdev(ctrl->cpphw);
  if (rc < 0) {
    CPP_ERR("open subdev failure %d", rc);
    goto error_hw;
  }
  cpp_hardware_cmd_t cmd;
  cmd.type = CPP_HW_CMD_LOAD_FIRMWARE;
  rc = cpp_hardware_process_command(ctrl->cpphw, cmd);
  if (rc < 0) {
    CPP_ERR("open subdev failure %d", rc);
    goto error_hw;
  }
  cpp_hardware_close_subdev(ctrl->cpphw);

  return ctrl;

error_hw:
  cpp_hardware_close_subdev(ctrl->cpphw);
  if (ctrl->pfd[READ_FD] != -1) {
    close(ctrl->pfd[READ_FD]);
    ctrl->pfd[READ_FD] = -1;
  }
  if (ctrl->pfd[WRITE_FD] != -1) {
    close(ctrl->pfd[WRITE_FD]);
    ctrl->pfd[WRITE_FD] = -1;
  }
error_pipe:
  free(ctrl->realtime_queue);
  free(ctrl->partial_frame_queue);
  free(ctrl->offline_queue);
error_queue:
  free(ctrl);
  return NULL;
}

static int32_t cpp_module_destroy_cpp_ctrl(cpp_module_ctrl_t *ctrl)
{
  if(!ctrl) {
    return 0;
  }
  /* TODO: remove all entries from queues */
  mct_queue_free(ctrl->realtime_queue);
  mct_queue_free(ctrl->partial_frame_queue);
  mct_queue_free(ctrl->offline_queue);
  pthread_mutex_destroy(&(ctrl->ack_list.mutex));
  pthread_mutex_destroy(&(ctrl->clk_rate_list.mutex));
  pthread_mutex_destroy(&(ctrl->cpp_mutex));
  pthread_cond_destroy(&(ctrl->th_start_cond));
  pp_native_buf_mgr_deinit(&ctrl->pp_buf_mgr);
  close(ctrl->pfd[READ_FD]);
  close(ctrl->pfd[WRITE_FD]);
  cpp_hardware_destroy(ctrl->cpphw);
  free(ctrl);
  return 0;
}

void cpp_module_set_mod(mct_module_t *module,  mct_module_type_t module_type,
  uint32_t identity)
{
  CPP_DBG("module_type=%d\n", module_type);
  if(!module) {
    CPP_ERR("failed");
    return;
  }
  if (mct_module_find_type(module, identity) != MCT_MODULE_FLAG_INVALID) {
    mct_module_remove_type(module, identity);
  }
  mct_module_add_type(module, module_type, identity);
}

boolean cpp_module_query_mod(mct_module_t *module, void *buf,
  uint32_t sessionid __unused)
{
  int32_t rc;
  if(!module || !buf || sessionid == 0) {
    CPP_ERR("failed, module=%p, query_buf=%p, session id = %d",
      module, buf, sessionid);
    return FALSE;
  }
  mct_pipeline_cap_t *query_buf = (mct_pipeline_cap_t *)buf;
  mct_pipeline_pp_cap_t *pp_cap = &(query_buf->pp_cap);

  mct_pipeline_common_cap_t  *common_cap = &query_buf->common_cap;

  cpp_module_ctrl_t *ctrl = (cpp_module_ctrl_t *) MCT_OBJECT_PRIVATE(module);

  /* TODO: Fill pp cap according to CPP HW caps*/
  cpp_hardware_cmd_t cmd;
  cmd.type = CPP_HW_CMD_GET_CAPABILITIES;
  rc = cpp_hardware_process_command(ctrl->cpphw, cmd);
  if(rc < 0) {
    CPP_ERR("failed\n");
    return FALSE;
  }
  /* TODO: Need a linking function to fill pp cap based on HW caps? */
  pp_cap->supported_effects[pp_cap->supported_effects_cnt++] =
    CAM_EFFECT_MODE_OFF;
  pp_cap->supported_effects[pp_cap->supported_effects_cnt++] =
    CAM_EFFECT_MODE_EMBOSS;
  pp_cap->supported_effects[pp_cap->supported_effects_cnt++] =
    CAM_EFFECT_MODE_SKETCH;
  pp_cap->supported_effects[pp_cap->supported_effects_cnt++] =
    CAM_EFFECT_MODE_NEON;
#ifdef BEAUTY_FACE
  pp_cap->supported_effects[pp_cap->supported_effects_cnt++] =
    CAM_EFFECT_MODE_BEAUTY;
#endif

  pp_cap->plane_padding = mct_util_calculate_lcm(
    pp_cap->plane_padding, CAM_PAD_TO_64);
  pp_cap->height_padding = mct_util_calculate_lcm(
    pp_cap->height_padding, CAM_PAD_TO_64);
  pp_cap->width_padding = mct_util_calculate_lcm(
    pp_cap->width_padding, CAM_PAD_TO_64);

  pp_cap->min_num_pp_bufs += MODULE_CPP_MIN_NUM_PP_BUFS;
  if (query_buf->sensor_cap.sensor_format != FORMAT_YCBCR) {
    pp_cap->feature_mask |= CAM_QCOM_FEATURE_SHARPNESS;
  }
  pp_cap->feature_mask |= CAM_QCOM_FEATURE_EFFECT;
  pp_cap->feature_mask |= CAM_QCOM_FEATURE_CROP |
    CAM_QCOM_FEATURE_SCALE;
  if (ctrl->cpphw->hwinfo.caps & TNR_CAPS) {
    pp_cap->feature_mask |= CAM_QCOM_FEATURE_CPP_TNR;
  }

  if (ctrl->cpphw->hwinfo.version != CPP_HW_VERSION_6_1_0) {
    pp_cap->feature_mask |= CAM_QCOM_FEATURE_ROTATION;
  }

  /* Add CPP CDS as capability for hardware version 6 or above */
  if (ctrl->cpphw->hwinfo.version == CPP_HW_VERSION_6_0_0 ||
    ctrl->cpphw->hwinfo.version == CPP_HW_VERSION_6_1_0) {
    pp_cap->feature_mask |= CAM_QCOM_FEATURE_DSDN;
  }

#ifndef CAMERA_FEATURE_WNR_SW
  if (query_buf->sensor_cap.sensor_format != FORMAT_YCBCR) {
    pp_cap->feature_mask |= CAM_QCOM_FEATURE_DENOISE2D;
  }
  pp_cap->feature_mask |= (CAM_QCOM_FEATURE_CROP |
    CAM_QCOM_FEATURE_ROTATION |
    CAM_QCOM_FEATURE_FLIP | CAM_QCOM_FEATURE_SCALE);
#else
  pp_cap->feature_mask |= (CAM_QCOM_FEATURE_CROP |
    CAM_QCOM_FEATURE_ROTATION | CAM_QCOM_FEATURE_FLIP | CAM_QCOM_FEATURE_SCALE);
#endif
#ifdef DISABLE_PPROC
  pp_cap->feature_mask = 0;
#endif
  /*CPP firmware supports only upto 12 currently */
  pp_cap->max_supported_pp_batch_size = 12;

  pp_cap->max_pixel_bandwidth =
    ((((float)ctrl->cpphw->hwinfo.freq_tbl[
      ctrl->cpphw->hwinfo.freq_tbl_count - 1]) * MAXIMUM_CPP_THROUGHPUT) /
      PADDING_FACTOR);

  CPP_HIGH("feature_mask 0x%llx hw version 0x%x max_pixel_bandwidth %lld",
   pp_cap->feature_mask, ctrl->cpphw->hwinfo.version, pp_cap->max_pixel_bandwidth);

  if (ctrl->cpphw->max_supported_padding) {
    common_cap->offset_info.offset_x = mct_util_calculate_lcm(
      common_cap->offset_info.offset_x, ctrl->cpphw->max_supported_padding);

    common_cap->offset_info.offset_y = mct_util_calculate_lcm(
      common_cap->offset_info.offset_y, ctrl->cpphw->max_supported_padding);
  }
  common_cap->plane_padding = mct_util_calculate_lcm(
    common_cap->plane_padding, CAM_PAD_TO_64);
  CPP_LOW("QUERY_CAP - x %d, y %d, plane pad %d",
    common_cap->offset_info.offset_x, common_cap->offset_info.offset_y,
    common_cap->plane_padding);

  return TRUE;
}

/** cpp_module_set_session_data: set session data
 *
 *  @module: cpp module handle
 *  @set_buf: set buffer handle that has session data
 *  @sessionid: session id for which session data shall be
 *            applied
 *
 *  This function provides session data that has per frame
 *  contorl parameters
 *
 *  Return: TRUE on success and FALSE on failure
 **/
static boolean cpp_module_set_session_data(mct_module_t *module,
  void *set_buf, uint32_t sessionid)
{
  boolean                      ret = FALSE;
  mct_pipeline_session_data_t *frame_ctrl_data = NULL;
  cpp_module_ctrl_t           *ctrl = NULL;
  cpp_module_session_params_t *session_params = NULL;
  cpp_per_frame_params_t      *per_frame_params = NULL;

  /* Validate input parameters */
  if (!module || !set_buf) {
    CPP_LOW("failed: invalid params %p %p\n", module, set_buf);
    return FALSE;
  }

  frame_ctrl_data = (mct_pipeline_session_data_t *)set_buf;

  ctrl = (cpp_module_ctrl_t *)MCT_OBJECT_PRIVATE(module);
  if(!ctrl) {
    CPP_ERR("failed");
    return FALSE;
  }

  /* get parameters based on the session id */
  cpp_module_get_params_for_session_id(ctrl, sessionid, &session_params);
  if(!session_params) {
    CPP_ERR("failed\n");
    return FALSE;
  }

  CPP_HIGH("sessionid:%d max_apply_delay%d max_report_delay:%d\n",
    sessionid, frame_ctrl_data->max_pipeline_frame_applying_delay,
    frame_ctrl_data->max_pipeline_meta_reporting_delay);
  per_frame_params = &session_params->per_frame_params;
  PTHREAD_MUTEX_LOCK(&per_frame_params->mutex);
  per_frame_params->max_apply_delay =
    frame_ctrl_data->max_pipeline_frame_applying_delay;
  per_frame_params->max_report_delay =
    frame_ctrl_data->max_pipeline_meta_reporting_delay;
  PTHREAD_MUTEX_UNLOCK(&per_frame_params->mutex);

  return TRUE;
}

boolean cpp_module_start_session(mct_module_t *module, uint32_t sessionid)
{
  int32_t rc = 0, i = 0, j = 0;
  cpp_per_frame_params_t *per_frame_params = NULL;
  cpp_module_loglevel(); //dynamic logging level
  CPP_HIGH("info: starting session %d", sessionid);
  if(!module) {
    CPP_ERR("failed");
    return FALSE;
  }
  cpp_module_ctrl_t *ctrl = (cpp_module_ctrl_t *) MCT_OBJECT_PRIVATE(module);
  if(!ctrl) {
    CPP_ERR("failed");
    return FALSE;
  }
  MCT_OBJECT_LOCK(module);
  if(ctrl->session_count >= CPP_MODULE_MAX_SESSIONS) {
    CPP_ERR("failed, too many sessions, count=%d", ctrl->session_count);
    MCT_OBJECT_UNLOCK(module);
    return FALSE;
  }

  /* create a new session specific params structure */
  for(i=0; i < CPP_MODULE_MAX_SESSIONS; i++) {
    if(ctrl->session_params[i] == NULL) {
      if (sessionid >= CPP_MODULE_MAX_SESSIONS) {
        CPP_ERR("failed: sessionid:%d exceeds range\n", sessionid);
        MCT_OBJECT_UNLOCK(module);
        return FALSE;
      }

      ctrl->session_params[i] =
        (cpp_module_session_params_t*)
           malloc(sizeof(cpp_module_session_params_t));
      if (!ctrl->session_params[i]) {
        CPP_ERR("failed: no memory\n");
        MCT_OBJECT_UNLOCK(module);
        return FALSE;
      }
      memset(ctrl->session_params[i], 0x00,
        sizeof(cpp_module_session_params_t));
      ctrl->session_params[i]->session_id = sessionid;
      ctrl->session_params[i]->frame_hold.is_frame_hold = FALSE;
      ctrl->session_params[i]->dis_hold.is_valid = FALSE;
      ctrl->session_params[i]->stream_on_count = 0;
      ctrl->session_params[i]->is_stream_on = FALSE;
      ctrl->session_params[i]->fps_range.max_fps = 30.0f;
      ctrl->session_params[i]->fps_range.min_fps = 30.0f;
      ctrl->session_params[i]->fps_range.video_max_fps = 30.0f;
      ctrl->session_params[i]->fps_range.video_min_fps = 30.0f;
      ctrl->session_params[i]->diag_params.control_asf7x7.enable = 1;
      ctrl->session_params[i]->diag_params.control_wnr.enable = 1;
      ctrl->session_params[i]->hw_params.sharpness_level = 0.0;
      ctrl->session_params[i]->hw_params.asf_mode =
        CPP_PARAM_ASF_OFF;
      ctrl->session_params[i]->hw_params.sharpness_level =
        ((float)CPP_DEFAULT_SHARPNESS) /
        ((float)(CPP_MAX_SHARPNESS - CPP_MIN_SHARPNESS));
      ctrl->session_params[i]->hw_params.edge_mode =
        CAM_EDGE_MODE_FAST;
      ctrl->session_params[i]->hw_params.effect_mode =
        CAM_EFFECT_MODE_OFF;
      pthread_mutex_init(&(ctrl->session_params[i]->dis_mutex), NULL);
      for (j = 0; j < CPP_MODULE_MAX_STREAMS; j++) {
        ctrl->port_map[sessionid][j][0] = NULL;
        ctrl->port_map[sessionid][j][1] = NULL;
      }
      break;
    }
  }

  if ((i >= CPP_MODULE_MAX_SESSIONS) || !ctrl->session_params[i]) {
    CPP_ERR("failed");
    MCT_OBJECT_UNLOCK(module);
    return FALSE;
  }

  per_frame_params = &ctrl->session_params[i]->per_frame_params;
  /* Initialize per frame control structure */
  per_frame_params->cpp_delay = 0;
  pthread_mutex_init(&per_frame_params->mutex, NULL);
  for (j = 0; j < FRAME_CTRL_SIZE; j++) {
    per_frame_params->frame_ctrl_q[j] =
      (mct_queue_t *)calloc(1, sizeof(mct_queue_t));
    if(!per_frame_params->frame_ctrl_q[j]) {
      CPP_ERR("calloc failed\n");
      goto ERROR;
    }
    mct_queue_init(per_frame_params->frame_ctrl_q[j]);
    /* Initialize frame control mutex */
    pthread_mutex_init(&per_frame_params->frame_ctrl_mutex[j], NULL);
    per_frame_params->real_time_stream_cnt = 0;
    per_frame_params->offline_stream_cnt = 0;
  }

  /* start the thread only when first session starts */
  if(ctrl->session_count == 0) {
    /* spawn the cpp thread */
    rc = cpp_thread_create(module);
    if(rc < 0) {
      CPP_ERR("cpp_thread_create() failed");
      goto ERROR;
    }
    CPP_HIGH("info: cpp_thread created");

  }
  ctrl->session_count++;
  MCT_OBJECT_UNLOCK(module);
  CPP_HIGH("info: session %d started.", sessionid);
  return TRUE;

ERROR:
  for (j--; j >= 0; j--) {
    /* Free the frame control queue*/
    mct_queue_free(per_frame_params->frame_ctrl_q[j]);
    per_frame_params->frame_ctrl_q[j] = NULL;
    /* Destroy frame control mutex */
    pthread_mutex_destroy(&per_frame_params->frame_ctrl_mutex[j]);
  }
  MCT_OBJECT_UNLOCK(module);
  return FALSE;
}

boolean cpp_module_stop_session(mct_module_t *module, uint32_t sessionid)
{
  int32_t rc, j = 0;
  cpp_per_frame_params_t *per_frame_params = NULL;
  if(!module) {
    CPP_ERR("Invalid cpp module, failed");
    return FALSE;
  }
  cpp_module_ctrl_t *ctrl = (cpp_module_ctrl_t *) MCT_OBJECT_PRIVATE(module);
  if(!ctrl) {
    CPP_ERR("Invalid cpp control, failed");
    return FALSE;
  }
  CPP_HIGH("info: stopping session %d ...", sessionid);
  MCT_OBJECT_LOCK(module);
  ctrl->session_count--;
  /* stop the thread only when last session terminates */
  if(ctrl->session_count == 0) {
    /* stop the CPP thread */
    PTHREAD_MUTEX_LOCK(&(ctrl->cpp_mutex));
    if (ctrl->cpp_thread_started) {
      CPP_DBG("info: stopping cpp_thread...");
      cpp_thread_msg_t msg;
      msg.type = CPP_THREAD_MSG_ABORT;
      rc = cpp_module_post_msg_to_thread(ctrl, msg);
      if(rc < 0) {
        CPP_ERR("cpp_module_post_msg_to_thread() failed");
        PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
        MCT_OBJECT_UNLOCK(module);
        return FALSE;
      }
    }
    PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
    /* wait for thread completion */
    pthread_join(ctrl->cpp_thread, NULL);
    /* close the cpp hardware */
    CPP_DBG("closing cpp subdev...");
    cpp_hardware_close_subdev(ctrl->cpphw);
  }

  /* remove the session specific params */
  uint32_t i;
  for(i=0; i < CPP_MODULE_MAX_SESSIONS; i++) {
    if(ctrl->session_params[i]) {
      if(ctrl->session_params[i]->session_id == sessionid) {
        per_frame_params = &ctrl->session_params[i]->per_frame_params;
        pthread_mutex_destroy(&per_frame_params->mutex);
        for (j = 0; j < FRAME_CTRL_SIZE; j++) {
          if (!per_frame_params->frame_ctrl_q[j]) {
            continue;
          }
          mct_queue_free_all(per_frame_params->frame_ctrl_q[j],
            cpp_module_utill_free_queue_data);
          pthread_mutex_destroy(&per_frame_params->frame_ctrl_mutex[j]);
        }
        if (ctrl->session_params[i]->def_chromatix_stripped) {
          free(ctrl->session_params[i]->def_chromatix_stripped);
        }
        per_frame_params->real_time_stream_cnt = 0;
        per_frame_params->offline_stream_cnt = 0;
        pthread_mutex_destroy(
                &(ctrl->session_params[i]->dis_mutex));
        free(ctrl->session_params[i]);
        ctrl->session_params[i] = NULL;
        break;
      }
    }
  }
  CPP_HIGH("info: session %d stopped.", sessionid);
  MCT_OBJECT_UNLOCK(module);
  return TRUE;
}

/* cpp_module_post_msg_to_thread:
 *
 * @module: cpp module pointer
 * @msg: message to be posted for thread
 * Description:
 *  Writes message to the pipe for which the cpp_thread is listening to.
 *
 **/
int32_t cpp_module_post_msg_to_thread(cpp_module_ctrl_t *ctrl,
  cpp_thread_msg_t msg)
{
  int32_t rc;
  if(!ctrl) {
    CPP_ERR(" module invalid-failed");
    return -EINVAL;
  }
  CPP_LOW("msg.type=%d", msg.type);
  if (ctrl->pfd[WRITE_FD] != -1) {
    rc = write(ctrl->pfd[WRITE_FD], &msg, sizeof(cpp_thread_msg_t));
    if (rc < 0) {
      CPP_ERR("write() failed\n");
      return -EIO;
    }
  }
  return 0;
}

/* cpp_module_enq_event:
 *
 * @ctrl: cpp ctrl pointer
 * @event:  cpp_event to be queued
 * @prio:   priority of the event(realtime/partial_frame/offline)
 *
 * Description:
 *  Enqueues a cpp_event into realtime, partial_frame, or offline queue based on the
 *  priority.
 *
 **/
int32_t cpp_module_enq_event(cpp_module_ctrl_t *ctrl,
  cpp_module_event_t* cpp_event, cpp_priority_t prio)
{
  if(!ctrl || !cpp_event) {
    CPP_ERR("failed, ctrl=%p, event=%p", ctrl, cpp_event);
    return -EINVAL;
  }

  CPP_LOW("prio=%d", prio);
  switch (prio) {
  case CPP_PRIORITY_REALTIME:
    mct_queue_push_tail(ctrl->realtime_queue, cpp_event);
    CPP_LOW("realtime queue size = %d", ctrl->realtime_queue->length);
    break;
  case CPP_PRIORITY_PARTIAL_FRAME:
    mct_queue_push_tail(ctrl->partial_frame_queue, cpp_event);
    CPP_HIGH("partial_frame_queue size= %d", ctrl->partial_frame_queue->length);
    break;
  case CPP_PRIORITY_OFFLINE:
    mct_queue_push_tail(ctrl->offline_queue, cpp_event);
    CPP_LOW("offline queue size = %d", ctrl->offline_queue->length);
    break;
  default:
    CPP_ERR("failed, bad prio value=%d", prio);
    return -EINVAL;
  }
  return 0;
}

/* cpp_module_flush_queue_events:
 *
 * @ctrl: cpp ctrl pointer
 * @frame_id: valid frame id to flush if non-zero
 * @identity: valid identity to flush if non-zero
 *
 * Description:
 *  Flush all the events from given queue (realtime/partial_frame/offline.
 *
 **/
int32_t cpp_module_flush_queue_events(cpp_module_ctrl_t *ctrl,
  int32_t frame_id, uint32_t identity, uint8_t flush_frame)
{
  mct_queue_t *queue = NULL;
  uint32_t queue_len = 0;

  if(!ctrl) {
    CPP_ERR("failed, ctrl=%p\n", ctrl);
    return -EINVAL;
  }

  queue = ctrl->partial_frame_queue;

  PTHREAD_MUTEX_LOCK(&(ctrl->cpp_mutex));
  queue_len = queue->length;
  while(queue_len) {
    cpp_module_event_t* cpp_event_temp;
    cpp_event_temp = (cpp_module_event_t *)mct_queue_pop_head(queue);
    if (cpp_event_temp && (!identity ||
      (cpp_event_temp->u.partial_frame.frame->identity == identity)) &&
      (!frame_id ||
      cpp_event_temp->u.partial_frame.frame->frame_id == frame_id)) {
      if (cpp_event_temp->u.partial_frame.partial_stripe_info.last_payload &&
        (flush_frame == TRUE)) {
        cpp_hardware_flush_frame(ctrl->cpphw,
          cpp_event_temp->u.partial_frame.frame);
      }
      free(cpp_event_temp);
    } else {
      mct_queue_push_tail(queue,(void *)cpp_event_temp);
    }
    queue_len--;
  }
  PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
  return 0;
}

int32_t cpp_module_send_event_downstream(mct_module_t* module,
   mct_event_t* event)
{
  boolean            ret;
  mct_port_t        *port;
  cpp_module_ctrl_t *ctrl;
  if(!module || !event) {
    CPP_ERR("failed, module=%p, event=%p", module, event);
    return -EINVAL;
  }
  uint32_t identity = event->identity;

  ctrl = (cpp_module_ctrl_t *)MCT_OBJECT_PRIVATE(module);
  if (!ctrl) {
    CPP_ERR("error: NULL ctrl ptr\n");
    return -EINVAL;
  }

  if ((CPP_GET_SESSION_ID(identity) >= CPP_MODULE_MAX_SESSIONS) ||
    (CPP_GET_STREAM_ID(identity) >= CPP_MODULE_MAX_STREAMS)) {
    CPP_ERR("error: invalid identity:0x%x\n", identity);
    return -EINVAL;
  }

  port = ctrl->port_map\
    [CPP_GET_SESSION_ID(identity)][CPP_GET_STREAM_ID(identity)][0];
  if(!port) {
    CPP_LOW("info: no source port found.with identity=0x%x", identity);
    return 0;
  }
  /* if port has a peer, post event to the downstream peer */
  if(MCT_PORT_PEER(port) == NULL) {
    CPP_ERR("failed, no downstream peer found.");
    return -EINVAL;
  }
  ret = mct_port_send_event_to_peer(port, event);
  if(ret == FALSE) {
    CPP_ERR("failed\n");
    return -EFAULT;
  }
  return 0;
}

/* cpp_module_send_event_upstream:
 *
 * Description:
 *  Sends event to the upstream peer based on the event identity.
 *
 **/
int32_t cpp_module_send_event_upstream(mct_module_t* module,
   mct_event_t* event)
{
  boolean ret;
  mct_port_t        *port;
  cpp_module_ctrl_t *ctrl;
  if(!module || !event) {
    CPP_ERR("failed, module=%p, event=%p", module, event);
    return -EINVAL;
  }
  uint32_t identity = event->identity;
  ctrl = (cpp_module_ctrl_t *)MCT_OBJECT_PRIVATE(module);
  if (!ctrl) {
    CPP_ERR("error: NULL ctrl ptr\n");
    return -EINVAL;
  }

    if ((CPP_GET_SESSION_ID(identity) >= CPP_MODULE_MAX_SESSIONS) ||
    (CPP_GET_STREAM_ID(identity) >= CPP_MODULE_MAX_STREAMS)) {
    CPP_ERR("error: invalid identity:0x%x\n", identity);
    return -EINVAL;
  }

  port = ctrl->port_map\
    [CPP_GET_SESSION_ID(identity)][CPP_GET_STREAM_ID(identity)][1];
  if(!port) {
    CPP_ERR("failed, no sink port found.with identity=0x%x", identity);
    return -EINVAL;
  }
  /* if port has a peer, post event to the upstream peer */
  if(!MCT_PORT_PEER(port)) {
    CPP_ERR("failed, no upstream peer found.");
    return -EINVAL;
  }
  ret = mct_port_send_event_to_peer(port, event);
  if(ret == FALSE) {
    CPP_ERR("failed\n");
    return -EFAULT;
  }
  return 0;
}

/** cpp_module_invalidate_and_free_qentry
 *    @queue: queue to invalidate and free entries
 *    @identity: identity to invalidate
 *
 *  Invalidate the queue entries corresponding to
 *  given identity. The invalidated entries are acked
 *  and then freed from the list.
 *
 *  Return: void
 **/
static void cpp_module_invalidate_and_free_qentry(cpp_module_ctrl_t* ctrl,
  mct_queue_t *queue, uint32_t identity)
{
  mct_list_t *key_list = NULL;
  void*  input[3];
  input[0] = ctrl;
  input[1] = &identity;
  input[2] = &key_list;
  /* First get all keys correspoding to the identity in key_list. Then traverse
     key_list and release the acks from ack_list. This is to avoid holding queue
     mutex when sending an event upstream to avoid potential deadlocks */
  PTHREAD_MUTEX_LOCK(&(ctrl->cpp_mutex));
  mct_queue_traverse(queue, cpp_module_invalidate_q_traverse_func,
    input);
  PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
  /* traverse key list to release acks */
  mct_list_traverse(key_list, cpp_module_release_ack_traverse_func, ctrl);
  /* free the key list */
  mct_list_free_all(key_list, cpp_module_key_list_free_traverse_func);
  return;
}

/* cpp_module_invalidate_queue:
 *
 **/
int32_t cpp_module_invalidate_queue(cpp_module_ctrl_t* ctrl,
  uint32_t identity)
{
  if(!ctrl) {
    CPP_ERR("failed\n");
    return -EINVAL;
  }
  cpp_module_invalidate_and_free_qentry(ctrl, ctrl->realtime_queue, identity);
  cpp_module_invalidate_and_free_qentry(ctrl, ctrl->partial_frame_queue, identity);
  cpp_module_invalidate_and_free_qentry(ctrl, ctrl->offline_queue, identity);
  return 0;
}

/* cpp_module_send_buf_divert_ack:
 *
 *  Sends a buf_divert_ack to upstream module.
 *
 **/
static int32_t cpp_module_send_buf_divert_ack(cpp_module_ctrl_t *ctrl,
  isp_buf_divert_ack_t isp_ack)
{
  mct_event_t event;
  int32_t rc;
  memset(&event, 0x00, sizeof(mct_event_t));
  event.type = MCT_EVENT_MODULE_EVENT;
  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = isp_ack.identity;
  event.u.module_event.type = MCT_EVENT_MODULE_BUF_DIVERT_ACK;
  event.u.module_event.module_event_data = &isp_ack;
  CPP_BUF_LOW("sending isp ack with identity=0x%x, is_buf_dirty=%d, "
           "buf_idx=%d channel_id %d", isp_ack.identity,
           isp_ack.is_buf_dirty, isp_ack.buf_idx, isp_ack.channel_id);

  rc = cpp_module_send_event_upstream(ctrl->p_module, &event);
  if(rc < 0) {
    CPP_ERR("failed");
    return -EFAULT;
  }
  return 0;
}

static int32_t cpp_module_send_metadata_dump(cpp_module_ctrl_t *ctrl,
  uint32_t identity, pproc_meta_data_dump_t metadata)
{
  mct_event_t event;
  int32_t rc;
  memset(&event, 0x00, sizeof(mct_event_t));
  event.type = MCT_EVENT_MODULE_EVENT;
  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = identity;
  event.u.module_event.type = MCT_EVENT_MODULE_PPROC_DUMP_METADATA;
  event.u.module_event.module_event_data = &metadata;
  CPP_LOW("sending metadata dump identity=0x%x, metaptr %p", identity, metadata);

  rc = cpp_module_send_event_upstream(ctrl->p_module, &event);
  if(rc < 0) {
    CPP_ERR("failed");
    return -EFAULT;
  }
  return 0;
}
/* cpp_module_do_ack:
 *
 *  Decrements the refcount of the ACK which is stored in the ack_list,
 *  correspoding to the key. If the refcount becomes 0, a buf_divert_ack
 *  is sent upstream. At this time the ack entry is removed from list.
 *
 **/
int32_t cpp_module_do_ack(cpp_module_ctrl_t *ctrl,
  cpp_module_ack_key_t key)
{
  if(!ctrl) {
    CPP_ERR("failed");
    return -EINVAL;
  }
  /* find corresponding ack from the list. If the all references
     to that ack are done, send the ack and remove the entry from the list */
  cpp_module_ack_t *cpp_ack;
  mct_module_type_t     mod_type = MCT_MODULE_FLAG_INVALID;

  CPP_BUF_DBG("buf_idx=%d, identity=0x%x, frme id %d",
    key.buf_idx, key.identity, key.frame_id);
  PTHREAD_MUTEX_LOCK(&(ctrl->ack_list.mutex));
  cpp_ack = cpp_module_find_ack_from_list(ctrl, key);
  if(!cpp_ack) {
    CPP_ERR("failed, ack not found in list, for buf_idx=%d, "
      "identity=0x%x", key.buf_idx, key.identity);
    PTHREAD_MUTEX_UNLOCK(&(ctrl->ack_list.mutex));
    return -EFAULT;
  }
  cpp_ack->ref_count--;
  CPP_BUF_LOW("cpp_ack->ref_count=%d\n", cpp_ack->ref_count);
  struct timeval tv;
  if(cpp_ack->ref_count == 0) {
    ctrl->ack_list.list = mct_list_remove(ctrl->ack_list.list, cpp_ack);
    ctrl->ack_list.size--;
    /* unlock before sending event to prevent any deadlock */
    PTHREAD_MUTEX_UNLOCK(&(ctrl->ack_list.mutex));
    gettimeofday(&(cpp_ack->out_time), NULL);
    CPP_PROFILE("in_time=%ld.%ld us, out_time=%ld.%ld us, ",
      cpp_ack->in_time.tv_sec, cpp_ack->in_time.tv_usec,
      cpp_ack->out_time.tv_sec, cpp_ack->out_time.tv_usec);
    CPP_PROFILE("holding time = %6ld us, iden 0x%x frame id %d",
      (cpp_ack->out_time.tv_sec - cpp_ack->in_time.tv_sec)*1000000L +
      (cpp_ack->out_time.tv_usec - cpp_ack->in_time.tv_usec),
      cpp_ack->isp_buf_divert_ack.identity, cpp_ack->isp_buf_divert_ack.frame_id);
    cpp_module_send_buf_divert_ack(ctrl, cpp_ack->isp_buf_divert_ack);

    mod_type = mct_module_find_type(ctrl->p_module,
      cpp_ack->isp_buf_divert_ack.identity);

    if(mod_type == MCT_MODULE_FLAG_SINK &&
      cpp_ack->isp_buf_divert_ack.meta_data) {
      CPP_LOW("CPP is sink so send metadata dump");
      pproc_meta_data_dump_t metadata;
      metadata.frame_id = cpp_ack->isp_buf_divert_ack.frame_id;
      metadata.meta_data = cpp_ack->isp_buf_divert_ack.meta_data;

      cpp_module_send_metadata_dump(ctrl, cpp_ack->isp_buf_divert_ack.identity,
        metadata);
    }

    gettimeofday(&tv, NULL);
    CPP_PROFILE("upstream event time = %6ld us, ",
      (tv.tv_sec - cpp_ack->out_time.tv_sec)*1000000L +
      (tv.tv_usec - cpp_ack->out_time.tv_usec));
    free(cpp_ack);
  } else {
    PTHREAD_MUTEX_UNLOCK(&(ctrl->ack_list.mutex));
  }
  return 0;
}

/* cpp_module_handle_ack_from_downstream:
 *
 *  Handles the buf_divert_ack event coming from downstream module.
 *  Corresponding ACK stored in ack_list is updated and/or released
 *  accordingly.
 *
 */
static int32_t cpp_module_handle_ack_from_downstream(mct_module_t* module,
  mct_event_t* event)
{
  cpp_hardware_cmd_t cmd;
  cpp_hardware_event_data_t event_data;
  int32_t rc = 0;
  if(!module || !event) {
    CPP_ERR("failed, module=%p, event=%p\n", module, event);
    return -EINVAL;
  }
  cpp_module_ctrl_t* ctrl = (cpp_module_ctrl_t*) MCT_OBJECT_PRIVATE(module);
  if(!ctrl) {
    CPP_ERR("failed\n");
    return -EINVAL;
  }
  /* Among the 2 types of buffer divert event from pproc (unprocessed divert
     and processed divert), we assume only processed divert is asynchronous.
     Hence we expect this ack event only for processed divert in which case
     there will not be any entry in the ack list. We make it a policy that
     if unprocessed divert is needed by downstream module, then it has to be
     synchronous. */
  isp_buf_divert_ack_t* isp_buf_ack =
    (isp_buf_divert_ack_t*)(event->u.module_event.module_event_data);
#if 0
  cpp_module_ack_key_t key;
  key.identity = isp_buf_ack->identity;
  key.buf_idx = isp_buf_ack->buf_idx;
  CPP_LOW("doing ack for divert_done ack from downstream");
  cpp_module_do_ack(ctrl, key);
#endif
  cmd.type = CPP_HW_CMD_QUEUE_BUF;
  cmd.u.event_data = &event_data;
  memset(&event_data, 0, sizeof(event_data));
  event_data.identity = isp_buf_ack->identity;
  event_data.out_buf_idx = isp_buf_ack->buf_idx;
  event_data.timestamp = isp_buf_ack->timestamp;
  event_data.frame_id = isp_buf_ack->frame_id;
  event_data.is_buf_dirty = isp_buf_ack->is_buf_dirty;
  CPP_BUF_LOW("frame id %d, identity %x",
    isp_buf_ack->frame_id, isp_buf_ack->identity);
  rc = cpp_hardware_process_command(ctrl->cpphw, cmd);
  if(rc < 0) {
    CPP_ERR("failed\n");
  }
  cmd.type = CPP_HW_CMD_NOTIFY_BUF_DONE;
  cmd.u.buf_done_identity = event_data.identity;
  rc = cpp_hardware_process_command(ctrl->cpphw, cmd);
  if (rc < 0) {
    CPP_ERR("failed");
  }
  return 0;
}

/* cpp_module_put_new_ack_in_list:
 *
 * Description:
 *   Adds a new ACK in the ack_list with the given params.
 **/
int32_t cpp_module_put_new_ack_in_list(cpp_module_ctrl_t *ctrl,
  cpp_module_ack_key_t key, int32_t buf_dirty, int32_t ref_count,
  isp_buf_divert_t *isp_buf)
{
  if(!ctrl) {
    CPP_ERR("failed\n");
    return -EINVAL;
  }
  /* this memory will be freed by thread when ack is removed from list */
  cpp_module_ack_t *cpp_ack =
    (cpp_module_ack_t *) malloc (sizeof(cpp_module_ack_t));
  if(!cpp_ack) {
    CPP_ERR("malloc failed\n");
    return -ENOMEM;
  }
  memset(cpp_ack, 0x00, sizeof(cpp_module_ack_t));
  cpp_ack->isp_buf_divert_ack.identity = key.identity;
  cpp_ack->isp_buf_divert_ack.buf_idx = key.buf_idx;
  cpp_ack->isp_buf_divert_ack.is_buf_dirty = buf_dirty;
  cpp_ack->isp_buf_divert_ack.channel_id = key.channel_id;
  cpp_ack->isp_buf_divert_ack.frame_id = key.frame_id;
  cpp_ack->isp_buf_divert_ack.timestamp = isp_buf->buffer.timestamp;
  cpp_ack->isp_buf_divert_ack.meta_data = key.meta_datas;
  cpp_ack->ref_count = ref_count;
  CPP_BUF_LOW("adding ack in list, identity=0x%x",
    cpp_ack->isp_buf_divert_ack.identity);
  CPP_BUF_LOW("buf_idx=%d, ref_count=%d",
    cpp_ack->isp_buf_divert_ack.buf_idx, cpp_ack->ref_count);
  PTHREAD_MUTEX_LOCK(&(ctrl->ack_list.mutex));
  gettimeofday(&(cpp_ack->in_time), NULL);
  ctrl->ack_list.list = mct_list_append(ctrl->ack_list.list,
                          cpp_ack, NULL, NULL);
  ctrl->ack_list.size++;
  PTHREAD_MUTEX_UNLOCK(&(ctrl->ack_list.mutex));
  return 0;
}

int32_t cpp_module_process_module_event(mct_module_t* module,
  mct_event_t *event)
{
  boolean ret;
  int rc = -EINVAL;
  if(!module || !event) {
    CPP_ERR("failed, module=%p, event=%p", module, event);
    return -EINVAL;
  }
  uint32_t identity = event->identity;
  cpp_module_ctrl_t *ctrl = (cpp_module_ctrl_t *) MCT_OBJECT_PRIVATE(module);
  /* handle events based on type, if not handled, forward it downstream */
  switch(event->type) {
  case MCT_EVENT_MODULE_EVENT: {
    switch(event->u.module_event.type) {
    case MCT_EVENT_MODULE_STREAM_CROP: {
      CPP_CROP_LOW("MCT_EVENT_MODULE_STREAM_CROP: identity=0x%x", identity);
      rc = cpp_module_handle_stream_crop_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
    }
      break;
    case MCT_EVENT_MODULE_ISP_INFORM_LPM: {
      CPP_LOW("MCT_EVENT_MODULE_ISP_INFORM_LPM: identity=0x%x", identity);
      rc = cpp_module_handle_inform_lpm_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
    }
      break;
    case MCT_EVENT_MODULE_STATS_AEC_UPDATE: {
      CPP_LOW("MCT_EVENT_MODULE_STATS_AEC_UPDATE: identity=0x%x", identity);
      rc = cpp_module_handle_aec_update_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
    }
      break;
    default:
      rc = 0;
      break;
    }
  }
    break;
  default:
    CPP_ERR("failed, bad event type=%d, identity=0x%x", event->type, identity);
    break;
  }
  return rc;
}

int32_t cpp_module_process_downstream_event(mct_module_t* module,
  mct_event_t* event)
{
  boolean ret;
  int32_t rc;
  if(!module || !event) {
    CPP_ERR("failed, module=%p, event=%p", module, event);
    return -EINVAL;
  }
  uint32_t identity = event->identity;
  cpp_module_ctrl_t *ctrl = (cpp_module_ctrl_t *) MCT_OBJECT_PRIVATE(module);
  /* handle events based on type, if not handled, forward it downstream */
  switch(event->type) {
  case MCT_EVENT_MODULE_EVENT: {
    switch(event->u.module_event.type) {
    case MCT_EVENT_MODULE_BUF_DIVERT:
      CPP_BUF_LOW("MCT_EVENT_MODULE_BUF_DIVERT: identity=0x%x", identity);
      rc = cpp_module_handle_buf_divert_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_ISP_OUTPUT_DIM:
      CPP_LOW("MCT_EVENT_MODULE_ISP_OUTPUT_DIM: identity=0x%x", identity);
      rc = cpp_module_handle_isp_out_dim_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_STATS_AEC_UPDATE:
      CPP_LOW("MCT_EVENT_MODULE_STATS_AEC_UPDATE: identity=0x%x", identity);
      /* Calculate and update hystersis info to perframe queue */
      cpp_module_util_get_hystersis_info(module, event);

      rc = cpp_module_handle_module_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_STATS_AEC_MANUAL_UPDATE:
      CPP_LOW("MCT_EVENT_MODULE_STATS_AEC_MANUAL_UPDATE: identity=0x%x", identity);
      rc = cpp_module_handle_aec_manual_update(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_SET_CHROMATIX_PTR:
      CPP_LOW("MCT_EVENT_MODULE_SET_CHROMATIX_PTR: identity=0x%x", identity);
      rc = cpp_module_handle_chromatix_ptr_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_STREAM_CROP:
      CPP_CROP_LOW("MCT_EVENT_MODULE_STREAM_CROP: identity=0x%x", identity);
      rc = cpp_module_handle_module_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_ISP_INFORM_LPM:
      CPP_LOW("MCT_EVENT_MODULE_ISP_INFORM_LPM: identity=0x%x", identity);
      rc = cpp_module_handle_module_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_FRAME_DROP_NOTIFY:
      CPP_BUF_LOW("MCT_EVENT_MODULE_FRAME_DROP_NOTIFY: identity=0x%x", identity);
      rc = cpp_module_handle_isp_drop_buffer(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_STATS_DIS_UPDATE:
      CPP_LOW("MCT_EVENT_MODULE_STATS_DIS_UPDATE: identity=0x%x", identity);
      rc = cpp_module_handle_dis_update_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_SET_STREAM_CONFIG:
      CPP_LOW("MCT_EVENT_MODULE_SET_STREAM_CONFIG: identity=0x%x", identity);
      ATRACE_BEGIN("CPP:set Strm config");
      rc = cpp_module_handle_stream_cfg_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      ATRACE_END();
      break;
    case MCT_EVENT_MODULE_IFACE_REQUEST_PP_DIVERT: {

      CPP_LOW("MCT_EVENT_MODULE_IFACE_REQUEST_PP_DIVERT: identity=0x%x",
        identity);
      rc = cpp_module_request_pproc_divert_info(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    }
    case MCT_EVENT_MODULE_SET_RELOAD_CHROMATIX: {
      CPP_LOW("MCT_EVENT_MODULE_SET_RELOAD_CHROMATIX: identity=0x%x", identity);
      rc = cpp_module_handle_load_chromatix_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    }
    case MCT_EVENT_MODULE_PPROC_SET_OUTPUT_BUFF:
      CPP_LOW("MCT_EVENT_MODULE_PPROC_SET_OUTPUT_BUFF:identity=0x%x", identity);
      rc = cpp_module_handle_set_output_buff_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    /* MCT_EVENT_MODULE_SOF_NOTIFY changed to MCT_EVENT_CONTROL_SOF event */
#if 0
    case MCT_EVENT_MODULE_SOF_NOTIFY:
      CPP_LOW("MCT_EVENT_MODULE_SOF_NOTIFY: identity=0x%x", identity);
      rc = cpp_module_handle_sof_notify(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
#endif
    case MCT_EVENT_MODULE_FRAME_SKIP_NOTIFY:
      CPP_LOW("MCT_EVENT_MODULE_LED_BUFFER_SKIP_NOTIFY: identity=0x%x",
        identity);
      rc = cpp_module_handle_frame_skip_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    case MCT_EVENT_MODULE_FACE_INFO:
      CPP_LOW("MCT_EVENT_MODULE_FACE_INFO: identity=0x%x", identity);
      rc = cpp_module_handle_face_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    default:
      rc = cpp_module_send_event_downstream(module, event);
      if(rc < 0) {
        CPP_ERR("failed, module_event_type=%d, identity=0x%x",
          event->u.module_event.type, identity);
        return -EFAULT;
      }
      break;
    }
    break;
  }
  case MCT_EVENT_CONTROL_CMD: {
    switch(event->u.ctrl_event.type) {
    case MCT_EVENT_CONTROL_STREAMON: {
      rc = cpp_module_handle_streamon_event(module, event);
      if(rc < 0) {
        CPP_ERR("streamon failed\n");
        return rc;
      }
      break;
    }
    case MCT_EVENT_CONTROL_STREAMOFF: {
      rc = cpp_module_handle_streamoff_event(module, event);
      if(rc < 0) {
        CPP_ERR("streamoff failed\n");
        return rc;
      }
      break;
    }
    case MCT_EVENT_CONTROL_SET_PARM: {
      rc = cpp_module_handle_set_parm_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }

      /* Since handle set param event is being reused from sof_set_param_event
         this function does not forward the event to downstream module.*/
      rc = cpp_module_send_event_downstream(module, event);
      if(rc < 0) {
        CPP_ERR("failed, module_event_type=%d, identity=0x%x",
          event->u.module_event.type, event->identity);
        return -EFAULT;
      }
      break;
    }
    case MCT_EVENT_CONTROL_PARM_STREAM_BUF: {
      rc = cpp_module_handle_set_stream_parm_event(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    }
    case MCT_EVENT_CONTROL_SOF:
      CPP_LOW("MCT_EVENT_MODULE_SOF_NOTIFY: identity=0x%x", identity);
      rc = cpp_module_handle_sof_notify(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
     case MCT_EVENT_CONTROL_SET_SUPER_PARM: {
       rc = cpp_module_handle_sof_set_parm_event(module, event);
       if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    }
    case MCT_EVENT_CONTROL_UPDATE_BUF_INFO:
      CPP_LOW("Update buf queue: APPEND\n");
      rc = cpp_module_handle_update_buf_info(module, event, FALSE);
      if(rc < 0) {
        CPP_ERR("failed to update buffer info\n");
        return rc;
      }
      break;
    case MCT_EVENT_CONTROL_REMOVE_BUF_INFO:
      CPP_LOW("Update buf queue: DELETE\n");
      rc = cpp_module_handle_update_buf_info(module, event, TRUE);
      if(rc < 0) {
        CPP_LOW("failed to update buffer info\n");
        return rc;
      }
      break;
    default:
      rc = cpp_module_send_event_downstream(module, event);
      if(rc < 0) {
        CPP_ERR("failed, control_event_type=%d, identity=0x%x",
          event->u.ctrl_event.type, identity);
        return -EFAULT;
      }
      break;
    }
    break;
  }
  default:
    CPP_ERR("failed, bad event type=%d, identity=0x%x",
      event->type, identity);
    return -EFAULT;
  }
  return 0;
}

int32_t cpp_module_process_upstream_event(mct_module_t* module,
  mct_event_t *event)
{
  int32_t rc;
  int8_t send_event = TRUE;
  if(!module || !event) {
    CPP_ERR("failed, module=%p, event=%p", module, event);
    return -EINVAL;
  }
  uint32_t identity = event->identity;
  CPP_LOW("identity=0x%x, event->type=%d", identity, event->type);
  /* todo : event handling */
  switch(event->type) {
  case MCT_EVENT_MODULE_EVENT: {
    switch(event->u.module_event.type) {
    case MCT_EVENT_MODULE_BUF_DIVERT_ACK:
      CPP_BUF_LOW("MCT_EVENT_MODULE_BUF_DIVERT_ACK: identity=0x%x", identity);
      rc = cpp_module_handle_ack_from_downstream(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      send_event = FALSE;
      break;
    case MCT_EVENT_MODULE_REQ_DIVERT:
      CPP_LOW("MCT_EVENT_MODULE_REQ_DIVERT: identity=0x%x", identity);
      rc = cpp_module_handle_request_divert(module, event);
      if(rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      send_event = FALSE;
      break;
    case MCT_EVENT_MODULE_FACE_INFO:
      CPP_LOW("MCT_EVENT_MODULE_FACE_INFO: identity=0x%x", identity);
      rc = cpp_module_handle_face_event(module, event);
      if (rc < 0) {
        CPP_ERR("failed\n");
        return rc;
      }
      break;
    default:
      break;
    }
    break;
  }
  default:
    /* all upstream events are module events */
    break;
  }
  /* forward the event upstream if we are not source/peerless module */
  if (send_event == TRUE) {
    rc = cpp_module_send_event_upstream(module, event);
    if(rc < 0) {
      CPP_ERR("failed\n");
    }
  }

  return rc;
}

/* int32_t cpp_module_set_clock_freq_for_HAL3:
 *   @ctrl - module's control data
 *   @module - module object
 *   @stream_params - current stream parameters
 *   @stream_event - Stream event showing whether it is streamon or streamoff
 *
 *  The function checks for stream information and select clock frequency.
 *  It also calculates bus bandwidth values. The results are sent to the thread
 *  to be loaded in the hardware. This function is used only in HAL context.
 **/
int32_t cpp_module_set_clock_freq_for_HAL3(cpp_module_ctrl_t *ctrl,
  mct_module_t* module __unused, cpp_module_stream_params_t *stream_params,
  uint32_t stream_event)
{
  int32_t rc = 0;
  uint32_t i;
  cpp_hardware_cmd_t cmd;
  uint32_t dim = 0;
  float input_fps;
  cpp_module_stream_clk_rate_t *clk_rate_obj;
  unsigned long clk_rate = 0;
  cpp_module_event_t *cpp_event = NULL;

  if (!ctrl || !module) {
    CPP_ERR("failed null ptr %p %p\n",
      ctrl, module);
    return -EINVAL;
  }

  cpp_event = (cpp_module_event_t*)
    malloc(sizeof(cpp_module_event_t));
  if(!cpp_event) {
    CPP_ERR("malloc() failed\n");
    return -ENOMEM;
  }
  memset(cpp_event, 0x00, sizeof(cpp_module_event_t));

  if (stream_event) {
    if(stream_params->stream_type != CAM_STREAM_TYPE_OFFLINE_PROC)
      input_fps = stream_params->hfr_skip_info.input_fps;
    else
      input_fps = MINIMUM_PROCESS_TIME;

    dim = stream_params->hw_params.output_info.width *
        stream_params->hw_params.output_info.height;
    if (dim)
      clk_rate = (float)(dim * 1.5f) * input_fps;

    /* Update the clock value & freq table index based on property or perf mode */
    rc = cpp_module_util_configure_clock_rate(ctrl, 0 /*perf mode */, &i, &clk_rate);
    CPP_LOW("freq table index %d, clk rate %lu", i, clk_rate);
    for (; i < ctrl->cpphw->hwinfo.freq_tbl_count; i++) {
      if (clk_rate < ctrl->cpphw->hwinfo.freq_tbl[i]) {
        clk_rate = ctrl->cpphw->hwinfo.freq_tbl[i];
        break;
      }
    }
    if (i == ctrl->cpphw->hwinfo.freq_tbl_count)
      clk_rate =
        ctrl->cpphw->hwinfo.freq_tbl[ctrl->cpphw->hwinfo.freq_tbl_count - 1];

    if ((clk_rate) && (clk_rate > ctrl->clk_rate)){

      if (clk_rate <= CPP_MININAL_CLK)
        ctrl->clk_rate = CPP_MININAL_CLK;
      else
        ctrl->clk_rate = clk_rate;

      cpp_event->u.clock_data.bandwidth_avg =  4 * ctrl->clk_rate;
      cpp_event->u.clock_data.bandwidth_inst = 6 * ctrl->clk_rate;
      cpp_event->u.clock_data.clk_rate = ctrl->clk_rate;
      cpp_event->type = CPP_MODULE_EVENT_CLOCK;
      cpp_event->hw_process_flag = TRUE;

      PTHREAD_MUTEX_LOCK(&(ctrl->cpp_mutex));
      if (ctrl->cpp_thread_started) {
        cpp_thread_msg_t msg;
        rc = cpp_module_enq_event(ctrl, cpp_event, CPP_PRIORITY_REALTIME);
        if (rc < 0) {
          CPP_LOW("Enqueue event failed");
          PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
          goto free_event;
        }
        msg.type = CPP_THREAD_MSG_NEW_EVENT_IN_Q;
        cpp_module_post_msg_to_thread(ctrl, msg);
      } else {
        CPP_ERR("Thread not started, return error");
        rc = -EINVAL;
        PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
        goto free_event;
      }
      PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
    }
    clk_rate_obj = malloc(sizeof(cpp_module_stream_clk_rate_t));
    if (NULL == clk_rate_obj) {
      CPP_ERR("malloc failed\n");
      rc = -ENOMEM;
      goto end;
    }

    clk_rate_obj->identity = stream_params->hw_params.identity;
    clk_rate_obj->total_load = clk_rate;
    clk_rate_obj->duplication_flag = 0;
    clk_rate_obj->system_overhead = SYSTEM_OVERHEAD;
    if ((stream_params->hw_params.rotation == 1) ||
        (stream_params->hw_params.rotation == 3))
    {
      clk_rate_obj->system_overhead = ROT_SYSTEM_OVERHEAD;
    }
    PTHREAD_MUTEX_LOCK(&(ctrl->clk_rate_list.mutex));
    ctrl->clk_rate_list.list = mct_list_append(ctrl->clk_rate_list.list,
      clk_rate_obj, NULL, NULL);
    ctrl->clk_rate_list.size++;
    PTHREAD_MUTEX_UNLOCK(&(ctrl->clk_rate_list.mutex));

  } else {

    PTHREAD_MUTEX_LOCK(&(ctrl->clk_rate_list.mutex));
    clk_rate_obj = cpp_module_find_clk_rate_by_identity(ctrl,
      stream_params->hw_params.identity);
    ctrl->clk_rate_list.list = mct_list_remove(ctrl->clk_rate_list.list,
       clk_rate_obj);
    ctrl->clk_rate_list.size--;
    free(clk_rate_obj);
    clk_rate_obj = NULL;
    clk_rate_obj = cpp_module_find_clk_rate_by_value(ctrl);
    PTHREAD_MUTEX_UNLOCK(&(ctrl->clk_rate_list.mutex));

    if ((clk_rate_obj) && (ctrl->clk_rate > clk_rate_obj->total_load)) {
      if (clk_rate_obj->total_load <= CPP_MININAL_CLK)
        ctrl->clk_rate = CPP_MININAL_CLK;
      else
        ctrl->clk_rate = clk_rate_obj->total_load;

      cpp_event->u.clock_data.bandwidth_avg =  4 * ctrl->clk_rate;
      cpp_event->u.clock_data.bandwidth_inst = 6 * ctrl->clk_rate;
      cpp_event->u.clock_data.clk_rate = ctrl->clk_rate;
      cpp_event->type = CPP_MODULE_EVENT_CLOCK;
      cpp_event->hw_process_flag = TRUE;

      PTHREAD_MUTEX_LOCK(&(ctrl->cpp_mutex));
      if (ctrl->cpp_thread_started) {
        cpp_thread_msg_t msg;
        rc = cpp_module_enq_event(ctrl, cpp_event, CPP_PRIORITY_REALTIME);
        if (rc < 0) {
          CPP_LOW("Enqueue event failed");
          PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
          goto free_event;
        }
        msg.type = CPP_THREAD_MSG_NEW_EVENT_IN_Q;
        cpp_module_post_msg_to_thread(ctrl, msg);
        PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
      } else {
        CPP_ERR("Thread not started, return error");
        rc = -EINVAL;
        PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
        goto free_event;
      }
    }
  }

end:
  return rc;
free_event:
  free(cpp_event);
  cpp_event = NULL;
  return rc;
}


/* cpp_module_update_clock_freq:
 *
 * Check for stream information and select clock frequency.
 **/
int32_t cpp_module_update_clock_freq(mct_module_t* module __unused,
  cpp_module_ctrl_t *ctrl)
{
  cpp_module_total_load_t current_load;
  float                   input_bw = 0.0f, output_bw = 0.0f;
  uint64_t                bandwidth_inst_max = 0;
  cpp_module_event_t      *cpp_event = NULL;
  unsigned long           clk_rate = 0, rounded_clk_rate = 0;
  int32_t                 rc = 0;
  uint32_t                i = 0;

  cpp_event = (cpp_module_event_t*)
    malloc(sizeof(cpp_module_event_t));
  if(!cpp_event) {
    CPP_ERR("malloc() failed\n");
    rc = -ENOMEM;
    goto end;
  }

  memset(cpp_event, 0, sizeof(cpp_module_event_t));
  memset(&current_load, 0, sizeof(cpp_module_total_load_t));
  current_load.perf_mode = CAM_PERF_NORMAL;
  rc = cpp_module_get_total_load_by_value(ctrl, &current_load);
  if (rc < 0) {
    CPP_ERR("Fail to get total load");
    rc = -EFAULT;
    goto end;
  }

  /* output bandwidth does not need to acomodate for padding factor
     input bandwidth is total load multiplied by the padding factor
     clock rate total load multiplied by the padding factor divided
     by throughput*/
  output_bw = (float)(current_load.output_bw + current_load.output_ref_load);
  input_bw = (float)(current_load.input_bw + current_load.input_ref_load) *
    PADDING_FACTOR;
  clk_rate = ((float)(current_load.clk) * PADDING_FACTOR) / MINIMUM_CPP_THROUGHPUT;

  /* Update the clock value & freq table index based on property or perf mode */
  rc = cpp_module_util_configure_clock_rate(ctrl,
    current_load.perf_mode , &i, &clk_rate);
  for (; i < ctrl->cpphw->hwinfo.freq_tbl_count; i++) {
    if (clk_rate < ctrl->cpphw->hwinfo.freq_tbl[i]) {
      rounded_clk_rate = ctrl->cpphw->hwinfo.freq_tbl[i];
        break;
    }
  }

  if (i == ctrl->cpphw->hwinfo.freq_tbl_count)
    rounded_clk_rate =
      ctrl->cpphw->hwinfo.freq_tbl[ctrl->cpphw->hwinfo.freq_tbl_count - 1];

  cpp_event->u.clock_data.bandwidth_avg =
    (output_bw + input_bw) + current_load.duplication_load;

  cpp_event->u.clock_data.bandwidth_inst =
    cpp_event->u.clock_data.bandwidth_avg;

  bandwidth_inst_max = rounded_clk_rate * CPP_MAX_ENGINE; //FE+WE+TNE
  if (cpp_event->u.clock_data.bandwidth_inst > bandwidth_inst_max) {
    cpp_event->u.clock_data.bandwidth_inst = bandwidth_inst_max;
  }
  cpp_event->u.clock_data.clk_rate = rounded_clk_rate;

  cpp_event->type = CPP_MODULE_EVENT_CLOCK;
  cpp_event->hw_process_flag = TRUE;

  PTHREAD_MUTEX_LOCK(&(ctrl->cpp_mutex));
  if (ctrl->cpp_thread_started) {
    rc = cpp_module_enq_event(ctrl, cpp_event, CPP_PRIORITY_REALTIME);
    if (rc < 0) {
      CPP_ERR("Enqueue event failed");
      goto free_event;
    }
    cpp_thread_msg_t msg;
    msg.type = CPP_THREAD_MSG_NEW_EVENT_IN_Q;
    cpp_module_post_msg_to_thread(ctrl, msg);
    goto unlock;
  } else {
    CPP_ERR("Thread not started");
    rc = -EINVAL;
    goto free_event;
  }

  CPP_HIGH("input_bw  %lld, output_bw %lld clk %ld, ab = %lld, ib = %lld",
    current_load.input_bw, current_load.output_bw,
    clk_rate, cpp_event->u.clock_data.bandwidth_avg,
    cpp_event->u.clock_data.bandwidth_inst);
free_event:
  free(cpp_event);
  cpp_event = NULL;
unlock:
  PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
end:
  return rc;
}

/* cpp_module_set_clock_freq:
 *
 * Check for stream information and select clock frequency.
 **/
int32_t cpp_module_set_clock_freq(cpp_module_ctrl_t *ctrl,
  mct_module_t* module __unused,
  cpp_module_stream_params_t *stream_params, uint32_t stream_event)
{
  int32_t rc = 0;
  cpp_hardware_cmd_t cmd;
  uint32_t input_dim = 0, output_dim = 0, dim = 0;
  float input_fps;
  float input_bytes_per_pixel, output_bytes_per_pixel;
  cpp_module_stream_clk_rate_t *clk_rate_obj = NULL;
  uint64_t total_stream_load;


  if (!ctrl || !module) {
    CPP_ERR("failed NULL ptr %p %p\n", ctrl, module);
    return -EINVAL;
  }

  if (stream_event) {

    switch (stream_params->hw_params.output_info.plane_fmt) {
    case CPP_PARAM_PLANE_CRCB444:
    case CPP_PARAM_PLANE_CBCR444: {
      output_bytes_per_pixel = 3;
      break;
    }
    case CPP_PARAM_PLANE_CRCB422:
    case CPP_PARAM_PLANE_CBCR422: {
      output_bytes_per_pixel = 2;
      break;
    }
    default: {
      output_bytes_per_pixel = 1.5f;
      break;
    }

    }

    switch (stream_params->hw_params.input_info.plane_fmt) {
    case CPP_PARAM_PLANE_CRCB444:
    case CPP_PARAM_PLANE_CBCR444: {
      input_bytes_per_pixel = 3;
      break;
    }
    case CPP_PARAM_PLANE_CRCB422:
    case CPP_PARAM_PLANE_CBCR422: {
      input_bytes_per_pixel = 2;
      break;
    }
    default: {
      input_bytes_per_pixel = 1.5f;
      break;
    }
    }

    if(stream_params->priority == CPP_PRIORITY_REALTIME)
      input_fps = stream_params->hfr_skip_info.input_fps;
    else
      input_fps = MINIMUM_PROCESS_TIME;

    output_dim = (float)stream_params->hw_params.output_info.width *
      stream_params->hw_params.output_info.height * output_bytes_per_pixel;

    input_dim = (float)stream_params->hw_params.input_info.width *
      stream_params->hw_params.input_info.height * input_bytes_per_pixel;
    dim = MAX(input_dim, output_dim);

    if (dim)
      total_stream_load = (float)dim * input_fps;
    else
      total_stream_load = 0;

    clk_rate_obj = malloc(sizeof(cpp_module_stream_clk_rate_t));
    if (NULL == clk_rate_obj) {
      CPP_ERR("malloc failed\n");
      return -ENOMEM;
    }
    clk_rate_obj->identity = stream_params->hw_params.identity;
    clk_rate_obj->total_load = total_stream_load;
    clk_rate_obj->system_overhead = SYSTEM_OVERHEAD;
    if ((stream_params->hw_params.rotation == 1) ||
        (stream_params->hw_params.rotation == 3))
    {
        clk_rate_obj->system_overhead = ROT_SYSTEM_OVERHEAD;
    }
    clk_rate_obj->duplication_flag = 0;
    clk_rate_obj->tnr_on_flag = stream_params->hw_params.tnr_enable;
    clk_rate_obj->dsdn_on_flag = stream_params->hw_params.dsdn_enable;
    clk_rate_obj->perf_mode = stream_params->stream_info->perf_mode;
    clk_rate_obj->ubwc_on_flag =
      UBWC_ENABLE(stream_params->hw_params.output_info.plane_fmt);

    PTHREAD_MUTEX_LOCK(&(ctrl->clk_rate_list.mutex));
    ctrl->runtime_clk_update = TRUE;
    ctrl->clk_rate_list.list = mct_list_append(ctrl->clk_rate_list.list,
      clk_rate_obj, NULL, NULL);
    ctrl->clk_rate_list.size++;
    PTHREAD_MUTEX_UNLOCK(&(ctrl->clk_rate_list.mutex));
    CPP_DBG("stream type %d iden 0x%x input:%dx%d output %dx%d fps %f "
        "load %lld, duplication_flag %d system_overhead %f",
      stream_params->stream_type, stream_params->identity,
      stream_params->hw_params.input_info.width,
      stream_params->hw_params.input_info.height,
      stream_params->hw_params.output_info.width,
      stream_params->hw_params.output_info.height,
      input_fps,
      clk_rate_obj->total_load, clk_rate_obj->duplication_flag,
      clk_rate_obj->system_overhead);

  } else {
    PTHREAD_MUTEX_LOCK(&(ctrl->clk_rate_list.mutex));
    ctrl->runtime_clk_update = TRUE;
    clk_rate_obj = cpp_module_find_clk_rate_by_identity(ctrl,
      stream_params->hw_params.identity);
    if (clk_rate_obj == NULL) {
      CPP_ERR("Cannot find object for this stream");
      PTHREAD_MUTEX_UNLOCK(&(ctrl->clk_rate_list.mutex));
    } else {
      ctrl->clk_rate_list.list = mct_list_remove(ctrl->clk_rate_list.list,
        clk_rate_obj);
      ctrl->clk_rate_list.size--;
      free(clk_rate_obj);
      PTHREAD_MUTEX_UNLOCK(&(ctrl->clk_rate_list.mutex));
    }
  }

  rc =  cpp_module_update_clock_freq(module, ctrl);
  if (rc < 0) {
    CPP_ERR("update frquency failed! rc = %d", rc);
  }

  return rc;
}

/* cpp_module_add_linked_streams:
 *
 * this function add all linked streams,
 * 2 way mapping cur_stream <->linked streams
 **/
int32_t cpp_module_add_linked_streams(
  cpp_module_stream_params_t *stream_params,
  cpp_module_link_info_t linked_info)
{
  uint32_t i = 0;
  uint32_t k = 0;
  cpp_module_stream_params_t *linked_stream_params = NULL;

  /* set linked stream */
  memset(stream_params->linked_streams, 0,
    sizeof(cpp_module_stream_params_t *) * CPP_MODULE_MAX_STREAMS);

  if (linked_info.num_linked_streams > 0) {
    for (k = 0; k < CPP_MODULE_MAX_STREAMS; k++) {
      linked_stream_params = linked_info.linked_stream_params[k];
      if (linked_stream_params != NULL &&
          linked_stream_params->identity != 0) {
        /*new stream are clean, so no need to find empty slot*/
        stream_params->linked_streams[stream_params->num_linked_streams++] =
          linked_stream_params;

        /*find empty slot in exisitng streams and link to this new stream*/
        for (i = 0; i < CPP_MODULE_MAX_STREAMS; i++) {
          if (linked_stream_params->linked_streams[i] == NULL) {
            linked_stream_params->linked_streams[i] = stream_params;
            linked_stream_params->num_linked_streams++;
            break;
          }
        }
      }
    }
  }

  return 0;
}

/* cpp_module_notify_add_stream:
 *
 * creates and initializes the stream-specific paramater structures when a
 * stream is reserved in port
 **/
int32_t cpp_module_notify_add_stream(mct_module_t* module, mct_port_t* port,
  mct_stream_info_t* stream_info)
{
  cpp_module_loglevel();
  uint32_t identity = 0;
  uint32_t session_id = 0;
  uint32_t i,j,k;
  int      rc = 0;
  boolean success = FALSE;
  cpp_hardware_params_t *hw_params = NULL;
  cam_pp_feature_config_t *pp_config = NULL;
  cpp_module_session_params_t *session_params = NULL;
  cpp_module_stream_params_t *stream_params = NULL;
  cpp_module_session_params_t *linked_session_params = NULL;
  cpp_module_stream_params_t *linked_stream_params = NULL;
  cpp_module_link_info_t linked_info;
  cpp_module_ctrl_t *ctrl = NULL;

  memset(&linked_info, 0 , sizeof(cpp_module_link_info_t));

  if(!module || !stream_info || !port) {
    CPP_ERR("failed, module=%p, port=%p, stream_info=%p\n",
      module, port, stream_info);
    return -EINVAL;
  }

  ctrl = (cpp_module_ctrl_t *) MCT_OBJECT_PRIVATE(module);
  if(!ctrl) {
    CPP_ERR("failed, module=%p\n", module);
    return -EINVAL;
  }

  identity = stream_info->identity;
  session_id = CPP_GET_SESSION_ID(identity);
  CPP_LOW("identity=0x%x\n", identity);

  /* find if a stream is already added on this port. If yes, we need to link
     that stream with this. (only for continuous streams)*/
  rc = cpp_port_get_linked_info(ctrl, port, identity, &linked_info);
  if (rc < 0) {
      CPP_ERR("failed cpp_port_get_linked_info\n");
      return -EINVAL;
  }

  for(i=0; i < CPP_MODULE_MAX_SESSIONS; i++) {
    if(ctrl->session_params[i]) {
      if(ctrl->session_params[i]->session_id == session_id) {
        for(j=0; j < CPP_MODULE_MAX_STREAMS; j++) {
          if(ctrl->session_params[i]->stream_params[j] == NULL) {
            ctrl->session_params[i]->stream_params[j] =
              (cpp_module_stream_params_t *)
                 malloc(sizeof(cpp_module_stream_params_t));
            if (!ctrl->session_params[i]->stream_params[j]) {
              CPP_ERR("failed: to malloc\n");
              return -ENOMEM;
            }
            memset(ctrl->session_params[i]->stream_params[j], 0x00,
              sizeof(cpp_module_stream_params_t));
            ctrl->session_params[i]->stream_params[j]->identity = identity;
            ctrl->session_params[i]->stream_params[j]->parent = port;

            /* assign priority */
            if(stream_info->stream_type != CAM_STREAM_TYPE_OFFLINE_PROC) {
              ctrl->session_params[i]->stream_params[j]->priority =
                CPP_PRIORITY_REALTIME;
            } else {
              ctrl->session_params[i]->stream_params[j]->priority =
                CPP_PRIORITY_OFFLINE;
            }

            if (stream_info->stream_type == CAM_STREAM_TYPE_VIDEO) {
              /* initialize input/output fps values */
              ctrl->session_params[i]->stream_params[j]->
                hfr_skip_info.input_fps =
                ctrl->session_params[i]->fps_range.video_max_fps;
              ctrl->session_params[i]->stream_params[j]->
                hfr_skip_info.output_fps =
                ctrl->session_params[i]->fps_range.video_max_fps;
            } else {
              /* initialize input/output fps values */
              ctrl->session_params[i]->stream_params[j]->
                hfr_skip_info.input_fps =
                ctrl->session_params[i]->fps_range.max_fps;
              ctrl->session_params[i]->stream_params[j]->
                hfr_skip_info.output_fps =
                ctrl->session_params[i]->fps_range.max_fps;
            }
            ctrl->session_params[i]->stream_params[j]->
              hfr_skip_info.skip_count = 0;
            /* hfr_skip_required in only in preview stream */
            ctrl->session_params[i]->stream_params[j]->
              hfr_skip_info.skip_required =
                ((stream_info->stream_type == CAM_STREAM_TYPE_PREVIEW) ||
                (stream_info->stream_type == CAM_STREAM_TYPE_CALLBACK) ||
                (stream_info->stream_type == CAM_STREAM_TYPE_VIDEO))?
                  TRUE : FALSE;

            /* assign stream type */
            ctrl->session_params[i]->stream_params[j]->stream_type =
              stream_info->stream_type;
            hw_params = &ctrl->session_params[i]->stream_params[j]->hw_params;
            /* output dimensions */
            hw_params->output_info.width = stream_info->dim.width;
            hw_params->output_info.height = stream_info->dim.height;
            hw_params->output_info.stride =
              stream_info->buf_planes.plane_info.mp[0].stride;
            hw_params->output_info.scanline =
              stream_info->buf_planes.plane_info.mp[0].scanline;
            hw_params->output_info.frame_len =
              stream_info->buf_planes.plane_info.frame_len;
            CPP_INFO(":width %d, height %d, stride %d, scanline %d, framelen %d",
              hw_params->output_info.width, hw_params->output_info.height,
              hw_params->output_info.stride, hw_params->output_info.scanline,
              hw_params->output_info.frame_len);
            for (k = 0; k < stream_info->buf_planes.plane_info.num_planes; k++) {
              hw_params->output_info.plane_info[k].plane_offset_x =
                 stream_info->buf_planes.plane_info.mp[k].offset_x;
              hw_params->output_info.plane_info[k].plane_offset_y =
                 stream_info->buf_planes.plane_info.mp[k].offset_y;
              hw_params->output_info.plane_info[k].meta_stride =
                stream_info->buf_planes.plane_info.mp[k].meta_stride;
              hw_params->output_info.plane_info[k].meta_scanline =
                stream_info->buf_planes.plane_info.mp[k].meta_scanline;
              hw_params->output_info.plane_info[k].meta_len =
                stream_info->buf_planes.plane_info.mp[k].meta_len;
              hw_params->output_info.plane_info[k].plane_offsets =
                stream_info->buf_planes.plane_info.mp[k].offset;
              hw_params->output_info.plane_info[k].plane_len =
                stream_info->buf_planes.plane_info.mp[k].len;
              CPP_INFO("offset_x %d, offset_y %d, offset %d, meta_len %d,"
                "meta_scanline %d, meta_stride %d, plane_len %d",
                stream_info->buf_planes.plane_info.mp[k].offset_x,
                stream_info->buf_planes.plane_info.mp[k].offset_y,
                stream_info->buf_planes.plane_info.mp[k].offset,
                stream_info->buf_planes.plane_info.mp[k].meta_len,
                stream_info->buf_planes.plane_info.mp[k].meta_scanline,
                stream_info->buf_planes.plane_info.mp[k].meta_stride,
                stream_info->buf_planes.plane_info.mp[k].len);
            }

            hw_params->stream_type = stream_info->stream_type;
            hw_params->ez_tune_asf_enable = 1;
            #ifdef CAMERA_FEATURE_WNR_SW
              hw_params->ez_tune_wnr_enable = 0;
            #else
              hw_params->ez_tune_wnr_enable = 1;
            #endif
            hw_params->diagnostic_enable =
              ctrl->session_params[i]->hw_params.diagnostic_enable;
            hw_params->asf_info.cpp_fw_version = ctrl->cpphw->fw_version;
            /* rotation/flip */
            if (stream_info->stream_type == CAM_STREAM_TYPE_OFFLINE_PROC) {
                pp_config = &stream_info->reprocess_config.pp_feature_config;
                if (pp_config->feature_mask & CAM_QCOM_FEATURE_SHARPNESS)
                  hw_params->asf_mask = TRUE;
                if (pp_config->feature_mask & CAM_QCOM_FEATURE_DENOISE2D)
                  hw_params->denoise_mask = TRUE;
                if (pp_config->feature_mask & CAM_QCOM_FEATURE_DSDN)
                  hw_params->dsdn_mask = TRUE;
            } else {
                pp_config = &stream_info->pp_config;
                if (stream_info->pp_config.feature_mask & CAM_QCOM_FEATURE_SHARPNESS) {
                  hw_params->asf_mode =
                    ctrl->session_params[i]->hw_params.asf_mode;
                  hw_params->sharpness_level =
                    ctrl->session_params[i]->hw_params.sharpness_level;
                  hw_params->asf_mask = TRUE;
                } else {
                  hw_params->asf_mode = CPP_PARAM_ASF_OFF;
                  hw_params->asf_mask = FALSE;
                  hw_params->sharpness_level = 0.0;
                }

                if (pp_config->feature_mask & CAM_QCOM_FEATURE_DENOISE2D) {
                  hw_params->denoise_mask = TRUE;
                  hw_params->denoise_enable = ctrl->session_params[i]->hw_params.denoise_enable;
                } else {
                  hw_params->denoise_mask = FALSE;
                  hw_params->denoise_enable = FALSE;
                }

                /* Update the CPP CDS feature mask and cds value from session params */
                if (pp_config->feature_mask & CAM_QCOM_FEATURE_DSDN) {
                  hw_params->dsdn_mask = TRUE;
                  hw_params->dsdn_enable =
                  ctrl->session_params[i]->hw_params.dsdn_enable;
                }

                hw_params->hdr_mode = ctrl->session_params[i]->hw_params.hdr_mode;
            }


            hw_params->mirror = pp_config->flip;
            hw_params->rotation = 0;
            if (pp_config->feature_mask & CAM_QCOM_FEATURE_ROTATION) {
              CPP_DBG("Rotation=%d", pp_config->rotation);
              if (pp_config->rotation == ROTATE_0) {
                hw_params->rotation = 0;
              } else if (pp_config->rotation == ROTATE_90) {
                hw_params->rotation = 1;
              } else if (pp_config->rotation == ROTATE_180) {
                hw_params->rotation = 2;
              } else if (pp_config->rotation == ROTATE_270) {
                hw_params->rotation = 3;
              }
            }

            if ((pp_config->feature_mask & CAM_QCOM_FEATURE_CPP_TNR) &&
              (ctrl->cpphw->hwinfo.caps & TNR_CAPS)) {
              hw_params->tnr_mask = TRUE;
              hw_params->tnr_enable =
                ctrl->session_params[i]->hw_params.tnr_enable;
            }

            if(pp_config->feature_mask & CAM_QCOM_FEATURE_SCALE) {
              hw_params->scale_enable = 1;
            } else {
              hw_params->scale_enable = 0;
            }
            if(pp_config->feature_mask & CAM_QCOM_FEATURE_CROP) {
              hw_params->crop_enable = 1;
            } else {
              hw_params->crop_enable = 0;
            }
            if(pp_config->feature_mask & CAM_QCOM_FEATURE_CDS) {
              hw_params->downsample_mask = 1;
            } else {
              hw_params->downsample_mask = 0;
            }

            /* format info */
            if (stream_info->fmt == CAM_FORMAT_YUV_420_NV12 ||
                stream_info->fmt == CAM_FORMAT_YUV_420_NV12_VENUS) {
              hw_params->output_info.plane_fmt = CPP_PARAM_PLANE_CBCR;
            } else if (stream_info->fmt == CAM_FORMAT_YUV_420_NV21 ||
                stream_info->fmt == CAM_FORMAT_YUV_420_NV21_VENUS) {
              hw_params->output_info.plane_fmt = CPP_PARAM_PLANE_CRCB;
            } else if (stream_info->fmt == CAM_FORMAT_YUV_422_NV16) {
              hw_params->output_info.plane_fmt = CPP_PARAM_PLANE_CBCR422;
            } else if (stream_info->fmt == CAM_FORMAT_YUV_422_NV61) {
              hw_params->output_info.plane_fmt = CPP_PARAM_PLANE_CRCB422;
            } else if (stream_info->fmt == CAM_FORMAT_YUV_420_YV12) {
              hw_params->output_info.plane_fmt = CPP_PARAM_PLANE_CRCB420;
            } else if (stream_info->fmt == CAM_FORMAT_YUV_420_NV12_UBWC) {
              CPP_HIGH("CPP_PARAM_PLANE_CBCR_UBWC");
              hw_params->output_info.plane_fmt = CPP_PARAM_PLANE_CBCR_UBWC;
            } else if (stream_info->fmt == CAM_FORMAT_YUV_444_NV24) {
              hw_params->output_info.plane_fmt = CPP_PARAM_PLANE_CBCR444;
            } else if (stream_info->fmt == CAM_FORMAT_YUV_444_NV42) {
              hw_params->output_info.plane_fmt = CPP_PARAM_PLANE_CRCB444;
            } else {
              CPP_ERR("failed. Format not supported %d\n", stream_info->fmt);
              return -EINVAL;
            }

            CPP_INFO(": stream %d, fmt %x, asf_mode %d, sharpness_level %f,"
              "asf mask %d, denoise %d, denoise_mask %d, dsdn mask %d,"
              "dsdn enable %d, tnr mask %d, tnr enable %d, ds_mask %d",
               hw_params->stream_type, stream_info->fmt, hw_params->asf_mode,
               hw_params->sharpness_level, hw_params->asf_mask,
               hw_params->denoise_enable, hw_params->denoise_mask,
               hw_params->dsdn_mask, hw_params->dsdn_enable,
               hw_params->tnr_mask, hw_params->tnr_enable,
               hw_params->downsample_mask);
            /* set linked stream */
            cpp_module_add_linked_streams(ctrl->session_params[i]->
              stream_params[j], linked_info);

            ctrl->session_params[i]->stream_params[j]->cur_frame_id = 0;
            if (linked_stream_params) {
              linked_stream_params->cur_frame_id = 0;
            }

            ctrl->session_params[i]->stream_params[j]->stream_info =
              stream_info;

            /* use output-duplication is possible for linked streams */
            cpp_module_set_output_duplication_flag(
              ctrl->session_params[i]->stream_params[j]);

            hw_params->identity = identity;

            /* initialize the mutex for stream_params */
            pthread_mutex_init(
              &(ctrl->session_params[i]->stream_params[j]->mutex), NULL);
            ctrl->session_params[i]->stream_count++;

            cpp_module_pbf_init(ctrl->cpphw->hwinfo.caps,
              &ctrl->pbf_module_func_tbl);

            /* Switch on PBF if TNR feature mask is ON for preview / video
               or callback streams */
            if ((ctrl->pbf_module_func_tbl.set) && (hw_params->tnr_mask) &&
              ((stream_info->stream_type == CAM_STREAM_TYPE_PREVIEW) ||
              (stream_info->stream_type == CAM_STREAM_TYPE_VIDEO) ||
              (stream_info->stream_type == CAM_STREAM_TYPE_CALLBACK))) {
              char pbf_prop[PROPERTY_VALUE_MAX];
              uint32_t enable = FALSE;
              memset(pbf_prop, 0, sizeof(pbf_prop));
              property_get("camera.cpp.pbf.enable", pbf_prop, "0");
              enable = atoi(pbf_prop);
              ctrl->pbf_module_func_tbl.set(ctrl, hw_params->identity,
                enable);
            }

            cpp_module_tnr_init(ctrl->cpphw->hwinfo.caps,
              &ctrl->tnr_module_func_tbl);

            if (stream_info->streaming_mode == CAM_STREAMING_MODE_BATCH) {
              CPP_HIGH("Reduce pending buf status for stream %d",
                stream_info->stream_type);
              cpp_hardware_cmd_t cmd;
              cmd.type = CPP_HW_CMD_UPDATE_PENDING_BUF;
              cmd.u.status = 0;
              rc = cpp_hardware_process_command(ctrl->cpphw, cmd);
              if (rc < 0) {
                CPP_ERR("update pending buf failed %d", rc);
              }
            }
            success = TRUE;
            cpp_module_dump_stream_params(
              ctrl->session_params[i]->stream_params[j], __func__, __LINE__);
            break;
          }
        }
      }
    }
    if(success == TRUE) {
      break;
    }
  }
  if(success == FALSE) {
    CPP_ERR("failed, identity=0x%x", identity);
    return -EFAULT;
  }
  CPP_HIGH("info: success, identity=0x%x", identity);
  return 0;
}

/* cpp_module_remove_linked_streams:
 *
 *  destroys stream-specific data structures when a stream is unreserved
 *  in port
 **/
int32_t cpp_module_remove_linked_streams(
  cpp_module_stream_params_t *stream_params)
{
  uint32_t i = 0;
  uint32_t j = 0;
  cpp_module_stream_params_t *linked_stream_params = NULL;
  cpp_module_stream_params_t *linked_stream_params2 = NULL;

  if (stream_params == NULL) {
    CPP_ERR("failed, NULL ptr %p", stream_params);
    return -1;
  }

  for (i = 0; i < CPP_MODULE_MAX_STREAMS; i++) {
    linked_stream_params = stream_params->linked_streams[i];
    if (linked_stream_params != NULL &&
        linked_stream_params->identity != 0) {
      for (j = 0; j < CPP_MODULE_MAX_STREAMS; j++) {
        linked_stream_params2 = linked_stream_params->linked_streams[j];
        if (linked_stream_params2 != NULL &&
            linked_stream_params2->identity != 0) {
          if (linked_stream_params2->identity == stream_params->identity) {
            linked_stream_params->linked_streams[j] = NULL;
            linked_stream_params->num_linked_streams--;
          }
        }
      }
      stream_params->linked_streams[i] = NULL;
    }
  }

  /*after done removing the streams, set num linked = 0*/
  stream_params->num_linked_streams = 0;

  return 0;
}
/* cpp_module_notify_remove_stream:
 *
 *  destroys stream-specific data structures when a stream is unreserved
 *  in port
 **/
int32_t cpp_module_notify_remove_stream(mct_module_t* module, uint32_t identity)
{
  uint32_t session_id;
  uint32_t i,j;
  boolean success = FALSE;
  cpp_hardware_cmd_t cmd;
  cpp_module_ctrl_t *ctrl = NULL;
  int32_t rc = 0;

  if(!module) {
    CPP_ERR("failed\n");
    return -EINVAL;
  }

  ctrl = (cpp_module_ctrl_t *) MCT_OBJECT_PRIVATE(module);
  if(!ctrl) {
    CPP_ERR("failed\n");
    return -EINVAL;
  }

  CPP_HIGH("identity=0x%x\n", identity);
  /* destroy stream specific params structure */

  session_id = CPP_GET_SESSION_ID(identity);
  for(i=0; i < CPP_MODULE_MAX_SESSIONS; i++) {
    if(ctrl->session_params[i]) {
      if(ctrl->session_params[i]->session_id == session_id) {
        for(j=0; j < CPP_MODULE_MAX_STREAMS; j++) {
          if(ctrl->session_params[i]->stream_params[j]) {
            if(ctrl->session_params[i]->stream_params[j]->identity ==
                identity) {
              /* remove linked params */
              cpp_module_remove_linked_streams(ctrl->session_params[i]->stream_params[j]);

              if (ctrl->session_params[i]->stream_params[j]->stream_info->
                streaming_mode == CAM_STREAMING_MODE_BATCH) {
                CPP_HIGH("Increase pending buf status for stream %d",
                  ctrl->session_params[i]->stream_params[j]->stream_info->stream_type);
                cpp_hardware_cmd_t cmd;
                cmd.type = CPP_HW_CMD_UPDATE_PENDING_BUF;
                cmd.u.status = 1;
                rc = cpp_hardware_process_command(ctrl->cpphw, cmd);
                if (rc < 0) {
                  CPP_ERR("update pending buf failed %d", rc);
                }
              }
              pthread_mutex_destroy(
                &(ctrl->session_params[i]->stream_params[j]->mutex));
              free(ctrl->session_params[i]->stream_params[j]);
              ctrl->session_params[i]->stream_params[j] = NULL;
              ctrl->session_params[i]->stream_count--;

              if (ctrl->session_params[i]->stream_count == 0)
                memset(&ctrl->session_params[i]->valid_stream_ids[0], 0,
                  sizeof(ctrl->session_params[i]->valid_stream_ids));

              success = TRUE;
              break;
            }
          }
        }
      }
    }
    if(success == TRUE) {
      break;
    }
  }
  if(success == FALSE) {
    CPP_ERR("failed, identity=0x%x", identity);
    return -EFAULT;
  }
  return 0;
}
