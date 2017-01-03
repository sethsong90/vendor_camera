/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#include "test_suite_sensor.h"

#undef LOG_TAG
#define LOG_TAG "mm-camera-test_suite_sensor"

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
  test_sensor *ts = new test_sensor(tmi);
  return ts;
}

/** Name: test_sensor
 *
 *  Arguments/Fields:
 *  tmi: Tm_Interface object
 *  Description:
 *    Constructor method
 **/
test_sensor::test_sensor(TM_Interface &tmi)
  : ITestModule(tmi),
    m_tminterface(tmi)
{
  TMDBG("%s", __func__);
}

/** Name: ~test_sensor
 *
 *  Arguments/Fields:
 *
 *  Description:
 *    Destructor method
 **/
test_sensor::~test_sensor()
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
void test_sensor::registerTestCases()
{
  uint8_t index = 0;
  unit_function_template_t local_fn_list[] = {
    {"sensor_test_prv", (test_fn)&sensor_test_prv},
    {"sensor_test_fps", (test_fn)&sensor_test_fps},
    {"sensor_test_dummy", (test_fn)&sensor_test_dummy}
  };
  num_test_cases = sizeof(local_fn_list) / sizeof(unit_function_template_t);
  for (index = 0; index < num_test_cases; index++) {
    strlcpy(list_testcases[index].name, local_fn_list[index].name,
      MAX_FN_NAME_LEN);
    list_testcases[index].test_function = local_fn_list[index].test_function;
  }
  TMDBG("Registering %d test cases", num_test_cases);
}


/** Name: sensor_test_prv
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
boolean test_sensor::sensor_test_prv (void *SuiteCtx)
{
  uint32_t              counter; // Count loop iterations
  boolean               ret;
  boolean               main_ret;
  tm_intf_buffer_desc_t buf_desc;

  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");
  test_sensor *pTestSuite = (test_sensor *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t   input = tmi->getInput();
  superParam        sup   = superParam(input.is_first_module_src);

  for (unsigned int i = 0; i < input.num_streams; i++) {

      counter = 0; // Count loop iterations
      ret = FALSE;
      main_ret = TRUE;

      uint32_t  str_identity = tmi->getStreamIdentity(input.stream_types[i]);

      UCDBG(": Enter: identity = 0x%x", str_identity);

      ret = sup.loadDefaultParams(&input);
      ASSERTTRUE(ret, "error in setting default params");
      ret = sup.sendSuperParams(tmi, 0, str_identity);
      ASSERTTRUE(ret, "error in sending super params");

      /* Stream-ON */
      ret = tmi->streamOn(input.stream_types[i]);
      ASSERTTRUE(ret, "error in streaming on");

      if (counter == 0) {
        mct_module_t *module = tmi->getModule("sensor");
        mct_port_t *port = tmi->getSrcPort(module);
        ret = tmi->sendStatsUpdate(port, counter+1);
        TMASSERT(ret, FALSE, "error in sending stats update");
      }

      while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION, &counter,
        input.stream_types[i])) {

        ret = tmi->bufReceiveMsg(&buf_desc);
        EXITTRUE(ret, STREAM_OFF, "error in receiving message");

        tmi->getStreamIspOutDims(&buf_desc.dims, buf_desc.identity);
        EXITFALSE(buf_desc.dims.width == 0, STREAM_OFF,
          "output sizes not found for stream identity: %x", buf_desc.identity);

        ret = tmi->calcBufSize(&buf_desc);
        EXITTRUE(ret, STREAM_OFF, "error in calculate buf size");

        UCDBG("--->>>buffer received: \n");
        UCDBG("addr: %p, index: %d, size: %d\n",
          buf_desc.vaddr, buf_desc.index, buf_desc.size);

        /* Dump output image */
        buf_desc.frame_id = counter;
        UCDBG("receive frame no: %d", buf_desc.frame_id);
        ret = tmi->validateResults(VALIDATE_OUTPUT_FRAME, &buf_desc);
        EXITTRUE(ret, STREAM_OFF, "validateResults failed");

        tmi->publishResults("", counter, &buf_desc);

        ret = tmi->sendBufDivertAck(&buf_desc);
        EXITTRUE(ret, STREAM_OFF, "error in sending BufDivertAck");

        counter++;
      }

    STREAM_OFF:

      main_ret = ret;

      /* Stream-OFF */
      ret = tmi->streamOff(input.stream_types[i]);
      ASSERTTRUE(ret, "error in streaming off");
      main_ret &= ret;
  }

  return main_ret;
FAIL:
  UCDBG(": Failed");
  return FALSE;
}

/** Name: sensor_test_fps
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
boolean test_sensor::sensor_test_fps (void *SuiteCtx)
{
  uint32_t str_identity;
  boolean  ret;
  boolean  main_ret;
  uint32_t counter; // Count loop iterations

  tm_intf_buffer_desc_t buf_desc;

  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");

  test_sensor *pTestSuite = (test_sensor *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t       input = tmi->getInput();
  superParam            sup = superParam(input.is_first_module_src);

  for (unsigned int i = 0; i < input.num_streams; i++) {

      counter  = 0;
      ret      = FALSE;
      main_ret = TRUE;

      str_identity = tmi->getStreamIdentity(input.stream_types[i]);

      UCDBG(": Enter: identity = 0x%x", str_identity);

      ret = sup.loadDefaultParams(&input);
      ASSERTTRUE(ret, "error in setting default params");
      ret = sup.sendSuperParams(tmi, 0, str_identity);
      ASSERTTRUE(ret, "error in sending super params");

      /* Stream-ON */
      ret = tmi->streamOn(input.stream_types[i]);
      ASSERTTRUE(ret, "error in streaming on");

      while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION, &counter,
        input.stream_types[0])) {

        ret = tmi->bufReceiveMsg(&buf_desc);
        EXITTRUE(ret, STREAM_OFF, "error in receiving message");

        UCDBG("--->>>buffer received\n");
        tmi->calcBufSize(&buf_desc);

        if (counter & 1) {
          cam_fps_range_t fps;
          fps.min_fps = fps.video_min_fps = 10;
          fps.max_fps = fps.video_max_fps = 10;
          ret = sup.addParamToBatch(CAM_INTF_PARM_FPS_RANGE,
            sizeof(fps), &fps);
          EXITTRUE(ret, STREAM_OFF, "error in adding fps params to batch");
        }

        ret = sup.sendSuperParams(tmi, counter+1, str_identity);
        EXITTRUE(ret, STREAM_OFF, "error in sending super params");

        if (counter == 0) {
          mct_module_t *module = tmi->getModule("isp");
          mct_port_t *port = tmi->getSrcPort(module);
          ret = tmi->sendStatsUpdate(port, counter+1);
          EXITTRUE(ret, STREAM_OFF, "error in sending stats update");
        }
        tmi->getStreamIspOutDims(&buf_desc.dims, buf_desc.identity);
        EXITFALSE(buf_desc.dims.width == 0, STREAM_OFF,
          "output sizes not found for stream identity: %x", buf_desc.identity);

        ret = tmi->calcBufSize(&buf_desc);
        EXITTRUE(ret, STREAM_OFF, "error in calculate buf size");

        UCDBG("receive frame no: %d", buf_desc.frame_id);
        /* Dump output image */
        buf_desc.frame_id = counter;
        ret = tmi->validateResults(VALIDATE_OUTPUT_FRAME, &buf_desc);
        EXITTRUE(ret, STREAM_OFF, "validateResults failed");

        tmi->publishResults("test_fps", counter, &buf_desc);

        ret = tmi->sendBufDivertAck(&buf_desc);
        EXITTRUE(ret, STREAM_OFF, "error in sending BufDivertAck");

        counter++;
      }
    STREAM_OFF:
      main_ret = ret;

      /* Stream-OFF */
      ret = tmi->streamOff(input.stream_types[i]);
      ASSERTTRUE(ret, "error in streaming off");

      main_ret &= ret;
  }

  return main_ret;
FAIL:
  UCDBG_ERROR(": Failed");
  return FALSE;
}

/** Name: sensor_test_dummy
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
boolean test_sensor::sensor_test_dummy (void *SuiteCtx)
{
  boolean         ret         = TRUE;
  test_sensor     *pTestSuite = (test_sensor *)SuiteCtx;
  TM_Interface    *tmi        = &(pTestSuite->m_tminterface);
  tm_intf_input_t input       = tmi->getInput();

  for (unsigned int i = 0; i < input.num_streams; i++) {
      uint32_t str_identity = tmi->getStreamIdentity(input.stream_types[i]);

      UCDBG(": Enter");
      if(!SuiteCtx) {
        TMDBG_ERROR("Missing suite context SuiteCtx");
        return ret;
      }
      UCDBG(": identity = 0x%x", str_identity);
      UCDBG(": Status: %d", ret);
  }
  return ret;
  FAIL:
  UCDBG_ERROR(": Failed");
  return FALSE;
}

/* Support functions for test suite */

static boolean test_suite_out_port_event(mct_port_t *port, mct_event_t *event)
{
  TM_Interface *tmi = (TM_Interface *)port->port_private;
  tm_intf_buffer_desc_t  buf_desc;
  isp_buf_divert_t  *isp_buf = NULL;

  if (MCT_EVENT_MODULE_EVENT == event->type) {

    if (MCT_EVENT_MODULE_BUF_DIVERT == event->u.module_event.type) {

      CDBG_HIGH("%s: MCT_EVENT_MODULE_BUF_DIVERT", __func__);
      TMDBG("%s: MCT_EVENT_MODULE_BUF_DIVERT", __func__);

      isp_buf = (isp_buf_divert_t *)(event->u.module_event.module_event_data);
      if (!isp_buf) {
        CDBG_ERROR("%s:%d: isp_buf is NULL!!!\n", __func__, __LINE__);
        return FALSE;
      }

      UCDBG("%s:%d send buf seq %d, addr: 0x%x, index: %d for processing\n",
        __func__, __LINE__,
        isp_buf->buffer.sequence, *(uint32_t *)isp_buf->vaddr, isp_buf->buffer.index);
      isp_buf->is_buf_dirty = 0;
      isp_buf->ack_flag = 0;

      buf_desc.vaddr = (void *)*(uint32_t *)isp_buf->vaddr;
      buf_desc.index = isp_buf->buffer.index;
      buf_desc.size  = isp_buf->buffer.length;
      buf_desc.frame_id = isp_buf->buffer.sequence;
      buf_desc.timestamp = isp_buf->buffer.timestamp;
      buf_desc.identity = isp_buf->identity;
      tmi->notifyBufAckEvent(&buf_desc);
      return TRUE;
    }
  }
  CDBG_HIGH("%s: call port event with identity: 0x%x", __func__, event->identity);
  return IEvent::portEvent(port, event);

}

static boolean test_suite_stats_port_event(mct_port_t *port, mct_event_t *event)
{
  TM_Interface *tmi = (TM_Interface *)port->port_private;

  CDBG_HIGH("%s: event from port <%s> identity: 0x%x", __func__,
    MCT_PORT_PEER(port)->object.name, event->identity);

  switch(event->type) {
  case MCT_EVENT_MODULE_EVENT: {
    switch(event->u.module_event.type) {
    case MCT_EVENT_MODULE_BUF_DIVERT_ACK: {
      // This event is sent from PPROC to ISP after native buffer is processed
      // So will use it as synchronization for PPROC buf_done
      isp_buf_divert_ack_t *buf_divert_ack =
        (isp_buf_divert_ack_t*)event->u.module_event.module_event_data;

      CDBG_HIGH("%s: MCT_EVENT_MODULE_BUF_DIVERT_ACK", __func__);

      UCDBG("MCT_EVENT_MODULE_BUF_DIVERT_ACK");

      UCDBG("buf index: %d, is dirty: %d, frame id: %d, timestamp: %ld,%06lds",
        buf_divert_ack->buf_idx, buf_divert_ack->is_buf_dirty,
        buf_divert_ack->frame_id, buf_divert_ack->timestamp.tv_sec,
        buf_divert_ack->timestamp.tv_usec);

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
      CDBG_HIGH("%s: MCT_EVENT_MODULE_ISP_OUTPUT_DIM", __func__);
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
    case MCT_EVENT_CONTROL_SOF: {
        mct_bus_msg_isp_sof_t *sof_event;
        sof_event =(mct_bus_msg_isp_sof_t *)(event->u.ctrl_event.control_event_data);
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_SOF", __func__);
      UCDBG("MCT_EVENT_CONTROL_SOF");

      tmi->sendStatsUpdate(port, sof_event->frame_id);
      }
      break;
    case MCT_EVENT_CONTROL_SET_PARM:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_SET_PARM", __func__);
//      ts->test_suite_handle_ctrl_param(port,
//        (mct_event_control_parm_t *)event->u.ctrl_event.control_event_data);
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

boolean test_sensor::outPortPrepareCaps(tm_uc_port_caps_t *caps)
{
  boolean rc;
  rc = m_tminterface.prepareFrameCaps(caps);
  if (FALSE == rc) {
    return rc;
  }
  caps->priv_data = (void *)&m_tminterface;
  caps->event_func = test_suite_out_port_event;

  return TRUE;
}

boolean test_sensor::statsPortPrepareCaps(tm_uc_port_caps_t *caps)
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

