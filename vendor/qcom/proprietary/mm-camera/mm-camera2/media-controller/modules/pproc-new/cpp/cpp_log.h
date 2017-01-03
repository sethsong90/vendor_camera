/*============================================================================

  Copyright (c) 2013-2015 Qualcomm Technologies, Inc. All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

============================================================================*/
#ifndef CPP_LOG_H
#define CPP_LOG_H

#include "camera_dbg.h"

#define CPP_LOG_SILENT     0
#define CPP_LOG_NORMAL     1
#define CPP_LOG_DEBUG      2
#define CPP_LOG_VERBOSE    3

/* -------------- change this macro to enable profiling logs  --------------*/
#define CPP_DEBUG_PROFILE  0

/* ------- change this macro to enable mutex debugging for deadlock --------*/
#define CPP_DEBUG_MUTEX    0

extern volatile int32_t gcam_cpp_loglevel;
/* -------------------------------------------------------------------------*/
#define CPP_PROFILE(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)
#define CPP_LOW(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_HIGH(fmt, args...) CLOGH(CAM_CPP_MODULE, fmt, ##args)
#define CPP_ERR(fmt, args...) CLOGE(CAM_CPP_MODULE, fmt, ##args)
#define CPP_DBG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)
#define CPP_INFO(fmt, args...) CLOGI(CAM_CPP_MODULE, fmt, ##args)

/* Frame message */
#define CPP_FRAME_MSG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)

/*stripe related messgaes */
#define CPP_STRIPE(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_STRIPE_SCALE(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_STRIPE_DSDN(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_STRIPE_MMU(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_STRIPE_ASF(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_STRIPE_IP(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_STRIPE_FE(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_STRIPE_WE(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_STRIPE_ROT(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)

/* ASF related message */
#define CPP_ASF_LOW(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_ASF_HIGH(fmt, args...) CLOGH(CAM_CPP_MODULE, fmt, ##args)
#define CPP_ASF_DBG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)

/* Prepare frame related messages */
#define CPP_FRAME_LOW(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_FRAME_HIGH(fmt, args...) CLOGH(CAM_CPP_MODULE, fmt, ##args)
#define CPP_FRAME_DBG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)

/* Crop related messages */
#define CPP_CROP_LOW(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_CROP_HIGH(fmt, args...) CLOGH(CAM_CPP_MODULE, fmt, ##args)
#define CPP_CROP_DBG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)

/* TNR related messages */
#define CPP_TNR_LOW(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_TNR_HIGH(fmt, args...) CLOGH(CAM_CPP_MODULE, fmt, ##args)
#define CPP_TNR_DBG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)

/* BUF related messages */
#define CPP_BUF_LOW(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_BUF_HIGH(fmt, args...) CLOGH(CAM_CPP_MODULE, fmt, ##args)
#define CPP_BUF_DBG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)

/* PER_FRAME related messages */
#define CPP_PER_FRAME_LOW(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_PER_FRAME_HIGH(fmt, args...) CLOGH(CAM_CPP_MODULE, fmt, ##args)
#define CPP_PER_FRAME_DBG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)

/* Meta data related messages */
#define CPP_META_LOW(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_META_HIGH(fmt, args...) CLOGH(CAM_CPP_MODULE, fmt, ##args)
#define CPP_META_DBG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)

/* WNR related messages */
#define CPP_DENOISE_LOW(fmt, args...) CLOGL(CAM_CPP_MODULE, fmt, ##args)
#define CPP_DENOISE_HIGH(fmt, args...) CLOGH(CAM_CPP_MODULE, fmt, ##args)
#define CPP_DENOISE_DBG(fmt, args...) CLOGD(CAM_CPP_MODULE, fmt, ##args)

#undef PTHREAD_MUTEX_LOCK
#undef PTHREAD_MUTEX_UNLOCK

#if (CPP_DEBUG_MUTEX)
  #define PTHREAD_MUTEX_LOCK(m) do { \
    CPP_HIGH("[cpp_mutex_log] before pthread_mutex_lock(%p)", m); \
    pthread_mutex_lock(m); \
    CPP_HIGH("[cpp_mutex_log] after pthread_mutex_lock(%p)", m); \
  } while(0)

  #define PTHREAD_MUTEX_UNLOCK(m) do { \
    CPP_HIGH("[cpp_mutex_log] before pthread_mutex_unlock(%p)\n", m); \
    pthread_mutex_unlock(m); \
    CPP_HIGH("[cpp_mutex_log] after pthread_mutex_unlock(%p)\n", m); \
  } while(0)
#else
  #define PTHREAD_MUTEX_LOCK(m)   pthread_mutex_lock(m)
  #define PTHREAD_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#endif
#endif
