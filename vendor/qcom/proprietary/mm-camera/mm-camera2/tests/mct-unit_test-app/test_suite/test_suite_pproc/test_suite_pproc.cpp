/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/


#include "test_suite_pproc.h"

#undef LOG_TAG
#define LOG_TAG "mm-camera-test_suite_pproc"

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
  test_module_pproc *ts = new test_module_pproc(tmi);
  return ts;
}

/** Name: test_module_pproc
 *
 *  Arguments/Fields:
 *  tmi: Tm_Interface object
 *  Description:
 *    Constructor method
 **/
test_module_pproc::test_module_pproc(TM_Interface &tmi)
  : ITestModule(tmi),
    m_tminterface(tmi)
{
  TMDBG("%s", __func__);
}

/** Name: ~test_module_pproc
 *
 *  Arguments/Fields:
 *
 *  Description:
 *    Destructor method
 **/
test_module_pproc::~test_module_pproc()
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
void test_module_pproc::registerTestCases()
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
boolean test_module_pproc::unitTest1 (void *SuiteCtx)
{
  boolean           ret = FALSE, main_ret;
  uint32_t          counter = 0; // Count loop iterations
  tm_intf_buffer_desc_t  buf_desc;
  tm_intf_buffer_desc_t  out_buf_desc;
  ASSERTNOTNULL(SuiteCtx, "Missing suite context SuiteCtx");

  test_module_pproc *pTestSuite = (test_module_pproc *)SuiteCtx;
  TM_Interface          *tmi = &(pTestSuite->m_tminterface);
  ASSERTNOTNULL(tmi, "Missing interface object");

  tm_intf_input_t        input = tmi->getInput();
  superParam             sup = superParam(input.is_first_module_src);

  UCDBG(": Enter: identity = 0x%x",
    pTestSuite->m_tminterface.getStreamIdentity(input.stream_types[0]));

  ret = sup.loadDefaultParams(&input);
  ASSERTTRUE(ret, "error in setting default params");


  /* Send super-params downstream */
  ret = sup.sendSuperParams(tmi, 0, tmi->getStreamIdentity(input.stream_types[0]));
  ASSERTTRUE(ret, "error in sending super params");

  ret = tmi->sendIspOutDims(input.stream_types[0]);
  ASSERTTRUE(ret, "error in sending ISP out dims");

  /* Stream-ON */
  ret = tmi->streamOn(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming on");

  /* Set output buffer to stream */
  ret = tmi->setOutputBuffer(input.stream_types[0]);
  ASSERTTRUE(ret, "error in setting output buffer");

  while (FALSE == tmi->checkForTermination(TM_CHECK_ITERATION,
    &counter, input.stream_types[0])) {

    ret = tmi->doReprocess(counter, input.stream_types[0], NULL);
    ASSERTTRUE(ret, "error in doing reprocess");


    ret = tmi->bufReceiveMsg(&buf_desc);
    ASSERTTRUE(ret, "error in receiving message");

    UCDBG("receive frame no: %d", buf_desc.frame_id);
    /* Dump output image */

    ret = tmi->getOutBufDesc(&out_buf_desc, buf_desc.identity);

    if(TRUE == ret) {
      out_buf_desc.frame_id = counter;
      out_buf_desc.identity = buf_desc.identity;

      tmi->getStreamOutDims(&out_buf_desc.dims, out_buf_desc.identity);
      if (out_buf_desc.dims.width == 0) {
        TMDBG_ERROR("output sizes not found for stream identity: %x",
          out_buf_desc.identity);
      }

      ret = tmi->validateResults(VALIDATE_OUTPUT_FRAME, &out_buf_desc);
      EXITTRUE(ret, STREAM_OFF, "validateResults failed");

      tmi->publishResults("ut1", counter, &out_buf_desc);
    }
    counter++;
  }
STREAM_OFF:

  main_ret = ret;

  /* Stream-OFF */
  ret = tmi->streamOff(input.stream_types[0]);
  ASSERTTRUE(ret, "error in streaming off");

  UCDBG(": Status: %d", main_ret);
  return main_ret;
FAIL:
  UCDBG(": Failed");
  return FALSE;
}

/** Name: unitTest2
 *
 *  Arguments/Fields:
 *  @SuiteCtx: Context to access test_module_pproc instance
 *  Return: Boolean result for success/failure
 *
 *  Description: Provide a detailed explanation
 *  of the objective(s) of the test case and include
 *  dependencies, assumptions and limitations if present.
 *
 **/
boolean test_module_pproc::unitTest2 (void *SuiteCtx)
{
  boolean ret = TRUE;

  UCDBG(": Enter");
  if(!SuiteCtx) {
    TMDBG_ERROR("Missing suite context SuiteCtx");
  return ret;
  }
  test_module_pproc *pTestSuite = (test_module_pproc *)SuiteCtx;
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
      buf_desc.identity = buf_divert_ack->identity;
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

boolean test_module_pproc::outPortPrepareCaps(tm_uc_port_caps_t *caps)
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

boolean test_module_pproc::inPortPrepareCaps(tm_uc_port_caps_t *caps)
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

