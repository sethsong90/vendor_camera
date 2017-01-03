/*============================================================================

  Copyright (c) 2015, 2016 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#include "test_module.h"

ITestModule::ITestModule(TM_Interface &tmi __unused)
{
  TMDBG("%s", __func__);
}

ITestModule::~ITestModule()
{
  TMDBG("%s", __func__);
}

boolean ITestModule::inPortPrepareCaps(tm_uc_port_caps_t *caps __unused)
{
  if (!caps)
    return FALSE;
  return FALSE;
}

boolean ITestModule::outPortPrepareCaps(tm_uc_port_caps_t *caps __unused)
{
  if (!caps)
    return FALSE;
  return FALSE;
}

boolean ITestModule::statsPortPrepareCaps(tm_uc_port_caps_t *caps __unused)
{
  if (!caps)
    return FALSE;
  return FALSE;
}

void ITestModule::registerTestCases()
{
}
