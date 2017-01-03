/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#include "test_suite_imglib.h"

#undef LOG_TAG
#define LOG_TAG "mm-camera-test_suite-imglib"

using namespace tm_interface;

/** Name: testcase_open
 *
 *  Arguments/Fields:
 *  Return: List of unit test cases exposed by the test suite
 *
 *  Description:
 *  This is a common signature method used for
 *  loading symbols dynamically into the test manager framework.
 *  It returns an object to the current test library to be used
 *  in test suite.
 **/
extern "C" ITestModule *test_suite_open(TM_Interface &tmi)
{
  test_module_imglib *ts = new test_module_imglib(tmi);
  return ts;
}

/** Name: test_module_imglib
 *
 *  Arguments/Fields:
 *  tmi: Tm_Interface object
 *  Description:
 *    Constructor method
 **/
test_module_imglib::test_module_imglib(TM_Interface &tmi)
  : ITestModule(tmi),
    m_tminterface(tmi)
{
  TMDBG("%s", __func__);
}

/** Name: ~test_module_imglib
 *
 *  Arguments/Fields:
 *
 *  Description:
 *    Destructor method
 **/
test_module_imglib::~test_module_imglib()
{
  TMDBG("%s", __func__);
}


/** Name: registerTestCases
 *
 *  Arguments/Fields:
 *
 *  Description:
 *  Registers test cases with ITestModule
 *  so that they can be invoked by client (TestSuite)
 *  in a variety of ways.
 **/
void test_module_imglib::registerTestCases()
{
  uint8_t index = 0;
  unit_function_template_t local_fn_list[] = {
    {"test faces", (test_fn)&unitTest1},
    {"unitTest2", (test_fn)&unitTest2},
  };
  num_test_cases = sizeof(local_fn_list) / sizeof(unit_function_template_t);
  for (index = 0; index < num_test_cases; index++) {
    strlcpy(list_testcases[index].name, local_fn_list[index].name,
      MAX_FN_NAME_LEN);
    list_testcases[index].test_function = local_fn_list[index].test_function;
  }
  TMDBG("Registering %d test cases", num_test_cases);
}


/** Name: unitTest1
 *
 *  Arguments/Fields:
 *  @SuiteCtx: Current class context passed by TestSuite
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends all configurations needed to stream on modules.
 *  Sends default parameters and does stream on.
 *  Saves incoming frames to output file and prints first 4 pixels of ouput
 *  buffer.
 *  Waits for a number of frames specified in configuration file and streams off.
 *
 **/
boolean test_module_imglib::unitTest1 (void *SuiteCtx)
{
  boolean                 ret = FALSE, main_ret;
  int                     rc;
  uint32_t                counter = 0; // Count loop iterations
  tm_intf_buffer_desc_t   buf_desc;
  cam_fd_set_parm_t       fd_set_parm;
  uint32_t                divert_mask;
  uint32_t                ack_flag;
  sensor_out_info_t       sen_out_info;
  sensor_set_dim_t        sensor_dims;
  mct_bus_msg_t           fd_bus_msg;
  tm_img_buf_desc_t       sec_img_buf_desc;
  tm_img_buf_desc_t       *p_sec_buf = NULL;
  tm_img_buf_desc_t       *p_cur_buf = NULL;
  uint32_t                in_inages_idx = 1;
  uint32_t                data_len;

  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");

  test_module_imglib      *pTestSuite = (test_module_imglib *)SuiteCtx;
  TM_Interface            *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t         input = tmi->getInput();
  SOF_Timer               soft = SOF_Timer();
  int                     rotation;
  uint32_t                str_identity = tmi->getStreamIdentity(input.stream_types[0]);
  superParam              sup = superParam(input.is_first_module_src);

  UCDBG(": Enter: identity = 0x%x", str_identity);

  tmi->fillSensorInfo(&sen_out_info, &sensor_dims);

  ret = tmi->sendModuleEventDown(MCT_EVENT_MODULE_SET_STREAM_CONFIG,
    &sen_out_info, str_identity);

  ret = sup.loadDefaultParams(&input);
  ASSERTTRUE(ret, "error in setting default params");


  /* Send super-params downstream */
  ret = sup.sendSuperParams(tmi, 0, str_identity);
  ASSERTTRUE(ret, "error in sending super params");

  ret = tmi->sendIspOutDims(input.stream_types[0]);
  ASSERTTRUE(ret, "error in sending ISP out dims");

  ret = tmi->sendStreamCropDims(input.stream_types[0]);
  ASSERTTRUE(ret, "error in sending stream crop dims");

  UCDBG("number of input images is: %d", input.num_in_images);

  memset(&fd_set_parm, 0, sizeof(cam_fd_set_parm_t));
  fd_set_parm.fd_mode = CAM_FACE_PROCESS_MASK_DETECTION;
  fd_set_parm.num_fd = 4;

  if (TRUE == input.read_input_y_only) {
    // Because will read just Y component need to read less than expected data.
    // Default input buffer is allocated but input file is not read.
    in_inages_idx = 0;
    data_len = input.sizes.input_width * input.sizes.input_height;
    tmi->readInputFile(input.input_filename[in_inages_idx++], data_len,
      NULL);
    tmi->getInBufDesc(&buf_desc, str_identity, NULL);
    memset((void *)((uint8_t *)buf_desc.vaddr + data_len), 0x80, data_len / 2);
    // First just allocate second buffer and then read just Y component data.
    ret = tmi->allocateAndReadInputBuf(NULL, &sec_img_buf_desc, FALSE);
    ASSERTTRUE(ret, "cannot allocate second input image buffer");

    tmi->getInBufDesc(&buf_desc, str_identity, &sec_img_buf_desc);
    memset((void *)((uint8_t *)buf_desc.vaddr + data_len), 0x80, data_len / 2);
  } else {
    ret = tmi->allocateAndReadInputBuf(input.input_filename[in_inages_idx++],
      &sec_img_buf_desc, TRUE);
    ASSERTTRUE(ret, "cannot allocate second input image buffer");
    tmi->getInBufDesc(&buf_desc, str_identity, NULL);
    data_len = buf_desc.size;
  }

  rotation = 90;
  ret = tmi->sendParam(CAM_INTF_META_JPEG_ORIENTATION, &rotation);
  ASSERTTRUE(ret, "error in send jpeg orientation");
  ret = tmi->sendParam(CAM_INTF_PARM_FD, &fd_set_parm);
  ASSERTTRUE(ret, "error in send FD");

  tmi->sendModuleEventDown(MCT_EVENT_MODULE_QUERY_DIVERT_TYPE,
    &divert_mask, str_identity);
  UCDBG("divert_mask: %x", divert_mask);

  /* Stream-ON */
  ret = tmi->streamOn(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming on");

  soft.startAsyncTimer(30);

  rc = soft.waitForTimer(50000);
  if (ETIMEDOUT == rc) {
    TMDBG_ERROR("Wait for SOF timeout");
  }
  tmi->prepSendCtrlSof(str_identity, counter);

    ret = tmi->sendBufDivert(input.stream_types[0], counter, &ack_flag, p_sec_buf);
    ASSERTTRUE(ret, "error in send buf divert");

  counter++;

  while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION,
    &counter, input.stream_types[0])) {

    if (FALSE == ack_flag) {
      ret = tmi->bufReceiveMsg(&buf_desc);
      EXITTRUE(ret, STREAM_OFF, "error in receiving message");

      UCDBG("receive frame no: %d", buf_desc.frame_id);
    }
    if ((counter & 1)) {
      p_sec_buf = &sec_img_buf_desc;
      tmi->readInputFile(input.input_filename[in_inages_idx++], data_len,
        &sec_img_buf_desc);
    } else {
      p_sec_buf = NULL;
      tmi->readInputFile(input.input_filename[in_inages_idx++], data_len,
        NULL);
    }
    if (in_inages_idx >= input.num_in_images) {
      in_inages_idx = 0;
    }

    rc = soft.waitForTimer(50000);
    if (ETIMEDOUT == rc) {
      TMDBG_ERROR("Wait for SOF timeout");
    }
    tmi->prepSendCtrlSof(str_identity, counter);

    ret = tmi->sendBufDivert(input.stream_types[0], counter, &ack_flag, p_sec_buf);
    ASSERTTRUE(ret, "error in send buf divert");

    /* Dump output image */
    ret = tmi->processMetadata(MCT_BUS_MSG_FACE_INFO, &fd_bus_msg);
    if (FALSE == ret) {
      TMDBG("no FD metadata");
    } else {
      ret = tmi->validateResults(VALIDATE_METADATA, &fd_bus_msg);
      EXITTRUE(ret, STREAM_OFF, "validateResults failed");
    }
    tmi->getStreamIspOutDims(&buf_desc.dims, str_identity);

    if (buf_desc.dims.width) {
      tmi->getInBufDesc(&buf_desc, str_identity, p_cur_buf);
      tmi->publishResults("fd", buf_desc.frame_id, &buf_desc);
    }
    p_cur_buf = p_sec_buf;
    counter++;
    if (TRUE == ack_flag) {
      buf_desc.frame_id++;
    }
  }
STREAM_OFF:

  soft.stopAsyncTimer();

  main_ret = ret;

  /* Stream-OFF */
  ret = tmi->streamOff(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming off");

  tmi->freeInputBuffer(&sec_img_buf_desc);

  UCDBG(": Status: %d", main_ret);
  return main_ret;
FAIL:
  UCDBG(": Failed");
  return FALSE;
}

/** Name: unitTest2
 *
 *  Arguments/Fields:
 *  @SuiteCtx: Context to access test_module_imglib instance
 *  Return: Boolean result for success/failure
 *
 *  Description: Provide a detailed explanation
 *  of the objective(s) of the test case and include
 *  dependencies, assumptions and limitations if present.
 *
 **/
boolean test_module_imglib::unitTest2 (void *SuiteCtx)
{
  boolean ret = TRUE;

  UCDBG(": Enter");
  if(!SuiteCtx) {
    TMDBG_ERROR("Missing suite context SuiteCtx");
  return ret;
  }
  test_module_imglib *pTestSuite = (test_module_imglib *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  tm_intf_input_t       input = tmi->getInput();

  UCDBG(": identity = 0x%x",
    pTestSuite->m_tminterface.getStreamIdentity(input.stream_types[0]));
  UCDBG(": Status: %d", ret);
  return ret;
  FAIL:
  UCDBG(": Failed");
  return FALSE;
}

void test_module_imglib::handleSuperParams(mct_event_t *event)
{
  mct_event_super_control_parm_t *ctrl_param =
    (mct_event_super_control_parm_t *)event->u.ctrl_event.control_event_data;
  uint32_t cnt;

  for ( cnt = 0 ; cnt < ctrl_param->num_of_parm_events ; cnt++) {
    TMDBG("%s: set type: %d", __func__, ctrl_param->parm_events[cnt].type);

    switch (ctrl_param->parm_events[cnt].type) {
    case CAM_INTF_PARM_HFR: {
      cam_hfr_mode_t hfr_mode =
        *(cam_hfr_mode_t *)ctrl_param->parm_events[cnt].parm_data;
      TMDBG("set HFR mode: %d", hfr_mode);
    }
    case CAM_INTF_PARM_FPS_RANGE: {
        cam_fps_range_t *fps =
          (cam_fps_range_t *)ctrl_param->parm_events[cnt].parm_data;
        TMDBG("set FPS min: %f, max: %f", fps->min_fps, fps->max_fps);
        cur_fps = *fps;
      }
      break;
    default:
      break;
    }
  }
}

/* Support functions for test suite */

static boolean test_suite_img_port_event(mct_port_t *port, mct_event_t *event)
{
  TM_Interface *tmi = (TM_Interface *)port->port_private;

  switch(event->type) {
  case MCT_EVENT_MODULE_EVENT: {
    switch(event->u.module_event.type) {
    case MCT_EVENT_MODULE_ISP_OUTPUT_DIM: {
      mct_stream_info_t *stream_info =
        (mct_stream_info_t *)(event->u.module_event.module_event_data);
      tmi->saveStreamIspOutDims(&stream_info->dim, event->identity);
      CDBG_HIGH("%s: MCT_EVENT_MODULE_ISP_OUTPUT_DIM %dx%d, identity: %x",
        __func__, stream_info->dim.width, stream_info->dim.height,
        event->identity);
    }
      break;
    default:
    break;
    }
  }
  default:
  break;
  }
  return IEvent::portEvent(port, event);
}

static boolean test_suite_in_port_event(mct_port_t *port, mct_event_t *event)
{
  tm_intf_buffer_desc_t  buf_desc;
  TM_Interface *tmi = (TM_Interface *)port->port_private;

  switch(event->type) {
  case MCT_EVENT_MODULE_EVENT: {
    switch(event->u.module_event.type) {
    case MCT_EVENT_MODULE_BUF_DIVERT_ACK: {
      isp_buf_divert_ack_t *buf_divert_ack =
        (isp_buf_divert_ack_t*)event->u.module_event.module_event_data;
      CDBG_HIGH("%s: MCT_EVENT_MODULE_BUF_DIVERT_ACK", __func__);
      UCDBG("%s: MCT_EVENT_MODULE_BUF_DIVERT_ACK", __func__);
      UCDBG("buf index: %d, is dirty: %d, frame id: %d", buf_divert_ack->buf_idx,
        buf_divert_ack->is_buf_dirty, buf_divert_ack->frame_id);

      buf_desc.frame_id = buf_divert_ack->frame_id;
      buf_desc.index = buf_divert_ack->buf_idx;
      buf_desc.timestamp = buf_divert_ack->timestamp;

      tmi->notifyBufAckEvent(&buf_desc);
    }
      break;
    case MCT_EVENT_MODULE_ISP_OUTPUT_DIM: {
      mct_stream_info_t *stream_info =
        (mct_stream_info_t *)(event->u.module_event.module_event_data);
      tmi->saveStreamIspOutDims(&stream_info->dim, event->identity);
      TMDBG_HIGH("%s: MCT_EVENT_MODULE_ISP_OUTPUT_DIM %dx%d, identity: %x",
        __func__, stream_info->dim.width, stream_info->dim.height,
        event->identity);
    }
      break;
    default:
      CDBG_HIGH("%s: module event type: %d", __func__, event->u.module_event.type);
      break;
    }
    break;
  }
  case MCT_EVENT_CONTROL_CMD: {
    switch(event->u.ctrl_event.type) {
    case MCT_EVENT_CONTROL_STREAMON:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_STREAMON", __func__);
      break;
    case MCT_EVENT_CONTROL_STREAMOFF:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_STREAMOFF", __func__);
      break;
    case MCT_EVENT_CONTROL_SET_PARM: {
      mct_event_control_parm_t *ctrl_parm = (mct_event_control_parm_t *)
        event->u.ctrl_event.control_event_data;
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_SET_PARM type: %d", __func__,
        ctrl_parm->type);
    }
      break;
    default:
      CDBG_ERROR("%s: control cmd type: %d", __func__, event->u.module_event.type);
      break;
    }
    break;
  }
  default:
    break;
  }
  return TRUE;
}

boolean test_module_imglib::outPortPrepareCaps(tm_uc_port_caps_t *caps)
{
  boolean rc;

  rc = m_tminterface.prepareFrameCaps(caps);
  if (FALSE == rc) {
    return rc;
  }
  caps->priv_data = (void *)&m_tminterface;
  caps->event_func = test_suite_img_port_event;
  return TRUE;
}

boolean test_module_imglib::inPortPrepareCaps(tm_uc_port_caps_t *caps)
{
  boolean rc;

  rc = m_tminterface.prepareFrameCaps(caps);
  if (FALSE == rc) {
    return rc;
  }
  caps->priv_data = (void *)&m_tminterface;
  caps->event_func = test_suite_in_port_event;
  return TRUE;
}

