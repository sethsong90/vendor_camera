/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#ifndef TEST_SUITE_IMGLIB_H_
#define TEST_SUITE_IMGLIB_H_

#include "mct_module.h"
#include "modules.h"
#include "cam_intf.h"
#include "camera_dbg.h"
#include "pproc_port.h"
#include "tm_interface.h"
#include "test_suite_common.h"
#include "test_module.h"

using namespace tm_interface;

class test_module_imglib : public ITestModule
{
public:
  test_module_imglib(TM_Interface &tmi);
  virtual ~test_module_imglib();
  virtual boolean inPortPrepareCaps(tm_uc_port_caps_t *caps);
  virtual boolean outPortPrepareCaps(tm_uc_port_caps_t *caps);
  void    registerTestCases();

  void handleSuperParams(mct_event_t *event);

  static boolean unitTest1 (void *SuiteCtx);
  static boolean unitTest2 (void *SuiteCtx);


private:
  TM_Interface &m_tminterface;
  cam_fps_range_t cur_fps;

};

#endif // TEST_SUITE_IMGLIB_H_

