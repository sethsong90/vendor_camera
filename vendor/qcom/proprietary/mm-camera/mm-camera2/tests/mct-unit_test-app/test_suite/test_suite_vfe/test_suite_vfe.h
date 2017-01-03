/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#ifndef TEST_SUITE_VFE_H_
#define TEST_SUITE_VFE_H_

#include "mct_module.h"
#include "modules.h"
#include "cam_intf.h"
#include "camera_dbg.h"
#include "isp_module.h"
#include "module_sensor.h"
#include "tm_interface.h"
#include "test_suite_common.h"
#include "test_module.h"

using namespace tm_interface;

typedef struct {
  TM_Interface *tm_intf;
  void *ts;
} port_data_t;

class test_module_vfe : public ITestModule
{
public:
  test_module_vfe(TM_Interface &tmi);
  virtual ~test_module_vfe();
  virtual boolean inPortPrepareCaps(tm_uc_port_caps_t *caps);
  virtual boolean outPortPrepareCaps(tm_uc_port_caps_t *caps);
  virtual boolean statsPortPrepareCaps(tm_uc_port_caps_t *caps);

  void registerTestCases();
  static boolean unitTest1 (void *SuiteCtx);
  static boolean test_effect (void *SuiteCtx);
  static boolean test_effect_prv (void *SuiteCtx);

  boolean test_suite_handle_ctrl_param(mct_port_t *port,
    mct_event_control_parm_t *event_control);

private:
  TM_Interface      &m_tminterface;
  cam_format_t      sensor_fmt;
  sensor_out_info_t sen_out_info;
  sensor_set_dim_t  sensor_dims;
  port_data_t       port_data;

};


#endif // TEST_SUITE_VFE_H_

