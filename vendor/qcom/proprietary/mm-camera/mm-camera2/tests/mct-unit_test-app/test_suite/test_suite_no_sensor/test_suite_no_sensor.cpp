/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#include "test_suite_no_sensor.h"

#undef LOG_TAG
#define LOG_TAG "mm-camera-test_suite_no_sensor"

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
  test_no_sensor *ts = new test_no_sensor(tmi);
  return ts;
}

/** Name: test_no_sensor
 *
 *  Arguments/Fields:
 *  tmi: Tm_Interface object
 *  Description:
 *    Constructor method
 **/
test_no_sensor::test_no_sensor(TM_Interface &tmi)
  : ITestModule(tmi),
    m_tminterface(tmi)
{
  TMDBG("%s", __func__);
  TM_Interface          *tmintf = &m_tminterface;
  /* This is needed because on message CAM_INTF_META_STREAM_INFO
  need to send MCT_EVENT_MODULE_SET_SENSOR_OUTPUT_INFO with
  sensor_dims structure filled */
  tmintf->fillSensorInfo(&sen_out_info, &sensor_dims);
}

/** Name: ~test_no_sensor
 *
 *  Arguments/Fields:
 *
 *  Description:
 *    Destructor method
 **/
 test_no_sensor::~test_no_sensor()
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
void test_no_sensor::registerTestCases()
{
  uint8_t index = 0;
  unit_function_template_t local_fn_list[] = {
    {"unitTest1", (test_fn)&unitTest1},
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
boolean test_no_sensor::unitTest1 (void *SuiteCtx)
{
  boolean           ret = FALSE, main_ret = TRUE;
  uint32_t          counter = 0; // Count loop iterations
  tm_intf_buffer_desc_t  buf_desc;
  tm_intf_buffer_desc_t  out_buf_desc;
  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");

  test_no_sensor *pTestSuite = (test_no_sensor *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t        input = tmi->getInput();
  superParam             sup = superParam(input.is_first_module_src);
  uint32_t               str_identity = tmi->getStreamIdentity(input.stream_types[0]);

  UCDBG(": Enter: identity = 0x%x", str_identity);

  ret = tmi->sendModuleEventDown(MCT_EVENT_MODULE_SET_SENSOR_OUTPUT_INFO,
    &(pTestSuite->sensor_dims), str_identity);
  ASSERTTRUE(ret, "error in sending sensor_output_info");

  TMDBG("MCT_EVENT_MODULE_SET_STREAM_CONFIG - sensor format: 0x%x",
    pTestSuite->sen_out_info.fmt);

  ret = tmi->sendModuleEventDown(MCT_EVENT_MODULE_SET_STREAM_CONFIG,
    &(pTestSuite->sen_out_info), str_identity);
  ASSERTTRUE(ret, "error in setting stream config");

  ret = tmi->sendChromatixPointers(str_identity);
  ASSERTTRUE(ret, "error in sending chromatix pointers");

  ret = sup.loadDefaultParams(&input);
  ASSERTTRUE(ret, "error in setting default params");

  /* Set output buffer to stream */
  ret = tmi->setOutputBuffer(input.stream_types[0]);
  ASSERTTRUE(ret, "error in setting output buffer");

  /* Send super-params downstream */
  ret = sup.sendSuperParams(tmi, 0, str_identity);
  ASSERTTRUE(ret, "error in sending super params");

  /* Stream-ON */
  ret = tmi->streamOn(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming on");

//  ret = tmi->doReprocess(counter);
//  EXITTRUE(ret, STREAM_OFF, "error in do reprocess\n");

  while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION,
    &counter, input.stream_types[0])) {

    ret = tmi->bufReceiveMsg(&buf_desc);
    EXITTRUE(ret, STREAM_OFF, "error in receiving message");

    TMDBG("");
    UCDBG("--->>>buffer received. frame no: %d\n", buf_desc.frame_id);

    ret = tmi->setOutputBuffer(input.stream_types[0]);
    EXITTRUE(ret, STREAM_OFF, "error in setting output buffer");

    /* Dump output image */
    ret = tmi->getOutBufDesc(&out_buf_desc, buf_desc.identity);

    tmi->getStreamOutDims(&out_buf_desc.dims, buf_desc.identity);
    EXITFALSE(out_buf_desc.dims.width == 0, STREAM_OFF,
      "output sizes not found for stream identity: %x", buf_desc.identity);

    if(TRUE == ret) {
      out_buf_desc.frame_id = counter;
      out_buf_desc.identity = buf_desc.identity;
      ret = tmi->validateResults(VALIDATE_OUTPUT_FRAME, &out_buf_desc);
      EXITTRUE(ret, STREAM_OFF, "validateResults failed");

      tmi->publishResults("ut1", counter, &out_buf_desc);
    }
    counter++;
  }
STREAM_OFF:

  main_ret = ret;

  /* Stream-OFF */
  UCDBG("going to stream off");
  ret = tmi->streamOff(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming off");

  return main_ret;
FAIL:
  UCDBG(": Failed");
  return FALSE;
}

/** Name: unitTest2
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
boolean test_no_sensor::unitTest2 (void *SuiteCtx)
{
  boolean ret = TRUE;

  UCDBG(": Enter");
  if(!SuiteCtx) {
    TMDBG_ERROR("Missing suite context SuiteCtx");
  return ret;
  }
  test_no_sensor *pTestSuite = (test_no_sensor *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  tm_intf_input_t       input = tmi->getInput();

  UCDBG(": identity = 0x%x",
    tmi->getStreamIdentity(input.stream_types[0]));
  UCDBG(": Status: %d", ret);
  return ret;
  FAIL:
  UCDBG(": Failed");
  return FALSE;
}

/* Support functions for test suite */

static boolean test_suite_img_port_event(mct_port_t *port, mct_event_t *event)
{
  return IEvent::portEvent(port, event);
}

static boolean test_suite_in_port_event(mct_port_t *port, mct_event_t *event)
{
  tm_intf_buffer_desc_t  buf_desc;
  port_data_t *port_data = (port_data_t *)port->port_private;
  test_no_sensor *ts = (test_no_sensor *)port_data->ts;
  TM_Interface *tmi = (TM_Interface *)port_data->tm_intf;

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
      tmi->notifyBufAckEvent(&buf_desc);
    }
      break;
    case MCT_EVENT_MODULE_SOF_NOTIFY:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_SOF_NOTIFY", __func__);
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
    case MCT_EVENT_CONTROL_SET_PARM:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_SET_PARM", __func__);
      ts->test_suite_handle_ctrl_param(port,
        (mct_event_control_parm_t *)event->u.ctrl_event.control_event_data);
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

static boolean test_suite_stats_port_event(mct_port_t *port, mct_event_t *event)
{
  TM_Interface *tmi = (TM_Interface *)port->port_private;

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
      buf_desc.identity = buf_divert_ack->identity;

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
        CDBG_HIGH("%s: MCT_EVENT_MODULE_SOF_NOTIFY: frame id: %d",
            __func__, sof_event->frame_id);
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
      tmi->saveStreamIspOutDims(&stream_info->dim, event->identity);
      CDBG_HIGH("%s: MCT_EVENT_MODULE_ISP_OUTPUT_DIM", __func__);
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

boolean test_no_sensor::test_suite_handle_ctrl_param(mct_port_t *port,
  mct_event_control_parm_t *event_control)
{
  boolean ret;
  port_data_t *port_data = (port_data_t *)port->port_private;
  TM_Interface *tmi = port_data->tm_intf;
  mct_module_t *module;

  switch (event_control->type) {
  case CAM_INTF_META_STREAM_INFO:
    CDBG_HIGH("%s: CAM_INTF_META_STREAM_INFO", __func__);
      module = tmi->getModule("isp");
    ret = tmi->sendModuleEvent(module, MCT_EVENT_MODULE_SET_SENSOR_OUTPUT_INFO,
      MCT_EVENT_DOWNSTREAM, &sensor_dims, tmi->getParmStreamIdentity());

    if (ret == FALSE) {
      UCDBG_ERROR("%s:%d] error in meta stream info event\n", __func__, __LINE__);
      return ret;
    }
    break;
  default:
    break;
  }
  return TRUE;
}

boolean test_no_sensor::outPortPrepareCaps(tm_uc_port_caps_t *caps)
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

boolean test_no_sensor::inPortPrepareCaps(tm_uc_port_caps_t *caps)
{
  boolean rc;
  sensor_src_port_cap_t *sen_caps;

  rc = m_tminterface.prepareSensorCaps(caps);
  if (FALSE == rc) {
    return rc;
  }
  sen_caps = (sensor_src_port_cap_t *)caps->caps.u.data;
  sensor_fmt = sen_caps->sensor_cid_ch[0].fmt;
  port_data.tm_intf = &m_tminterface;
  port_data.ts = this;
  caps->priv_data = (void *)&port_data;
  caps->event_func = test_suite_in_port_event;
  return TRUE;
}

boolean test_no_sensor::statsPortPrepareCaps(tm_uc_port_caps_t *caps)
{
  m_tminterface.prepareStatsCaps(caps);
  caps->priv_data = (void *)&m_tminterface;
  caps->event_func = test_suite_stats_port_event;

  return TRUE;
}

