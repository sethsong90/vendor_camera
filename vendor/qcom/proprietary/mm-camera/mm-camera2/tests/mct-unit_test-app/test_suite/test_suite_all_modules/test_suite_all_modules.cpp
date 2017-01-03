/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#include "test_suite_all_modules.h"

#undef LOG_TAG
#define LOG_TAG "mm-camera-test_suite_all"

using namespace tm_interface;

/** Name: test_suite_open
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
  test_all_modules *ts = new test_all_modules(tmi);
  return ts;
}

/** Name: test_all_modules
 *
 *  Arguments/Fields:
 *  tmi: Tm_Interface object
 *  Description:
 *    Constructor method
 **/
test_all_modules::test_all_modules(TM_Interface &tmi)
  : ITestModule(tmi),
    m_tminterface(tmi)
{
  TMDBG("%s", __func__);
}

/** Name: ~test_all_modules
 *
 *  Arguments/Fields:
 *
 *  Description:
 *    Destructor method
 **/
test_all_modules::~test_all_modules()
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
void test_all_modules::registerTestCases()
{
  uint8_t index = 0;
  unit_function_template_t local_fn_list[] = {
    {"unit_test_prv", (test_fn)&unit_test_prv},
    {"unit_test_fps", (test_fn)&unit_test_fps},
    {"unit_test_3", (test_fn)&unit_test_3},
    {"unit_test_4", (test_fn)&unit_test_4}
  };
  num_test_cases = sizeof(local_fn_list) / sizeof(unit_function_template_t);
  for (index = 0; index < num_test_cases; index++) {
    strlcpy(list_testcases[index].name, local_fn_list[index].name,
      MAX_FN_NAME_LEN);
    list_testcases[index].test_function = local_fn_list[index].test_function;
  }
  TMDBG("Registering %d test cases", num_test_cases);
}


/** Name: unit_test_prv
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
boolean test_all_modules::unit_test_prv (void *SuiteCtx)
{
  boolean               ret = FALSE, main_ret = TRUE;
  uint32_t              counter = 0; // Count loop iterations
  uint32_t              counter2 = 0; // Count loop iterations
  tm_intf_buffer_desc_t buf_desc;
  tm_intf_buffer_desc_t out_buf_desc;
  uint32_t              main_stream_identity;

  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");
  test_all_modules *pTestSuite = (test_all_modules *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t       input = tmi->getInput();
  superParam            sup = superParam(input.is_first_module_src);
  cam_stream_type_t     main_stream_type = input.stream_types[0];

  main_stream_identity =
    pTestSuite->m_tminterface.getStreamIdentity(main_stream_type);

  UCDBG(": Enter: identity = 0x%x", main_stream_identity);

  ret = sup.loadDefaultParams(&input);
  ASSERTTRUE(ret, "error in setting default params");
  ret = sup.sendSuperParams(tmi, 0, tmi->getParmStreamIdentity());
  ASSERTTRUE(ret, "error in sending super params");

  /* Set output buffer to stream */
  ret = tmi->setOutputBuffer(main_stream_type);
  ASSERTTRUE(ret, "error in setting output buffer");

  if (input.num_streams > 1) {
    ret = tmi->setOutputBuffer(input.stream_types[1]);
    ASSERTTRUE(ret, "error in setting output buffer");
  }
  /* Stream-ON */
  ret = tmi->streamOn(main_stream_type);
  ASSERTTRUE(ret, "error in streaming on");

  if (input.num_streams > 1) {
    ret = tmi->streamOn(input.stream_types[1]);
    ASSERTTRUE(ret, "error in streaming on");
  }
//  ret = tmi->doReprocess(counter);
//  EXITTRUE(ret, STREAM_OFF, "error in do reprocess\n");

  mct_module_t *module = tmi->getModule("isp");
  mct_port_t *port = tmi->getSrcPort(module);
  ret = tmi->sendStatsUpdate(port, counter+1);
  ASSERTTRUE(ret, "error in sending stats update");

  while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION,
    &counter, main_stream_type)) {

    ret = tmi->bufReceiveMsg(&buf_desc);
    EXITTRUE(ret, STREAM_OFF, "error in receiving message");

    UCDBG("--->>>buffer received with identity: %x, frame no: %d\n",
        buf_desc.identity, buf_desc.frame_id);

    /* Dump output image */
    ret = tmi->getOutBufDesc(&out_buf_desc, buf_desc.identity);
    EXITTRUE(ret, STREAM_OFF, "error in getting output buf address");

    tmi->getStreamOutDims(&out_buf_desc.dims, out_buf_desc.identity);
    if (out_buf_desc.dims.width == 0) {
      TMDBG_ERROR("output sizes not found for stream identity: %x",
        out_buf_desc.identity);
    }
    out_buf_desc.frame_id = counter;

    ret = tmi->validateResults(VALIDATE_OUTPUT_FRAME, &out_buf_desc);
    EXITTRUE(ret, STREAM_OFF, "validateResults failed");

    if (buf_desc.identity == main_stream_identity) {
      tmi->publishResults("prv", counter, &out_buf_desc);
      counter++;
    } else {
      tmi->publishResults("cap", counter2, &out_buf_desc);
      counter2++;
    }
  }

STREAM_OFF:

  main_ret = ret;

  /* Stream-OFF */

  if (input.num_streams > 1) {
    ret = tmi->streamOff(input.stream_types[1]);
    ASSERTTRUE(ret, "error in streaming off on stream type: %d", input.stream_types[1]);
  }
  ret = tmi->streamOff(main_stream_type);
  ASSERTTRUE(ret, "error in streaming off on stream type: %d", main_stream_type);

  return main_ret;
FAIL:
  UCDBG(": Failed");
  return FALSE;
}

/** Name: unit_test_fps
 *
 *  Arguments/Fields:
 *  @SuiteCtx: Current class context passed by TestSuite
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends all configurations needed to stream on modules.
 *  Sends default parameters and does stream on.
 *  Saves incoming frames to output file and prints first 4 pixels of ouput
 *  buffer.
 *  Changes frame rate.
 *  Waits for a number of frames specified in configuration file and streams off.
 *
 **/
boolean test_all_modules::unit_test_fps (void *SuiteCtx)
{
  boolean               ret = FALSE, main_ret = TRUE;
  uint32_t              counter = 0; // Count loop iterations
  tm_intf_buffer_desc_t buf_desc;
  tm_intf_buffer_desc_t out_buf_desc;
  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");

  test_all_modules *pTestSuite = (test_all_modules *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t       input = tmi->getInput();
  superParam            sup = superParam(input.is_first_module_src);

  UCDBG(": Enter: identity = 0x%x",
    pTestSuite->m_tminterface.getStreamIdentity(input.stream_types[0]));

  ret = sup.loadDefaultParams(&input);
  ASSERTTRUE(ret, "error in setting default params");
  ret = sup.sendSuperParams(tmi, 0, tmi->getStreamIdentity(input.stream_types[0]));
  ASSERTTRUE(ret, "error in sending super params");

  cam_fps_range_t fps;
  fps.min_fps = fps.video_min_fps = 10;
  fps.max_fps = fps.video_max_fps = 10;
  ret = sup.addParamToBatch(CAM_INTF_PARM_FPS_RANGE,
    sizeof(fps), &fps);
  ASSERTTRUE(ret, "error in adding fps params to batch");
  ret = sup.sendSuperParams(tmi, counter+1,
    tmi->getStreamIdentity(input.stream_types[0]));
  EXITTRUE(ret, STREAM_OFF, "error in sending super params");

  /* Set output buffer to stream */
  ret = tmi->setOutputBuffer(input.stream_types[0]);
  ASSERTTRUE(ret, "error in setting output buffer");

  /* Stream-ON */
  ret = tmi->streamOn(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming on");

  while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION,
    &counter, input.stream_types[0])) {

    ret = tmi->bufReceiveMsg(&buf_desc);
    EXITTRUE(ret, STREAM_OFF, "error in receiving message");

    UCDBG("--->>>buffer received. frame no: %d\n", buf_desc.frame_id);

    if (counter == 0) {
      mct_module_t *module = tmi->getModule("isp");
      mct_port_t *port = tmi->getSrcPort(module);
      ret = tmi->sendStatsUpdate(port, counter+1);
      EXITTRUE(ret, STREAM_OFF, "error in sending stats update");
    }

    ret = tmi->getOutBufDesc(&out_buf_desc, buf_desc.identity);
    EXITTRUE(ret, STREAM_OFF, "error in getting output buf address");

    tmi->getStreamOutDims(&out_buf_desc.dims, out_buf_desc.identity);
    if (out_buf_desc.dims.width == 0) {
      TMDBG_ERROR("output sizes not found for stream identity: %x",
        out_buf_desc.identity);
    }

    /* Dump output image */
    out_buf_desc.frame_id = counter;
    out_buf_desc.identity = buf_desc.identity;
    ret = tmi->validateResults(VALIDATE_OUTPUT_FRAME, &out_buf_desc);
    EXITTRUE(ret, STREAM_OFF, "validateResults failed");

    tmi->publishResults("test_fps", counter, &out_buf_desc);
    counter++;
  }
STREAM_OFF:
  main_ret = ret;

  /* Stream-OFF */
  ret = tmi->streamOff(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming off");

  main_ret &= ret;

  return main_ret;
FAIL:
  UCDBG_ERROR(": Failed");
  return FALSE;
}

/** Name: unit_test_3
 *
 *  Arguments/Fields:
 *  @SuiteCtx: Current class context passed by TestSuite
 *  Return: Boolean result for success/failure
 *
 *  Description: Provide a detailed explanation
 *  of the objective(s) of the test case and include
 *  dependencies, assumptions and limitations if present.
 *
 **/
boolean test_all_modules::unit_test_3 (void *SuiteCtx)
  {
  boolean ret = TRUE;

  UCDBG(": Enter");
  if(!SuiteCtx) {
    TMDBG_ERROR("Missing suite context SuiteCtx");
  return ret;
  }
  test_all_modules *pTestSuite = (test_all_modules *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  tm_intf_input_t   input = tmi->getInput();

  UCDBG(": identity = 0x%x",
    pTestSuite->m_tminterface.getStreamIdentity(input.stream_types[0]));
  UCDBG(": Status: %d", ret);
  return ret;
  FAIL:
  UCDBG_ERROR(": Failed");
  return FALSE;
}


/** Name: unit_test_4
 *
 *  Arguments/Fields:
 *  @SuiteCtx: Current class context passed by TestSuite
 *  Return: Boolean result for success/failure
 *
 *  Description: Provide a detailed explanation
 *  of the objective(s) of the test case and include
 *  dependencies, assumptions and limitations if present.
 *
 **/
boolean test_all_modules::unit_test_4 (void *SuiteCtx)
{
  boolean ret = TRUE;

  UCDBG(": Enter");
  if(!SuiteCtx) {
    TMDBG_ERROR("Missing suite context SuiteCtx");
  return ret;
  }
  test_all_modules *pTestSuite = (test_all_modules *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  tm_intf_input_t  input = tmi->getInput();

  UCDBG(": identity = 0x%x",
    pTestSuite->m_tminterface.getStreamIdentity(input.stream_types[0]));
  UCDBG(": Status: %d", ret);
  return ret;
  FAIL:
  UCDBG_ERROR(": Failed");
  return FALSE;
}

/* Support functions for test suite */

static boolean test_suite_img_port_event(mct_port_t *port, mct_event_t *event)
{
  CDBG_HIGH("%s: event identity: 0x%x", __func__, event->identity);

  return IEvent::portEvent(port, event);
}

static boolean test_suite_stats_port_event(mct_port_t *port, mct_event_t *event)
{
  TM_Interface *tmi = (TM_Interface *)port->port_private;

  CDBG_HIGH("%s: event identity: 0x%x", __func__, event->identity);

  switch(event->type) {
  case MCT_EVENT_MODULE_EVENT: {
    switch(event->u.module_event.type) {
    case MCT_EVENT_MODULE_BUF_DIVERT_ACK: {
      // This event is sent from PPROC to ISP after native buffer is processed
      // So will use it as synchronization for PPROC buf_done
      isp_buf_divert_ack_t *buf_divert_ack =
        (isp_buf_divert_ack_t*)event->u.module_event.module_event_data;
      tm_intf_buffer_desc_t  buf_desc;

      CDBG_HIGH("%s: MCT_EVENT_MODULE_BUF_DIVERT_ACK", __func__);

      UCDBG("MCT_EVENT_MODULE_BUF_DIVERT_ACK");

      UCDBG("buf index: %d, is dirty: %d, frame id: %d, timestamp: %ld,%06lds",
        buf_divert_ack->buf_idx, buf_divert_ack->is_buf_dirty,
        buf_divert_ack->frame_id, buf_divert_ack->timestamp.tv_sec,
        buf_divert_ack->timestamp.tv_usec);

      buf_desc.frame_id = buf_divert_ack->frame_id;
      buf_desc.identity = event->identity;

      tmi->notifyBufAckEvent(&buf_desc);
    }
      break;
    case MCT_EVENT_MODULE_STATS_DATA:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_STATS_DATA", __func__);
      break;
    case MCT_EVENT_MODULE_STATS_GET_DATA: {
//      tm_stats_get_data(port, event);
      CDBG_HIGH("%s: MCT_EVENT_MODULE_STATS_GET_DATA", __func__);
    }
      break;
    case MCT_EVENT_MODULE_SET_DIGITAL_GAIN:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_SET_DIGITAL_GAIN", __func__);
      break;
    case MCT_EVENT_MODULE_SOF_NOTIFY: {
        mct_bus_msg_isp_sof_t *sof_event;
        sof_event =(mct_bus_msg_isp_sof_t *)(event->u.module_event.module_event_data);
        TMDBG_HIGH("%s: MCT_EVENT_MODULE_SOF_NOTIFY: frame id: %d, num streams: %d",
            __func__, sof_event->frame_id, sof_event->num_streams);
//        tmi->sendCtrlSof(sof_event);
        tmi->sendStatsUpdate(port, sof_event->frame_id);
      }
      break;
    case MCT_EVENT_MODULE_PPROC_GET_AEC_UPDATE: {
      tmi->statsGetData(event);
      CDBG_HIGH("%s: MCT_EVENT_MODULE_PPROC_GET_AEC_UPDATE", __func__);
    }
      break;
    case MCT_EVENT_MODULE_STATS_AWB_UPDATE:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_STATS_AWB_UPDATE", __func__);
      break;
    case MCT_EVENT_MODULE_DIVERT_BUF_TO_STATS:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_DIVERT_BUF_TO_STATS", __func__);
      break;
    case MCT_EVENT_MODULE_SET_STREAM_CONFIG:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_SET_STREAM_CONFIG", __func__);
      break;
    case MCT_EVENT_MODULE_ISP_OUTPUT_DIM: {
      mct_stream_info_t *stream_info =
        (mct_stream_info_t *)(event->u.module_event.module_event_data);
      UCDBG("%s: MCT_EVENT_MODULE_ISP_OUTPUT_DIM %dx%d, identity: %x",
        __func__, stream_info->dim.width, stream_info->dim.height,
        event->identity);
      tmi->saveStreamIspOutDims(&stream_info->dim, event->identity);
    }
      break;
    case MCT_EVENT_MODULE_SET_CHROMATIX_PTR:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_SET_CHROMATIX_PTR", __func__);
      break;
    case MCT_EVENT_MODULE_SET_AF_TUNE_PTR:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_SET_AF_TUNE_PTR", __func__);
      break;
    case MCT_EVENT_MODULE_ISP_STATS_INFO:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_ISP_STATS_INFO", __func__);
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
      UCDBG("MCT_EVENT_CONTROL_STREAMON");

      tmi->sendAWBStatsUpdate();

      break;
    case MCT_EVENT_CONTROL_STREAMOFF:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_STREAMOFF", __func__);
      break;
    case MCT_EVENT_CONTROL_SET_PARM:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_SET_PARM", __func__);
      break;
    case MCT_EVENT_CONTROL_SET_SUPER_PARM:
      tmi->statsSetSuperparam(event);
      break;
    default:
      ALOGE("%s: control cmd type: %d", __func__, event->u.module_event.type);
      break;
    }
    break;
  }
  default:
    break;
  }
  return TRUE;
}

boolean test_all_modules::outPortPrepareCaps(tm_uc_port_caps_t *caps)
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

boolean test_all_modules::statsPortPrepareCaps(tm_uc_port_caps_t *caps)
{
  boolean rc;
  rc = m_tminterface.prepareStatsCaps(caps);
  if (FALSE == rc) {
    return rc;
  }
  caps->priv_data = (void *)&m_tminterface;
  caps->event_func = test_suite_stats_port_event;

  return TRUE;
}

