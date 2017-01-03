/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "mct_controller.h"
#include "mct_pipeline.h"
#include "mct_module.h"
#include "mct_stream.h"
#include "modules.h"
#include "camera_dbg.h"
#include "test_manager.h"
#include "tm_interface.h"

//#define TM_DEBUG_WAIT_KEY

#undef LOG_TAG
#define LOG_TAG "mm-camera-main"

#define MAX_SUITE_NAME_LEN 100
using namespace tm_interface;

static struct option long_options[] = {
  {"help", no_argument, 0, 'h'},
  {"suite", required_argument, 0, 's'},
  {"usecases", no_argument, 0, 'u'},
  {"index", required_argument, 0, 'i'},
  {"name", required_argument, 0, 'n'},
  {"all", no_argument, 0, 'a'},
  {0, 0, 0, 0}
};


boolean check_input_sanity(char *str)
{
  unsigned int index = 0;
  if (!str) {
    TMPRINT("\nNo input!\n");
    return FALSE;
  }
  for (index = 0; index < strlen(str); index++)
  {
    if( (str[index] >= 'a' && str[index] <= 'z') ||
      (str[index] >= 'A' && str[index] <= 'Z') ||
      (str[index] >= '0' && str[index] <= '9') ||
      (str[index] == '_' || str[index] == '-')) {
      continue;
    }
    else {
      break;
    }
  }
  if (index == strlen(str))
    return TRUE;
  else {
    return FALSE;
  }
}

boolean verify_user_input(char *testsuite_name, char *config_filename, char *param_filename)
{
  boolean rc = TRUE;
  char datapath[10] = "/data/";
  int cfg_file_fd = 0, param_file_fd = 0;
  if(!testsuite_name || !config_filename || !param_filename) {
    TMPRINT("\nInvalid entr(y)ies: testsuite_name: [%p], config_filename: [%p],\
      param_filename: [%p]\n",
      testsuite_name, config_filename, param_filename);
    return FALSE;
  }
  if (FALSE == check_input_sanity(testsuite_name)) {
    TMPRINT("\nBad argument: testsuite_name: [%s]\n", testsuite_name);
    return FALSE;
  }
  snprintf(config_filename, MAX_SUITE_NAME_LEN, "%s%s_config.xml",
    datapath, testsuite_name);
  snprintf(param_filename, MAX_SUITE_NAME_LEN, "%s%s_params.xml",
    datapath, testsuite_name);
  cfg_file_fd = open(config_filename, O_RDONLY);
  param_file_fd = open(param_filename, O_RDONLY);

  if(cfg_file_fd < 0 || param_file_fd < 0) {
    TMPRINT("\nFiles <%s>, <%s> were not found. Exit",
      config_filename, param_filename);
    rc = FALSE;
    goto EXIT;
  }

  EXIT:
    close (cfg_file_fd);
    close (param_file_fd);
    return rc;
}

int main(int argc, char * argv[])
{
  boolean     rc = FALSE;
  int         ret = 0;
  int         option;
  int         option_index = 0;
  char        testsuite_name[MAX_SUITE_NAME_LEN];
  char        config_filename[MAX_SUITE_NAME_LEN];
  char        param_filename[MAX_SUITE_NAME_LEN];
  char        test_name[MAX_FN_NAME_LEN];
  int8_t     test_index = -1;
  tm_intf_execute_method_t  exec_method = TM_EXECUTE_MAX;
  boolean  print_UCList = FALSE;
  boolean  run_test = FALSE;

  TMPRINT("\n### commencing %s ###\n", UNIT_TEST_APP_NAME);

#ifdef TM_DEBUG_WAIT_KEY
  getchar();
#endif //TM_DEBUG_WAIT_KEY


  if (argc > 1) {
    while ((option = getopt_long (argc, argv, "hs:ui:n:a",long_options,
      &option_index)) != -1)
    {
      switch (option) {
        case 'h':
          TMPRINT("USAGE HELP:\n");
          TMPRINT("%s -s/--suite <test_suite_name>\n", UNIT_TEST_APP_NAME);
          TMPRINT("Additional Options:\n");
          TMPRINT("-u/--usecases: Lists usecases in test-suite \
            specified with -s\n");
          TMPRINT("-i/--index: Specify index of usecase in test-suite \
            to run\n");
          TMPRINT("-n/--name: Specify name of usecase in test-suite \
            to run\n");
          TMPRINT("-a/--all : Run all usecases in test-suite to run\n");
          goto EXIT;
          break;
        case 's':
          if (optarg != NULL) {
            strlcpy(testsuite_name, optarg, MAX_SUITE_NAME_LEN);
            run_test = TRUE;
            TMPRINT("\nOpening test suite: %s\n", testsuite_name);
            if(FALSE == verify_user_input(testsuite_name,
              config_filename, param_filename)) {
              goto EXIT;
            }
          }
          break;
        case 'u':
            print_UCList = TRUE;
          break;
        case 'a':
          exec_method = TM_EXECUTE_ALL;
          break;
        case 'i':
          if (optarg != NULL) {
            if (isdigit (optarg[0])) {
              test_index = (uint8_t) (atoi (optarg) - 1);
              TMPRINT("selected test index: %d", test_index);
            }
          } else {
            TMPRINT("index is not specified!");
            goto EXIT;
          }
          break;
        case 'n':
          if (optarg != NULL) {
           strlcpy(test_name, optarg, MAX_FN_NAME_LEN);
           TMPRINT("selected test name: %s", test_name);
          } else {
            TMPRINT("name is not specified!");
            goto EXIT;
          }
          break;
        default:
          TMDBG_ERROR("Unrecognized tag");
          goto EXIT;
          break;
      }
    }

    if (run_test) {
      Test_Manager             tst_mgr;
      XML_Parser              xml_parse;
      TM_Interface            tmi;
      tm_intf_input_t           input;

      memset(&input, 0, sizeof (tm_intf_input_t));
      rc = xml_parse.parseConfig(config_filename, &input);
      if (FALSE == rc) {
        TMPRINT("Parsing config file %s unsuccessful\n", config_filename);
        goto EXIT;
      }
      rc = xml_parse.parseUcParams(param_filename, &input, &input.params_lut);
      if (FALSE == rc) {
        TMPRINT("Parsing params file %s unsuccessful\n", param_filename);
        goto EXIT;
      }

      /* Override options for test-suite usecases */
      if (TM_EXECUTE_ALL == exec_method) {
        input.execute_params.method = TM_EXECUTE_ALL;
      }
      if (-1 != test_index) {
        input.execute_params.index = test_index;
        input.execute_params.method = TM_EXECUTE_BY_INDEX;
      }
      if(test_name[0] != '\0') {
        strlcpy(input.execute_params.name, test_name, MAX_FN_NAME_LEN);
        input.execute_params.method = TM_EXECUTE_BY_NAME;
      }
      if(print_UCList) {
        input.print_usecases = TRUE;
      }

      rc = tst_mgr.executeTest(&input);
      if (rc == FALSE)
        goto EXIT1;
    }
    else {
      TMPRINT("Specify test-suite to run with tag --suite\n");
      goto EXIT;
    }
  } else {
    TMPRINT("Usage: %s [OPTIONS] <test_suite_name>\n", UNIT_TEST_APP_NAME);
    goto EXIT;
  }

EXIT1:
  if (TRUE == rc) {
    TMPRINT("\nSuccessfully finished %s\n", UNIT_TEST_APP_NAME);
  } else {
    TMPRINT("\nFail in %s\n", UNIT_TEST_APP_NAME);
    ret = -1;
  }

EXIT:
  return ret;
}


