/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#ifndef TEST_MODULE_H
#define TEST_MODULE_H

#include "tm_interface.h"


#define MAX_TEST_CASES 100

using namespace tm_interface;

typedef boolean (*test_fn) (void *SuiteCtx);

typedef struct {
  char name[MAX_FN_NAME_LEN];
  test_fn test_function;
}unit_function_template_t;


class ITestModule
{
  friend class TestSuite;
public:
  ITestModule(TM_Interface &tmi);

  virtual ~ITestModule();

  virtual void registerTestCases();

  virtual boolean inPortPrepareCaps(tm_uc_port_caps_t *caps);

  virtual boolean outPortPrepareCaps(tm_uc_port_caps_t *caps);

  virtual boolean statsPortPrepareCaps(tm_uc_port_caps_t *caps);

protected:
  uint8_t num_test_cases;
  unit_function_template_t list_testcases[MAX_TEST_CASES];

};

#endif // TEST_MODULE_H

