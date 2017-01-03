/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#ifndef TEST_SUITE_H
#define TEST_SUITE_H

#include "tm_interface.h"
#include "test_module.h"

using namespace tm_interface;

#define RESULT(a) ((a == TRUE) ? ("PASS") : ("FAIL"))
class TestSuite
{
public:

  TestSuite(TM_Interface &tmi, char *lib_name);

  virtual ~TestSuite();

  void listUCNames (void);

  boolean executeSuiteTests(void);

  boolean executeSuiteTests(char* testname);

  boolean executeSuiteTests(uint8_t index);

  boolean executeSuiteTests(int* index_array, uint8_t num_items);

  virtual boolean createDummyPorts(void);

  virtual void deleteDummyPorts(void);

public:

private:
  void                    *lib_fd;
  TM_Interface            &m_tminterface;

  ITestModule *m_testmodule;
};

#endif // TEST_SUITE_H

