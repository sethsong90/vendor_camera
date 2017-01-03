/*============================================================================

  Copyright (c) 2013-2015 Qualcomm Technologies, Inc. All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

============================================================================*/
#ifndef C2D_LOG_H
#define C2D_LOG_H

#include "camera_dbg.h"

#define C2D_LOG_SILENT   0
#define C2D_LOG_NORMAL   1
#define C2D_LOG_DEBUG    2
#define C2D_LOG_VERBOSE  3


/* ------- change this macro to enable mutex debugging for deadlock --------*/
#define C2D_DEBUG_MUTEX  0

extern volatile int32_t gcam_c2d_loglevel;
/* -------------------------------------------------------------------------*/

#define C2D_LOW(fmt, args...) CLOGL(CAM_C2D_MODULE, fmt, ##args)
#define C2D_HIGH(fmt, args...) CLOGH(CAM_C2D_MODULE, fmt, ##args)
#define C2D_ERR(fmt, args...) CLOGE(CAM_C2D_MODULE, fmt, ##args)
#define C2D_DBG(fmt, args...) CLOGD(CAM_C2D_MODULE, fmt, ##args)
#define C2D_INFO(fmt, args...) CLOGI(CAM_C2D_MODULE, fmt, ##args)
#define C2D_PROFILE(fmt, args...) CAM_DBG(CAM_C2D_MODULE, fmt, ##args)

#undef PTHREAD_MUTEX_LOCK
#undef PTHREAD_MUTEX_UNLOCK

#if (C2D_DEBUG_MUTEX == 1)
  #define PTHREAD_MUTEX_LOCK(m) do { \
    C2D_DBG("[c2d_mutex_log] before pthread_mutex_lock(%p)\n", \
      m); \
    pthread_mutex_lock(m); \
    C2D_DBG("[c2d_mutex_log] after pthread_mutex_lock(%p)\n", \
      m); \
  } while(0)

  #define PTHREAD_MUTEX_UNLOCK(m) do { \
    C2D_DBG("[c2d_mutex_log] before pthread_mutex_unlock(%p)\n", \
      m); \
    pthread_mutex_unlock(m); \
    C2D_DBG("[c2d_mutex_log] after pthread_mutex_unlock(%p)\n", \
      m); \
  } while(0)
#else
  #define PTHREAD_MUTEX_LOCK(m)   pthread_mutex_lock(m)
  #define PTHREAD_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#endif
#endif
