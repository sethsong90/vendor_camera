/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#include <dlfcn.h>
#include "test_suite.h"

void *(*open_lib)(TM_Interface &);


TestSuite::TestSuite(TM_Interface &tmi, char *lib_name)
  : lib_fd(NULL),
    m_tminterface(tmi)
{
  uint32_t i;

  lib_fd = dlopen(lib_name, RTLD_NOW);
  if (!lib_fd) {
    TMDBG_ERROR("Cannot open lib: %s. Error: %s", lib_name, dlerror());
    return;
  }
  *(void **)&open_lib = dlsym(lib_fd, "test_suite_open");
  if (!open_lib) {
    TMDBG_ERROR("Fail to open test_suite. Error: %s",dlerror());
    dlclose(lib_fd);
    return;
  }
  m_testmodule = (ITestModule *)open_lib(m_tminterface);
  if (NULL == m_testmodule) {
    TMDBG_ERROR("Failed to open test suite library!");
    dlclose(lib_fd);
    return;
  }
  TMDBG("Registering test cases with module");
  for (i = 0 ; i < MAX_TEST_CASES ; i++) {
    m_testmodule->list_testcases[i].test_function = NULL;
    m_testmodule->list_testcases[i].name[0] = 0;
  }
  m_testmodule->registerTestCases();
  if (m_testmodule->num_test_cases >= MAX_TEST_CASES) {
    m_testmodule->num_test_cases = MAX_TEST_CASES - 1;
    TMDBG_ERROR("num test case returned by test suite is bigger than MAX");
  }
}

TestSuite::~TestSuite()
{
  if (m_testmodule)
    delete m_testmodule;
  if (lib_fd)
    dlclose(lib_fd);
}

void TestSuite::listUCNames (void)
{
  uint8_t index = 0;
  TMPRINT("\nUseCase Names:");
  for (index = 0; index < m_testmodule->num_test_cases; index++) {
    TMPRINT("%d:  %s", index+1, m_testmodule->list_testcases[index].name);
  }
}

boolean TestSuite::executeSuiteTests(void)
{
  uint8_t i = 0;
  boolean  unit_test_rc = FALSE, result = TRUE;

  if (!m_testmodule) {
    TMDBG_ERROR("No ITestModule object instance!");
    return FALSE;
  }

  TMDBG("\nEntering process function--->>>\n");
  for (i = 0; i < m_testmodule->num_test_cases; i++) {
    TMDBG("\nNow running: test case [%d] %s:", i,
      m_testmodule->list_testcases[i].name);
    if (!m_testmodule->list_testcases[i].test_function) {
      TMDBG_ERROR("no test case function!");
      unit_test_rc = FALSE;
      continue;
    }
    unit_test_rc = m_testmodule->list_testcases[i].test_function(m_testmodule);
    TMDBG("test case [%d] %s: Result = %s\n", i,
      m_testmodule->list_testcases[i].name, RESULT(unit_test_rc));
    result &= unit_test_rc;
  }
  TMDBG("\nExited from process function. Overall result: %s<<<---\n",
    RESULT(result));
  return result;
}

boolean TestSuite::executeSuiteTests(char* testname)
{
  uint8_t i;
  boolean  unit_test_rc = FALSE;
  if (!m_testmodule) {
    TMDBG_ERROR("No ITestModule object instance!");
    return FALSE;
  }
  if (!testname) {
    TMDBG_ERROR("No test case provided");
    return unit_test_rc;
  }
  for (i = 0; i < m_testmodule->num_test_cases; i++) {
    if(!strcmp(testname, m_testmodule->list_testcases[i].name)) {
      break;
    }
  }
  if(i == m_testmodule->num_test_cases) {
    TMDBG_ERROR("No test with matching name [%s]", testname);
    return FALSE;
  }
  TMDBG("\nNow running: test case [%d] %s:", i,
    m_testmodule->list_testcases[i].name);
  unit_test_rc = m_testmodule->list_testcases[i].test_function(m_testmodule);
  TMDBG("test case [%d] %s: Result = %s\n", i,
    m_testmodule->list_testcases[i].name, RESULT(unit_test_rc));

  return unit_test_rc;
}

boolean TestSuite::executeSuiteTests(uint8_t index)
{
  boolean  unit_test_rc = FALSE;
  if (!m_testmodule) {
    TMDBG_ERROR("No ITestModule object instance!");
    return FALSE;
  }
  if (index >= m_testmodule->num_test_cases) {
    TMDBG_ERROR("Invalid test-case index %d", index);
    return unit_test_rc;
  }
  if (!m_testmodule->list_testcases[index].test_function) {
    TMDBG_ERROR("No test case function!");
    return FALSE;
  }
  TMDBG("\nNow running: test case [%d] %s:", index,
    m_testmodule->list_testcases[index].name);
  unit_test_rc = m_testmodule->list_testcases[index].test_function(m_testmodule);
  TMDBG("test case [%d] %s: Result = %s\n", index,
    m_testmodule->list_testcases[index].name, RESULT(unit_test_rc));
  return unit_test_rc;
}

boolean TestSuite::executeSuiteTests(int *index_array, uint8_t num_items)
{
  uint8_t i = 0;
  boolean  unit_test_rc = FALSE, result = TRUE;
  if (!m_testmodule) {
    TMDBG_ERROR("No ITestModule object instance!");
    return FALSE;
  }
  if (!index_array || 0 == num_items || num_items >= MAX_BATCH_INDEXES) {
    TMDBG_ERROR("Invalid entries: index_array = %p, num_items = %d",
        index_array, num_items);
    return unit_test_rc;
  }

  for (i = 0; i < num_items; i++) {
    if (index_array[i] >= m_testmodule->num_test_cases) {
      TMDBG_ERROR("Invalid test-case index %d", index_array[i]);
    } else {
      TMPRINT("\nNow running: test case [%d] %s:", index_array[i],
        m_testmodule->list_testcases[index_array[i]].name);
      if (!m_testmodule->list_testcases[index_array[i]].test_function) {
        TMDBG_ERROR("no test case function!");
        unit_test_rc = FALSE;
        continue;
      }
      unit_test_rc = m_testmodule->list_testcases
        [index_array[i]].test_function(m_testmodule);
      TMPRINT("test case [%d] %s: Result = %s\n", index_array[i],
        m_testmodule->list_testcases[index_array[i]].name, RESULT(unit_test_rc));
      result &= unit_test_rc;
    }
  }
  TMPRINT("\nOverall result: %s\n", RESULT(result));
  return result;
}

boolean TestSuite::createDummyPorts(void)
{
  boolean rc;
  tm_uc_port_caps_t       port_caps;
  uint32_t          num_ports = 0;

  if (!m_testmodule) {
    TMDBG_ERROR("No ITestModule object instance!");
    return FALSE;
  }

  // Create a dummy input port and set function handles
  // if first module is iface, then will create dummy sensor port.
  // If port type is opaque then data field will be allocated and
  // should allocate data for every port
  do {
    rc = m_testmodule->inPortPrepareCaps(&port_caps);
    if (FALSE == rc) {
      break;
    }
    if (MCT_PORT_CAPS_OPAQUE == port_caps.caps.port_caps_type) {
      rc = m_tminterface.createInputPort(&port_caps, 1);
      num_ports++;
    } else {
      rc = m_tminterface.createInputPort(&port_caps, 12);
      num_ports = 12;
    }
    if (FALSE == rc) {
      TMDBG_ERROR("Cannot create input port!");
      goto EXIT;
    }
  } while (num_ports < 12);

  // create stats port - will be used if isp is present
  if (TRUE == m_testmodule->statsPortPrepareCaps(&port_caps)) {
    rc = m_tminterface.createStatsPort(&port_caps, 12);
    if (FALSE == rc) {
      TMDBG_ERROR("Cannot create stats port!");
      goto EXIT;
    }
  }

  // create sink output port
  if (TRUE == m_testmodule->outPortPrepareCaps(&port_caps)) {
    rc = m_tminterface.createOutputPort(&port_caps, 12);
    if (FALSE == rc) {
      TMDBG_ERROR("Cannot create output port!");
      goto EXIT;
    }
  }
  return TRUE;


EXIT2:
  m_tminterface.destroyStatsPort();
EXIT1:
  m_tminterface.destroyInputPort();
EXIT:
  return FALSE;
}

void TestSuite::deleteDummyPorts(void)
{
  m_tminterface.destroyStatsPort();
  m_tminterface.destroyInputPort();
  m_tminterface.destroyOutputPort();
}
