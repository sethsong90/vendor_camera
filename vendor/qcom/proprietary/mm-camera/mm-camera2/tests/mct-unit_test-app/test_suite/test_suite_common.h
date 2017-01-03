/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#ifndef TEST_SUITE_COMMON_H
#define TEST_SUITE_COMMON_H

#define MAX_SUPPORTED_IMG_WIDTH 6240
#define MAX_SUPPORTED_IMG_HEIGHT 4680

/* Macros related to debug prints */
#define UC_LOG_SILENT   0
#define UC_LOG_DEBUG    1

#define UC_DBGLEVEL UC_LOG_DEBUG

#if (UC_DBGLEVEL == UC_LOG_SILENT)
  #define UCDBG(fmt, args...)       do {} while (0)
#else
  #define UCDBG(fmt, args...)       { \
    struct timeval tv;  \
    gettimeofday(&tv, NULL);  \
    printf("<%lu:%06lu>[%s:%d]: " fmt"\n", tv.tv_sec, tv.tv_usec, \
      __func__, __LINE__, ##args); \
  }
#endif

#define UCDBG_ERROR(fmt, args...) printf("[%s:%d]: Error: " fmt"\n", \
  __func__, __LINE__, ##args)

#define UCPRINTF(fmt, args...) printf(fmt, ##args)

#define ASSERTTRUE(cond, ...)  if (!cond) { \
                                      UCDBG_ERROR(__VA_ARGS__); \
                                        return FALSE;   \
                                    }
#define ASSERTFALSE(cond, ...)  if (cond) { \
                                      UCDBG_ERROR(__VA_ARGS__); \
                                        return FALSE;   \
                                    }
#define ASSERTNULL(ptr, ...)  if (ptr) { \
                                      UCDBG_ERROR(__VA_ARGS__); \
                                        return FALSE;   \
                                    }
#define ASSERTNOTNULL(ptr, ...)  if (!ptr) { \
                                      UCDBG_ERROR(__VA_ARGS__); \
                                        return FALSE;   \
                                    }
#define ASSERTEQUAL(expected, actual, ...)  if (expected != actual) { \
                                      UCDBG_ERROR(__VA_ARGS__); \
                                        return FALSE;   \
                                    }
#define ASSERTNOTEQUAL(expected, actual, ...)  if (expected == actual) { \
                                      UCDBG_ERROR(__VA_ARGS__); \
                                        return FALSE;   \
                                    }

#define EXITTRUE(cond, label, ...)  if (!cond) { \
                                      UCDBG_ERROR(__VA_ARGS__); \
                                      goto label;   \
                                    }

#define EXITFALSE(cond, label, ...)  if (cond) { \
                                      UCDBG_ERROR(__VA_ARGS__); \
                                      goto label;   \
                                    }


#endif // TEST_SUITE_COMMON_H

