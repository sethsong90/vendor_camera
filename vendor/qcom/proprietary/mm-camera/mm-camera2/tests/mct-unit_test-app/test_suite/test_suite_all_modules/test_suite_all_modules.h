/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#ifndef TEST_SUITE_ALL_MODULES_H_

#define TEST_SUITE_ALL_MODULES_H_

#include "mct_module.h"
#include "modules.h"
#include "cam_intf.h"
#include "camera_dbg.h"

#include "pproc_port.h"
#include "isp_module.h"
#include "module_sensor.h"

#include "tm_interface.h"
#include "test_suite_common.h"
#include "test_module.h"

using namespace tm_interface;

class test_all_modules : public ITestModule
{
public:
  test_all_modules(TM_Interface &tmi);
  virtual ~test_all_modules();
  virtual boolean outPortPrepareCaps(tm_uc_port_caps_t *caps);
  virtual boolean statsPortPrepareCaps(tm_uc_port_caps_t *caps);

  void registerTestCases();

  static boolean unit_test_prv (void *SuiteCtx);
  static boolean unit_test_fps (void *SuiteCtx);
  static boolean unit_test_3 (void *SuiteCtx);
  static boolean unit_test_4 (void *SuiteCtx);

private:
  TM_Interface &m_tminterface;
};

#endif // TEST_SUITE_ALL_MODULES_H_

