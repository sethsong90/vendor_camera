/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#include "test_suite_vfe.h"

#undef LOG_TAG
#define LOG_TAG "mm-camera-test_suite_vfe"

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
  test_module_vfe *ts = new test_module_vfe(tmi);
  return ts;
}

/** Name: test_module_vfe
 *
 *  Arguments/Fields:
 *  tmi: Tm_Interface object
 *  Description:
 *    Constructor method
 **/
test_module_vfe::test_module_vfe(TM_Interface &tmi)
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

/** Name: ~test_module_vfe
 *
 *  Arguments/Fields:
 *
 *  Description:
 *    Destructor method
 **/
test_module_vfe::~test_module_vfe()
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
void test_module_vfe::registerTestCases()
{
  uint8_t index = 0;
  unit_function_template_t local_fn_list[] = {
    {"unitTest1", (test_fn)&unitTest1},
    {"test_effect", (test_fn)&test_effect},
    {"test_effect_prv", (test_fn)&test_effect_prv}
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
boolean test_module_vfe::unitTest1 (void *SuiteCtx)
{
  boolean                 ret = FALSE, main_ret = TRUE;
  uint32_t                counter = 0; // Count loop iterations
  tm_intf_buffer_desc_t   buf_desc;

  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");

  test_module_vfe *pTestSuite = (test_module_vfe *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t        input = tmi->getInput();
  uint32_t               str_identity = tmi->getStreamIdentity(input.stream_types[0]);
  superParam             sup = superParam(input.is_first_module_src);

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
  if(FALSE == ret) {
    UCDBG_ERROR("%s:%d] error in setting default params\n", __func__, __LINE__);
    goto FAIL;
  }

/* Send super-params downstream */
  ret = sup.sendSuperParams(tmi, 0, tmi->getStreamIdentity(input.stream_types[0]));
  ASSERTTRUE(ret, "error in sending super params");

  /* Stream-ON */
  ret = tmi->streamOn(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming on");

  while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION,
    &counter, input.stream_types[0])) {

    ret = tmi->bufReceiveMsg(&buf_desc);
    EXITTRUE(ret, STREAM_OFF, "error in receiving message");

    TMDBG("");
    UCDBG("--->>>receive frame no: %d", buf_desc.frame_id);
    /* Dump output image */

    tmi->getStreamIspOutDims(&buf_desc.dims, buf_desc.identity);
    EXITFALSE(buf_desc.dims.width == 0, STREAM_OFF,
      "output sizes not found for stream identity: %x", buf_desc.identity);

    ret = tmi->calcBufSize(&buf_desc);
    EXITTRUE(ret, STREAM_OFF, "error in calculate buf size");

    UCDBG("%s: receive buffer: addr: %p, index: %d, size: %d for processing\n",
      __func__, buf_desc.vaddr, buf_desc.index, buf_desc.size);
    CDBG("%s: receive buffer: addr: %p, index: %d, size: %d for processing\n",
      __func__, buf_desc.vaddr, buf_desc.index, buf_desc.size);

    if ((uint32_t)buf_desc.vaddr & 0xFFF) {
      UCDBG_ERROR("Wrong ISP buffer address!!!");
      continue;
    }

    if(TRUE == ret) {
      buf_desc.frame_id = counter;
      ret = tmi->validateResults(VALIDATE_OUTPUT_FRAME, &buf_desc);
      EXITTRUE(ret, STREAM_OFF, "validateResults failed, stream identity: %x",
        buf_desc.identity);

      tmi->publishResults("ut1", counter, &buf_desc);

      ret = tmi->sendBufDivertAck(&buf_desc);
      ASSERTTRUE(ret, "error on sending BufDivert ACK");
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

/** Name: test_effect
 *
 *  Arguments/Fields:
 *  @SuiteCtx: Current class context passed by TestSuite
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends all configurations needed to stream on modules.
 *  Sends default parameters.
 *  Sets effect mode and does stream on.
 *  Saves incoming frames to output file and prints first 4 pixels of ouput
 *  buffer.
 *  Waits for a number of frames specified in configuration file and streams off.
 *
 **/
boolean test_module_vfe::test_effect (void *SuiteCtx)
{
  boolean                 ret = FALSE, main_ret = TRUE;
  uint32_t                counter = 0; // Count loop iterations
  tm_intf_buffer_desc_t   buf_desc;
  uint32_t                idx;

  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");

  test_module_vfe *pTestSuite = (test_module_vfe *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t        input = tmi->getInput();
  uint32_t               str_identity = tmi->getStreamIdentity(input.stream_types[0]);
  superParam             sup = superParam(input.is_first_module_src);

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

  /* Effect mode */
  {
    uint8_t effectMode = CAM_EFFECT_MODE_SEPIA;
    ret = sup.addParamToBatch(CAM_INTF_PARM_EFFECT,
      sizeof(effectMode), &effectMode);
    ASSERTTRUE(ret, "error in setting effect: SEPIA");
  }
/* Send super-params downstream */
  ret = sup.sendSuperParams(tmi, 0, tmi->getStreamIdentity(input.stream_types[0]));
  ASSERTTRUE(ret, "error in sending super params");

  /* Stream-ON */
  ret = tmi->streamOn(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming on");

  UCDBG("\n <<< exit stream on \n");

  while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION,
    &counter, input.stream_types[0])) {

    ret = tmi->bufReceiveMsg(&buf_desc);
    EXITTRUE(ret, STREAM_OFF, "error in receiving message");

    TMDBG("");
    UCDBG("--->>>receive frame no: %d", buf_desc.frame_id);
    /* Dump output image */

    tmi->getStreamIspOutDims(&buf_desc.dims, buf_desc.identity);
    EXITFALSE(buf_desc.dims.width == 0, STREAM_OFF,
      "output sizes not found for stream identity: %x", buf_desc.identity);

    ret = tmi->calcBufSize(&buf_desc);
    EXITTRUE(ret, STREAM_OFF, "error in calculate buf size");

    UCDBG("%s: receive buffer: addr: %p, index: %d, size: %d for processing\n",
      __func__, buf_desc.vaddr, buf_desc.index, buf_desc.size);
    CDBG_HIGH("%s: receive buffer: addr: %p, index: %d, size: %d for processing\n",
      __func__, buf_desc.vaddr, buf_desc.index, buf_desc.size);

    if ((uint32_t)buf_desc.vaddr & 0xFFF) {
      UCDBG_ERROR("Wrong ISP buffer address!!!");
      continue;
    }

    if(TRUE == ret) {
      buf_desc.frame_id = counter;
      ret = tmi->validateResults(VALIDATE_OUTPUT_FRAME, &buf_desc);
      EXITTRUE(ret, STREAM_OFF, "validateResults failed");

      tmi->publishResults("effect", counter, &buf_desc);

      ret = tmi->sendBufDivertAck(&buf_desc);
      EXITTRUE(ret, STREAM_OFF, "error in sending buf_divert ACK");
    }
    counter++;
  }
STREAM_OFF:
  main_ret = ret;

  /* Stream-OFF */
  UCDBG("going to stream off");
  ret = tmi->streamOff(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming off");

  UCDBG("finished stream off");

  return main_ret;
FAIL:
  UCDBG(": Failed");
  return FALSE;
}

/** Name: test_effect_prv
 *
 *  Arguments/Fields:
 *  @SuiteCtx: Current class context passed by TestSuite
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends all configurations needed to stream on modules.
 *  Sends default parameters and does stream on.
 *  After two frames does stream off sets effect mode
 *  and does stream on.
 *  Saves incoming frames to output file and prints first 4 pixels of ouput
 *  buffer.
 *  Waits for a number of frames specified in configuration file and streams off.
 *
 **/
boolean test_module_vfe::test_effect_prv (void *SuiteCtx)
{
  boolean           ret = FALSE, main_ret = TRUE;
  uint32_t          counter = 0; // Count loop iterations
  tm_intf_buffer_desc_t  buf_desc;
  uint32_t          idx;

  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");

  test_module_vfe *pTestSuite = (test_module_vfe *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t        input = tmi->getInput();
  uint32_t               str_identity = tmi->getStreamIdentity(input.stream_types[0]);
  superParam             sup = superParam(input.is_first_module_src);

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

  /* Send super-params downstream */
  ret = sup.sendSuperParams(tmi, 0, str_identity);
  ASSERTTRUE(ret, "error in sending super params");

  /* Stream-ON */
  ret = tmi->streamOn(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming on");

  while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION,
    &counter, input.stream_types[0])) {

    ret = tmi->bufReceiveMsg(&buf_desc);
    EXITTRUE(ret, STREAM_OFF, "error in receiving message");

    if(TRUE == ret) {
      TMDBG("");
      UCDBG("--->>>receive frame no: %d", buf_desc.frame_id);
      /* Dump output image */

      tmi->getStreamIspOutDims(&buf_desc.dims, buf_desc.identity);
      EXITFALSE(buf_desc.dims.width == 0, STREAM_OFF,
        "output sizes not found for stream identity: %x", buf_desc.identity);

      ret = tmi->calcBufSize(&buf_desc);
      EXITTRUE(ret, STREAM_OFF, "error in calculate buf size");

      UCDBG("%s: receive buffer: addr: %p, index: %d, size: %d for processing\n",
        __func__, buf_desc.vaddr, buf_desc.index, buf_desc.size);
      CDBG("%s: receive buffer: addr: %p, index: %d, size: %d for processing\n",
        __func__, buf_desc.vaddr, buf_desc.index, buf_desc.size);

      if ((uint32_t)buf_desc.vaddr & 0xFFF) {
        UCDBG_ERROR("Wrong ISP buffer address!!!");
        continue;
      }
      buf_desc.frame_id = counter;
      ret = tmi->validateResults(VALIDATE_OUTPUT_FRAME, &buf_desc);
      EXITTRUE(ret, STREAM_OFF, "validateResults failed");

      tmi->publishResults("effect_prv", counter, &buf_desc);

      ret = tmi->sendBufDivertAck(&buf_desc);
      EXITTRUE(ret, STREAM_OFF, "error in sending BufDivertAck");

      /* Effect mode */
      if (counter == 1) {
        ret = tmi->streamOff(input.stream_types[0]);
        ASSERTTRUE(ret, "error in streaming off");

        TMDBG("MCT_EVENT_MODULE_SET_STREAM_CONFIG - sensor format: 0x%x",
          pTestSuite->sen_out_info.fmt);
        ret = tmi->sendModuleEventDown(MCT_EVENT_MODULE_SET_STREAM_CONFIG,
          &(pTestSuite->sen_out_info), str_identity);
        ASSERTTRUE(ret, "error in sending set_stream_config");

        ret = tmi->sendChromatixPointers(str_identity);
        ASSERTTRUE(ret, "error in sending chromatix pointers");

        uint8_t effectMode = CAM_EFFECT_MODE_SEPIA;
        ret = sup.addParamToBatch(CAM_INTF_PARM_EFFECT,
          sizeof(effectMode), &effectMode);
        ASSERTTRUE(ret, "error in setting effect: SEPIA");

        ret = sup.sendSuperParams(tmi, 0, str_identity);
        ASSERTTRUE(ret, "error in sending super params");

        /* Stream-ON */
        ret = tmi->streamOn(input.stream_types[0]);
        ASSERTTRUE(ret, "error in streaming on");
      }

    }
    counter++;
  }
STREAM_OFF:
  main_ret = ret;

  /* Stream-OFF */
  ret = tmi->streamOff(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming off");
  return main_ret;
FAIL:
  UCDBG(": Failed");
  return FALSE;
}

static boolean test_suite_out_port_event(mct_port_t *port, mct_event_t *event)
{
  TM_Interface *tmi = (TM_Interface *)port->port_private;
  tm_intf_buffer_desc_t  buf_desc;
  isp_buf_divert_t  *isp_buf = NULL;

  if (MCT_EVENT_MODULE_EVENT == event->type) {

    switch (event->u.module_event.type) {
      case MCT_EVENT_MODULE_BUF_DIVERT: {

        CDBG_HIGH("%s: MCT_EVENT_MODULE_BUF_DIVERT", __func__);
        UCDBG("%s: MCT_EVENT_MODULE_BUF_DIVERT - identity: %x", __func__, event->identity);

        isp_buf = (isp_buf_divert_t *)(event->u.module_event.module_event_data);
        if (!isp_buf) {
          CDBG_ERROR("%s:%d: isp_buf is NULL!!!\n", __func__, __LINE__);
          return FALSE;
        }

        UCDBG("%s:%d send buf seq %d, addr: 0x%x, index: %d identity: 0x%x for processing\n",
          __func__, __LINE__,
          isp_buf->buffer.sequence, *(uint32_t *)isp_buf->vaddr,
          isp_buf->buffer.index, isp_buf->identity);
        isp_buf->is_buf_dirty = 0;
        isp_buf->ack_flag = 0;

        buf_desc.vaddr = (void *)*(uint32_t *)isp_buf->vaddr;
        buf_desc.index = isp_buf->buffer.index;
        buf_desc.size  = isp_buf->buffer.length;
        buf_desc.frame_id = isp_buf->buffer.sequence;
        buf_desc.timestamp = isp_buf->buffer.timestamp;
        buf_desc.identity = isp_buf->identity;
        tmi->notifyBufAckEvent(&buf_desc);
      }
        break;

      case MCT_EVENT_MODULE_ISP_OUTPUT_DIM: {
        mct_stream_info_t *stream_info =
          (mct_stream_info_t *)(event->u.module_event.module_event_data);
        CDBG_HIGH("%s: MCT_EVENT_MODULE_ISP_OUTPUT_DIM %dx%d, identity: %x",
          __func__, stream_info->dim.width, stream_info->dim.height,
          event->identity);
        tmi->saveStreamIspOutDims(&stream_info->dim, event->identity);
      }
        break;

      case MCT_EVENT_MODULE_STREAM_CROP: {
        mct_bus_msg_stream_crop_t *stream_crop = (mct_bus_msg_stream_crop_t *)
          (event->u.module_event.module_event_data);
        CDBG_HIGH("%s: MCT_EVENT_MODULE_STREAM_CROP %dx%d, identity: %x, " \
          "stream id: %d, frame id: %d, offset %dx%d",
          __func__, stream_crop->crop_out_x, stream_crop->crop_out_y,
          event->identity, stream_crop->stream_id, stream_crop->frame_id,
          stream_crop->x, stream_crop->y);
        tmi->saveStreamIspOutCrop(stream_crop, event->identity);
      }
        break;

      default:
        goto PRINT_UNHANDLED;
      break;
      return TRUE;
    }
  }
PRINT_UNHANDLED:
  CDBG_HIGH("%s: call port event with identity: 0x%x", __func__, event->identity);
  return IEvent::portEvent(port, event);

}

/* Support functions for test suite */

static boolean test_suite_in_port_event(mct_port_t *port, mct_event_t *event)
{
  port_data_t *port_data = (port_data_t *)port->port_private;
  test_module_vfe *ts = (test_module_vfe *)port_data->ts;

  switch(event->type) {
  case MCT_EVENT_CONTROL_CMD:
    switch(event->u.ctrl_event.type) {
    case MCT_EVENT_CONTROL_SET_PARM:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_SET_PARM", __func__);
      ts->test_suite_handle_ctrl_param(port,
        (mct_event_control_parm_t *)event->u.ctrl_event.control_event_data);
      return TRUE;
    default:
      break;
    }
  default:
    break;
  }
  CDBG_HIGH("%s: call port event with identity: 0x%x", __func__, event->identity);
  return IEvent::portEvent(port, event);
}

static boolean test_suite_stats_port_event(mct_port_t *port, mct_event_t *event)
{
  port_data_t *port_data = (port_data_t *)port->port_private;
  TM_Interface *tmi = port_data->tm_intf;
  test_module_vfe *ts = (test_module_vfe *)port_data->ts;

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

      UCDBG("MCT_EVENT_MODULE_BUF_DIVERT_ACK identity: 0x%x",
        buf_divert_ack->identity);

      CDBG_HIGH("buf index: %d, is dirty: %d, frame id: %d, timestamp: %ld,%06lds",
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
        UCDBG("%s: MCT_EVENT_MODULE_SOF_NOTIFY: frame id: %d, num streams: %d",
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
    case MCT_EVENT_MODULE_ISP_OUTPUT_DIM:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_ISP_OUTPUT_DIM", __func__);
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

boolean test_module_vfe::test_suite_handle_ctrl_param(mct_port_t *port,
  mct_event_control_parm_t *event_control)
{
  boolean ret;
  port_data_t *port_data = (port_data_t *)port->port_private;
  TM_Interface *tmi = port_data->tm_intf;
  mct_module_t *module;

  switch (event_control->type) {
  case CAM_INTF_META_STREAM_INFO:
    CDBG_HIGH("%s: CAM_INTF_META_STREAM_INFO", __func__);
    TMDBG("%s: CAM_INTF_META_STREAM_INFO", __func__);
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

boolean test_module_vfe::outPortPrepareCaps(tm_uc_port_caps_t *caps)
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

boolean test_module_vfe::inPortPrepareCaps(tm_uc_port_caps_t *caps)
{
  sensor_src_port_cap_t *sen_caps;
  m_tminterface.prepareSensorCaps(caps);
  sen_caps = (sensor_src_port_cap_t *)caps->caps.u.data;
  sensor_fmt = sen_caps->sensor_cid_ch[0].fmt;
  port_data.tm_intf = &m_tminterface;
  port_data.ts = this;
  caps->priv_data = (void *)&port_data;
  caps->event_func = test_suite_in_port_event;
  return TRUE;
}

boolean test_module_vfe::statsPortPrepareCaps(tm_uc_port_caps_t *caps)
{
  m_tminterface.prepareStatsCaps(caps);
  port_data.tm_intf = &m_tminterface;
  port_data.ts = this;
  caps->priv_data = (void *)&port_data;
  caps->event_func = test_suite_stats_port_event;

  return TRUE;
}

