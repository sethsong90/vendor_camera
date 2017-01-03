/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#ifndef TEST_SUITE_SENSOR_H_
#define TEST_SUITE_SENSOR_H_

#include "mct_module.h"
#include "modules.h"
#include "cam_intf.h"
#include "camera_dbg.h"
#include "module_sensor.h"
#include "tm_interface.h"
#include "test_suite_common.h"
#include "test_module.h"

using namespace tm_interface;

class test_sensor : public ITestModule
{
public:
  test_sensor(TM_Interface &tmi);
  virtual ~test_sensor();
  virtual boolean outPortPrepareCaps(tm_uc_port_caps_t *caps);
  virtual boolean statsPortPrepareCaps(tm_uc_port_caps_t *caps);

  void registerTestCases();

  static boolean sensor_test_prv (void *SuiteCtx);
  static boolean sensor_test_fps (void *SuiteCtx);
  static boolean sensor_test_dummy (void *SuiteCtx);

private:
  TM_Interface &m_tminterface;
};

#endif // TEST_SUITE_SENSOR_H_

