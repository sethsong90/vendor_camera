/* q3a_platform.h
 *                                                                   .
 * Copyright (c) 2014-2016 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __Q3A_PLATFORM_H__
#define __Q3A_PLATFORM_H__

/* ==========================================================================
                     INCLUDE FILES FOR MODULE
========================================================================== */
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#include "stats_debug.h"
#include "cam_types.h"


/* ==========================================================================
                       Preprocessor Definitions and Constants
========================================================================== */
/**
 * Typedef for boolean.
 */
typedef   int   boolean;

typedef uint8_t     uint8;
typedef int8_t      int8;
typedef uint16_t    uint16;
typedef int16_t     int16;
typedef uint32_t    uint32;
typedef int32_t     int32;
typedef uint64_t    uint64;
typedef int64_t     int64;

#ifndef        TRUE
#define        TRUE         1
#endif
#ifndef        FALSE
#define        FALSE        0
#endif


#define STATS_TEST(_test) (stats_debug_test == _test)

/**
 * Allocate and free macros.
 */
#define AWB_MALLOC(numBytes)                malloc(numBytes)
#define AWB_FREE(ptr)                       free(ptr)

#define AEC_MALLOC(numBytes)                malloc(numBytes)
#define AEC_FREE(ptr)                       free(ptr)

#define AF_MALLOC(numBytes)                 malloc(numBytes)
#define AF_FREE(ptr)                        free(ptr)

/**
 * Math macros.
 */
#define Q3A_LOG2                            log2

/**
* Sleep macro.
*/
#define Q3A_SLEEP(value)                    sleep(value / 1000)

/**
 * Memory macros.
 */
#define Q3A_MEMSET(ptr, value, num_bytes)       memset(ptr, value, num_bytes)
#define Q3A_MEMCPY(to, from, length)            memcpy(to, from, length)
#define Q3A_MEMCMP(pSource1, pSource2, nLength) memcmp(pSource1, pSource2, nLength)

#define Q3A_MEMCPY_S(dest, destLength, source, sourceLength) Q3A_MEMCPY(dest, source, sourceLength)
/**
 * kpi macros.
 */
#define Q3A_ATRACE_INT KPI_ATRACE_INT
#define Q3A_ATRACE_END ATRACE_END
#define Q3A_ATRACE_INT_IF Q3A_TRACE_INT
#define Q3A_ATRACE_BEGIN_SNPRINTF ATRACE_BEGIN_SNPRINTF

/**
 * HAL macros dependencies
 */
#define MAX_STATS_ROI_NUM (10)

/**
 * Logging macros.
 */
#undef LOG_TAG
#define LOG_TAG "mm-camera-CORE"

#define AWB_MSG_ERROR(fmt, args...) \
    CLOGE(CAM_STATS_AWB_MODULE,  fmt, ##args)

#define AWB_MSG_HIGH(fmt, args...) \
    CLOGH(CAM_STATS_AWB_MODULE,  fmt, ##args)

#define AWB_MSG_LOW(fmt, args...) \
    CLOGL(CAM_STATS_AWB_MODULE,  fmt, ##args)

#define AWB_MSG_INFO(fmt, args...) \
    CLOGI(CAM_STATS_AWB_MODULE,  fmt, ##args)


#define AEC_MSG_ERROR(fmt, args...) \
    CLOGE(CAM_STATS_AEC_MODULE,  fmt, ##args)

#define AEC_MSG_HIGH(fmt, args...) \
    CLOGH(CAM_STATS_AEC_MODULE,  fmt, ##args)

#define AEC_MSG_LOW(fmt, args...) \
    CLOGL(CAM_STATS_AEC_MODULE,  fmt, ##args)

#define AEC_MSG_INFO(fmt, args...) \
    CLOGI(CAM_STATS_AEC_MODULE,  fmt, ##args)


#define AF_MSG_ERROR(fmt, args...) \
    CLOGE(CAM_STATS_AF_MODULE,  fmt, ##args)

#define AF_MSG_HIGH(fmt, args...) \
    CLOGH(CAM_STATS_AF_MODULE,  fmt, ##args)

#define AF_MSG_LOW(fmt, args...) \
    CLOGL(CAM_STATS_AF_MODULE,  fmt, ##args)

#define AF_MSG_INFO(fmt, args...) \
    CLOGI(CAM_STATS_AF_MODULE,  fmt, ##args)


#define HAF_MSG_ERROR(fmt, args...) \
    CLOGE(CAM_STATS_HAF_MODULE,  fmt, ##args)

#define HAF_MSG_HIGH(fmt, args...) \
    CLOGH(CAM_STATS_HAF_MODULE,  fmt, ##args)

#define HAF_MSG_LOW(fmt, args...) \
    CLOGL(CAM_STATS_HAF_MODULE,  fmt, ##args)

#define HAF_MSG_INFO(fmt, args...) \
    CLOGI(CAM_STATS_HAF_MODULE,  fmt, ##args)

#define CAF_SCAN_MSG(fmt, args...) \
    CLOGL(CAM_STATS_CAF_SCAN_MODULE,  fmt, ##args)

#define Q3A_MSG_ERROR(fmt, args...) \
    CLOGE(CAM_STATS_Q3A_MODULE,  fmt, ##args)

#define Q3A_MSG_HIGH(fmt, args...) \
    CLOGH(CAM_STATS_Q3A_MODULE,  fmt, ##args)

#define Q3A_MSG_INFO(fmt, args...) \
    CLOGI(CAM_STATS_Q3A_MODULE,  fmt, ##args)



/**
 * Max value of floating point number.
 */
#define Q3A_FLT_MAX FLT_MAX

/**
 * Assert macro.
 */
#define Q3A_ASSERT assert

/**
 * MAX and MIN macros.
 */
#undef MAX
#define MAX(x, y)       (((x) > (y)) ? (x) : (y))

#undef MIN
#define MIN(x, y)       (((x) < (y)) ? (x) : (y))


#define VSNPRINT(buf, size, fmt, arg) vsnprintf(buf, size, fmt, arg)
/* ==========================================================================
                Functions
========================================================================== */
/**
* Returns the current time in ms.
*/
uint32 q3a_get_time_ms();
void q3a_core_set_log_level ();
void q3a_algo_set_log_level(
  uint32_t kpi_debug_level_algo,
  uint32_t stats_debug_mask_algo);


#endif // __Q3A_PLATFORM_H__

