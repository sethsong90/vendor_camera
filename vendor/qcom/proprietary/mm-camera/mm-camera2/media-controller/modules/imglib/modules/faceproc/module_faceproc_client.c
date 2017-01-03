/**********************************************************************
*  Copyright (c) 2013-2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/

#ifdef _ANDROID_
#include <cutils/properties.h>
#endif
#ifdef FD_USES_FASTCV
#include "fastcv/fastcv.h"
#endif
#include <linux/media.h>
#include "mct_module.h"
#include "module_faceproc.h"
#include "mct_stream.h"
#include "modules.h"
#include "fd_chromatix.h"
#include "img_buffer.h"
#include <stdbool.h>

//#define USE_OFFLINE_BUFFER
//#define FD_RESULT_DEBUG
//#define FD_DYNAMIC_REGION_RESULT_DEBUG
//#define FD_USE_PROP

/** IMG_IS_HAL3
 *
 *  Macro to check if the HAL version is 3.0
 **/
#define IMG_IS_HAL3(p_client) (p_client->hal_version == CAM_HAL_V3)

/** IMG_IS_FACE_SCALING_NEEDED
 *
 *  Macro to check if face scaling is needed
 **/
#define IMG_IS_FACE_SCALING_NEEDED(t) \
  (((t).h_scale > 1.0) || ((t).v_scale > 1.0) || \
   ((t).h_offset > 0) || ((t).v_offset > 0))

/** IMG_IS_FACE_FLIP_NEEDED
 *
 *  Macro to check if face flip is needed
 **/
#define IMG_IS_FACE_FLIP_NEEDED(t) (t != 0)

/** IMG_IS_FD_ENABLED
 *
 *  Macro to check if face detection is enabled
 **/
#define IMG_IS_FD_ENABLED(fd_mask) ((fd_mask & 1) || (fd_mask & 2))

/** IMG_IS_FACE_PRIORITY_SET
 *
 *  Macro to check if face priority is enabled
 **/
#define IMG_IS_FACE_PRIORITY_SET(fd_mask) (fd_mask & 2)

/** IMG_FD_TRANSLATE
 *
 *  Macro to scale face info w.r.t desired output size
 *
 *  HAL1 requires face info w.r.t reporting stream size
 *  HAL3 requires face info w.r.t active array size
 **/
#define IMG_FD_TRANSLATE(p_client, v, s, o) ({\
  int32_t ret = 0; \
  if (IMG_IS_HAL3(p_client)) \
    ret = (int32_t)IMG_TRANSLATE2(v, s, o); \
  else \
    ret = (int32_t)IMG_TRANSLATE(v, s, o); \
  ret; \
})

/** SWAP
 *
 *  Macro to swap 2 objects
 **/
#define SWAP(T,x,y){T *p = &(x), *q = &(y); \
  T z = *p; *p = *q; *q = z; }


/** IMG_FIND_FD_RESULT_INDEX
 *
 *  Macro to get fd result index from active index
 **/
#define IMG_FIND_FD_RESULT_INDEX(fd_active_index) \
 ((fd_active_index == 0) ? 1 : 0)

/** IMG_MAX_DOWNSCALE_RATIO
 *
 *  Max downscale ratio
 **/
#define IMG_MAX_DOWNSCALE_RATIO (20.0)

/** faceproc_defs_t
 *   @reporting_stream: Reporting stream type.
 *   @processing_stream: Processing stream type.
 *   @internal_buffers: Internal buffers need to be used.
 *
 *   Structure used to describe faceproc client
 *   stream selections.
 **/
typedef struct {
  cam_stream_type_t reporting_stream;
  cam_stream_type_t processing_stream;
  boolean internal_buffers;
} faceproc_stream_select_t;


/** faceproc_defs_t
 *   @streams: Array containing matching streams.
 *   @normal: Normal light stream selection.
 *   @low_light: Low light stream selection.
 *
 *   Structure used to describe faceproc client
 *   definitions.
 **/
typedef struct {
  cam_stream_type_t streams[CAM_STREAM_TYPE_MAX];
  faceproc_stream_select_t normal;
  faceproc_stream_select_t low_light;
} faceproc_defs_t;

/** faceproc_defs
 *
 *  Faceproc client definitions. Normal and low light Processing/Reporting
 *  streams are chosen based on present streams
 *  described in streams Arrays.
 **/
static const faceproc_defs_t faceproc_defs[] = {
  {
    .streams = {CAM_STREAM_TYPE_PREVIEW, CAM_STREAM_TYPE_MAX},
    .normal = {
       .reporting_stream = CAM_STREAM_TYPE_PREVIEW,
       .processing_stream = CAM_STREAM_TYPE_PREVIEW,
       .internal_buffers = TRUE,
    },
    .low_light = {
       .reporting_stream = CAM_STREAM_TYPE_PREVIEW,
       .processing_stream = CAM_STREAM_TYPE_PREVIEW,
       .internal_buffers = TRUE,
    },
  },
  {
    .streams = {CAM_STREAM_TYPE_ANALYSIS, CAM_STREAM_TYPE_MAX},
    .normal = {
       .reporting_stream = CAM_STREAM_TYPE_ANALYSIS,
       .processing_stream = CAM_STREAM_TYPE_ANALYSIS,
       .internal_buffers = FALSE,
    },
    .low_light = {
       .reporting_stream = CAM_STREAM_TYPE_ANALYSIS,
       .processing_stream = CAM_STREAM_TYPE_ANALYSIS,
       .internal_buffers = FALSE,
    },
  },
  {
    .streams = {CAM_STREAM_TYPE_PREVIEW, CAM_STREAM_TYPE_VIDEO,
      CAM_STREAM_TYPE_MAX},
    .normal = {
       .reporting_stream = CAM_STREAM_TYPE_MAX,
       .processing_stream = CAM_STREAM_TYPE_MAX,
       .internal_buffers = FALSE,
    },
    .low_light = {
       .reporting_stream = CAM_STREAM_TYPE_MAX,
       .processing_stream = CAM_STREAM_TYPE_MAX,
       .internal_buffers = FALSE,
    },
  },
  {
    .streams = {CAM_STREAM_TYPE_ANALYSIS, CAM_STREAM_TYPE_PREVIEW,
      CAM_STREAM_TYPE_MAX},
    .normal = {
       .reporting_stream = CAM_STREAM_TYPE_PREVIEW,
       .processing_stream = CAM_STREAM_TYPE_ANALYSIS,
       .internal_buffers = FALSE,
    },
    .low_light = {
       .reporting_stream = CAM_STREAM_TYPE_PREVIEW,
       .processing_stream = CAM_STREAM_TYPE_PREVIEW,
       .internal_buffers = TRUE,
    },
  },
  {
    .streams = {CAM_STREAM_TYPE_ANALYSIS, CAM_STREAM_TYPE_PREVIEW,
      CAM_STREAM_TYPE_VIDEO, CAM_STREAM_TYPE_MAX},
    .normal = {
      .reporting_stream = CAM_STREAM_TYPE_PREVIEW,
      .processing_stream = CAM_STREAM_TYPE_ANALYSIS,
      .internal_buffers = FALSE,
    },
    .low_light = {
      .reporting_stream = CAM_STREAM_TYPE_PREVIEW,
      .processing_stream = CAM_STREAM_TYPE_ANALYSIS,
      .internal_buffers = FALSE,
    },
  },
};

/** g_sw_detection_chromatix:
 *
 *  Chromatix for SW Face detection
 **/
static fd_chromatix_t g_sw_detection_chromatix = {
  #include "fd_chromatix_detect_qc.h"
};

/** g_detection_lite_chromatix:
 *
 *  Chromatix for Light SW Face detection
 **/
static fd_chromatix_t g_sw_detection_lite_chromatix = {
  #include "fd_chromatix_detect_lite_qc.h"
};

/** g_sw_detection_video_chromatix:
 *
 *  Chromatix for Video SW Face detection
 **/
static fd_chromatix_t g_sw_detection_video_chromatix = {
  #include "fd_chromatix_detect_video_qc.h"
};

/** g_sw_detection_video_lite_chromatix:
 *
 *  Chromatix for Video Lite SW Face detection
 **/
static fd_chromatix_t g_sw_detection_video_lite_chromatix = {
  #include "fd_chromatix_detect_video_lite_qc.h"
};

/** g_sw_detection_multi_client_chromatix:
 *
 *  Chromatix for multi client SW Face detection
 **/
static fd_chromatix_t g_sw_detection_multi_chromatix = {
  #include "fd_chromatix_detect_multi_client_qc.h"
};

/** g_sw_detection_multi_lite_chromatix:
 *
 *  Chromatix for multi client Lite SW Face detection
 **/
static fd_chromatix_t g_sw_detection_multi_lite_chromatix = {
  #include "fd_chromatix_detect_multi_client_lite_qc.h"
};

/** g_detection_chromatix:
 *
 *  Chromatix for HW Face detection
 **/
static fd_chromatix_t g_hw_detection_chromatix = {
  #include "fd_hw_chromatix_detect_qc.h"
};

/** g_detection_lite_chromatix:
 *
 *  Chromatix for Light HW Face detection
 **/
static fd_chromatix_t g_hw_detection_lite_chromatix = {
  #include "fd_hw_chromatix_detect_lite_qc.h"
};

/** g_hw_detection_video_chromatix:
 *
 *  Chromatix for video HW Face detection
 **/
static fd_chromatix_t g_hw_detection_video_chromatix = {
  #include "fd_hw_chromatix_detect_video_qc.h"
};

/** g_hw_detection_video_lite_chromatix:
 *
 *  Chromatix for light video HW Face detection
 **/
static fd_chromatix_t g_hw_detection_video_lite_chromatix = {
  #include "fd_hw_chromatix_detect_video_lite_qc.h"
};

/** g_hw_detection_multi_chromatix:
 *
 *  Chromatix for multi client HW Face detection
 **/
static fd_chromatix_t g_hw_detection_multi_chromatix = {
  #include "fd_hw_chromatix_detect_multi_client_qc.h"
};

/** g_hw_detection_multi_lite_chromatix:
 *
 *  Chromatix for light multi client HW Face detection
 **/
static fd_chromatix_t g_hw_detection_multi_lite_chromatix = {
  #include "fd_hw_chromatix_detect_multi_client_lite_qc.h"
};

/** g_recognition_chromatix:
 *
 *  Chromatix for Face recognition
 **/
static fd_chromatix_t g_recognition_chromatix = {
  #include "fd_chromatix_recog_qc.h"
};

/** g_bsgc_chromatix:
 *
 *  Chromatix for Blink, Smile, Gaze detection
 **/
static fd_chromatix_t g_bsgc_chromatix = {
  #include "fd_chromatix_bsgc.h"
};

/** g_hw_bsgc_chromatix:
 *
 *  HW FD Chromatix for Blink, Smile, Gaze detection
 **/
static fd_chromatix_t g_hw_bsgc_chromatix = {
  #include "fd_hw_chromatix_bsgc.h"
};

/** cam_face_ct_point_eye_t:
 *
 *  Convert lookup from cam_types to faceproc internal interface
 **/
static const int convert_ct_eye[][2] = {
  {CAM_FACE_CT_POINT_EYE_L_PUPIL, FACE_CONTOUR_POINT_EYE_L_PUPIL},
  {CAM_FACE_CT_POINT_EYE_L_IN, FACE_CONTOUR_POINT_EYE_L_IN},
  {CAM_FACE_CT_POINT_EYE_L_OUT, FACE_CONTOUR_POINT_EYE_L_OUT},
  {CAM_FACE_CT_POINT_EYE_L_UP, FACE_CONTOUR_POINT_EYE_L_UP},
  {CAM_FACE_CT_POINT_EYE_L_DOWN, FACE_CONTOUR_POINT_EYE_L_DOWN},
  {CAM_FACE_CT_POINT_EYE_R_PUPIL, FACE_CONTOUR_POINT_EYE_R_PUPIL},
  {CAM_FACE_CT_POINT_EYE_R_IN, FACE_CONTOUR_POINT_EYE_R_IN},
  {CAM_FACE_CT_POINT_EYE_R_OUT, FACE_CONTOUR_POINT_EYE_R_OUT},
  {CAM_FACE_CT_POINT_EYE_R_UP, FACE_CONTOUR_POINT_EYE_R_UP},
  {CAM_FACE_CT_POINT_EYE_R_DOWN, FACE_CONTOUR_POINT_EYE_R_DOWN},
};

/** cam_face_ct_point_forh_t:
 *
 *  Convert lookup from cam_types to faceproc internal interface
 **/
static const int convert_ct_forh[][2] = {
  {CAM_FACE_CT_POINT_FOREHEAD, FACE_CONTOUR_POINT_FOREHEAD},
};

/** cam_face_ct_point_nose_t:
 *
 *  Convert lookup from cam_types to faceproc internal interface
 **/
static const int convert_ct_nose[][2] = {
  {CAM_FACE_CT_POINT_NOSE, FACE_CONTOUR_POINT_NOSE},
  {CAM_FACE_CT_POINT_NOSE_TIP, FACE_CONTOUR_POINT_NOSE_TIP},
  {CAM_FACE_CT_POINT_NOSE_L, FACE_CONTOUR_POINT_NOSE_L},
  {CAM_FACE_CT_POINT_NOSE_R, FACE_CONTOUR_POINT_NOSE_R},
  {CAM_FACE_CT_POINT_NOSE_L_0, FACE_CONTOUR_POINT_NOSE_L_0},
  {CAM_FACE_CT_POINT_NOSE_R_0, FACE_CONTOUR_POINT_NOSE_R_0},
  {CAM_FACE_CT_POINT_NOSE_L_1, FACE_CONTOUR_POINT_NOSE_L_1},
  {CAM_FACE_CT_POINT_NOSE_R_1, FACE_CONTOUR_POINT_NOSE_R_1},
};

/** cam_face_ct_point_mouth_t:
 *
 *  Convert lookup from cam_types to faceproc internal interface
 **/
static const int convert_ct_mouth[][2] = {
  {CAM_FACE_CT_POINT_MOUTH_L, FACE_CONTOUR_POINT_MOUTH_L},
  {CAM_FACE_CT_POINT_MOUTH_R, FACE_CONTOUR_POINT_MOUTH_R},
  {CAM_FACE_CT_POINT_MOUTH_UP, FACE_CONTOUR_POINT_MOUTH_UP},
  {CAM_FACE_CT_POINT_MOUTH_DOWN, FACE_CONTOUR_POINT_MOUTH_DOWN},
};

/** cam_face_ct_point_lip_t:
 *
 *  Convert lookup from cam_types to faceproc internal interface
 **/
static const int convert_ct_lip[][2] = {
  {CAM_FACE_CT_POINT_LIP_UP, FACE_CONTOUR_POINT_LIP_UP},
  {CAM_FACE_CT_POINT_LIP_DOWN, FACE_CONTOUR_POINT_LIP_DOWN},
};

/** cam_face_ct_point_brow_t:
 *
 *  Convert lookup from cam_types to faceproc internal interface
 **/
static const int convert_ct_brow[][2] = {
  {CAM_FACE_CT_POINT_BROW_L_UP, FACE_CONTOUR_POINT_BROW_L_UP},
  {CAM_FACE_CT_POINT_BROW_L_DOWN, FACE_CONTOUR_POINT_BROW_L_DOWN},
  {CAM_FACE_CT_POINT_BROW_L_IN, FACE_CONTOUR_POINT_BROW_L_IN},
  {CAM_FACE_CT_POINT_BROW_L_OUT,FACE_CONTOUR_POINT_BROW_L_OUT},
  {CAM_FACE_CT_POINT_BROW_R_UP,FACE_CONTOUR_POINT_BROW_R_UP},
  {CAM_FACE_CT_POINT_BROW_R_DOWN, FACE_CONTOUR_POINT_BROW_R_DOWN},
  {CAM_FACE_CT_POINT_BROW_R_IN, FACE_CONTOUR_POINT_BROW_R_IN},
  {CAM_FACE_CT_POINT_BROW_R_OUT, FACE_CONTOUR_POINT_BROW_R_OUT},
};

/** cam_face_ct_point_chin_t:
 *
 *  Convert lookup from cam_types to faceproc internal interface
 **/
static const int convert_ct_chin[][2] = {
  {CAM_FACE_CT_POINT_CHIN, FACE_CONTOUR_POINT_CHIN},
  {CAM_FACE_CT_POINT_CHIN_L, FACE_CONTOUR_POINT_CHIN_L},
  {CAM_FACE_CT_POINT_CHIN_R, FACE_CONTOUR_POINT_CHIN_R},
};

/** cam_face_ct_point_ear_t:
 *
 *  Convert lookup from cam_types to faceproc internal interface
 **/
static const int convert_ct_ear[][2] = {
  {CAM_FACE_CT_POINT_EAR_L_DOWN, FACE_CONTOUR_POINT_EAR_L_DOWN},
  {CAM_FACE_CT_POINT_EAR_R_DOWN, FACE_CONTOUR_POINT_EAR_R_DOWN},
  {CAM_FACE_CT_POINT_EAR_L_UP, FACE_CONTOUR_POINT_EAR_L_UP},
  {CAM_FACE_CT_POINT_EAR_R_UP, FACE_CONTOUR_POINT_EAR_R_UP},
};

/**
 * Function: module_faceproc_client_reset_face_info
 *
 * Description: resets the parts of combined face detection data
 *  based on face info mask
 *
 * Arguments:
 *   @p_client: faceproc client.
 *   @p_face_data: face data to be reset
 *
 * Return values:
 *    none
 *
 * Notes: none
 **/
void module_faceproc_client_reset_face_info(faceproc_client_t *p_client,
 combined_cam_face_detection_data_t *p_face_data);

/**
 * Function: faceproc_reset_client_result
 *
 * Description: Inline function used to reset the client ids
 *
 * Arguments:
 *   @p_client: faceproc client.
 *
 * Return values:
 *    none
 *
 * Notes: none
 **/
static inline void faceproc_reset_client_result(faceproc_client_t *p_client)
{
  int32_t i;
  img_rb_reset(&p_client->result_rb, (uint8_t *)p_client->result,
    sizeof(p_client->result[0]), sizeof(p_client->result));
  for (i = 0; i < MAX_FACE_STATS; i++)
    p_client->result[i].client_id = -1;
} /*faceproc_reset_client_result*/

/**
 * Function: module_faceproc_client_get_current_comp
 *
 * Description: Gets the current Component handle
 *
 * Arguments:
 *   @p_client: faceproc client.
 *
 * Return values:
 *    Pointer to current component.
 *
 * Notes: none
 **/
img_component_ops_t * module_faceproc_client_get_current_comp(
  faceproc_client_t *p_client)
{
  img_component_ops_t *p_comp;
  boolean switch_comp = FALSE;

  if (p_client->engine >= FACEPROC_ENGINE_MAX) {
    IDBG_ERROR("%s:%d] Invalid engine %d", __func__, __LINE__,
      p_client->engine);
    p_client->engine = FACEPROC_ENGINE_SW;
    return &p_client->comps[p_client->engine];
  }

  if (p_client->new_engine >= FACEPROC_ENGINE_MAX) {
    IDBG_ERROR("%s:%d] Invalid new_engine %d", __func__, __LINE__,
      p_client->new_engine);
    p_client->new_engine = p_client->engine;
    return &p_client->comps[p_client->engine];
  }
  p_comp = &p_client->comps[p_client->engine];

  if (p_client->num_comp <= 1) {
    IDBG_LOW("%s:%d] cannot switch", __func__, __LINE__);
    return &p_client->comps[p_client->engine];
  }
  // check whether the connection is valid or not.
  // p_client->curr_comp_connection_lost is updated in Component's
  // thread context. So we need a mutex here.
  pthread_mutex_lock(&p_client->mutex);

  if (p_client->curr_comp_connection_lost == TRUE) {
    switch_comp = TRUE;
  }

  pthread_mutex_unlock(&p_client->mutex);

  // If Primary component connection was lost switch to Secondary component
  if (switch_comp == TRUE) {
    int rc = IMG_SUCCESS;

    // First try to ABORT the current component.
    rc = IMG_COMP_ABORT(&p_client->comps[p_client->engine], NULL);
    if (IMG_ERROR(rc)) {
      // This is expected to fail as the components connection
      // is already lost.
      IDBG_MED("%s:%d] Abort of Primary component failed %d",
        __func__, __LINE__, rc);
    }

    IDBG_INFO("%s:%d] - using new component",
      __func__, __LINE__);

    // Prepare the secondary component to use it from now onwards.
    img_component_ops_t *p_comp2 = &p_client->comps[p_client->new_engine];

    rc = IMG_COMP_SET_PARAM(p_comp2, QWD_FACEPROC_MODE,
      (void *)&p_client->mode);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] SetParam(QWD_FACEPROC_MODE) failed rc=%d",
        __func__, __LINE__, rc);
    }

    rc = IMG_COMP_SET_PARAM(p_comp2, QWD_FACEPROC_CFG,
      (void *)&p_client->config);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] SetParam(QWD_FACEPROC_CFG) failed rc=%d",
        __func__, __LINE__, rc);
    }

    rc = IMG_COMP_SET_PARAM(p_comp2, QWD_FACEPROC_CHROMATIX,
      (void *)p_client->p_fd_chromatix);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] SetParam(QWD_FACEPROC_CHROMATIX) failed rc=%d",
        __func__, __LINE__, rc);
    }

    rc = IMG_COMP_SET_PARAM(p_comp2, QWD_FACEPROC_DUMP_DATA,
      &p_client->dump_mode);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] SetParam(QWD_FACEPROC_DUMP_DATA) failed rc=%d",
        __func__, __LINE__, rc);
    }

    rc = IMG_COMP_SET_PARAM(p_comp, QWD_FACEPROC_BATCH_MODE_DATA,
      &p_client->frame_batch_info);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    }

    rc = IMG_COMP_START(p_comp2, NULL);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Secondary Component START failed rc=%d",
        __func__, __LINE__, rc);
    }
    p_client->engine = p_client->new_engine;
  }

  return &p_client->comps[p_client->engine];
}

/**
 * Function: faceproc_get_processing_stream_idx
 *
 * Description: Function used to obtain processing stream index.
 *
 * Arguments:
 *   @p_client: faceproc client.
 *
 * Return values:
 *    processing stream index.
 *
 * Notes: none
 **/
static int faceproc_get_processing_stream_idx(faceproc_client_t *p_client)
{
  uint32_t i = 0;
  int s_idx = 0;
  for (i = 0; i < p_client->ref_count; i++) {
    if (p_client->fp_stream[i].processing_stream) {
      s_idx = i;
      break;
    }
  }
  return s_idx;
}

/**
 * Function: faceproc_get_reporting_stream_idx
 *
 * Description: Function used to obtain reporting stream index.
 *
 * Arguments:
 *   @p_client: faceproc client.
 *
 * Return values:
 *    Reporting stream index.
 *
 * Notes: none
 **/
static int faceproc_get_reporting_stream_idx(faceproc_client_t *p_client)
{
  uint32_t i = 0;
  int s_idx = 0;
  for (i = 0; i < p_client->ref_count; i++) {
    if (p_client->fp_stream[i].reporting_stream) {
      s_idx = i;
      break;
    }
  }
  return s_idx;
}

/**
 * Function: module_faceproc_client_send_ack_event
 *
 * Description: This function is for sending the buffer ack
 * event
 *
 * Arguments:
 *   @p_client: faceproc client.
 *   @p_divert: buffer divert message.
 *
 * Return values:
 *     none
 *
 * Notes: none
 **/
void module_faceproc_client_send_ack_event(faceproc_client_t *p_client,
  mod_img_msg_buf_divert_t *p_divert)
{
  isp_buf_divert_ack_t buff_divert_ack;
  mct_event_t mct_event;

  if (!p_client || !p_divert)
    return;

  memset(&mct_event, 0x00, sizeof(mct_event));
  mct_event.u.module_event.type = MCT_EVENT_MODULE_BUF_DIVERT_ACK;
  mct_event.u.module_event.module_event_data = (void *)&buff_divert_ack;
  mct_event.type = MCT_EVENT_MODULE_EVENT;
  mct_event.identity = p_divert->identity;
  mct_event.direction = MCT_EVENT_UPSTREAM;
  memset(&buff_divert_ack,  0,  sizeof(buff_divert_ack));
  buff_divert_ack.meta_data = p_divert->buf_divert.meta_data;
  buff_divert_ack.channel_id = p_divert->buf_divert.channel_id;
  buff_divert_ack.buf_idx = p_divert->buf_divert.buffer.index;
  buff_divert_ack.frame_id = p_divert->buf_divert.buffer.sequence;
  buff_divert_ack.is_buf_dirty = TRUE;
  buff_divert_ack.identity = p_divert->identity;

  mct_port_send_event_to_peer(p_divert->port, &mct_event);
}

/**
 * Function: module_faceproc_client_req_divert
 *
 * Description: Function used to request buffer divert on given stream.
 *
 * Arguments:
 *   @p_stream: faceproc client.
 *   @divert_req: buffer divert request flag, if set buffer divert is needed.
 *
 * Return values:
 *     Imglib error codes.
 *
 * Notes: none
 **/
int module_faceproc_client_req_divert(faceproc_stream_t *p_stream,
  uint32_t divert_req)
{
  mct_event_t mct_event;
  boolean ret;

  if (!p_stream) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  if (p_stream->buff_divert_requested == divert_req) {
    IDBG_MED("%s:%d] Same flag skip", __func__, __LINE__);
    return IMG_SUCCESS;
  }

  /* For now limit this functionality only for preview stream */
  if (p_stream->stream_info->stream_type != CAM_STREAM_TYPE_PREVIEW) {
    IDBG_MED("%s:%d] Request stream type is supported only for preview stream",
      __func__, __LINE__);
    return IMG_SUCCESS;
  }

  memset(&mct_event, 0x00, sizeof(mct_event));
  mct_event.type = MCT_EVENT_MODULE_EVENT;
  mct_event.identity = p_stream->identity;
  mct_event.direction = MCT_EVENT_UPSTREAM;
  mct_event.u.module_event.type = MCT_EVENT_MODULE_REQ_DIVERT;
  mct_event.u.module_event.module_event_data = (void *)&divert_req;

  ret = mct_port_send_event_to_peer(p_stream->p_sink_port, &mct_event);
  if (ret == FALSE) {
    IDBG_ERROR("%s:%d] Fail to request buffer divert", __func__, __LINE__);
    return IMG_ERR_GENERAL;
  }
  p_stream->buff_divert_requested = divert_req;

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_get_buf
 *
 * Description: get the buffer for face detection
 *
 * Arguments:
 *   @p_client - Faceproc client pointer
 *
 * Return values:
 *   buffer index
 *
 * Notes: none
 **/
inline int module_faceproc_client_get_buf(faceproc_client_t *p_client)
{
  int32_t idx = p_client->buf_idx;
  p_client->buf_idx = (p_client->buf_idx + 1) % MAX_NUM_FD_FRAMES;
  return idx;
}

/**
 * Function: module_faceproc_client_get_offline_buf
 *
 * Description: get the offline buffer for face detection
 *
 * Arguments:
 *   @p_client - Faceproc client pointer
 *
 * Return values:
 *   buffer index
 *
 * Notes: none
 **/
inline int32_t module_faceproc_client_get_offline_buf(
  faceproc_client_t *p_client)
{
  int32_t idx = p_client->buf_idx_offline;
  p_client->buf_idx_offline++;
  return idx;
}

/**
 * Function: module_faceproc_client_scaling_need.
 *
 * Description: Check if scaling is needed based on input
 *   buffer size and component configuration size.
 *
 * Arguments:
 *   @f_conf - Pointer to faceproc_frame_cfg_t.
 *   @p_fp_stream - Pointer to facproc stream holder.
 *
 * Return values:
 *   0 - if scaling is not needed, 1 - Scaling is needed.
 *
 **/
static int module_faceproc_client_scaling_need(faceproc_frame_cfg_t *f_conf,
  faceproc_stream_t *p_fp_stream)
{
  uint32_t in_width = p_fp_stream->input_dim.width;
  uint32_t in_height = p_fp_stream->input_dim.height;
  int scale_needed = 0;

  if (in_width > f_conf->max_width || in_height > f_conf->max_height) {
    scale_needed  = 1;
  }
  IDBG_MED("%s:%d] Scaling needed %s", __func__, __LINE__,
    scale_needed ? "yes" : "no");

  return scale_needed;
}

/**
 * Function: module_faceproc_client_stride_corr_need.
 *
 * Description: Check if stride correction is needed based
 *  on minimum stride required.
 *
 * Arguments:
 *   @f_conf - Pointer to faceproc_frame_cfg_t.
 *   @stride - Stride need to be corrected.
 *
 * Return values:
 *   0 - Stride correction is not needed, 1 - Scaling is needed.
 *
 **/
static int module_faceproc_client_stride_corr_need(faceproc_frame_cfg_t *f_cfg,
 uint32_t stride)
{
  int correction_needed = 0;

  if (stride < f_cfg->min_stride) {
    correction_needed  = 1;
  }

  IDBG_MED("%s:%d] Stride correction needed %s", __func__, __LINE__,
    correction_needed ? "yes" : "no");

  return correction_needed;
}

/**
 * Function: module_faceproc_client_format_conv_need.
 *
 * Description: Check if format conversion is needed.
 *
 * Arguments:
 *   @p_stream - Pointer to faceproc stream holder.
 *
 * Return values:
 *   0 - if scaling is not needed, 1 - Scaling is needed.
 *   negative if format is not supported.
 *
 **/
static int module_faceproc_client_format_conv_need(faceproc_stream_t *p_stream)
{
  int conv_needed = 0;

  switch (p_stream->input_fmt) {
  case CAM_FORMAT_YUV_420_NV12:
  case CAM_FORMAT_YUV_420_NV21:
  case CAM_FORMAT_YUV_420_NV21_ADRENO:
  case CAM_FORMAT_YUV_420_YV12:
  case CAM_FORMAT_YUV_422_NV16:
  case CAM_FORMAT_YUV_422_NV61:
  case CAM_FORMAT_YUV_420_NV12_VENUS:
  case CAM_FORMAT_Y_ONLY:
  case CAM_FORMAT_YUV_420_NV21_VENUS:
  /*
   * Adding UBWC format in FD module. FD is not concerned with UBWC
   * and input format should be used. So ignoring color conversion
   */
  case CAM_FORMAT_YUV_420_NV12_UBWC:
    conv_needed  = 0;
    break;
  case CAM_FORMAT_YUV_RAW_8BIT_YUYV:
  case CAM_FORMAT_YUV_RAW_8BIT_YVYU:
  case CAM_FORMAT_YUV_RAW_8BIT_UYVY:
  case CAM_FORMAT_YUV_RAW_8BIT_VYUY:
    conv_needed  = 1;
    break;
  default:
    IDBG_ERROR("%s:%d] Format not supported", __func__, __LINE__);
    return -1;
  }

  IDBG_MED("%s:%d] Format conv needed %s", __func__, __LINE__,
    conv_needed ? "yes" : "no");

  return conv_needed;
}

/**
 * Function: module_faceproc_client_use_int_bufs.
 *
 * Description: Check if internal buffers are needed.
 *
 * Arguments:
 *   @p_client: faceproc client.
 *   @p_stream - Pointer to faceproc stream holder.
 *
 * Return values:
 *   0 - if scaling is not needed, 1 - Scaling is needed.
 *   negative if format is not supported.
 *
 **/
static int module_faceproc_client_use_int_bufs(faceproc_client_t *p_client,
  faceproc_stream_t *p_stream)
{
  int use_int_bufs = p_client->alloc_int_bufs;
  uint32_t comp_needs_int_buffs = FALSE;
  int32_t rc = IMG_SUCCESS;
  /* If processing stream is not analysis always use internal copy */
  if (p_stream->stream_info->stream_type != CAM_STREAM_TYPE_ANALYSIS) {
  IDBG_MED("%s:%d] Using internal buffers ",__func__, __LINE__);
    use_int_bufs = 1;
  }

  /* check with comp if it needs internal buffers*/
  /* Request from component buffer type */
  rc = IMG_COMP_GET_PARAM(module_faceproc_client_get_current_comp(p_client),
    QWD_FACEPROC_USE_INT_BUFF, (void *)&comp_needs_int_buffs);
  if (IMG_ERROR(rc)) {
    IDBG_MED("%s:%d] rc %d", __func__, __LINE__, rc);
    comp_needs_int_buffs = FALSE;
  }
  /* override if comp wants to use internal buffers */
  if (comp_needs_int_buffs == TRUE) {
    use_int_bufs = 1;
    IDBG_MED("%s:%d] Using internal buffers for component",
      __func__, __LINE__);
  }

  IDBG_HIGH("%s:%d] Use internal buffers %s", __func__, __LINE__,
    use_int_bufs ? "yes" : "no");

  return use_int_bufs;
}

/**
 * Function: module_faceproc_client_get_bufsize.
 *
 * Description: Calculate internal buffer size.
 *
 * Arguments:
 *   @f_conf - Pointer to faceproc_frame_cfg_t.
 *   @p_fp_stream - Pointer to facproc stream holder.
 *
 * Return values:
 *  Internal buffer size in bytes.
 *
 **/
static uint32_t module_faceproc_client_get_bufsize(faceproc_frame_cfg_t *f_cfg,
  faceproc_stream_t *p_fp_stream)
{
  uint32_t stride, scanline;
  uint32_t chroma_downsample;
  uint32_t lenght;

  switch (p_fp_stream->input_fmt) {
  case CAM_FORMAT_YUV_420_NV12:
  case CAM_FORMAT_YUV_420_NV21:
  case CAM_FORMAT_YUV_420_NV21_ADRENO:
  case CAM_FORMAT_YUV_420_NV12_VENUS:
  case CAM_FORMAT_YUV_420_NV21_VENUS:
  case CAM_FORMAT_YUV_420_YV12:
  case CAM_FORMAT_YUV_422_NV16:
  case CAM_FORMAT_YUV_422_NV61:
  case CAM_FORMAT_Y_ONLY:
    /* We are using only luma */
    chroma_downsample = 0;
    break;
  case CAM_FORMAT_YUV_RAW_8BIT_YUYV:
  case CAM_FORMAT_YUV_RAW_8BIT_YVYU:
  case CAM_FORMAT_YUV_RAW_8BIT_UYVY:
  case CAM_FORMAT_YUV_RAW_8BIT_VYUY:
    /* We will deinterleave chroma plane */
    chroma_downsample  = 1;
    break;
  default:
    IDBG_ERROR("%s:%d] Format not supported", __func__, __LINE__);
    return 0;
  }

  if (p_fp_stream->input_dim.stride < f_cfg->min_stride) {
    stride = f_cfg->min_stride;
  } else {
    stride = p_fp_stream->input_dim.stride;
  }

  if (p_fp_stream->input_dim.scanline < f_cfg->min_scanline) {
    scanline = f_cfg->min_scanline;
  } else {
    scanline = p_fp_stream->input_dim.scanline;
  }

  lenght = stride * scanline;
  if (chroma_downsample) {
    lenght += (lenght / chroma_downsample);
  }

  return lenght;
}

 /**
 * Function: module_faceproc_client_update_face_info
 *
 * Description: Update face information from face detection result
 *
 * Arguments:
 *   @f_d - Face detection data to be filled
 *   @ix  - index inside f_d at which data to be filled
 *   @roi - Face proc information output of the algorithm
 *   @p_client - faceproc client
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
int module_faceproc_client_update_face_info(
  combined_cam_face_detection_data_t *f_d,
  uint8_t ix,
  faceproc_info_t *roi,
  faceproc_client_t *p_client)
{
  uint32_t i = 0;

  if (!f_d || !roi) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  f_d->fd_data.faces[ix].face_id = abs(roi->unique_id);
  f_d->fd_data.faces[ix].score = (int8_t)(roi->fd_confidence/10);

  f_d->fd_data.faces[ix].face_boundary.top = (int32_t)roi->face_boundary.y;
  f_d->fd_data.faces[ix].face_boundary.left = (int32_t)roi->face_boundary.x;
  f_d->fd_data.faces[ix].face_boundary.width = (int32_t)roi->face_boundary.dx;
  f_d->fd_data.faces[ix].face_boundary.height = (int32_t)roi->face_boundary.dy;
#if(FACE_PART_DETECT)

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_SMILE) {
    f_d->smile_data.smile[ix].smile_degree = (uint8_t)roi->sm.smile_degree;
    f_d->smile_data.smile[ix].smile_confidence = (uint8_t)roi->sm.confidence/10;
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_RECOG) {
    f_d->fr_data.face_rec[ix].face_recognised =
      (uint8_t)roi->is_face_recognised;
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_GAZE) {
    f_d->gaze_data.gaze[ix].gaze_angle = (int8_t)roi->gaze_angle;
    f_d->gaze_data.gaze[ix].left_right_gaze = (int8_t)roi->left_right_gaze;
    f_d->gaze_data.gaze[ix].top_bottom_gaze = (int8_t)roi->top_bottom_gaze;
    f_d->gaze_data.gaze[ix].leftright_dir = roi->fp.direction_left_right;
    f_d->gaze_data.gaze[ix].updown_dir = roi->fp.direction_up_down;
    f_d->gaze_data.gaze[ix].roll_dir = roi->fp.direction_roll;
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_BLINK) {
    f_d->blink_data.blink[ix].blink_detected = (uint8_t)roi->blink_detected;
    f_d->blink_data.blink[ix].left_blink = (uint8_t)(roi->left_blink/10);
    f_d->blink_data.blink[ix].right_blink = (uint8_t)(roi->right_blink/10);
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_FACE_LANDMARKS) {
    IDBG_LOW("%s:%d] LandMarks fd enabled",__func__, __LINE__);
    f_d->fp_data.face_landmarks[ix].left_eye_center.x =
      (uint32_t)roi->fp.face_pt[FACE_PART_LEFT_EYE].x;
    f_d->fp_data.face_landmarks[ix].left_eye_center.y =
      (uint32_t)roi->fp.face_pt[FACE_PART_LEFT_EYE].y;
    f_d->fp_data.face_landmarks[ix].right_eye_center.x =
      (uint32_t)roi->fp.face_pt[FACE_PART_RIGHT_EYE].x;
    f_d->fp_data.face_landmarks[ix].right_eye_center.y =
      (uint32_t)roi->fp.face_pt[FACE_PART_RIGHT_EYE].y;
    f_d->fp_data.face_landmarks[ix].mouth_center.x =
      (uint32_t)roi->fp.face_pt[FACE_PART_MOUTH].x;
    f_d->fp_data.face_landmarks[ix].mouth_center.y =
      (uint32_t)roi->fp.face_pt[FACE_PART_MOUTH].y;
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_CONTOUR) {
    f_d->cont_data.face_contour[ix].is_eye_valid = roi->ct.is_eye_valid;
    if (f_d->cont_data.face_contour[ix].is_eye_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(convert_ct_eye); i++) {
        f_d->cont_data.face_contour[ix].contour_eye_pt[convert_ct_eye[i][0]].x =
          roi->ct.eye_pt[convert_ct_eye[i][1]].x;
        f_d->cont_data.face_contour[ix].contour_eye_pt[convert_ct_eye[i][0]].y =
          roi->ct.eye_pt[convert_ct_eye[i][1]].y;
      }
    }

    f_d->cont_data.face_contour[ix].is_forehead_valid =
      roi->ct.is_forehead_valid;
    if (f_d->cont_data.face_contour[ix].is_forehead_valid) {
      for (i = 0; i <  IMGLIB_ARRAY_SIZE(convert_ct_forh); i++) {
        f_d->cont_data.face_contour[ix].
          contour_forh_pt[convert_ct_forh[i][0]].x =
          roi->ct.forh_pt[convert_ct_forh[i][1]].x;
        f_d->cont_data.face_contour[ix].
          contour_forh_pt[convert_ct_forh[i][0]].y =
          roi->ct.forh_pt[convert_ct_forh[i][1]].y;
      }
    }

    f_d->cont_data.face_contour[ix].is_nose_valid = roi->ct.is_nose_valid;
    if (f_d->cont_data.face_contour[ix].is_nose_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(convert_ct_nose); i++) {
        f_d->cont_data.face_contour[ix].
          contour_nose_pt[convert_ct_nose[i][0]].x =
          roi->ct.nose_pt[convert_ct_nose[i][1]].x;
        f_d->cont_data.face_contour[ix].
          contour_nose_pt[convert_ct_nose[i][0]].y =
          roi->ct.nose_pt[convert_ct_nose[i][1]].y;
      }
    }

    f_d->cont_data.face_contour[ix].is_mouth_valid = roi->ct.is_mouth_valid;
    if (f_d->cont_data.face_contour[ix].is_mouth_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(convert_ct_mouth); i++) {
        f_d->cont_data.face_contour[ix].
          contour_mouth_pt[convert_ct_mouth[i][0]].x =
          roi->ct.mouth_pt[convert_ct_mouth[i][1]].x;
        f_d->cont_data.face_contour[ix].
          contour_mouth_pt[convert_ct_mouth[i][0]].y =
          roi->ct.mouth_pt[convert_ct_mouth[i][1]].y;
      }
    }

    f_d->cont_data.face_contour[ix].is_lip_valid = roi->ct.is_lip_valid;
    if (f_d->cont_data.face_contour[ix].is_lip_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(convert_ct_lip); i++) {
        f_d->cont_data.face_contour[ix].
          contour_lip_pt[convert_ct_lip[i][0]].x =
          roi->ct.lip_pt[convert_ct_lip[i][1]].x;
        f_d->cont_data.face_contour[ix].
          contour_lip_pt[convert_ct_lip[i][0]].y =
          roi->ct.lip_pt[convert_ct_lip[i][1]].y;
      }
    }

    f_d->cont_data.face_contour[ix].is_brow_valid = roi->ct.is_brow_valid;
    if (f_d->cont_data.face_contour[ix].is_brow_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(convert_ct_brow); i++) {
        f_d->cont_data.face_contour[ix].
          contour_brow_pt[convert_ct_brow[i][0]].x =
          roi->ct.brow_pt[convert_ct_brow[i][1]].x;
        f_d->cont_data.face_contour[ix].
          contour_brow_pt[convert_ct_brow[i][0]].y =
          roi->ct.brow_pt[convert_ct_brow[i][1]].y;
      }
    }

    f_d->cont_data.face_contour[ix].is_chin_valid = roi->ct.is_chin_valid;
    if (f_d->cont_data.face_contour[ix].is_chin_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(convert_ct_chin); i++) {
        f_d->cont_data.face_contour[ix].
          contour_chin_pt[convert_ct_chin[i][0]].x =
          roi->ct.chin_pt[convert_ct_chin[i][1]].x;
        f_d->cont_data.face_contour[ix].
          contour_chin_pt[convert_ct_chin[i][0]].y =
          roi->ct.chin_pt[convert_ct_chin[i][1]].y;
      }
    }

    f_d->cont_data.face_contour[ix].is_ear_valid = roi->ct.is_ear_valid;
    if (f_d->cont_data.face_contour[ix].is_ear_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(convert_ct_ear); i++) {
        f_d->cont_data.face_contour[ix].contour_ear_pt[convert_ct_ear[i][0]].x =
          roi->ct.ear_pt[convert_ct_ear[i][1]].x;
        f_d->cont_data.face_contour[ix].contour_ear_pt[convert_ct_ear[i][0]].y =
          roi->ct.ear_pt[convert_ct_ear[i][1]].y;
      }
    }
  }
#endif

  return IMG_SUCCESS;
}

 /**
 * Function: module_faceproc_client_scale_face_info
 *
 * Description: Scale face information from face detection result
 *
 * Arguments:
 *   @p_client - faceproc client
 *   @f_d - Face detection data to be filled
 *   @ix  - index inside f_d at which data to be filled
 *   @trans_info - Transition info to be applied
 *   @inv_scale - Whether to invert scale factor
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
int module_faceproc_client_scale_face_info(faceproc_client_t *p_client,
  combined_cam_face_detection_data_t *f_d,
  uint8_t ix,
  img_trans_info_t *trans_info,
  uint8_t inv_scale)
{
  uint32_t i = 0;
  float h_scale, v_scale;

  if (!f_d || !trans_info) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  if (IMG_F_EQUAL(trans_info->h_scale, 0.0)
    || (IMG_F_EQUAL(trans_info->v_scale, 0.0))) {
    IDBG_ERROR("%s:%d] Invalid scale factors", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  if (inv_scale) {
    h_scale = 1.0f/trans_info->h_scale;
    v_scale = 1.0f/trans_info->v_scale;
  } else {
    h_scale = trans_info->h_scale;
    v_scale = trans_info->v_scale;
  }

  IDBG_MED("%s:%d] %f %f %d %d", __func__, __LINE__,
    h_scale,
    v_scale,
    trans_info->h_offset,
    trans_info->v_offset);

  IDBG_LOW("%s:%d] Before face info (%d %d %d %d)",
    __func__, __LINE__,
    f_d->fd_data.faces[ix].face_boundary.left,
    f_d->fd_data.faces[ix].face_boundary.top,
    f_d->fd_data.faces[ix].face_boundary.width,
    f_d->fd_data.faces[ix].face_boundary.height);

  f_d->fd_data.faces[ix].face_boundary.top =
    IMG_FD_TRANSLATE(p_client, (float)f_d->fd_data.faces[ix].face_boundary.top,
      v_scale,
      (float)trans_info->v_offset);

  f_d->fd_data.faces[ix].face_boundary.left =
    IMG_FD_TRANSLATE(p_client, (float)f_d->fd_data.faces[ix].face_boundary.left,
      h_scale,
      (float)trans_info->h_offset);

  f_d->fd_data.faces[ix].face_boundary.width =
    (int32_t)((float)f_d->fd_data.faces[ix].face_boundary.width * h_scale);

  f_d->fd_data.faces[ix].face_boundary.height =
    (int32_t)((float)f_d->fd_data.faces[ix].face_boundary.height * v_scale);

  IDBG_LOW("%s:%d] face info (%d %d %d %d)",
    __func__, __LINE__,
    f_d->fd_data.faces[ix].face_boundary.left,
    f_d->fd_data.faces[ix].face_boundary.top,
    f_d->fd_data.faces[ix].face_boundary.width,
    f_d->fd_data.faces[ix].face_boundary.height);

#if(FACE_PART_DETECT)
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_FACE_LANDMARKS) {
    f_d->fp_data.face_landmarks[ix].left_eye_center.x =
      IMG_FD_TRANSLATE(p_client,
        (float)f_d->fp_data.face_landmarks[ix].left_eye_center.x,
        h_scale,
        (float)trans_info->h_offset);

    f_d->fp_data.face_landmarks[ix].left_eye_center.y =
      IMG_FD_TRANSLATE(p_client,
        (float)f_d->fp_data.face_landmarks[ix].left_eye_center.y,
        v_scale,
        (float)trans_info->v_offset);

    f_d->fp_data.face_landmarks[ix].right_eye_center.x =
      IMG_FD_TRANSLATE(p_client,
        (float)f_d->fp_data.face_landmarks[ix].right_eye_center.x,
        h_scale,
        (float)trans_info->h_offset);

    f_d->fp_data.face_landmarks[ix].right_eye_center.y =
      IMG_FD_TRANSLATE(p_client,
        (float)f_d->fp_data.face_landmarks[ix].right_eye_center.y,
        v_scale,
        (float)trans_info->v_offset);

    f_d->fp_data.face_landmarks[ix].mouth_center.x =
      IMG_FD_TRANSLATE(p_client,
        (float)f_d->fp_data.face_landmarks[ix].mouth_center.x,
        h_scale,
        (float)trans_info->h_offset);

    f_d->fp_data.face_landmarks[ix].mouth_center.y =
      IMG_FD_TRANSLATE(p_client,
        (float)f_d->fp_data.face_landmarks[ix].mouth_center.y,
        v_scale,
        (float)trans_info->v_offset);
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_CONTOUR) {
    if (f_d->cont_data.face_contour[ix].is_eye_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_eye_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_eye_pt[i].x =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_eye_pt[i].x,
          h_scale, trans_info->h_offset);
        f_d->cont_data.face_contour[ix].contour_eye_pt[i].y =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_eye_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_forehead_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_forh_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_forh_pt[i].x =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_forh_pt[i].x,
          h_scale, trans_info->h_offset);
        f_d->cont_data.face_contour[ix].contour_forh_pt[i].y =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_forh_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_nose_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_nose_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_nose_pt[i].x =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_nose_pt[i].x,
          h_scale, trans_info->h_offset);
        f_d->cont_data.face_contour[ix].contour_nose_pt[i].y =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_nose_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_mouth_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_mouth_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_mouth_pt[i].x =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_mouth_pt[i].x,
          h_scale, trans_info->h_offset);
        f_d->cont_data.face_contour[ix].contour_mouth_pt[i].y =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_mouth_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_lip_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_lip_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_lip_pt[i].x =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_lip_pt[i].x,
          h_scale, trans_info->h_offset);
        f_d->cont_data.face_contour[ix].contour_lip_pt[i].y =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_lip_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_brow_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_brow_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_brow_pt[i].x =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_brow_pt[i].x,
          h_scale, trans_info->h_offset);
        f_d->cont_data.face_contour[ix].contour_brow_pt[i].y =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_brow_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_chin_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_chin_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_chin_pt[i].x =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_chin_pt[i].x,
          h_scale, trans_info->h_offset);
        f_d->cont_data.face_contour[ix].contour_chin_pt[i].y =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_chin_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_ear_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_ear_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_ear_pt[i].x =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_ear_pt[i].x,
          h_scale, trans_info->h_offset);
        f_d->cont_data.face_contour[ix].contour_ear_pt[i].y =
          IMG_FD_TRANSLATE(p_client,
          f_d->cont_data.face_contour[ix].contour_ear_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }
  }
#endif

  return IMG_SUCCESS;
}

 /**
  * Function: module_faceproc_client_flip_horizantal
  *
  * Description: flip face information horizantally
  *
  * Arguments:
  *   @f_d - Face detection data to be filled
  *   @ix  - index inside f_d at which data to be filled
  *   @p_fp_stream - faceproc stream info
  *   @p_client - faceproc client
  *
  * Return values:
  *   none
  *
  * Notes: none
  **/
void module_faceproc_client_flip_horizantal(
  combined_cam_face_detection_data_t *f_d,
  uint8_t ix,
  faceproc_stream_t *p_fp_stream,
  faceproc_client_t *p_client)
{
  uint32_t i = 0;

  f_d->fd_data.faces[ix].face_boundary.left =
    (uint32_t)IMG_OFFSET_FLIP(p_fp_stream->input_dim.width,
    f_d->fd_data.faces[ix].face_boundary.left,
    f_d->fd_data.faces[ix].face_boundary.width);

#if(FACE_PART_DETECT)
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_FACE_LANDMARKS) {
    f_d->fp_data.face_landmarks[ix].left_eye_center.x =
      (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
      f_d->fp_data.face_landmarks[ix].left_eye_center.x);

    f_d->fp_data.face_landmarks[ix].right_eye_center.x =
      (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
      f_d->fp_data.face_landmarks[ix].right_eye_center.x);

    f_d->fp_data.face_landmarks[ix].mouth_center.x =
      (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
      f_d->fp_data.face_landmarks[ix].mouth_center.x);
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_CONTOUR) {
    if (f_d->cont_data.face_contour[ix].is_eye_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_eye_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_eye_pt[i].x =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
          f_d->cont_data.face_contour[ix].contour_eye_pt[i].x);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_forehead_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_forh_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_forh_pt[i].x =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
          f_d->cont_data.face_contour[ix].contour_forh_pt[i].x);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_nose_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_nose_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_nose_pt[i].x =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
          f_d->cont_data.face_contour[ix].contour_nose_pt[i].x);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_mouth_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_mouth_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_mouth_pt[i].x =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
          f_d->cont_data.face_contour[ix].contour_mouth_pt[i].x);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_lip_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_lip_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_lip_pt[i].x =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
          f_d->cont_data.face_contour[ix].contour_lip_pt[i].x);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_brow_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_brow_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_brow_pt[i].x =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
          f_d->cont_data.face_contour[ix].contour_brow_pt[i].x);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_chin_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_chin_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_chin_pt[i].x =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
          f_d->cont_data.face_contour[ix].contour_chin_pt[i].x);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_ear_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_ear_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_ear_pt[i].x =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.width,
          f_d->cont_data.face_contour[ix].contour_ear_pt[i].x);
      }
    }
  }
#endif //FACE_PART_DETECT
}

 /**
  * Function: module_faceproc_client_flip_vertical
  *
  * Description: flip face information verticallly
  *
  * Arguments:
  *   @f_d - Face detection data to be filled
  *   @ix  - index inside f_d at which data to be filled
  *   @p_fp_stream - faceproc stream info
  *   @p_client - faceproc client
  *
  * Return values:
  *   none
  *
  * Notes: none
  **/
void module_faceproc_client_flip_vertical(
  combined_cam_face_detection_data_t *f_d,
  uint8_t ix,
  faceproc_stream_t *p_fp_stream,
  faceproc_client_t *p_client)
{
  uint32_t i = 0;

  f_d->fd_data.faces[ix].face_boundary.top =
    (uint32_t)IMG_OFFSET_FLIP(p_fp_stream->input_dim.height,
    f_d->fd_data.faces[ix].face_boundary.top,
    f_d->fd_data.faces[ix].face_boundary.height);

#if(FACE_PART_DETECT)
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_FACE_LANDMARKS) {
    f_d->fp_data.face_landmarks[ix].left_eye_center.y =
      (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
      f_d->fp_data.face_landmarks[ix].left_eye_center.y);

    f_d->fp_data.face_landmarks[ix].right_eye_center.y =
      (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
      f_d->fp_data.face_landmarks[ix].right_eye_center.y);

    f_d->fp_data.face_landmarks[ix].mouth_center.y =
      (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
      f_d->fp_data.face_landmarks[ix].mouth_center.y);
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_CONTOUR) {
    if (f_d->cont_data.face_contour[ix].is_eye_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_eye_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_eye_pt[i].y =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
          f_d->cont_data.face_contour[ix].contour_eye_pt[i].y);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_forehead_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_forh_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_forh_pt[i].y =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
          f_d->cont_data.face_contour[ix].contour_forh_pt[i].y);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_nose_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_nose_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_nose_pt[i].y =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
          f_d->cont_data.face_contour[ix].contour_nose_pt[i].y);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_mouth_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_mouth_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_mouth_pt[i].y =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
          f_d->cont_data.face_contour[ix].contour_mouth_pt[i].y);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_lip_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_lip_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_lip_pt[i].y =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
          f_d->cont_data.face_contour[ix].contour_lip_pt[i].y);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_brow_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_brow_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_brow_pt[i].y =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
          f_d->cont_data.face_contour[ix].contour_brow_pt[i].y);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_chin_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_chin_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_chin_pt[i].y =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
          f_d->cont_data.face_contour[ix].contour_chin_pt[i].y);
      }
    }

    if (f_d->cont_data.face_contour[ix].is_ear_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(
        f_d->cont_data.face_contour[ix].contour_ear_pt); i++) {
        f_d->cont_data.face_contour[ix].contour_ear_pt[i].y =
          (uint32_t)IMG_FLIP(p_fp_stream->input_dim.height,
          f_d->cont_data.face_contour[ix].contour_ear_pt[i].y);
      }
    }
  }

#endif //FACE_PART_DETECT
}

 /**
  * Function: module_faceproc_client_flip_face_info
  *
  * Description: flip face information from face detection result
  *
  * Arguments:
  *   @p_client - faceproc client
  *   @f_d - Face detection data to be filled
  *   @ix  - index inside f_d at which data to be filled
  *   @p_fp_stream - faceproc stream info
  *
  * Return values:
  *   0 for success else failure
  *
  * Notes: none
  **/
int module_faceproc_client_flip_face_info(faceproc_client_t *p_client,
  combined_cam_face_detection_data_t *f_d,
  uint8_t ix,
  faceproc_stream_t *p_fp_stream)
{
  uint32_t i = 0;
  img_trans_info_t *trans_info = &p_fp_stream->out_trans_info;

  if (!f_d || !trans_info) {
    IDBG_ERROR("%s:%d] Invalid input %p %p", __func__, __LINE__,
      f_d, trans_info);
    return IMG_ERR_INVALID_INPUT;
  }
  IDBG_LOW("%s:%d] left eye info before flip %d %d; right: %d %d",
    __func__, __LINE__,
    f_d->fp_data.face_landmarks[ix].left_eye_center.x,
    f_d->fp_data.face_landmarks[ix].left_eye_center.y,
    f_d->fp_data.face_landmarks[ix].right_eye_center.x,
    f_d->fp_data.face_landmarks[ix].right_eye_center.y);

  switch (p_client->flip_mask) {
    case FLIP_H: {
      IDBG_MED("%s:%d] Horizantal flip", __func__, __LINE__);
      module_faceproc_client_flip_horizantal(f_d, i, p_fp_stream,p_client);
      break;
    }
    case FLIP_V: {
      IDBG_MED("%s:%d] Vertical flip", __func__, __LINE__);
      module_faceproc_client_flip_vertical(f_d, i, p_fp_stream,p_client);
      break;
    }
    case FLIP_V_H: {
      IDBG_MED("%s:%d] HV flip", __func__, __LINE__);
      module_faceproc_client_flip_horizantal(f_d, i, p_fp_stream,p_client);
      module_faceproc_client_flip_vertical(f_d, i, p_fp_stream,p_client);
      break;
    }
    case FLIP_NONE: {
      break;
    }
  }

  IDBG_LOW("%s:%d] left eye info after flip %d %d; right: %d %d",
    __func__, __LINE__,
    f_d->fp_data.face_landmarks[ix].left_eye_center.x,
    f_d->fp_data.face_landmarks[ix].left_eye_center.y,
    f_d->fp_data.face_landmarks[ix].right_eye_center.x,
    f_d->fp_data.face_landmarks[ix].right_eye_center.y);

  return IMG_SUCCESS;
}

 /**
 * Function: module_faceproc_client_scale_algo_result
 *
 * Description: Scale faceproc component result.
 *
 * Arguments:
 *   @p_roi - Pointer to faceproc result information.
 *   @trans_info - Transition info to be applied.
 *   @inv_scale - Whether to invert scale factor.
 *   @p_client - faceproc client
 *
 * Return values:
 *   IMG error values,
 *
 * Notes: none
 **/
static int module_faceproc_client_scale_algo_result(faceproc_info_t *p_roi,
  img_trans_info_t *trans_info, uint8_t inv_scale,faceproc_client_t *p_client)
{
  uint32_t i = 0;
  float h_scale, v_scale;

  if (!p_roi || !trans_info) {
    IDBG_ERROR("%s:%d]Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  if (IMG_F_EQUAL(trans_info->h_scale, 0.0)
    || (IMG_F_EQUAL(trans_info->v_scale, 0.0))) {
    IDBG_ERROR("%s:%d]Invalid scale factors", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  if (inv_scale) {
    h_scale = 1.0f / trans_info->h_scale;
    v_scale = 1.0f / trans_info->v_scale;
  } else {
    h_scale = trans_info->h_scale;
    v_scale = trans_info->v_scale;
  }

  p_roi->face_boundary.x = (uint32_t)IMG_TRANSLATE2(p_roi->face_boundary.x,
    h_scale, trans_info->h_offset);

  p_roi->face_boundary.y = (uint32_t)IMG_TRANSLATE2(p_roi->face_boundary.y,
    v_scale, trans_info->v_offset);

  p_roi->face_boundary.dx = (uint32_t)IMG_TRANSLATE2(p_roi->face_boundary.dx,
    h_scale, 0);

  p_roi->face_boundary.dy = (uint32_t)IMG_TRANSLATE2(p_roi->face_boundary.dy,
    v_scale, 0);

#if(FACE_PART_DETECT)
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_FACE_LANDMARKS) {
    p_roi->fp.face_pt[FACE_PART_LEFT_EYE].x =
      (int32_t)IMG_TRANSLATE2(p_roi->fp.face_pt[FACE_PART_LEFT_EYE].x,
      h_scale, trans_info->h_offset);

    p_roi->fp.face_pt[FACE_PART_LEFT_EYE].y =
      (int32_t)IMG_TRANSLATE2(p_roi->fp.face_pt[FACE_PART_LEFT_EYE].y,
      v_scale, trans_info->v_offset);

    p_roi->fp.face_pt[FACE_PART_RIGHT_EYE].x =
      (int32_t)IMG_TRANSLATE2(p_roi->fp.face_pt[FACE_PART_RIGHT_EYE].x,
      h_scale, trans_info->h_offset);

    p_roi->fp.face_pt[FACE_PART_RIGHT_EYE].y =
      (int32_t)IMG_TRANSLATE2(p_roi->fp.face_pt[FACE_PART_RIGHT_EYE].y,
      v_scale, trans_info->v_offset);

    p_roi->fp.face_pt[FACE_PART_MOUTH].x =
      (int32_t)IMG_TRANSLATE2(p_roi->fp.face_pt[FACE_PART_MOUTH].x,
      h_scale, trans_info->h_offset);

    p_roi->fp.face_pt[FACE_PART_MOUTH].y =
      (int32_t)IMG_TRANSLATE2(p_roi->fp.face_pt[FACE_PART_MOUTH].y,
      v_scale, trans_info->v_offset);

    if (p_roi->ct.is_eye_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(p_roi->ct.eye_pt); i++) {
        p_roi->ct.eye_pt[i].x =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.eye_pt[i].x,
          h_scale, trans_info->h_offset);
        p_roi->ct.eye_pt[i].y =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.eye_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_CONTOUR) {
    if (p_roi->ct.is_forehead_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(p_roi->ct.forh_pt); i++) {
        p_roi->ct.forh_pt[i].x =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.forh_pt[i].x,
          h_scale, trans_info->h_offset);
        p_roi->ct.forh_pt[i].y =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.forh_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (p_roi->ct.is_nose_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(p_roi->ct.nose_pt); i++) {
        p_roi->ct.nose_pt[i].x =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.nose_pt[i].x,
          h_scale, trans_info->h_offset);
        p_roi->ct.nose_pt[i].y =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.nose_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (p_roi->ct.is_mouth_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(p_roi->ct.mouth_pt); i++) {
        p_roi->ct.mouth_pt[i].x =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.mouth_pt[i].x,
          h_scale, trans_info->h_offset);
        p_roi->ct.mouth_pt[i].y =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.mouth_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (p_roi->ct.is_lip_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(p_roi->ct.lip_pt); i++) {
        p_roi->ct.lip_pt[i].x =
          (int32_t)IMG_TRANSLATE2( p_roi->ct.lip_pt[i].x,
          h_scale, trans_info->h_offset);
        p_roi->ct.lip_pt[i].y =
          (int32_t)IMG_TRANSLATE2( p_roi->ct.lip_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (p_roi->ct.is_brow_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(p_roi->ct.brow_pt); i++) {
        p_roi->ct.brow_pt[i].x =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.brow_pt[i].x,
          h_scale, trans_info->h_offset);
        p_roi->ct.brow_pt[i].y =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.brow_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (p_roi->ct.is_chin_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(p_roi->ct.chin_pt); i++) {
        p_roi->ct.chin_pt[i].x =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.chin_pt[i].x,
          h_scale, trans_info->h_offset);
        p_roi->ct.chin_pt[i].y =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.chin_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }

    if (p_roi->ct.is_ear_valid) {
      for (i = 0; i < IMGLIB_ARRAY_SIZE(p_roi->ct.ear_pt); i++) {
        p_roi->ct.ear_pt[i].x =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.ear_pt[i].x,
          h_scale, trans_info->h_offset);
        p_roi->ct.ear_pt[i].y =
          (int32_t)IMG_TRANSLATE2(p_roi->ct.ear_pt[i].y,
          v_scale, trans_info->v_offset);
      }
    }
  }
#endif

 return IMG_SUCCESS;
}
/**
 * Function: module_faceproc_post_bus_msg
 *
 * Description: post a particular message to media bus
 *
 * Arguments:
 *   @p_mct_mod - media controller module
 *   @sessionid - session id
 *   @msg_id - bus message id
 *   @msg_data - bus message data
 *   @msg_size - bus message size
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
void module_faceproc_post_bus_msg(mct_module_t *p_mct_mod,
    unsigned int sessionid, mct_bus_msg_type_t msg_id, void *msg_data,
    uint32_t msg_size)
{
  mct_bus_msg_t bus_msg;
  memset(&bus_msg, 0, sizeof(mct_bus_msg_t));
  bus_msg.type = msg_id;
  bus_msg.size = msg_size;
  bus_msg.msg = msg_data;
  bus_msg.sessionid = sessionid;
  IDBG_MED("%s:%d] session id %d mct_mod %p", __func__, __LINE__,
    bus_msg.sessionid, p_mct_mod);
  mct_module_post_bus_msg(p_mct_mod, &bus_msg);
}

/**
 * Function: module_faceproc_client_check_fd_boundary
 *
 * Description: validates the boundary of the FD cordiantes
 *
 * Arguments:
 *   @p_faces - HAL Faces pointer
 *   @width - Maximum width
 *   @height - Maximum height
 *
 * Return values:
 *   TRUE/FALSE
 *
 * Notes: none
 **/
inline boolean module_faceproc_client_check_fd_boundary(
  cam_face_detection_info_t *p_faces,
  int width,
  int height)
{
  if ((p_faces->face_boundary.top < 0) ||
    (p_faces->face_boundary.left < 0) ||
    (p_faces->face_boundary.width < 0) ||
    (p_faces->face_boundary.height < 0) ||
    (p_faces->face_boundary.top +
    p_faces->face_boundary.height >= height) ||
    (p_faces->face_boundary.left +
    p_faces->face_boundary.width >= width)) {
    /* out of boundary */
    IDBG_ERROR("%s:%d] Error invalid values (%d %d %d %d)",
      __func__, __LINE__,
      p_faces->face_boundary.left,
      p_faces->face_boundary.top,
      p_faces->face_boundary.width,
      p_faces->face_boundary.height);
    return FALSE;
  }
  return TRUE;
}

/**
 * Function: module_faceproc_client_send_faceinfo
 *
 * Description: send the face info to metadata stream
 *
 * Arguments:
 *   @p_client - Faceproc client pointer
 *   @p_fp_stream - Face detection stream
 *   @p_result - Face detection result
 *   @faces_data - Populated face data
 *
 * Return values:
 *   imaging error values
 *
 * Notes:
 *   ROI translations in HAL1 and HAL3:
 *   processing stream -> report stream -> active array size
 **/
int module_faceproc_client_send_faceinfo(faceproc_client_t *p_client,
  faceproc_result_t *p_result,
  faceproc_stream_t *p_fp_stream,
  combined_cam_face_detection_data_t *faces_data)
{
  uint32_t i = 0;
  combined_cam_face_detection_data_t lfaces_data;

  if (faces_data == NULL) {
      IDBG_ERROR("%s:%d] Invalid face data", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
  }

  if (p_result->num_faces_detected == 0) {
    /* translation not needed */
    faces_data->fd_data.num_faces_detected = 0;
    faces_data->fd_data.frame_id = p_result->frame_id;

    return IMG_SUCCESS;
  }

  IDBG_MED("%s:%d]out scale %f %f", __func__, __LINE__,
    p_fp_stream->out_trans_info.h_scale,
    p_fp_stream->out_trans_info.v_scale);

  IDBG_MED("%s:%d] [OFFLINE_FD]Num face detected After "
    "stabalization %d", __func__, __LINE__,
    p_result->num_faces_detected);

  module_faceproc_client_reset_face_info(p_client, faces_data);
  module_faceproc_client_reset_face_info(p_client, &lfaces_data);

  /* populate the values */
  lfaces_data.fd_data.num_faces_detected = (uint8_t)p_result->num_faces_detected;
  faces_data->fd_data.frame_id = p_result->frame_id;

  for (i = 0; i < p_result->num_faces_detected; i++) {
    module_faceproc_client_update_face_info(&lfaces_data, i,
      &p_result->roi[i], p_client);
  }

  /* Apply additional correction based on output preview size */
  if (IMG_IS_FACE_SCALING_NEEDED(p_fp_stream->out_trans_info)) {
    IDBG_MED("%s:%d] Scale to output preview size", __func__, __LINE__);

    for (i = 0; i < p_result->num_faces_detected; i++) {
      module_faceproc_client_scale_face_info(p_client, &lfaces_data,
        i,
        &p_fp_stream->out_trans_info, 0);
    }
  }

  /* Apply flip on fd coordinates if flip enabled */
  if (IMG_IS_FACE_FLIP_NEEDED(p_client->flip_mask)) {
    for (i = 0; i < p_result->num_faces_detected; i++) {
      module_faceproc_client_flip_face_info(p_client, &lfaces_data,
        i,
        p_fp_stream);
    }
  }

  /* Apply mapping from stream size coordinate space to
   * active array coordinate space */
  if (IMG_IS_HAL3(p_client) &&
    IMG_IS_FACE_SCALING_NEEDED(p_fp_stream->camif_out_trans_info)) {
    IDBG_MED("%s:%d] Scale to active array coordinate space size",
      __func__, __LINE__);

    for (i = 0; i < p_result->num_faces_detected; i++) {
      module_faceproc_client_scale_face_info(p_client, &lfaces_data, i,
        &p_fp_stream->camif_out_trans_info, 0);
    }
  }

  /* Filter the coordinates */
  faces_data->fd_data.num_faces_detected = 0;
  for (i = 0; i < p_result->num_faces_detected; i++) {
    if (module_faceproc_client_check_fd_boundary(&lfaces_data.fd_data.faces[i],
      (int)p_client->main_dim.width,
      (int)p_client->main_dim.height)) {
      faces_data->fd_data.faces[faces_data->fd_data.num_faces_detected] =
        lfaces_data.fd_data.faces[i];
      faces_data->smile_data.smile[faces_data->fd_data.num_faces_detected] =
        lfaces_data.smile_data.smile[i];
      faces_data->fp_data.face_landmarks
        [faces_data->fd_data.num_faces_detected] =
        lfaces_data.fp_data.face_landmarks[i];
      faces_data->blink_data.blink[faces_data->fd_data.num_faces_detected] =
        lfaces_data.blink_data.blink[i];
      faces_data->gaze_data.gaze[faces_data->fd_data.num_faces_detected] =
        lfaces_data.gaze_data.gaze[i];

      faces_data->fd_data.num_faces_detected++;
    }
  }

  faces_data->fd_data.frame_id = p_result->frame_id;

  faces_data->fd_data.fd_frame_dim.width  = p_fp_stream->input_dim.width;
  faces_data->fd_data.fd_frame_dim.height = p_fp_stream->input_dim.height;

  IDBG_MED("%s:%d] [FD_HAL3] Num face detected %d", __func__, __LINE__,
    faces_data->fd_data.num_faces_detected);
  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_sort_results
 *
 * Description: Sort the face detection results depending on
 * largest face in in descending order
 *
 * Arguments:
 *   @p_client - Faceproc client pointer
 *   @p_result - Face detection result
 *   @frame_id - frame id
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/

int module_faceproc_sort_results(faceproc_result_t *p_result,
  int start, int end)
{
  if(end > start+1){
    int width = (int32_t)p_result->roi[start].face_boundary.dx;
    int height = (int32_t)p_result->roi[start].face_boundary.dy;
    int pivot = width * height;
    int left = start+1, right = end;
    IDBG_MED("%s:%d] width=%d, height %d pivot=%d ", __func__, __LINE__,
      width, height, pivot);
    while(left < right) {
      int lwidth = (int32_t)p_result->roi[left].face_boundary.dx;
      int lheight = (int32_t)p_result->roi[left].face_boundary.dy;
      IDBG_MED("%s:%d] lwidth=%d, lheight %d pivot=%d ", __func__, __LINE__,
        lwidth, height, pivot);
      if((lwidth * lheight) >= pivot)
        left++;
      else
        SWAP(faceproc_info_t, p_result->roi[left], p_result->roi[--right]);

    }
    SWAP(faceproc_info_t, p_result->roi[--left], p_result->roi[start]);
    module_faceproc_sort_results(p_result, start, left);
    module_faceproc_sort_results(p_result, right, end);
  }
  return 1;
 }

/**
 * Function: module_faceproc_client_check_boundary
 *
 * Description: validates the boundary of the FD cordiantes
 *
 * Arguments:
 *   @p_fd_rect - Face detection input cordinates
 *   @p_img_rect - Display window
 *
 * Return values:
 *   TRUE/FALSE
 *
 * Notes: none
 **/
static inline boolean module_faceproc_client_check_boundary(
  fd_rect_t *p_fd_rect,
  img_rect_t *p_img_rect)
{
  if ((p_fd_rect->x < (uint32_t)p_img_rect->pos.x) ||
    (p_fd_rect->y < (uint32_t)p_img_rect->pos.y) ||
    ((p_fd_rect->x + p_fd_rect->dx) >
    (uint32_t)(p_img_rect->pos.x + p_img_rect->size.width)) ||
    ((p_fd_rect->y + p_fd_rect->dy) >
    (uint32_t)(p_img_rect->pos.y + p_img_rect->size.height))) {
    /* out of boundary */
    IMG_PRINT_RECT(p_img_rect);
    FD_PRINT_RECT(p_fd_rect);
    return FALSE;
  }
  IMG_PRINT_RECT(p_img_rect);
  FD_PRINT_RECT(p_fd_rect);
  return TRUE;
}


/**
 * Function: module_faceproc_client_filter_roi
 *
 * Description: filter the ROIs which falls outside the display
 *              window
 *
 * Arguments:
 *   @p_in_result - Face detection input result
 *   @p_out_result - Face detection output result
 *   @p_fp_stream - Face proc stream
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
void module_faceproc_client_filter_roi(
  faceproc_result_t *p_in_result,
  faceproc_result_t *p_out_result,
  faceproc_stream_t *p_fp_stream)
{
  uint32_t i = 0, j = 0;
  p_out_result->num_faces_detected = 0;
  for (i = 0; i < p_in_result->num_faces_detected; i++) {
    if (module_faceproc_client_check_boundary(
      &p_in_result->roi[i].face_boundary, &p_fp_stream->crop_info)) {
      p_out_result->roi[j] = p_in_result->roi[i];
      p_out_result->num_faces_detected++;
      j++;
    }
  }
}

/**
 * Function: module_faceproc_client_stats_filter_get_score
 *
 * Description: Stats filter to improve consistency
 *
 * Arguments:
 *   @p_last - Last accepted faceinfo
 *   @p_prev - rrevious faceinfo
 *   @p_new - New face info
 *
 * Return values:
 *   face score
 *
 * Notes: none
 **/
fd_score_t module_faceproc_client_stats_filter_get_score(
  mct_face_info_t *p_last,
  mct_face_info_t *p_prev __unused,
  mct_face_info_t *p_new)
{
  if (p_new->face_count > p_last->face_count) {
    return FD_SCORE_HIGH;
  }

  if (!p_new->face_count && (p_new->face_count < p_last->face_count)) {
    return FD_SCORE_LOW;
  }

  if (!p_new->face_count) {
    return FD_SCORE_SAME;
  }
  /* same level. check the id of largest face */
  if (p_new->faces[0].face_id != p_last->faces[0].face_id) {
    return FD_SCORE_HIGH;
  }

  return FD_SCORE_SAME;
}

/**
 * Function: module_faceproc_client_ui_filter_get_score
 *
 * Description: UI filter to improve consistency
 *
 * Arguments:
 *   @old_f_cnt - old face count
 *   @new_f_cnt - new face count
 *
 * Return values:
 *   face score
 *
 * Notes: none
 **/
fd_score_t module_faceproc_client_ui_filter_get_score(uint32_t old_f_cnt,
  uint32_t new_f_cnt)
{
  if (!new_f_cnt && (new_f_cnt < old_f_cnt)) {
    return FD_SCORE_LOW;
  }

  return FD_SCORE_HIGH;
}

/**
 * Function: module_faceproc_client_get_face_info
 *
 * Description: Function to run the temporal filter and get the
 *         face info
 *
 * Arguments:
 *   @old_f_cnt - old face count
 *   @new_f_cnt - new face count
 *
 * Return values:
 *   face score
 *
 * Notes: none
 **/
mct_face_info_t *module_faceproc_client_get_face_info(
  faceproc_client_t *p_client,
  mct_face_info_t *p_face_info)
{
  mct_face_info_t *p_new_fi = p_face_info;

  fd_score_t score = module_faceproc_client_stats_filter_get_score(
    &p_client->stats_filter.mfi_last_updated,
    NULL,
    p_face_info);
  fd_score_t ui_score = module_faceproc_client_ui_filter_get_score(
    p_client->stats_filter.mfi_last_updated.face_count,
    p_face_info->face_count);

  IDBG_MED("%s:%d] [FD_FALSE_NEG_DBG] cnt %d score %d hold %d locked %d",
    __func__, __LINE__, p_face_info->face_count, score,
    p_client->stats_filter.hold_cnt,
    p_client->stats_filter.locked);

  if (p_client->stats_filter.locked) {
    p_new_fi = &p_client->stats_filter.mfi_last_updated;
  } else if ((score != FD_SCORE_LOW) || !p_client->stats_filter.hold_cnt) {
    p_client->stats_filter.mfi_last_updated = *p_face_info;
    p_client->stats_filter.hold_cnt =
      p_client->p_fd_chromatix->stats_filter_max_hold;
  } else {
    p_client->stats_filter.hold_cnt--;
    p_new_fi = &p_client->stats_filter.mfi_last_updated;
    p_client->use_old_stats = true;
  }

  if ((ui_score != FD_SCORE_LOW) || !p_client->stats_filter.ui_hold_cnt) {
    p_client->stats_filter.ui_hold_cnt =
      p_client->p_fd_chromatix->ui_filter_max_hold;
  } else {
    p_client->stats_filter.ui_hold_cnt--;
    p_client->use_old_stats = true;
  }

  IDBG_MED("%s:%d] [FD_FALSE_NEG_DBG] last cnt %d id %d (%d %d %d %d)",
    __func__, __LINE__, p_new_fi->face_count,
    p_new_fi->faces[0].face_id,
    p_new_fi->faces[0].roi.left,
    p_new_fi->faces[0].roi.top,
    p_new_fi->faces[0].roi.width,
    p_new_fi->faces[0].roi.height);
  return p_new_fi;
}

/**
 * Function: module_faceproc_client_send_roi_event
 *
 * Description: send the face info to upstream
 *
 * Arguments:
 *   @p_client - Faceproc client pointer
 *   @p_result - Face detection result
 *   @frame_id - frame id
 *   @p_histogram: pointer to face histogram
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
void module_faceproc_client_send_roi_event(faceproc_client_t *p_client,
  faceproc_result_t *p_result,
  faceproc_stream_t *p_fp_stream,
  mct_face_hist_t *p_histogram)
{
  mct_event_t mct_event;
  mct_face_info_t face_info;
  uint32_t i = 0;
  img_rect_t in_region;
  img_rect_t out_region;
  img_size_t preview_dim;

  memset(&mct_event, 0x0, sizeof(mct_event_t));
  memset(&face_info, 0x0, sizeof(mct_face_info_t));

  /* populate the values */
  face_info.frame_id = p_result->frame_id;
  face_info.region_priority = p_client->p_fd_chromatix->region_filter.enable;
  for (i = 0; i < p_result->num_faces_detected; i++) {
    face_info.face_count++;
    face_info.faces[i].score = (int8_t)(p_result->roi[i].fd_confidence/10);
    face_info.faces[i].face_id = p_result->roi[i].unique_id;

    in_region.pos.x = (int32_t)p_result->roi[i].face_boundary.x;
    in_region.pos.y = (int32_t)p_result->roi[i].face_boundary.y;
    in_region.size.width = (int32_t)p_result->roi[i].face_boundary.dx;
    in_region.size.height = (int32_t)p_result->roi[i].face_boundary.dy;
    preview_dim.width = p_fp_stream->stream_info->dim.width;
    preview_dim.height = p_fp_stream->stream_info->dim.height;

    out_region.pos.x =
      (int32_t)IMG_TRANSLATE2((float)in_region.pos.x,
        p_fp_stream->camif_out_trans_info.h_scale,
        (float)p_fp_stream->camif_out_trans_info.h_offset);
    out_region.pos.y =
      (int32_t)IMG_TRANSLATE2((float)in_region.pos.y,
        p_fp_stream->camif_out_trans_info.v_scale,
        (float)p_fp_stream->camif_out_trans_info.v_offset);
    out_region.size.width =
      (int32_t)IMG_TRANSLATE2((float)in_region.size.width,
        p_fp_stream->camif_out_trans_info.h_scale,
      0);
    out_region.size.height =
      (int32_t)IMG_TRANSLATE2((float)in_region.size.height,
        p_fp_stream->camif_out_trans_info.v_scale,
      0);

    face_info.orig_faces[i].roi.top = (int32_t)in_region.pos.y;
    face_info.orig_faces[i].roi.left = (int32_t)in_region.pos.x;
    face_info.orig_faces[i].roi.width = (int32_t)in_region.size.width;
    face_info.orig_faces[i].roi.height = (int32_t)in_region.size.height;
    face_info.faces[i].roi.top = (int32_t)out_region.pos.y;
    face_info.faces[i].roi.left = (int32_t)out_region.pos.x;
    face_info.faces[i].roi.width = (int32_t)out_region.size.width;
    face_info.faces[i].roi.height = (int32_t)out_region.size.height;

    if (p_client->face_hist_enable &&
      p_histogram[i].num_samples) {
      face_info.p_histogram[i] = &p_histogram[i];
    }
  }

  /* create MCT event and send */
  uint32_t session_str_idx =
    faceproc_get_stream_by_type(p_client, CAM_STREAM_TYPE_PARM);

  mct_event.u.module_event.type = MCT_EVENT_MODULE_FACE_INFO;
  mct_event.u.module_event.module_event_data =
    module_faceproc_client_get_face_info(p_client, &face_info);
  mct_event.type = MCT_EVENT_MODULE_EVENT;
  mct_event.identity = p_client->fp_stream[session_str_idx].identity;
  mct_event.direction = MCT_EVENT_UPSTREAM;
  mct_port_send_event_to_peer(
    p_client->fp_stream[session_str_idx].p_sink_port, &mct_event);
  p_client->prev_face_count = (int8_t)face_info.face_count;
}

/**
 * Function: module_faceproc_client_debug
 *
 * Description: print the faceproc result
 *
 * Arguments:
 *   p_result - Face detection result
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
void module_faceproc_client_debug(faceproc_result_t *p_result)
{
  uint32_t i = 0;
  uint32_t ct_cnt;
  IDBG_HIGH("Faceproc result num_faces_detected %d",
    p_result->num_faces_detected);

  for (i = 0; i < p_result->num_faces_detected; i++) {
    IDBG_HIGH("Faceproc face[%d] blink_detected %d", i,
      p_result->roi[i].blink_detected);
    IDBG_HIGH("Faceproc face[%d] face_boundary (%d %d %d %d)", i,
      p_result->roi[i].face_boundary.x,
      p_result->roi[i].face_boundary.y,
      p_result->roi[i].face_boundary.dx,
      p_result->roi[i].face_boundary.dy);
    IDBG_HIGH("Faceproc face[%d] fd_confidence %d", i,
      p_result->roi[i].fd_confidence);
    IDBG_HIGH("Faceproc face[%d] fp.direction_left_right %d", i,
      p_result->roi[i].fp.direction_left_right);
    IDBG_HIGH("Faceproc face[%d] fp.direction_up_down %d", i,
      p_result->roi[i].fp.direction_up_down);
    IDBG_HIGH("Faceproc face[%d] fp.direction_roll %d", i,
      p_result->roi[i].fp.direction_roll);
    IDBG_HIGH("Faceproc face[%d] gaze_angle %d", i,
      p_result->roi[i].gaze_angle);
    IDBG_HIGH("Faceproc face[%d] is_face_recognised %d", i,
      p_result->roi[i].is_face_recognised);
    IDBG_HIGH("Faceproc face[%d] left_blink %d", i,
      p_result->roi[i].left_blink);
    IDBG_HIGH("Faceproc face[%d] right_blink %d", i,
      p_result->roi[i].right_blink);
    IDBG_HIGH("Faceproc face[%d] left_right_gaze %d", i,
      p_result->roi[i].left_right_gaze);
    IDBG_HIGH("Faceproc face[%d] top_bottom_gaze %d", i,
      p_result->roi[i].top_bottom_gaze);
    IDBG_HIGH("Faceproc face[%d] sm.confidence %d", i,
      p_result->roi[i].sm.confidence);
    IDBG_HIGH("Faceproc face[%d] sm.smile_degree %d", i,
      p_result->roi[i].sm.smile_degree);
    IDBG_HIGH("Faceproc face[%d] Left Eye (%d %d)", i,
      p_result->roi[i].fp.face_pt[FACE_PART_LEFT_EYE].x,
      p_result->roi[i].fp.face_pt[FACE_PART_LEFT_EYE].y);
    IDBG_HIGH("Faceproc face[%d] Right Eye (%d %d)", i,
      p_result->roi[i].fp.face_pt[FACE_PART_RIGHT_EYE].x,
      p_result->roi[i].fp.face_pt[FACE_PART_RIGHT_EYE].y);
    IDBG_HIGH("Faceproc face[%d] Mouth (%d %d)", i,
      p_result->roi[i].fp.face_pt[FACE_PART_MOUTH].x,
      p_result->roi[i].fp.face_pt[FACE_PART_MOUTH].y);
    if (p_result->roi->ct.is_eye_valid) {
      for (ct_cnt = 0; ct_cnt < IMGLIB_ARRAY_SIZE(convert_ct_eye); ct_cnt++) {
        IDBG_HIGH("Faceproc face[%d] Contour eye %d (%d %d)", i, ct_cnt,
          p_result->roi->ct.eye_pt[convert_ct_eye[ct_cnt][1]].x,
          p_result->roi->ct.eye_pt[convert_ct_eye[ct_cnt][1]].y);
      }
    }
    if (p_result->roi->ct.is_forehead_valid) {
      for (ct_cnt = 0; ct_cnt < IMGLIB_ARRAY_SIZE(convert_ct_forh); ct_cnt++) {
        IDBG_HIGH("Faceproc face[%d] Contour forehead %d (%d %d)", i, ct_cnt,
          p_result->roi->ct.forh_pt[convert_ct_forh[ct_cnt][1]].x,
          p_result->roi->ct.forh_pt[convert_ct_forh[ct_cnt][1]].y);
      }
    }
    if (p_result->roi->ct.is_nose_valid) {
      for (ct_cnt = 0; ct_cnt < IMGLIB_ARRAY_SIZE(convert_ct_nose); ct_cnt++) {
        IDBG_HIGH("Faceproc face[%d] Contour nose %d (%d %d)", i, ct_cnt,
          p_result->roi->ct.nose_pt[convert_ct_nose[ct_cnt][1]].x,
          p_result->roi->ct.nose_pt[convert_ct_nose[ct_cnt][1]].y);
      }
    }
    if (p_result->roi->ct.is_mouth_valid) {
      for (ct_cnt = 0; ct_cnt < IMGLIB_ARRAY_SIZE(convert_ct_mouth); ct_cnt++) {
        IDBG_HIGH("Faceproc face[%d] Contour mouth %d (%d %d)", i, ct_cnt,
          p_result->roi->ct.mouth_pt[convert_ct_mouth[ct_cnt][1]].x,
          p_result->roi->ct.mouth_pt[convert_ct_mouth[ct_cnt][1]].y);
      }
    }
    if (p_result->roi->ct.is_lip_valid) {
      for (ct_cnt = 0; ct_cnt < IMGLIB_ARRAY_SIZE(convert_ct_lip); ct_cnt++) {
        IDBG_HIGH("Faceproc face[%d] Contour lip %d (%d %d)", i, ct_cnt,
          p_result->roi->ct.lip_pt[convert_ct_lip[ct_cnt][1]].x,
          p_result->roi->ct.lip_pt[convert_ct_lip[ct_cnt][1]].y);
      }
    }
    if (p_result->roi->ct.is_brow_valid) {
      for (ct_cnt = 0; ct_cnt < IMGLIB_ARRAY_SIZE(convert_ct_brow); ct_cnt++) {
        IDBG_HIGH("Faceproc face[%d] Contour brow %d (%d %d)", i, ct_cnt,
          p_result->roi->ct.brow_pt[convert_ct_brow[ct_cnt][1]].x,
          p_result->roi->ct.brow_pt[convert_ct_brow[ct_cnt][1]].y);
      }
    }
    if (p_result->roi->ct.is_chin_valid) {
      for (ct_cnt = 0; ct_cnt < IMGLIB_ARRAY_SIZE(convert_ct_chin); ct_cnt++) {
        IDBG_HIGH("Faceproc face[%d] Contour chin %d (%d %d)", i, ct_cnt,
          p_result->roi->ct.chin_pt[convert_ct_chin[ct_cnt][1]].x,
          p_result->roi->ct.chin_pt[convert_ct_chin[ct_cnt][1]].y);
      }
    }
    if (p_result->roi->ct.is_ear_valid) {
      for (ct_cnt = 0; ct_cnt < IMGLIB_ARRAY_SIZE(convert_ct_ear); ct_cnt++) {
        IDBG_HIGH("Faceproc face[%d] Contour ear %d (%d %d)", i, ct_cnt,
          p_result->roi->ct.ear_pt[convert_ct_ear[ct_cnt][1]].x,
          p_result->roi->ct.ear_pt[convert_ct_ear[ct_cnt][1]].y);
      }
    }
  }
}

/**
 * Function: module_faceproc_run_stabilization
 *
 * Description: Run faceproc stabilization
 *
 * Arguments:
 *   p_client - Pointer to faceproc client
 *   p_result - Pointer face detection result
 * Return values:
 *   imglib error codes
 *
 * Notes: none
 **/
int module_faceproc_run_stabilization(faceproc_client_t *p_client,
  faceproc_result_t *p_result)
{
  img_size_t frame_dim;
  int s_idx;
  int ret;

  if (!p_client->p_fd_chromatix->stab_enable) {
    return IMG_SUCCESS;
  }

  s_idx = p_result->client_id;
  if (s_idx < 0 || s_idx >= MAX_NUM_FD_FRAMES) {
    IDBG_ERROR("%s:%d] Invalid client id %d", __func__, __LINE__,
      p_result->client_id);
    return IMG_ERR_INVALID_INPUT;
  }

  frame_dim.width = p_client->fp_stream[s_idx].input_dim.width;
  frame_dim.height = p_client->fp_stream[s_idx].input_dim.height;
  ret = module_faceproc_faces_stabilization(&p_client->stabilization,
    p_client->p_fd_chromatix, &frame_dim, p_result);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("%s:%d] Error face stabilization %d", __func__, __LINE__, ret);
  }

  return ret;
}

/**
 * Function: module_faceproc_client_calculate_score
 *
 * Description: Calculate the face score and weight for region filtering
 *
 * Arguments:
 *   region_roi - Pointer to region roi
 *   face_roi - Pointer face roi
 * Return values:
 *   imglib error codes
 *
 * Notes: none
 **/
static int module_faceproc_client_calculate_score(
  faceproc_region_info_t *region_info,
  faceproc_info_t *face_roi)
{
  uint32_t temp_score = 0;
  float temp_weight = 0;

  if (((face_roi->face_boundary.x >= region_info->region_boundary.x) &&
    (face_roi->face_boundary.x + face_roi->face_boundary.dx) <=
    (region_info->region_boundary.x + region_info->region_boundary.dx)) &&
    ((face_roi->face_boundary.y >= region_info->region_boundary.y) &&
    (face_roi->face_boundary.y + face_roi->face_boundary.dy) <=
    (region_info->region_boundary.y + region_info->region_boundary.dy))) {
    // inside the region
    if (face_roi->face_boundary.dx > region_info->min_face_size) {
      temp_score = 1;
      temp_weight = region_info->weight;
    }
  }

  if (face_roi->score < temp_score) {
    face_roi->score = temp_score;
  }
  if (face_roi->weight < temp_weight) {
    face_roi->weight = temp_weight;
  }
  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_roi_sort_by_weight
 *
 * Description: CAllback function for qsort used for sorting
 *  faces based on face x,y weight
 *
 * Arguments:
 *  @arg1 - faceproc_info_t * First Roi
 *  @arg2 - faceproc_info_t * Second Roi
 *
 * Return values:
 *  0 - equal
 *  1 - weight of face1 is smaller than of face2
 *  -1 - weight of face1 is bigger than of face2
 * Notes: none
 **/

int module_faceproc_client_roi_sort_by_weight(const void *arg1,
  const void *arg2)
{
  faceproc_info_t *first_roi = (faceproc_info_t *)arg1;
  faceproc_info_t *second_roi = (faceproc_info_t *)arg2;

  if (first_roi->weight > second_roi->weight) {
    return -1;
  } else if (first_roi->weight < second_roi->weight){
    return 1;
  }

  return 0;
}

/**
 * Function: module_faceproc_client_roi_sort_by_size
 *
 * Description: CAllback function for qsort used for sorting
 *  faces based on face x,y weight
 *
 * Arguments:
 *  @arg1 - faceproc_info_t * First Roi
 *  @arg2 - faceproc_info_t * Second Roi
 *
 * Return values:
 *  0 - equal
 *  1 - weight of face1 is smaller than of face2
 *  -1 - weight of face1 is bigger than of face2
 * Notes: none
 **/

int module_faceproc_client_roi_sort_by_size(const void *arg1,
  const void *arg2)
{
  faceproc_info_t *first_roi = (faceproc_info_t *)arg1;
  faceproc_info_t *second_roi = (faceproc_info_t *)arg2;

  if (first_roi->face_boundary.dx > second_roi->face_boundary.dx) {
    return -1;
  } else if (first_roi->face_boundary.dx < second_roi->face_boundary.dx){
    return 1;
  }

  return 0;
}


/**
 * Function: module_faceproc_run_region_filter
 *
 * Description: Run region filter
 *
 * Arguments:
 *   p_client - Pointer to faceproc client
 *   p_result - Pointer face detection result
 * Return values:
 *   imglib error codes
 *
 * Notes: none
 **/
int module_faceproc_run_region_filter(faceproc_client_t *p_client,
  faceproc_result_t *p_result)
{
  uint32_t faces_detected = 0, i = 0, j = 0;

  if (p_result->num_faces_detected == 0)
    return IMG_SUCCESS;

  if (!p_client->p_fd_chromatix->region_filter.enable) {
    qsort(p_result->roi, p_result->num_faces_detected, sizeof(p_result->roi[0]),
      module_faceproc_client_roi_sort_by_size);
    return IMG_SUCCESS;
  }

  for (i = 0; i < p_result->num_faces_detected; i++) {
    p_result->roi[i].score = 0;
    p_result->roi[i].weight = 0;
    for (j = 0; j < FD_CHROMATIX_MAX_REGION_NUM; j++) {
      if (p_result->roi[i].score == 1)
        break;
      module_faceproc_client_calculate_score(&(p_client->config.region_info[j]),
        &(p_result->roi[i]));
    }
    IDBG_MED("%s:%d] face %d, score %d", __func__, __LINE__, i,
      p_result->roi[i].score);
  }

  for (i = 0; i < p_result->num_faces_detected; i++) {
    p_result->roi[i].weight *= p_result->roi[i].face_boundary.dx;
  }

  /* Sort the output ROI */
  qsort(p_result->roi, p_result->num_faces_detected, sizeof(p_result->roi[0]),
    module_faceproc_client_roi_sort_by_weight);

  // filter face result using the score
  i = 0;
  faces_detected = p_result->num_faces_detected;
  while (i < faces_detected) {
    /*if face score less than 1 or face number exceed the max num,
      remove the face*/
    if (p_result->roi[i].score < 1 || i > (MAX_FACE_NUM - 1)) {
      faces_detected--;
      if (i < faces_detected) {
        memcpy(&p_result->roi[i], &p_result->roi[i + 1],
          sizeof(p_result->roi[0]) * (faces_detected - i));
      }
    } else {
      i++;
    }
  }

  p_result->num_faces_detected = faces_detected;

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_get_region_info
 *
 * Description: Fill region info by chromatix and dimension.
 *
 * Arguments:
 *   @p_client: faceproc client.
 *
 * Return values:
 *   IMG_SUCCESS
 **/
static int module_faceproc_client_get_region_info(faceproc_client_t *p_client)
{
  uint32_t i;
  fd_face_region_filter_t *p_region_filter;
  p_region_filter = &(p_client->p_fd_chromatix->region_filter);
  IDBG_MED("%s:%d] frame dim %d %d", __func__, __LINE__,
    p_client->config.frame_cfg.max_width,
    p_client->config.frame_cfg.max_height);

  for (i = 0; i < FD_CHROMATIX_MAX_REGION_NUM; i++) {
    p_client->config.region_info[i].region_boundary.x =
      (int)((1 - p_region_filter->p_region[i]) *
      p_client->config.frame_cfg.max_width / 2);
    p_client->config.region_info[i].region_boundary.y =
      (int)((1 - p_region_filter->p_region[i]) *
      p_client->config.frame_cfg.max_height / 2);
    p_client->config.region_info[i].region_boundary.dx =
      (int)(p_region_filter->p_region[i] *
      p_client->config.frame_cfg.max_width);
    p_client->config.region_info[i].region_boundary.dy =
      (int)(p_region_filter->p_region[i] *
      p_client->config.frame_cfg.max_height);

    p_client->config.region_info[i].min_face_size =
      p_region_filter->size_region[i];
    p_client->config.region_info[i].weight =
      p_region_filter->w_region[i];
  }
  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_calc_face_hist
 *
 * Description: Calculate histogram for detected faces
 *
 * Arguments:
 *   p_client - Pointer to faceproc client
 *   p_frame - Pointer to frame used for face detection
 * Return values:
 *   imglib error codes
 *
 * Notes: none
 **/
int module_faceproc_calc_face_hist(faceproc_client_t *p_client,
  img_frame_t *p_frame)
{
  faceproc_result_t *p_result;
  faceproc_info_t *p_roi;
  uint8_t *p_img_ptr;
  uint32_t stride;
  uint32_t i, y, x;
  uint32_t res_idx;
  mct_face_hist_t *p_histogram;

  if (!p_client->face_hist_enable) {
    /* skip the histogram computation */
    return IMG_SUCCESS;
  }

  if (!p_frame || !p_frame->frame[0].plane[0].addr) {
    IDBG_ERROR("%s:%d] Invalid input %p", __func__, __LINE__, p_frame);
    return IMG_ERR_INVALID_INPUT;
  }

  p_result = NULL;
  for (i = 0; i < IMGLIB_ARRAY_SIZE(p_client->result); i++) {
    if (p_frame->frame_id == p_client->result[i].frame_id) {
      p_result = &p_client->result[i];
      res_idx = i;
      IDBG_MED("%s:%d] Found result frame id %d", __func__, __LINE__,
        p_frame->frame_id);
      break;
    }
  }
  if (NULL == p_result) {
    IDBG_ERROR("%s:%d] Can not find frame id %d for histogram",
      __func__, __LINE__, p_frame->frame_id);
    return IMG_ERR_GENERAL;
  }

  stride = p_frame->frame[0].plane[0].stride;
  for (i = 0; i < p_result->num_faces_detected; i++) {
    p_roi = &p_result->roi[i];

    p_img_ptr = p_frame->frame[0].plane[0].addr +
      (stride * p_roi->face_boundary.y + p_roi->face_boundary.x);

    p_histogram = &p_client->histogram[res_idx][i];
    memset(p_histogram->bin, 0x00, sizeof(p_histogram->bin));

    /* Skip if face is out of the bounds  */
    if ((p_roi->face_boundary.x + p_roi->face_boundary.dx) >
      p_frame->frame[0].plane[0].width) {
      IDBG_HIGH("%s:%d] x-%d dx-%d width %d out of the bounds",
        __func__, __LINE__, p_roi->face_boundary.x, p_roi->face_boundary.dx,
        p_frame->frame[0].plane[0].width);
      continue;
    }
    if ((p_roi->face_boundary.y + p_roi->face_boundary.dy) >
      p_frame->frame[0].plane[0].height) {
      IDBG_HIGH("%s:%d] y-%d dy-%d height %d out of the bounds",
        __func__, __LINE__, p_roi->face_boundary.y, p_roi->face_boundary.dy,
        p_frame->frame[0].plane[0].height);
      continue;
    }

    p_histogram->num_samples =
      p_roi->face_boundary.dx * p_roi->face_boundary.dy;

    for (y = 0; y < p_roi->face_boundary.dy; y++) {
      for (x = 0; x < p_roi->face_boundary.dx; x++) {
        p_histogram->bin[p_img_ptr[x]]++;
      }
      p_img_ptr += stride;
    }
  }

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_event_handler
 *
 * Description: event handler for FaceProc client
 *
 * Arguments:
 *   p_appdata - FaceProc test object
 *   p_event - pointer to the event
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
static int module_faceproc_client_event_handler(void* p_appdata,
  img_event_t *p_event)
{
  faceproc_client_t *p_client = (faceproc_client_t *)p_appdata;
  int rc = IMG_SUCCESS;
  img_frame_t *p_frame = NULL;
  // Note : Do not call module_faceproc_client_get_current_comp in
  // callback even handler thread. module_faceproc_client_get_current_comp
  // will actually check for current index validity and switch the component
  // index.
  img_component_ops_t *p_comp =
    &p_client->comps[p_client->engine];

  if ((NULL == p_event) || (NULL == p_appdata)) {
    IDBG_ERROR("%s:%d] invalid event", __func__, __LINE__);
    return IMG_SUCCESS;
  }
  IDBG_LOW("%s:%d] type %d", __func__, __LINE__, p_event->type);

  switch (p_event->type) {
  case QIMG_EVT_FACE_PROC: {
    /* get the result*/
    int index;
    pthread_mutex_lock(&p_client->result_mutex);
    index = img_rb_enqueue_idx(&p_client->result_rb);
    if (index < 0) {
      IDBG_ERROR("%s:%d] Error result rb incorrect (%d %d)",
        __func__, __LINE__,
        p_client->result_rb.front, p_client->result_rb.rear);
      /* skip processing */
      pthread_mutex_unlock(&p_client->result_mutex);
      break;
    }
    rc = IMG_COMP_GET_PARAM(p_comp, QWD_FACEPROC_RESULT,
      (void *)&p_client->result[index]);
    if (IMG_SUCCEEDED(rc)) {
      module_faceproc_run_stabilization(p_client, &p_client->result[index]);
#ifdef FD_DYNAMIC_REGION_RESULT_DEBUG
      IDBG_HIGH("%s:%d] index %d", __func__, __LINE__, index);
      module_faceproc_client_debug(&p_client->result[index]);
#endif
      module_faceproc_run_region_filter(p_client, &p_client->result[index]);
      p_client->result[index].updated_result = TRUE;
    }
    pthread_mutex_unlock(&p_client->result_mutex);

    /* Signal module that face register is done */
    if (p_client->mode == FACE_REGISTER)
      pthread_cond_signal(&p_client->cond);

#ifdef FD_RESULT_DEBUG
    IDBG_HIGH("%s:%d] index %d", __func__, __LINE__, index);
    module_faceproc_client_debug(&p_client->result[index]);
#endif
    break;
  }

  case QIMG_EVT_BUF_DONE: {
    /*send the buffer back*/
    rc = IMG_COMP_DQ_BUF(p_comp, &p_frame);
    IDBG_MED("%s:%d] buffer idx %d", __func__, __LINE__, p_frame->idx);

    /* If private data is available sent buffer divert acknowledge */
    if (IMG_SUCCEEDED(rc)) {
      /* Calculate histogram first */
      pthread_mutex_lock(&p_client->result_mutex);
      module_faceproc_calc_face_hist(p_client, p_frame);
      pthread_mutex_unlock(&p_client->result_mutex);

      if (p_frame->private_data) {
        faceproc_divert_holder_t *p_divert_holder = p_frame->private_data;
        mod_img_msg_buf_divert_t msg;
        boolean send_ack = FALSE;

        pthread_mutex_lock(&p_divert_holder->mutex);
        IDBG_MED("%s:%d] buffer idx %d send ack %d id %x locked %d",
          __func__, __LINE__, p_frame->idx,
          p_divert_holder->return_ack, p_divert_holder->msg.identity,
          p_divert_holder->locked);
        if (p_divert_holder->locked) {
          msg = p_divert_holder->msg;
          p_divert_holder->locked = FALSE;
          send_ack = p_divert_holder->return_ack;
        }
        pthread_mutex_unlock(&p_divert_holder->mutex);

        if (send_ack) {
          module_faceproc_client_send_ack_event(p_client, &msg);
        }
      }
    }
    break;
  }

  case QIMG_EVT_COMP_CONNECTION_FAILED :{
    IDBG_ERROR("%s:%d] Connection Failed. current comp=%d",
      __func__, __LINE__, p_client->engine);

    pthread_mutex_lock(&p_client->mutex);
    p_client->curr_comp_connection_lost = TRUE;
    pthread_mutex_unlock(&p_client->mutex);

    break;
  }

  case QIMG_EVT_ERROR: {
    rc = IMG_ERR_GENERAL;
    IDBG_ERROR("%s:%d] QIMG_EVT_ERROR received", __func__, __LINE__);
    /*send the buffer back*/
    if (p_client->mode == FACE_REGISTER)
      pthread_cond_signal(&p_client->cond);
    break;
  }

  default:
    break;
  }
  return rc;
}

/**
 * Function: module_faceproc_client_destroy
 *
 * Description: This function is used to destroy the faceproc client
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @idenitity: Stream identity
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
void module_faceproc_client_destroy(faceproc_client_t *p_client,
  uint32_t identity)
{
  int rc = IMG_SUCCESS;
  uint32_t i;

  if (NULL == p_client) {
    return;
  }

  IDBG_MED("%s:%d] state %d", __func__, __LINE__, p_client->state);

  if ((IMGLIB_STATE_STARTED == p_client->state)
   || (IMGLIB_STATE_START_PENDING == p_client->state)) {
    module_faceproc_client_stop(p_client, identity);
  }

  if (IMGLIB_STATE_INIT == p_client->state) {
    rc = module_faceproc_client_comp_destroy(p_client);
  }

  if (IMGLIB_STATE_IDLE == p_client->state) {

    /* clear the fpc queue */
    if (p_client->p_fpc_q && (p_client->fpc_q_size > 0)) {
      for (i = 0; i < p_client->fpc_q_size; i++) {
        /* flush and remove the elements of the queue */
        img_q_flush_and_destroy(&p_client->p_fpc_q[i]);
        /* de-initialize the queue */
        img_q_deinit(&p_client->p_fpc_q[i]);
      }
      free(p_client->p_fpc_q);
    }

    pthread_mutex_destroy(&p_client->result_mutex);
    pthread_mutex_destroy(&p_client->mutex);
    pthread_cond_destroy(&p_client->cond);
    for (i = 0; i < MAX_FD_STREAM; i++) {
      if (p_client->fp_stream[i].buffer_info.p_buffer) {
        free(p_client->fp_stream[i].buffer_info.p_buffer);
        p_client->fp_stream[i].buffer_info.p_buffer = NULL;
        p_client->fp_stream[i].buffer_info.buf_count = 0;
      }
    }

    for (i = 0; i < MAX_NUM_FD_FRAMES; i++) {
      pthread_mutex_destroy(&p_client->divert_holder[i].mutex);
    }

    free(p_client);
    p_client = NULL;
  }
  IDBG_MED("%s:%d] X", __func__, __LINE__);
}

/**
 * Function: module_faceproc_client_exec
 *
 * Description: This function is used to execute the faceproc
 *              client
 *
 * Arguments:
 *   @p_client: faceproc client
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_exec(faceproc_client_t *p_client)
{
  int rc = IMG_SUCCESS;
  img_component_ops_t *p_comp =
    module_faceproc_client_get_current_comp(p_client);
  mct_stream_info_t *stream_info;
  int s_idx;
  faceproc_stream_t *p_fp_stream;
  int fd_comp_state;

  IDBG_MED("%s:%d] E", __func__, __LINE__);

  fd_comp_state = IMG_COMP_GET_STATE(p_comp);
  if (fd_comp_state != IMG_STATE_INIT) {
    IDBG_MED("%s: %d:] Component already started", __func__, __LINE__);
    return IMG_SUCCESS;
  }

  s_idx = faceproc_get_processing_stream_idx(p_client);
  if ((s_idx < 0) || (s_idx >= MAX_FD_STREAM)) {
    IDBG_MED("%s:%d] E", __func__, __LINE__);
    return IMG_SUCCESS;
  }

  p_fp_stream = &p_client->fp_stream[s_idx];

  /* get the maximum dimensions from the sensor */
  stream_info = p_fp_stream->stream_info;

#ifdef FD_USE_INTERNAL_MEM
  if ((uint32_t)p_fp_stream->input_dim.width > MAX_FD_WIDTH ||
    (uint32_t)p_fp_stream->input_dim.height > MAX_FD_HEIGHT) {
    p_client->config.frame_cfg.max_width =
      (uint32_t)p_fp_stream->input_dim.width;
    p_client->config.frame_cfg.max_height =
      (uint32_t)p_fp_stream->input_dim.height;
  } else {
    p_client->config.frame_cfg.max_width = MAX_FD_WIDTH;
    p_client->config.frame_cfg.max_height = MAX_FD_HEIGHT;
  }
  p_client->config.frame_cfg.min_stride =
    p_client->selected_size.min_stride;
  p_client->config.frame_cfg.min_scanline =
    p_client->selected_size.min_scanline;
#else
  p_client->config.frame_cfg = p_client->selected_size;
#endif

  if (p_client->p_fd_chromatix->region_filter.enable)
    module_faceproc_client_get_region_info(p_client);

  IDBG_MED("%s:%d] max dim %dx%d", __func__, __LINE__,
    p_client->config.frame_cfg.max_width,
    p_client->config.frame_cfg.max_height);

  p_client->config.face_cfg.face_orientation_hint =
    FD_FACE_ORIENTATION_UNKNOWN;

  /* Set orientation to unknown if device orientation is not used */
  if (p_client->use_device_orientation == FALSE) {
    p_client->config.face_cfg.face_orientation_hint =
      module_imglib_common_get_prop(
      "persist.camera.imglib.orient", "4");
  }


  /* Set flag is sw assisted fd will be used */
  if (p_client->use_sw_assisted_fd == TRUE) {
    p_client->config.face_cfg.use_sw_assisted_fd = 1;
  }

  /* Initialize output transition info */
  p_fp_stream->out_trans_info.h_scale  = 1.0;
  p_fp_stream->out_trans_info.v_scale  = 1.0;
  p_fp_stream->out_trans_info.h_offset = 0;
  p_fp_stream->out_trans_info.v_offset = 0;

#ifdef FD_USE_PROP
  if (p_client->config.fd_feature_mask & FACE_PROP_MIN_FACE_SIZE) {
    int face_size = (p_client->config.fd_feature_mask &
      FACE_PROP_MIN_FACE_SIZE) >> FACE_PROP_MIN_FACE_SIZE_SHIFT;
    switch (face_size) {
    case 1:
      p_client->p_fd_chromatix->min_face_size = 25;
      break;
    case 2:
      p_client->p_fd_chromatix->min_face_size = 100;
      break;
    case 3:
      p_client->p_fd_chromatix->min_face_size = 200;
      break;
    }
    p_client->p_fd_chromatix->min_face_adj_type = FD_FACE_ADJ_FIXED;
  }
#endif

  rc = IMG_COMP_SET_PARAM(p_comp, QWD_FACEPROC_MODE, (void *)&p_client->mode);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    return rc;
  }

  rc = IMG_COMP_SET_PARAM(p_comp, QWD_FACEPROC_CFG, (void *)&p_client->config);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    return rc;
  }
  IDBG_MED("%s:%d]from module to comp XXXmin_face_size %d orient %d",
    __func__, __LINE__,
    p_client->p_fd_chromatix->min_face_size,
    p_client->config.face_cfg.face_orientation_hint);

  rc = IMG_COMP_SET_PARAM(p_comp, QWD_FACEPROC_CHROMATIX,
    (void *)p_client->p_fd_chromatix);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    return rc;
  }

  rc = IMG_COMP_SET_PARAM(p_comp, QWD_FACEPROC_DUMP_DATA,
    &p_client->dump_mode);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    return rc;
  }

  p_client->frame_batch_info.batch_mode =
    p_client->p_fd_chromatix->enable_frame_batch_mode;
  if (FACE_FRAME_BATCH_MODE_ON == p_client->frame_batch_info.batch_mode) {
    p_client->frame_batch_info.frame_batch_size =
      p_client->p_fd_chromatix->frame_batch_size;
  }
  rc = IMG_COMP_SET_PARAM(p_comp, QWD_FACEPROC_BATCH_MODE_DATA,
    &p_client->frame_batch_info);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
  }
  rc = IMG_COMP_START(p_comp, NULL);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] create failed %d", __func__, __LINE__, rc);
    return rc;
  }
  IDBG_MED("%s:%d] X rc %d", __func__, __LINE__, rc);
  return rc;
}

/**
 * Function: module_faceproc_client_thread_func
 *
 * Description: This function represents the event thread for
 *             the faceproc modules
 *
 * Arguments:
 *   @data: faceproc client
 *
 * Return values:
 *     NULL
 *
 * Notes: none
 **/
void *module_faceproc_client_thread_func(void *data)
{
  int rc = IMG_SUCCESS;
  faceproc_client_t *p_client = (faceproc_client_t *)data;

  /*signal the base class*/
  pthread_mutex_lock(&p_client->mutex);
  pthread_cond_signal(&p_client->cond);
  pthread_mutex_unlock(&p_client->mutex);
  pthread_setname_np(p_client->threadid, "CAM_fd_client");

  rc = module_faceproc_client_exec(p_client);
  if (IMG_ERROR(rc)) {
    p_client->status = rc;
    return NULL;
  }

  /* event handler at module level?? */
  return NULL;
}

/**
 * Function: module_faceproc_comp_destroy
 *
 * Description: This function is used to destroy the faceproc
 * client component
 *
 * Arguments:
 *   @p_comp: pointer to the faceproc component that needs to be destroyed
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_comp_destroy(img_component_ops_t *p_comp)
{
  int rc = IMG_SUCCESS;

  if (!p_comp->handle) {
    /* comp already destroyed */
    goto end;
  }

  rc = IMG_COMP_DEINIT(p_comp);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] deinit failed %d", __func__, __LINE__, rc);
    goto end;
  }

  p_comp->handle = NULL;

end:
  return rc;
}

/**
 * Function: module_faceproc_client_comp_destroy
 *
 * Description: This function is used to destroy all the faceproc
 * client components that were intialized
 *
 * Arguments:
 *   @p_client: pointer to the faceproc client
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_comp_destroy(faceproc_client_t *p_client)
{
  img_component_ops_t *p_comp = NULL;
  int rc = IMG_ERR_GENERAL;
  faceproc_engine_t eng_type;

  if (!p_client) {
    IDBG_ERROR("%s:%d] Null pointer detected", __func__, __LINE__);
    goto end;
  }

  for (eng_type = FACEPROC_ENGINE_HW; eng_type < FACEPROC_ENGINE_MAX;
    eng_type++) {
    p_comp = &p_client->comps[eng_type];
    if (p_comp->handle) {
      IDBG_HIGH("%s:%d] -- Destroying %d Comp", __func__, __LINE__, eng_type);
      rc = module_faceproc_comp_destroy(p_comp);
    }
  }

  p_client->state = IMGLIB_STATE_IDLE;

end:
  return rc;
}

/**
 * Function: module_faceproc_comp_create
 *
 * Description: This function is used to create the faceproc
 * client component
 *
 * Arguments:
 *   @p_client: pointer to the faceproc client
 *   @p_core_ops: pointer to the faceproc component handle
 *   @p_comp: ptr to the faceproc client component that is being initialized
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_comp_create(faceproc_client_t *p_client,
  img_core_ops_t *p_core_ops, img_component_ops_t *p_comp)
{
  int rc = IMG_ERR_GENERAL;

  if (p_comp->handle) {
    /* comp already created */
    IDBG_HIGH("%s:%d] comp already created", __func__, __LINE__);
    rc = IMG_SUCCESS;
  } else {
    rc = IMG_COMP_CREATE(p_core_ops, p_comp);
    if (IMG_ERROR(rc)) {
      IDBG_HIGH("%s:%d] could not create component %d",
        __func__, __LINE__, rc);
      goto failure;
    }

    rc = IMG_COMP_INIT(p_comp, p_client, NULL);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] init failed %d", __func__, __LINE__, rc);
      goto failure;
    }

    rc = IMG_COMP_SET_CB(p_comp, module_faceproc_client_event_handler);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
      goto failure;
    }
    IDBG_MED("%s:%d] create comp success", __func__, __LINE__);
  }

  return IMG_SUCCESS;

failure:
  // Error cases, Need to destroy comp (reverse of CREATE, INIT)?
  IDBG_WARN("create comp failed");
  return rc;
}

/**
 * Function: module_faceproc_client_comp_create
 *
 * Description: This function is used to create all the faceproc
 * client components that are supported
 *
 * Arguments:
 *   @p_client: pointer to the faceproc client
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_comp_create(faceproc_client_t *p_client)
{
  int rc = IMG_ERR_GENERAL;
  img_core_ops_t *p_core_ops;
  img_component_ops_t *p_comp;
  module_faceproc_t *p_mod;
  faceproc_engine_t eng_type;

  if (!p_client) {
    IDBG_ERROR("%s:%d] Null pointer detected", __func__, __LINE__);
    goto end;
  }
  // Get the module handle
  p_mod = (module_faceproc_t *)p_client->p_mod;
  if (!p_mod) {
    IDBG_ERROR("%s:%d] Faceproc Module handle is NULL", __func__, __LINE__);
    goto end;
  }

  if (p_client->state == IMGLIB_STATE_INIT) {
    IDBG_HIGH("%s:%d] comp already created", __func__, __LINE__);
    /* comp already created */
    rc = IMG_SUCCESS;
    goto end;
  }

  for (eng_type = FACEPROC_ENGINE_HW; eng_type < FACEPROC_ENGINE_MAX;
    eng_type++) {
    // create component
    p_core_ops = &p_mod->core_ops[eng_type];
    p_comp = &p_client->comps[eng_type];

    if (p_core_ops->create) {
      rc = module_faceproc_comp_create(p_client, p_core_ops, p_comp);
      if (IMG_SUCCEEDED(rc)) {
        p_client->num_comp++;
        IDBG_HIGH("%s:%d] Comp creation success, eng_type %d %p",
          __func__, __LINE__, eng_type, p_comp->handle);
      }
    }
  }

  if (p_client->set_mode) {
    p_client->set_mode = FALSE;
    module_faceproc_client_set_mode(p_client, p_client->new_mode, FALSE);
  }
  rc = IMG_SUCCESS;
  p_client->state = IMGLIB_STATE_INIT;

end:
  return rc;
}

/**
 * Function: module_faceproc_client_set_fd_chromatix
 *
 * Description: Set faceproc chromatix pointer.
 *
 * Arguments:
 *   @p_client: Pointer to faceproc client
 *
 * Return values:
 *     none
 **/
static fd_chromatix_t *module_faceproc_client_chromatix(
   faceproc_client_t *p_client)
{
  fd_chromatix_t *p_fd_chromatix = NULL;
  boolean use_lite = FALSE;
  boolean use_video = FALSE;
  module_faceproc_t *p_mod = (module_faceproc_t *)p_client->p_mod;

  if ((p_client->mode == FACE_DETECT_LITE) ||
    ((p_client->use_fd_lite_for_front_camera == TRUE) &&
    (p_mod != NULL) &&
    (p_mod->session_data.position == CAM_POSITION_FRONT))) {
    IDBG_MED("%s:%d] Using fd_lite for front camera", __func__, __LINE__);
    use_lite = TRUE;
  }

  if ((p_client->use_video_chromatix == TRUE) &&
    (p_client->video_mode == TRUE)) {
    IDBG_MED("%s:%d] Using video chromatix", __func__, __LINE__);
    use_video = TRUE;
  }

  switch (p_client->engine) {
  case FACEPROC_ENGINE_HW:
    if (use_video) {
      p_fd_chromatix = (use_lite == TRUE) ?
        &g_hw_detection_video_lite_chromatix : &g_hw_detection_video_chromatix;
    } else if (p_client->enable_bsgc) {
      p_fd_chromatix = &g_hw_bsgc_chromatix;
    } else if (p_client->multi_client) {
      p_fd_chromatix = (use_lite == TRUE) ?
        &g_hw_detection_multi_lite_chromatix : &g_hw_detection_multi_chromatix;
    } else {
      p_fd_chromatix = (use_lite == TRUE) ?
        &g_hw_detection_lite_chromatix : &g_hw_detection_chromatix;
    }
    break;
  case FACEPROC_ENGINE_SW:
  default:
    if (use_video) {
      p_fd_chromatix = (use_lite == TRUE) ?
        &g_sw_detection_video_lite_chromatix : &g_sw_detection_video_chromatix;
    } else if (p_client->enable_bsgc) {
      p_fd_chromatix = &g_bsgc_chromatix;
    } else if (p_client->multi_client) {
      p_fd_chromatix = (use_lite == TRUE) ?
        &g_sw_detection_multi_lite_chromatix : &g_sw_detection_multi_chromatix;
    } else {
      p_fd_chromatix = (use_lite == TRUE) ?
        &g_sw_detection_lite_chromatix : &g_sw_detection_chromatix;
    }
    break;
  }

  return p_fd_chromatix;
}

/**
 * Function: module_faceproc_client_update_chromatix
 *
 * Description: Update chromatix.
 *
 * Arguments:
 *   @p_client: faceproc client
 *
 * Return values:
 *     IMG ret codes
 *
 * NOTE: Function is not protected use with locked p_client.
 **/
static int module_faceproc_client_update_chromatix(faceproc_client_t *p_client)
{
  fd_chromatix_t *p_fd_chromatix = NULL;
  int ret;

  IDBG_MED("%s:%d] xxmin ", __func__, __LINE__);
  p_fd_chromatix = module_faceproc_client_chromatix(p_client);
  if (!p_fd_chromatix) {
    IDBG_MED("%s:%d] NULL chromatix pointer", __func__, __LINE__);
    return IMG_ERR_GENERAL;
  }

  IDBG_LOW("%s:%d] Chromatix update multi client %d engine %d mode %d bsgc %d",
    __func__, __LINE__, p_client->multi_client,
    p_client->engine, p_client->mode, p_client->enable_bsgc);

  if (p_client->p_fd_chromatix != p_fd_chromatix) {
    ret = IMG_COMP_SET_PARAM(module_faceproc_client_get_current_comp(p_client),
      QWD_FACEPROC_CHROMATIX, (void *)p_fd_chromatix);
    if (IMG_ERROR(ret)) {
      IDBG_MED("%s:%d] Fail set chromatix rc %d", __func__, __LINE__, ret);
      return ret;
    }
    p_client->p_fd_chromatix = p_fd_chromatix;
    p_client->frame_skip_cnt = p_fd_chromatix->frame_skip;
    p_client->initial_frame_no_skip_cnt =
      p_fd_chromatix->initial_frame_no_skip_cnt;
    p_client->current_count = 0;
    p_client->current_frame_cnt = 0;

    IDBG_MED("%s:%d] Chromatix update multi client %d engine %d"
      " mode %d bsgc %d",
      __func__, __LINE__, p_client->multi_client,
      p_client->engine, p_client->mode, p_client->enable_bsgc);
  }

  return IMG_SUCCESS;
}
/**
 * Function: module_faceproc_client_create
 *
 * Description: This function is used to create the faceproc client
 *
 * Arguments:
 *   @p_mct_mod: mct module pointer
 *   @p_port: mct port pointer
 *   @identity: identity of the stream
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_create(mct_module_t *p_mct_mod, mct_port_t *p_port,
  uint32_t identity, mct_stream_info_t *stream_info)
{
  IMG_UNUSED(identity);
  IMG_UNUSED(stream_info);

  int rc = IMG_SUCCESS;
  faceproc_client_t *p_client = NULL;

  module_faceproc_t *p_mod = (module_faceproc_t *)p_mct_mod->module_private;
  int fd_feature_mask = 0;
  uint32_t total_delay;
  uint32_t i = 0;

  IDBG_MED("%s:%d]", __func__, __LINE__);
  p_client = (faceproc_client_t *)malloc(sizeof(faceproc_client_t));
  if (NULL == p_client) {
    IDBG_ERROR("%s:%d] client alloc failed", __func__, __LINE__);
    return IMG_ERR_NO_MEMORY;
  }

  // Todo : Check this again. We need either a re-entrant mutex or a separate
  // mutex to control current_comp_idx, curr_comp_connection_lost variables.

  // Create a re-entrant mutex
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);

  /* initialize the variables */
  memset(p_client, 0x0, sizeof(faceproc_client_t));
  pthread_mutex_init(&p_client->mutex, &attr);
  pthread_cond_init(&p_client->cond, NULL);
  pthread_mutex_init(&p_client->result_mutex, NULL);
  p_client->threadid = -1;
  p_client->state = IMGLIB_STATE_IDLE;
  p_client->sync = MOD_FACEPROC_SYNC;
  p_client->engine = p_mod->engine;
  p_client->use_device_orientation = TRUE;
  p_client->use_sw_assisted_fd = FALSE;
  p_client->dump_mode = FACE_FRAME_DUMP_OFF;
  total_delay = p_mod->session_data.max_pipeline_frame_applying_delay +
    p_mod->session_data.max_pipeline_meta_reporting_delay;
  p_client->frame_batch_info.batch_mode = FACE_FRAME_BATCH_MODE_OFF;
  p_client->frame_batch_info.frame_batch_size = 1;

  if (p_client->engine >= FACEPROC_ENGINE_MAX) {
    IDBG_ERROR("%s:%d] Error invalid mode %d", __func__, __LINE__,
      p_client->engine);
    rc = IMG_ERR_NO_MEMORY;
    goto error;
  }
  p_client->p_fpc_q = malloc(sizeof(img_queue_t) * (total_delay + 1));
  if (NULL == p_client->p_fpc_q) {
    IDBG_ERROR("%s:%d] p_client->p_fpc_q alloc failed", __func__, __LINE__);
    rc = IMG_ERR_NO_MEMORY;
    goto error;
  }
  p_client->fpc_q_size = total_delay + 1;

  for (i = 0; i < p_client->fpc_q_size; i++) {
    /* initialize the queue */
    img_q_init(&p_client->p_fpc_q[i], "");
  }

  for (i = 0; i < MAX_FD_STREAM; i++) {
    p_client->fp_stream[i].buffer_info.p_buffer = malloc(MAX_NUM_FRAMES *
    sizeof(mod_img_buffer_t));
    if (NULL == p_client->fp_stream[i].buffer_info.p_buffer) {
      IDBG_ERROR("%s:%d] buffer %d alloc failed", __func__, __LINE__, i);
      rc = IMG_ERR_NO_MEMORY;
      goto error;
    }
    memset(p_client->fp_stream[i].buffer_info.p_buffer, 0x0, MAX_NUM_FRAMES *
      sizeof(mod_img_buffer_t));
    p_client->fp_stream[i].buffer_info.total_count = MAX_NUM_FRAMES;
    p_client->fp_stream[i].buffer_info.fd_buf_count = MAX_NUM_FD_FRAMES;
  }

  for (i = 0; i < MAX_NUM_FD_FRAMES; i++) {
    pthread_mutex_init(&p_client->divert_holder[i].mutex, NULL);
  }

  p_port->port_private = p_client;
  p_client->buf_idx = 0;
  p_client->p_mod = p_mod;
  p_client->fd_async = FALSE;
  p_client->current_frame_cnt = 0;
  faceproc_reset_client_result(p_client);

  /* enable the features based on property */
  p_client->enable_bsgc = module_imglib_common_get_prop(
    "persist.camera.imglib.bsgc", "0");

  /* Todo: If module is set as source we will do face registration */
  if (p_mod->module_type == MCT_MODULE_FLAG_SOURCE) {
    p_client->mode = FACE_REGISTER;
    p_client->p_fd_chromatix = &g_recognition_chromatix;
  } else {
    p_client->mode = FACE_DETECT_OFF;
    p_client->p_fd_chromatix = module_faceproc_client_chromatix(p_client);
  }
  p_client->frame_skip_cnt = p_client->p_fd_chromatix->frame_skip;
  p_client->initial_frame_no_skip_cnt =
    p_client->p_fd_chromatix->initial_frame_no_skip_cnt;
  p_client->fpc_mode = p_client->mode;
  p_client->multi_client = FALSE;
  p_client->stats_filter.hold_cnt =
    p_client->p_fd_chromatix->stats_filter_max_hold;
  p_client->stats_filter.locked = p_client->p_fd_chromatix->stats_filter_lock;

  // Get the featurue mask from property
#ifdef FD_USE_PROP
  char value[32];
  property_get("debug.sf.fdfeature", value, "0");
  fd_feature_mask = atoi(value);
  if (fd_feature_mask > 0) {
    p_client->config.fd_feature_mask = fd_feature_mask;
  } else {
    p_client->config.fd_feature_mask = FACE_PROP_DEFAULT;
  }
#else
  p_client->config.fd_feature_mask = FACE_PROP_DEFAULT;
#endif

#ifdef _ANDROID_
  char prop[PROPERTY_VALUE_MAX];
  int atoi_val;

  property_get("persist.camera.imglib.face_all", prop, "1");
  /* If all angles search is used disable device usage of device orientation */
  if (atoi(prop)) {
    p_client->use_device_orientation = FALSE;
  }

  property_get("persist.camera.imglib.face_sw", prop, "0");
  if (atoi(prop)) {
    p_client->use_sw_assisted_fd = TRUE;
  }

  // Whether to use video specific chromatix for fd in video mode
  property_get("persist.camera.usefdvideocrtx", prop, "1");
  if (atoi(prop)) {
    p_client->use_video_chromatix = TRUE;
  }

  // Whether to use lite chromatix for front camera
  property_get("persist.camera.imglib.usefdlite", prop, "0");
  if (atoi(prop)) {
    p_client->use_fd_lite_for_front_camera = TRUE;
  }

  /* Get dump mode property */
  property_get("persist.camera.fd.dump", prop, "-1");
  atoi_val = atoi(prop);
  if (atoi_val >= 0) {
    switch (atoi_val) {
    case 0:
      p_client->dump_mode = FACE_FRAME_DUMP_OFF;
      break;
    case 1:
      p_client->dump_mode = FACE_FRAME_DUMP_NON_TRACKED;
      break;
    case 2:
      p_client->dump_mode = FACE_FRAME_DUMP_TRACKED;
      break;
    case 3:
      p_client->dump_mode = FACE_FRAME_DUMP_NOT_DETECTED;
      break;
    case 4:
      p_client->dump_mode = FACE_FRAME_DUMP_ALL;
      break;
    case 5:
      p_client->dump_mode = FACE_FRAME_DUMP_NUMBERED_SET;
      break;
    default:
      p_client->dump_mode = FACE_FRAME_DUMP_OFF;
    }
  }
#endif

  p_client->config.fd_face_info_mask = FACE_INFO_MASK_FACE_DETECT;
  p_client->fd_ui_mode = CAM_FACE_DETECT_MODE_SIMPLE;

  IDBG_HIGH("%s:%d] Face proc feature mask %x %x enable_bsgc %d"
    , __func__, __LINE__,
    p_client->config.fd_feature_mask, fd_feature_mask,
    p_client->enable_bsgc);

  IDBG_MED("%s:%d] port %p client %p X", __func__, __LINE__, p_port, p_client);
  return rc;

error:
  if (p_client) {
    module_faceproc_client_destroy(p_client, identity);
    p_client = NULL;
  }
  return rc;
}

/**
 * Function: module_faceproc_client_configure_buffers
 *
 * Description: This function is used to configure the buffers
 * dimensions for each stream
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @p_stream_info: Stream info
 *   @s_idx: Stream index
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_configure_buffers(faceproc_client_t *p_client,
  mct_stream_info_t *p_stream_info,
  int s_idx)
{
  mct_stream_info_t *stream_info;
  mod_img_buffer_info_t *p_buf_info;
  faceproc_stream_t *p_fp_stream;

  if (!p_client)
    return IMG_ERR_INVALID_INPUT;

  if (p_client->state == IMGLIB_STATE_IDLE) {
    IDBG_ERROR("%s:%d] client not in proper state %d",
      __func__, __LINE__, p_client->state);
    return IMG_SUCCESS;
  }

  p_fp_stream = &p_client->fp_stream[s_idx];
  if (p_fp_stream->mapped) {
    IDBG_MED("%s:%d] stream already mapped %x", __func__, __LINE__,
      p_client->fp_stream[s_idx].identity);
    return IMG_SUCCESS;
  }

  stream_info = p_fp_stream->stream_info;

  if (p_stream_info) {
    p_fp_stream->input_dim.width = (uint32_t)p_stream_info->dim.width;
    p_fp_stream->input_dim.height = (uint32_t)p_stream_info->dim.height;
    p_fp_stream->input_dim.stride =
      p_stream_info->buf_planes.plane_info.mp[0].stride;
    p_fp_stream->input_dim.scanline =
      p_stream_info->buf_planes.plane_info.mp[0].scanline;
    p_fp_stream->input_fmt = p_stream_info->fmt;
  } else {
    p_fp_stream->input_dim.width = (uint32_t)stream_info->dim.width;
    p_fp_stream->input_dim.height = (uint32_t)stream_info->dim.height;
    p_fp_stream->input_dim.stride =
      stream_info->buf_planes.plane_info.mp[0].stride;
    p_fp_stream->input_dim.scanline =
      stream_info->buf_planes.plane_info.mp[0].scanline;
    p_fp_stream->input_fmt = stream_info->fmt;
  }

  if (p_fp_stream->input_dim.width > MAX_FD_WIDTH ||
    p_fp_stream->input_dim.height > MAX_FD_HEIGHT) {
    IDBG_ERROR("%s:%d] Exceeded max size %dx%d %dx%d", __func__, __LINE__,
      p_fp_stream->input_dim.width, p_fp_stream->input_dim.height,
      MAX_FD_WIDTH, MAX_FD_HEIGHT);
    return IMG_ERR_INVALID_INPUT;
  }

  p_buf_info = &p_client->fp_stream[s_idx].buffer_info;
  p_buf_info->buf_count = 0;

  if (p_client->mode == FACE_REGISTER) {
    mct_list_traverse(stream_info->img_buffer_list, mod_imglib_map_fr_buffer,
      p_buf_info);
  } else {
    mct_list_traverse(stream_info->img_buffer_list, mod_imglib_map_fd_buffer,
      p_buf_info);
  }
  p_fp_stream->buff_configured = 1;

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_map_buffers
 *
 * Description: This function is used to map the buffers when
 * the stream is started
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @s_idx: Stream index
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_map_buffers(faceproc_client_t *p_client,
  int s_idx)
{
  faceproc_stream_t *p_fp_stream;
  faceproc_frame_cfg_t frame_cfg;
  int scale_need, stride_corr;
  int fmt_conv, use_int_bufs;
  int rc, i;
  int length;
  img_buf_type_t buff_type;
  boolean alloc_buff = FALSE;

  p_fp_stream = &p_client->fp_stream[s_idx];
  if (!p_fp_stream) {
    IDBG_ERROR("%s %d]: Cannot find stream mapped for index %d",
      __func__, __LINE__, s_idx);
    return IMG_ERR_GENERAL;
  }
  /* Skip if this is not processing buffer */
  if (!p_fp_stream->processing_stream) {
    return IMG_SUCCESS;
  }

  /* If stream is already mapped prepare client for start*/
  if (p_fp_stream->mapped) {
    p_client->state = IMGLIB_STATE_START_PENDING;
    return IMG_SUCCESS;
  }

  /* Check component supported resolution */
  frame_cfg.max_width = p_fp_stream->input_dim.width;
  frame_cfg.max_height = p_fp_stream->input_dim.height;
  frame_cfg.min_stride = p_fp_stream->input_dim.stride;
  frame_cfg.min_scanline = p_fp_stream->input_dim.scanline;

  rc = IMG_COMP_SET_PARAM(module_faceproc_client_get_current_comp(p_client),
    QWD_FACEPROC_TRY_SIZE, (void *)&frame_cfg);
  IDBG_MED("%s:%d] input dim width %d, height %d, stride %d, scanline %d",
    __func__, __LINE__, frame_cfg.max_width, frame_cfg.max_height,
    frame_cfg.min_stride, frame_cfg.min_scanline);

  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    goto error;
  }
  p_client->selected_size = frame_cfg;
  p_fp_stream->mapped = 1;

  /* Check if scaling is needed */
  scale_need = module_faceproc_client_scaling_need(&frame_cfg, p_fp_stream);
  /* Check if stride correction is needed */
  stride_corr = module_faceproc_client_stride_corr_need(&frame_cfg,
    p_fp_stream->input_dim.stride);
  /* Check if format conversion is needed */
  fmt_conv = module_faceproc_client_format_conv_need(p_fp_stream);
  if (fmt_conv < 0) {
    IDBG_ERROR("%s:%d] Format not supported", __func__, __LINE__);
    goto error;
  }
  /* Check if internal buffers are needed */
  use_int_bufs = module_faceproc_client_use_int_bufs(p_client, p_fp_stream);

  /* Temp for sw face detection(heap buff type) use
    internal buffers */
  if (scale_need || stride_corr || fmt_conv || use_int_bufs) {
    length = module_faceproc_client_get_bufsize(&frame_cfg, p_fp_stream);

    /* Request from component buffer type */
    rc = IMG_COMP_GET_PARAM(module_faceproc_client_get_current_comp(p_client),
      QWD_FACEPROC_BUFF_TYPE, (void *)&buff_type);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
      goto error;
    }

    /* Allocate memory for stream buffers */
    for (i = 0; i < MAX_NUM_FD_FRAMES; i++) {
      IDBG_MED("%s:%d] Allocating from buff_type %d size %d rc %d",
        __func__, __LINE__, (int) buff_type ,length,  rc);
      rc = img_buffer_get(buff_type, -1, 1, length,
        &p_client->faceproc_mem_handle[i]);
      if (rc != IMG_SUCCESS) {
        IDBG_ERROR("%s:%d] Img buffer get failed", __func__, __LINE__);
        goto error_alloc;
      }
    }
    alloc_buff = TRUE;
    p_fp_stream->use_int_bufs = TRUE;
  }

#ifdef USE_OFFLINE_BUFFER
  p_client->offline_buf_count =
    img_common_get_prop("persist.camera.fd.offline.ct", "0");
  IDBG_MED("%s %d: [OFFLINE_FD]Use offlne buffer, file count %d",
    __func__, __LINE__, p_client->offline_buf_count);
  if (p_client->offline_buf_count > 0) {
      rc = mod_imglib_read_file("file",
        p_client->offline_buf_count, p_client->faceproc_offline_bufs);
      if (IMG_SUCCESS != rc) {
        IDBG_ERROR("%s:%d: Error reading offline buffers", __func__, __LINE__);
        p_client->offline_buf_count = 0;
      }
  } else {
    IDBG_MED("%s:%d: offline buffer count is 0", __func__, __LINE__);
  }
#endif

  /* Initialize buffer divert structure */
  memset(p_client->divert_holder, 0x00, sizeof(p_client->divert_holder));
  p_client->state = IMGLIB_STATE_START_PENDING;

  IDBG_HIGH("%s:%d] dim %dx%d alloc_buff %d", __func__, __LINE__,
    p_fp_stream->input_dim.width, p_fp_stream->input_dim.height,
    alloc_buff);

  return IMG_SUCCESS;

error:
  p_fp_stream->mapped = 0;
  return IMG_ERR_NO_MEMORY;
error_alloc:
  for (; i > 0; i--) {
    img_buffer_release(&p_client->faceproc_mem_handle[i - 1]);
  }
  return IMG_ERR_NO_MEMORY;
}

/**
 * Function: module_faceproc_client_unmap_buffers
 *
 * Description: This function is used to unmap the buffers when
 * the stream is started
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @identity: Identity of the stream
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_unmap_buffers(faceproc_client_t *p_client,
  uint32_t identity)
{
  uint32_t i = 0;
  int s_idx = 0;

  /* When face registration is used buffers are not allocated */
  if (p_client->mode == FACE_REGISTER) {
    p_client->fp_stream[0].buffer_info.buf_count = 0;
    return IMG_SUCCESS;
  }

  s_idx = faceproc_get_stream_by_id(p_client, identity);
  if (s_idx < 0) {
    IDBG_ERROR("%s %d:] Cannot find stream idn for identity 0x%x", __func__,
      __LINE__, identity);
    return IMG_ERR_GENERAL;
  }

  if (!p_client->fp_stream[s_idx].mapped) {
    return IMG_SUCCESS;
  }

  for (i = 0; i < MAX_NUM_FD_FRAMES; i++) {
    if (p_client->faceproc_mem_handle[i].handle) {
      if (IMG_SUCCESS !=
        img_buffer_release(&p_client->faceproc_mem_handle[i])) {
        IDBG_ERROR("%s:%d:] Img buffer release failed",
        __func__, __LINE__);
        return IMG_ERR_GENERAL;
      }
    }
  }

    for (i = 0; i < MAX_NUM_OFFLINE_FD_FRAMES; i++) {
    if (p_client->faceproc_offline_bufs[i].handle) {
      if (IMG_SUCCESS !=
        img_buffer_release(&p_client->faceproc_offline_bufs[i])) {
        IDBG_ERROR("%s:%d:] Img buffer release failed",
        __func__, __LINE__);
        return IMG_ERR_GENERAL;
      }
    }
  }

  for (i = 0; i < p_client->ref_count; i++) {
    p_client->fp_stream[i].buffer_info.buf_count = 0;
  }
  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_set_scale_ratio
 *
 * Description: Set face detection scale ratio.
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @stream_crop: Stream crop event structure
 *
 * Return values:
 *     imaging error values
 *
 * Notes: Calculate just scale ratio which we assume that will be applied
 *  to get preview resolution.
 **/
int module_faceproc_client_set_scale_ratio(faceproc_client_t *p_client,
  mct_bus_msg_stream_crop_t *stream_crop,
  int s_idx)
{
  int rc = IMG_SUCCESS;

  faceproc_stream_t *p_fp_stream;

  if (!(p_client && p_client->fp_stream[s_idx].stream_info) || !stream_crop) {
    IDBG_ERROR("%s:%d] Error invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  p_fp_stream = &p_client->fp_stream[s_idx];

  if (!((stream_crop->x >= (uint32_t)p_fp_stream->input_dim.width) ||
    (stream_crop->y >= (uint32_t)p_fp_stream->input_dim.height) ||
    (stream_crop->crop_out_x == 0) ||
    (stream_crop->crop_out_y == 0))) {
    p_fp_stream->crop_info.pos.x = (int32_t)stream_crop->x;
    p_fp_stream->crop_info.pos.y = (int32_t)stream_crop->y;
    p_fp_stream->crop_info.size.width = (int32_t)stream_crop->crop_out_x;
    p_fp_stream->crop_info.size.height= (int32_t)stream_crop->crop_out_y;
  } else {
    p_fp_stream->crop_info.pos.x = 0;
    p_fp_stream->crop_info.pos.y = 0;
    p_fp_stream->crop_info.size.width = p_fp_stream->input_dim.width;
    p_fp_stream->crop_info.size.height = p_fp_stream->input_dim.height;
  }

  IDBG_MED("%s:%d] [FD_HAL3] CropOrig (%d %d %d %d) s_idx %d",
    __func__, __LINE__,
    stream_crop->x,
    stream_crop->y,
    stream_crop->crop_out_x,
    stream_crop->crop_out_y,
    s_idx);

  IDBG_MED("%s:%d] [FD_HAL3] Crop (%d %d %d %d) process %d report %d",
    __func__, __LINE__,
    p_fp_stream->crop_info.pos.x,
    p_fp_stream->crop_info.pos.y,
    p_fp_stream->crop_info.size.width,
    p_fp_stream->crop_info.size.height,
    p_fp_stream->processing_stream,
    p_fp_stream->reporting_stream);

  if (!IMG_IS_HAL3(p_client) && (p_fp_stream->input_dim.width >
    (uint32_t)p_fp_stream->crop_info.size.width)) {

    p_fp_stream->out_trans_info.h_scale =
      (float)p_fp_stream->input_dim.width /
      (float)p_fp_stream->crop_info.size.width;
    p_fp_stream->out_trans_info.h_offset =
      (uint32_t)p_fp_stream->crop_info.pos.x;

  } else {
    p_fp_stream->out_trans_info.h_scale = 1.0;
    p_fp_stream->out_trans_info.h_offset = 0;
  }

  if (!IMG_IS_HAL3(p_client) && (p_fp_stream->input_dim.height >
    (uint32_t)p_fp_stream->crop_info.size.height)) {

    p_fp_stream->out_trans_info.v_scale =
      (float)p_fp_stream->input_dim.height /
      (float)p_fp_stream->crop_info.size.height;
    p_fp_stream->out_trans_info.v_offset =
      (uint32_t)p_fp_stream->crop_info.pos.y;
  } else {
    p_fp_stream->out_trans_info.v_scale = 1.0;
    p_fp_stream->out_trans_info.v_offset = 0;
  }

  p_fp_stream->camif_out_trans_info.h_scale =
    (float)stream_crop->width_map / (float)p_fp_stream->input_dim.width;
  p_fp_stream->camif_out_trans_info.v_scale =
    (float)stream_crop->height_map / (float) p_fp_stream->input_dim.height;
  p_fp_stream->camif_out_trans_info.h_offset = stream_crop->x_map;
  p_fp_stream->camif_out_trans_info.v_offset = stream_crop->y_map;

  IDBG_MED("%s:%d] [FD_HAL3] Map(%d %d %d %d)",
    __func__, __LINE__,
    stream_crop->width_map,
    stream_crop->height_map,
    stream_crop->x_map,
    stream_crop->y_map);
  IDBG_MED("%s:%d] [FD_HAL3] Scale(%f %f %d %d)  %d %d",
    __func__, __LINE__,
      p_fp_stream->camif_out_trans_info.h_scale,
      p_fp_stream->camif_out_trans_info.v_scale,
      p_fp_stream->camif_out_trans_info.h_offset,
      p_fp_stream->camif_out_trans_info.v_offset,
      p_fp_stream->processing_stream,
      p_fp_stream->reporting_stream);

  return rc;
}

/**
 * Function: module_faceproc_client_set_default_scale_ratio
 *
 * Description: Set face detection default scale ratio.
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @p_stream_info: Pointer to stream info
 *   @s_idx: Stream index
 *
 * Return values:
 *     imaging error values
 *
 * Notes: Calculate default scale ratio based on output and sensor dimension
 *  which we assume that will be applied to get preview resolution.
 **/
int module_faceproc_client_set_default_scale_ratio(faceproc_client_t *p_client,
    mct_stream_info_t *p_stream_info, int s_idx)
{
  mct_bus_msg_stream_crop_t crop;
  double sensor_ratio;
  double output_ratio;
  int rc;

  if (!p_client || !p_stream_info) {
    IDBG_ERROR("%s:%d] Error invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  memset (&crop, 0x00, sizeof(crop));

  crop.session_id = IMGLIB_SESSIONID(p_stream_info->identity);
  crop.stream_id = IMGLIB_STREAMID(p_stream_info->identity);

  crop.x = 0;
  crop.y = 0;
  crop.crop_out_x = p_stream_info->dim.width;
  crop.crop_out_y = p_stream_info->dim.height;

  /* Adjust input map to aspect ratio */
  sensor_ratio = (double)p_client->main_dim.width /
    (double)p_client->main_dim.height;

  output_ratio = (double)p_stream_info->dim.width /
    (double)p_stream_info->dim.height;

  if (sensor_ratio < output_ratio) {
    crop.x_map = 0;
    crop.width_map = p_client->main_dim.width;

    crop.height_map = (int32_t)((double)crop.width_map / output_ratio);
    crop.y_map = (p_client->main_dim.height - crop.height_map) / 2;
  } else {
    crop.y_map = 0;
    crop.height_map = p_client->main_dim.height;

    crop.width_map = (int32_t)((double)crop.height_map * output_ratio);
    crop.x_map = (p_client->main_dim.width - crop.width_map) / 2;
  }

  IDBG_MED("%s:%d]Map x,y=%dx%d size=%dx%d, Out size=%dx%d",
    __func__, __LINE__, crop.x_map, crop.y_map,
    crop.width_map, crop.height_map,
    crop.crop_out_x, crop.crop_out_y);

  rc = module_faceproc_client_set_scale_ratio(p_client, &crop, s_idx);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] Scale ratio Fail %d", __func__, __LINE__, rc);
  }

  return rc;
}

/**
 * Function: module_faceproc_client_start
 *
 * Description: This function is used to start the faceproc
 *              client
 *
 * Arguments:
 *   @p_client: faceproc client
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_start(faceproc_client_t *p_client)
{
  int rc = IMG_SUCCESS;
  int s_idx;

  IDBG_MED("%s:%d] E", __func__, __LINE__);

  memset(&p_client->last_stats, 0x0,
    sizeof(p_client->last_stats));
  memset(&p_client->stats_filter.mfi_last_updated, 0x0,
    sizeof(p_client->stats_filter.mfi_last_updated));

  p_client->stats_filter.hold_cnt =
    p_client->p_fd_chromatix->stats_filter_max_hold;
  p_client->stats_filter.locked =
    p_client->p_fd_chromatix->stats_filter_lock;

  if ((p_client->state != IMGLIB_STATE_START_PENDING) &&
    (p_client->state != IMGLIB_STATE_PROCESSING) &&
    (p_client->state != IMGLIB_STATE_STARTED)){
    IDBG_ERROR("%s:%d] invalid state %d",
      __func__, __LINE__, p_client->state);
    return IMG_ERR_INVALID_OPERATION;
  }

  /* create the client thread */
  if (FALSE == p_client->sync) {
    p_client->is_ready = FALSE;
    rc = pthread_create(&p_client->threadid, NULL,
       module_faceproc_client_thread_func,
      (void *)p_client);
    if (rc < 0) {
      IDBG_ERROR("%s:%d] pthread creation failed %d",
        __func__, __LINE__, rc);
      rc = IMG_ERR_GENERAL;
      goto error;
    }
    pthread_setname_np(p_client->threadid, "CAM_fd_client");

    if (FALSE == p_client->is_ready) {
      IDBG_MED("%s: before wait", __func__);
      pthread_cond_wait(&p_client->cond, &p_client->mutex);
    }
    IDBG_MED("%s: after wait", __func__);
    if (IMG_ERROR(p_client->status)) {
      rc = p_client->status;
      IDBG_ERROR("%s:%d] create failed %d", __func__, __LINE__, rc);
      goto error;
    }
    p_client->state = IMGLIB_STATE_PROCESSING;
    /* If processing stream is available enable buffer diverts */
    s_idx = faceproc_get_processing_stream_idx(p_client);
    if ((s_idx >= 0) && (s_idx < MAX_NUM_FD_FRAMES)) {
      rc = module_faceproc_client_req_divert(&p_client->fp_stream[s_idx], 1);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Fail to enable diverts", __func__, __LINE__);
      }
    }
  } else {
    rc = module_faceproc_client_exec(p_client);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] create failed %d", __func__, __LINE__, rc);
      goto error;
    }
    p_client->state = IMGLIB_STATE_PROCESSING;
    /* If processing stream is available enable buffer diverts */
    s_idx = faceproc_get_processing_stream_idx(p_client);
    if ((s_idx >= 0) && (s_idx < MAX_NUM_FD_FRAMES)) {
      rc = module_faceproc_client_req_divert(&p_client->fp_stream[s_idx], 1);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Fail to enable diverts", __func__, __LINE__);
      }
    }
  }
  IDBG_MED("%s:%d] X", __func__, __LINE__);

  return rc;

error:
  IDBG_MED("%s:%d] error %d X", __func__, __LINE__, rc);
  return rc;
}

/**
 * Function: module_faceproc_client_stop
 *
 * Description: This function is used to stop the faceproc
 *              client
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @identity: Identity of the stream
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_stop(faceproc_client_t *p_client, uint32_t identity)
{
  int rc = IMG_SUCCESS;
  img_component_ops_t *p_comp =
    module_faceproc_client_get_current_comp(p_client);
  int s_idx = 0, i;
  int is_client_active = 0;

  s_idx = faceproc_get_stream_by_id(p_client, identity);
  if (s_idx < 0) {
    IDBG_ERROR("%s %d:] Cannot find stream idn for identity 0x%x", __func__,
      __LINE__, identity);
    return IMG_ERR_GENERAL;
  }

  rc = IMG_COMP_ABORT(p_comp, NULL);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] create failed %d", __func__, __LINE__, rc);
    return rc;
  }

  /* Clear the result on stop client to prevent any additional operations */
  pthread_mutex_lock(&p_client->result_mutex);
  memset(p_client->result, 0x00, sizeof(p_client->result));
  faceproc_reset_client_result(p_client);
  pthread_mutex_unlock(&p_client->result_mutex);

  for (i = 0; i < MAX_FD_STREAM; i++) {
    if (p_client->fp_stream[i].streamon) {
      is_client_active = 1;
    }
  }

  if (!is_client_active) {
    p_client->state = IMGLIB_STATE_INIT;
    p_client->active = FALSE;
  }
  return rc;
}

/**
 * Function: module_faceproc_client_trans_from_stream
 *
 * Description: Calculate input translation from a given stream.
 *   This function is used if we want to map result from a given stream
 *   to out stream. Input translations to our stream  will
 *   be filled in this function.
 *
 * Arguments:
 *   @p_from: Stream we want to map to.
 *   @p_to_stream: Our stream.
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
static int module_faceproc_client_trans_from_stream(faceproc_stream_t *p_from,
  faceproc_stream_t *p_to_stream)
{
  if (!p_from || !p_to_stream) {
      IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
  }

  p_to_stream->input_trans_info.v_scale =
    p_from->input_trans_info.v_scale *
    (p_from->camif_out_trans_info.v_scale /
    p_to_stream->camif_out_trans_info.v_scale);

  p_to_stream->input_trans_info.h_scale =
    p_from->input_trans_info.h_scale *
    (p_from->camif_out_trans_info.h_scale /
      p_to_stream->camif_out_trans_info.h_scale);

  p_to_stream->input_trans_info.h_offset =
    p_from->camif_out_trans_info.h_offset -
    p_to_stream->camif_out_trans_info.h_offset;

  p_to_stream->input_trans_info.h_offset /=
    (p_to_stream->camif_out_trans_info.h_scale);

  p_to_stream->input_trans_info.v_offset =
    p_from->camif_out_trans_info.v_offset -
    p_to_stream->camif_out_trans_info.v_offset;

  p_to_stream->input_trans_info.v_offset /=
    (p_to_stream->camif_out_trans_info.v_scale);

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_send_info
 *
 * Description: This function is for get the active result and
 *    send it to the metadata and stats module
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @faces_data: populated face data
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
static int module_faceproc_client_send_info(faceproc_client_t *p_client,
  combined_cam_face_detection_data_t *faces_data)
{
  int index;
  faceproc_result_t filtered_result;
  faceproc_result_t algo_result;
  faceproc_result_t *p_out_result;
  img_rect_t *p_crop;
  mct_stream_info_t *p_stream_info;
  int s_idx;
  faceproc_stream_t *p_fp_stream;
  faceproc_stream_t *p_freport_stream;
  uint32_t i;
  int rc = IMG_SUCCESS;

  memset(&filtered_result, 0x0, sizeof(faceproc_result_t));

  pthread_mutex_lock(&p_client->result_mutex);
  index = img_rb_deque_idx(&p_client->result_rb);
  if (index < 0) {
    IDBG_MED("%s:%d] Invalid result idx %d (%d %d) cnt %d",
      __func__, __LINE__,
      index, p_client->result_rb.front,
      p_client->result_rb.rear,
      img_rb_count(&p_client->result_rb));
    rc = IMG_ERR_OUT_OF_BOUNDS;
    goto exit;
  }

  /* Get processing stream index */
  s_idx = p_client->result[index].client_id;
  if ((s_idx < 0) || (s_idx >= (int)p_client->ref_count)) {
    IDBG_MED("%s:%d] Invalid client ID s_idx %d cnt %d",
      __func__, __LINE__, s_idx,
      p_client->ref_count);
    rc = IMG_ERR_INVALID_OPERATION;
    goto exit;
  }
  p_fp_stream = &p_client->fp_stream[s_idx];

  /* Get reporting stream index */
  s_idx = faceproc_get_reporting_stream_idx(p_client);
  if ((s_idx < 0) || (s_idx >= (int)p_client->ref_count)) {
    IDBG_MED("%s:%d] Reporting stream is not ready idx %d ref %d",
      __func__, __LINE__, s_idx, p_client->ref_count);
    rc = IMG_ERR_INVALID_OPERATION;
    goto exit;
  }
  p_freport_stream = &p_client->fp_stream[s_idx];

  IDBG_MED("%s:%d] active index %d face_cnt %d s_idx %d",
    __func__, __LINE__, index,
    p_client->result[index].num_faces_detected,
    s_idx);

  if (!p_fp_stream->buff_configured) {
    IDBG_MED("%s:%d] Skip reporting stream not ready %d", __func__, __LINE__,
      p_fp_stream->buff_configured);
    rc = IMG_ERR_INVALID_OPERATION;
    goto exit;
  }

  /* If needed switch to reporting stream and translate coordinates */
  if (p_fp_stream != p_freport_stream) {
    rc = module_faceproc_client_trans_from_stream(p_fp_stream,
      p_freport_stream);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Error translating coordinates to stream %d",
        __func__, __LINE__, rc);
      goto exit;
    }
    p_fp_stream = p_freport_stream;
  }

  algo_result = p_client->result[index];
  p_out_result = &algo_result;
  p_crop = &p_fp_stream->crop_info;
  p_stream_info = p_fp_stream->stream_info;

  /* Scale results from processing stream to reporting stream because
    HAL1 and 3A module require faceinfo wrt reporting stream */
  IDBG_MED("%s:%d] Translate coordinates to stream %x [%f %f %d %d]",
    __func__, __LINE__, p_fp_stream->identity,
    p_fp_stream->input_trans_info.h_scale,
    p_fp_stream->input_trans_info.v_scale,
    p_fp_stream->input_trans_info.h_offset,
    p_fp_stream->input_trans_info.v_offset);
  for (i = 0; i < p_out_result->num_faces_detected; i++) {
    module_faceproc_client_scale_algo_result(&p_out_result->roi[i],
      &p_fp_stream->input_trans_info, 0, p_client);
  }

  /* Filter result coordinates */
  if (IMG_RECT_IS_VALID(p_crop, p_stream_info->dim.width,
    p_stream_info->dim.height)) {
    p_out_result = &filtered_result;
    module_faceproc_client_filter_roi(&algo_result, p_out_result, p_fp_stream);
    IDBG_MED("%s:%d] face_cnt %d", __func__, __LINE__,
      p_out_result->num_faces_detected);
  }

  p_client->use_old_stats = false;
  if (IMG_IS_HAL3(p_client)) {
    /* Send face Info to 3a only if face priority is set*/
    if(IMG_IS_FACE_PRIORITY_SET(p_client->fd_apply_mask)) {
      module_faceproc_client_send_roi_event(p_client, p_out_result,
        p_fp_stream, &p_client->histogram[index][0]);
    }
  } else {
    module_faceproc_client_send_roi_event(p_client, p_out_result,
      p_fp_stream, &p_client->histogram[index][0]);
  }

  if (!p_client->use_old_stats)
    module_faceproc_client_send_faceinfo(p_client, p_out_result,
    p_fp_stream, faces_data);

exit:
  pthread_mutex_unlock(&p_client->result_mutex);

  return rc;
}

/**
 * Function: module_faceproc_client_conv_format.
 *
 * Description: Convert format to planar.
 *
 * Arguments:
 *   @p_fp_stream - Pointer to facaproc stream holder.
 *   @p_src_buff - Pointer to src buffer.
 *   @p_frame - Pointer to face component frame where converted
 *     frame will be stored.
 *
 * Return values:
 *   IMG error codes.
 *
 **/
static int module_faceproc_client_conv_format(faceproc_stream_t *p_fp_stream,
  void *p_src_buff, img_frame_t *p_frame)
{
  uint8_t *in = p_src_buff;
  int width = p_fp_stream->input_dim.width;
  int height = p_fp_stream->input_dim.height;
  uint32_t size = width * height * 2;
  uint32_t i = 0;
  uint8_t *out;
  uint8_t *out1;

  switch (p_fp_stream->input_fmt) {
  case CAM_FORMAT_YUV_RAW_8BIT_YUYV:
  case CAM_FORMAT_YUV_RAW_8BIT_YVYU:
    out = p_frame->frame[0].plane[0].addr;
    out1 = p_frame->frame[0].plane[0].addr +
      p_frame->frame[0].plane[0].length;
    break;
  case CAM_FORMAT_YUV_RAW_8BIT_UYVY:
  case CAM_FORMAT_YUV_RAW_8BIT_VYUY:
    out1 = p_frame->frame[0].plane[0].addr;
    out = p_frame->frame[0].plane[0].addr +
      p_frame->frame[0].plane[0].length;
    break;
  default:
    IDBG_ERROR("%s:%d] Format not supported", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  for (i = 0; i < size>>5; i++) {
    __asm__ __volatile__ (
      "vld4.u8 {d0-d3}, [%2]! \n"
      "vst2.u8 {d0,d2}, [%0]! \n"
      "vst2.u8 {d1,d3}, [%1]! \n"
      :"+r"(out), "+r"(out1), "+r"(in)
      :
      :"memory","d0","d1","d2","d3"
    );
  }

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_downscale_frame.
 *
 * Description: Downscale frame based on required component sizes.
 *
 * Arguments:
 *   @f_conf - Frame configuration
 *   @p_fp_stream - Pointer to facaproc stream holder.
 *   @p_frame - Pointer to face component frame here downscaled
 *     frame will be stored.
 *   @p_src_buff - Pointer to src buffer.
 *   @p_fcv_hdl - Pointer to fastcv lib handle
 *
 * Return values:
 *   IMG error codes.
 *
 * Notes: MAX downscale factor is 4x.
 **/
static int module_faceproc_client_downscale_frame(faceproc_frame_cfg_t *f_conf,
  faceproc_stream_t *p_fp_stream, img_frame_t *p_frame, void *p_src_buff,
  img_fcv_lib_handle_t *p_fcv_hdl)
{
  uint32_t in_stride, out_stride;
  uint32_t in_width, in_height;
  uint32_t out_width, out_height;
  uint8_t *p_in_ptr, *p_out_ptr;
  float ratio, ratio_width, ratio_height;

  if (p_fcv_hdl->pFcvScaleDownMNu8 == NULL) {
    return IMG_ERR_INVALID_INPUT;
  }

  p_in_ptr = p_src_buff;
  p_out_ptr = p_frame->frame[0].plane[0].addr;

  in_width = p_frame->frame[0].plane[0].width;
  in_height = p_frame->frame[0].plane[0].height;
  in_stride = p_frame->frame[0].plane[0].stride;

  out_stride = f_conf->min_stride ?
    f_conf->min_stride : f_conf->max_width;

  ratio_width = (float) in_width / f_conf->max_width;
  ratio_height = (float) in_height / f_conf->max_height;

  ratio = (ratio_width > ratio_height) ? ratio_width : ratio_height;

  /* Upscale is not allowed */
  if (ratio < 1.0) {
    IDBG_ERROR("%s:%d] Fail: Upscale not allowed ratio %.6f",
      __func__, __LINE__, ratio);
    return IMG_ERR_INVALID_INPUT;
  }

  if (ratio > IMG_MAX_DOWNSCALE_RATIO) {
    /* Max download ratio is 20  */
    IDBG_ERROR("%s:%d] Fail: Exceeded MAX downscale ratio %.6f",
      __func__, __LINE__, IMG_MAX_DOWNSCALE_RATIO);
    return IMG_ERR_INVALID_INPUT;
  }

  out_width = (uint32_t)((float)in_width / ratio);
  out_height = (uint32_t)((float)in_height / ratio);

  p_fcv_hdl->pFcvScaleDownMNu8(p_in_ptr, in_width, in_height,
    in_stride, p_out_ptr, out_width, out_height, out_stride);

  /* Adjust frame size based on scaling */
  p_frame->info.width = out_width;
  p_frame->info.height = out_height;
  p_frame->frame[0].plane[0].width = p_frame->info.width;
  p_frame->frame[0].plane[0].height = p_frame->info.height;
  p_frame->frame[0].plane[0].stride = out_stride;
  p_frame->frame[0].plane[0].scanline = p_frame->info.height;

  p_fp_stream->input_trans_info.h_offset = 0;
  p_fp_stream->input_trans_info.h_scale = ratio;
  p_fp_stream->input_trans_info.v_offset = 0;
  p_fp_stream->input_trans_info.v_scale = ratio;

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_corr_stride.
 *
 * Description: Correct stride in a given frame.
 *
 * Arguments:
 *   @p_client - Pointer to faceproc client.
 *   @p_fp_stream - Pointer to facaproc stream holder.
 *   @p_frame - Pointer to face component frame here downscaled
 *     frame will be stored.
 *   @p_src_buff - Pointer to src buffer.
 *
 * Return values:
 *   IMG error codes.
 *
 * Notes: MAX downscale factor is 4x.
 **/
static int module_faceproc_client_corr_stride(void *p_src_buff,
  img_frame_t *p_frame, uint32_t req_stride)
{
  uint8_t *src, *dst;
  uint32_t i;

  if (req_stride < p_frame->frame[0].plane[0].width) {
    IDBG_ERROR("%s:%d] Fail: Stride %d smaller then width %d",
      __func__, __LINE__, req_stride, p_frame->frame[0].plane[0].width);
    return IMG_ERR_INVALID_INPUT;
  }

  dst = (uint8_t *)p_frame->frame[0].plane[0].addr;
  src = (uint8_t *)p_src_buff;
  for (i = 0; i < p_frame->frame[0].plane[0].height; i++) {
    memcpy(dst, src, req_stride);
    dst += req_stride;
    src +=  p_frame->frame[0].plane[0].stride;
  }
  p_frame->frame[0].plane[0].stride = req_stride;

  return IMG_SUCCESS;
}

/**
* Function: module_faceproc_client_fill_frame_size
*
* Description: Prepare buffer before queuing to faceproc component.
*   Memcpy or scaling will be perfoment based on component requirements.
*
* Arguments:
*   @p_client - Pointer to faceproc client.
*   @p_fp_stream - Pointer to facproc stream holder.
*   @p_frame - Pointer to face component frame.
*   @p_src_buff - Pointer to src buffer.
*   @input_stride - Stride of the src buffer.
*
* Return values:
*   0 - if scalling is not needed, 1 - Scalling is needed.
*
* Notes: If SW faceproc is selected function will return 0 always.
**/
static int module_faceproc_client_fill_frame_size(faceproc_stream_t *p_stream,
  img_frame_t *p_frame)
{

  if (!p_stream || !p_frame) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  /* Update dimensions */
  p_frame->frame[0].plane_cnt = 1;
  p_frame->frame[0].plane[0].plane_type = PLANE_Y;
  p_frame->frame[0].plane[0].width = p_stream->input_dim.width;
  p_frame->frame[0].plane[0].height = p_stream->input_dim.height;
  p_frame->frame[0].plane[0].stride = p_stream->input_dim.stride;
  p_frame->frame[0].plane[0].scanline = p_stream->input_dim.scanline;
  p_frame->frame[0].plane[0].length =
    p_frame->frame[0].plane[0].stride * p_frame->frame[0].plane[0].scanline;

  p_frame->info.width = p_frame->frame[0].plane[0].width;
  p_frame->info.height = p_frame->frame[0].plane[0].height;

  p_stream->input_trans_info.h_offset = 0;
  p_stream->input_trans_info.h_scale = 1.0;
  p_stream->input_trans_info.v_offset = 0;
  p_stream->input_trans_info.v_scale = 1.0;

  return IMG_SUCCESS;
}

/** module_faceproc_validate_buff_index
 *    @data1: list buf object
 *    @data2: buffer index to be checked
 *
 *    Checks for the buffer is with specified index
 *
 *    Return TRUE if the stream buffer list has the buffer
 *
 **/
static boolean module_faceproc_validate_buff_index(void *data1, void *data2)
{
  mct_stream_map_buf_t *list_buf = (mct_stream_map_buf_t *)data1;
  uint32_t *buf_idx = (uint32_t *)data2;
  boolean ret_val = FALSE;

  if (list_buf && buf_idx) {
    if (list_buf->buf_index == *buf_idx) {
      ret_val = TRUE;
    }
  } else {
    IDBG_ERROR("Null pointer detected in %s\n", __func__);
  }
  IDBG_MED("%s -", __func__);
  return ret_val;
}

/**
 * Function: module_faceproc_client_handle_buf_divert
 *
 * Description: This function is for handling the buffers
 *            sent from the peer modules
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @buf_idx: index of the buffer to be processed
 *   @frame_id: frame id
 *   @s_idx: stream index
 *   @p_native_buf: pointer to native buf addr
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
boolean module_faceproc_client_handle_buf_divert(faceproc_client_t *p_client,
  mod_img_msg_buf_divert_t *p_msg_divert)
{
  img_frame_t *p_frame;
  void *p_src_buff;
  faceproc_stream_t *p_fp_stream;
  uint32_t buf_idx;
  int img_idx, frame_id, s_idx, ion_fd;
  boolean frame_processed = TRUE;
  boolean buffer_locked;
  int rc;

  if (!p_client || !p_msg_divert) {
    return IMG_ERR_INVALID_INPUT;
  }

  img_component_ops_t *p_comp =
    module_faceproc_client_get_current_comp(p_client);

  pthread_mutex_lock(&p_client->mutex);

  module_faceproc_t *p_mod = (module_faceproc_t *)(p_client->p_mod);

  if (TRUE != p_client->active) {
    IDBG_LOW("%s:%d] Client not active", __func__, __LINE__);
    goto unlock_client_and_exit;
  }

  s_idx = faceproc_get_stream_by_id(p_client, p_msg_divert->identity);
  if (s_idx >= MAX_FD_STREAM) {
    IDBG_LOW("%s:%d] Invalid client ", __func__, __LINE__);
    goto unlock_client_and_exit;
  }
  p_fp_stream = &p_client->fp_stream[s_idx];

  /* process the frame only in IMGLIB_STATE_PROCESSING state */
  if (IMGLIB_STATE_PROCESSING != p_client->state ||
    p_client->mode == FACE_DETECT_OFF) {
    /* Disable any enabled buffer diverts */
    rc = module_faceproc_client_req_divert(&p_client->fp_stream[s_idx], 0);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Fail to stop buff diverts %d",
        __func__, __LINE__, rc);
    }
    IDBG_LOW("%s:%d] Skip state %d mode %d", __func__, __LINE__,
      p_client->state,
      p_client->mode);
    goto unlock_client_and_exit;
  }

  if ((!p_fp_stream->processing_stream)) {
    IDBG_MED("%s:%d] exit- p stream %d", __func__, __LINE__,
      p_fp_stream->processing_stream);
    goto unlock_client_and_exit;
  }

  p_client->current_frame_cnt++;
  p_client->current_count =
    (p_client->current_count + 1) % (p_client->frame_skip_cnt + 1);

  if ((0 != p_client->current_count) &&
    (p_client->current_frame_cnt > p_client->initial_frame_no_skip_cnt)) {
    IDBG_LOW("%s:%d]Skip frame", __func__, __LINE__);
    goto unlock_client_and_exit;
  }

  img_idx = module_faceproc_client_get_buf(p_client);
  IDBG_MED("%s:%d] img_idx %d", __func__, __LINE__, img_idx);

  /* If buffer is locked (still processing by face detection) skip */
  pthread_mutex_lock(&p_client->divert_holder[img_idx].mutex);
  buffer_locked = p_client->divert_holder[img_idx].locked;
  pthread_mutex_unlock(&p_client->divert_holder[img_idx].mutex);

  if (buffer_locked) {
    IDBG_HIGH("%s:%d] Buffer divert is locked skip img_idx %d",
      __func__, __LINE__, img_idx);
    goto unlock_client_and_exit;
  }

  frame_id = p_msg_divert->buf_divert.buffer.sequence;
  buf_idx = p_msg_divert->buf_divert.buffer.index;

  if (p_msg_divert->buf_divert.native_buf) {
    p_src_buff = (void *)((unsigned long *)p_msg_divert->buf_divert.vaddr)[0];
    ion_fd = p_msg_divert->buf_divert.fd;
    IDBG_LOW("%s %d: Native buffers!", __func__, __LINE__);
  } else {
    mct_stream_map_buf_t *p_map_buf = NULL;
    mct_list_t *img_buf_list = NULL;

    if (IMG_IS_HAL3(p_client)) {
      img_buf_list = mct_list_find_custom(
        p_fp_stream->stream_info->img_buffer_list,
        &buf_idx, module_faceproc_validate_buff_index);
      if (img_buf_list) {
        p_map_buf = (mct_stream_map_buf_t *)img_buf_list->data;
      }
      if (p_map_buf) {
        p_src_buff = (uint8_t *)p_map_buf->buf_planes[0].buf;
      } else {
        IDBG_ERROR("%s:%d] Invalid Buffer Divert!!!", __func__, __LINE__);
        goto unlock_client_and_exit;
      }
    } else {
      if (buf_idx >= p_client->fp_stream[s_idx].buffer_info.buf_count) {
        IDBG_ERROR("%s:%d] Buffer index out of limit", __func__, __LINE__);
        goto unlock_client_and_exit;
      }
      p_map_buf = &p_fp_stream->buffer_info.p_buffer[buf_idx].map_buf;
      p_src_buff = p_map_buf->buf_planes[0].buf;
    }

    ion_fd = p_map_buf->buf_planes[0].fd;
  }

  p_frame = &p_client->frame[img_idx];

  p_frame->info.client_id = s_idx;
  p_frame->frame_id = frame_id;
  p_frame->idx = buf_idx;
  IDBG_MED("%s:%d] id %x dim %dx%d client_id %d frame_id %d", __func__,
    __LINE__, p_fp_stream->identity, p_fp_stream->input_dim.width,
    p_fp_stream->input_dim.height, p_frame->info.client_id,
    p_frame->frame_id);

  rc = module_faceproc_client_fill_frame_size(p_fp_stream, p_frame);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] Cannot downscale buffer %d", __func__, __LINE__, rc);
    goto unlock_client_and_exit;
  }

  if (IMG_UNLIKELY(p_client->current_frame_cnt <= 1)) {
    IDBG_MED("%s:%d] Face detection skip %d dim %dx%d pad %dx%d",
      __func__, __LINE__,
      p_client->frame_skip_cnt,
      p_fp_stream->input_dim.width,
      p_fp_stream->input_dim.height,
      p_fp_stream->input_dim.stride,
      p_fp_stream->input_dim.scanline);
  }

  IDBG_MED("%s:%d] buffer %d %p", __func__, __LINE__, buf_idx,
    p_frame->frame[0].plane[0].addr);

  IDBG_MED("%s:%d] Use Internal copy %d", __func__, __LINE__,
    p_client->faceproc_mem_handle[img_idx].handle ? TRUE : FALSE);

  if (p_fp_stream->use_int_bufs) {

    faceproc_frame_cfg_t *p_frame_cfg = &p_client->config.frame_cfg;
    boolean memcpy_needed = TRUE;
    uint32_t plane_length =
      p_fp_stream->input_dim.stride * p_fp_stream->input_dim.scanline;

    if (plane_length > p_client->faceproc_mem_handle[img_idx].length) {
      IDBG_ERROR("%s:%d] Can not process no memory", __func__, __LINE__);
      goto unlock_client_and_exit;
    }

    /* Get buffer from memory handle */
    p_frame->idx = img_idx;
    p_frame->frame[0].plane[0].addr =
      p_client->faceproc_mem_handle[img_idx].vaddr;
    p_frame->frame[0].plane[0].fd =
      p_client->faceproc_mem_handle[img_idx].fd;
    p_frame->frame[0].plane[0].length = plane_length;

    /* First convert format if required */
    if (module_faceproc_client_format_conv_need(p_fp_stream)) {

      IDBG_MED("%s:%d] Color conv needed", __func__, __LINE__);
      /* Check if we have memory for format conversion */
      if ((plane_length * 2) > p_client->faceproc_mem_handle[img_idx].length) {
        IDBG_ERROR("%s:%d] Fail to convert, no memory", __func__, __LINE__);
        goto unlock_client_and_exit;
      }

      rc = module_faceproc_client_conv_format(p_fp_stream,
        p_src_buff, p_frame);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Cannot convert format %d", __func__, __LINE__, rc);
        goto unlock_client_and_exit;
      }
      p_src_buff = p_frame->frame[0].plane[0].addr;
      memcpy_needed = FALSE;
    }

    if (module_faceproc_client_scaling_need(p_frame_cfg, p_fp_stream)) {
      IDBG_MED("%s:%d] Scaling needed", __func__, __LINE__);
      rc = module_faceproc_client_downscale_frame(p_frame_cfg,
        p_fp_stream, p_frame, p_src_buff, p_mod->p_fcv_hdl);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Cannot downscale frame %d", __func__, __LINE__, rc);
        goto unlock_client_and_exit;
      }
      p_src_buff = p_frame->frame[0].plane[0].addr;
      memcpy_needed = FALSE;
    }

    /* Use stride from the frame it may be already corrected in downscale*/
    if (module_faceproc_client_stride_corr_need(p_frame_cfg,
      p_frame->frame[0].plane[0].stride)) {

      IDBG_MED("%s:%d] Stride corr needed", __func__, __LINE__);
      rc = module_faceproc_client_corr_stride(p_src_buff, p_frame,
        p_frame_cfg->min_stride);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Stride correction failed", __func__, __LINE__);
        goto unlock_client_and_exit;
      }
      p_src_buff = p_frame->frame[0].plane[0].addr;
      memcpy_needed = FALSE;
    }

    if (memcpy_needed) {
      memcpy(p_frame->frame[0].plane[0].addr, p_src_buff, plane_length);
    }

    rc = img_buffer_cacheops(&p_client->faceproc_mem_handle[img_idx],
      IMG_CACHE_CLEAN_INV, IMG_INTERNAL);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Cache invalidate fail %d", __func__, __LINE__, rc);
      goto unlock_client_and_exit;
    }
    /* Buffer is consumed set private data to NULL */
    p_frame->private_data = NULL;

    frame_processed = TRUE;
  } else {
#ifdef USE_OFFLINE_BUFFER

    uint32_t off_img_idx = 0;
    int exec_flag = 1;
    if (!p_client->offline_buf_count) {
      IDBG_ERROR("%s: Error : No offline input..Not processing FD", __func__);
     frame_processed = TRUE;
     goto unlock_client_and_exit;
    }
    if (p_client->buf_idx_min_try < MIN_TRY_NUM_OFFLINE_FD_FRAMES) {
      off_img_idx = 0;
      p_client->buf_idx_min_try++;
      p_client->trial_on = 1;
    } else {
      IDBG_HIGH("%s %d [OFFLINE_FD]FD start", __func__, __LINE__);
      off_img_idx = module_faceproc_client_get_offline_buf(p_client);
      p_client->trial_on = 0;
    }

    uint32_t plane_length = p_fp_stream->input_dim.stride *
      p_fp_stream->input_dim.scanline;
    if ((!p_client->trial_on) && (off_img_idx >= p_client->offline_buf_count)) {
      exec_flag = 0;
    }

    if (exec_flag) {
      IDBG_HIGH("%s: Using offline index %d", __func__, off_img_idx);
      if (p_client->faceproc_offline_bufs[off_img_idx].handle) {
        if (plane_length <
          p_client->faceproc_offline_bufs[off_img_idx].length) {
          IDBG_ERROR("%s %d: length %d < %d", __func__, __LINE__, plane_length,
            p_client->faceproc_offline_bufs[off_img_idx].length);
            frame_processed = TRUE;
            goto unlock_client_and_exit;
        }

        p_frame->frame[0].plane[0].addr = p_src_buff;
        memcpy(p_frame->frame[0].plane[0].addr,
          p_client->faceproc_offline_bufs[off_img_idx].vaddr,
          plane_length);
        p_frame->frame[0].plane[0].fd = ion_fd;

      IDBG_INFO("%s:%d] Actually using index %d", __func__, __LINE__,
        off_img_idx);
    } else {
      IDBG_ERROR("%s:%d] Handle invalid for offline buffer at index %d",
        __func__, __LINE__, off_img_idx);
    }
    frame_processed = FALSE;
   } else {
     IDBG_ERROR("%s:%d] [OFFLINE_FD] Not processing FD", __func__);
     frame_processed = TRUE;
     goto unlock_client_and_exit;
   }
#else
    p_frame->frame[0].plane[0].addr = p_src_buff;
    p_frame->frame[0].plane[0].fd = ion_fd;

    frame_processed = FALSE;
#endif
  }
  IDBG_MED("%s:%d] buffer %d %p frame_processed %d boost %d %f",
    __func__, __LINE__, buf_idx,
    p_frame->frame[0].plane[0].addr, frame_processed,
    p_client->p_fd_chromatix->enable_boost,
   p_client->p_fd_chromatix->backlite_boost_factor);

  /* Update chromatix if needed */
  rc = module_faceproc_client_update_chromatix(p_client);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] Cannot update chromatix %d", __func__, __LINE__, rc);
    frame_processed = TRUE;
    goto unlock_client_and_exit;
  }

  pthread_mutex_lock(&p_client->divert_holder[img_idx].mutex);
  p_client->divert_holder[img_idx].msg = *p_msg_divert;
  p_client->divert_holder[img_idx].locked = TRUE;
  p_client->divert_holder[img_idx].return_ack = frame_processed ? FALSE : TRUE;
  p_frame->private_data = &p_client->divert_holder[img_idx];
  pthread_mutex_unlock(&p_client->divert_holder[img_idx].mutex);

  // uint32_t all_frames = module_imglib_common_get_prop(
    // "persist.camera.imglib.pp_all", "0");

  if (p_client->p_fd_chromatix->enable_boost &&
    (!p_client->p_fd_chromatix->enable_frame_batch_mode ||
    p_frame->frame_id & 0x1) &&
    (p_client->p_fd_chromatix->backlite_boost_factor > 1.0f)) {
    img_boost_linear_k(QIMG_ADDR(p_frame, 0),
      QIMG_STRIDE(p_frame, 0),
      QIMG_HEIGHT(p_frame, 0),
      QIMG_STRIDE(p_frame, 0),
      p_client->p_fd_chromatix->backlite_boost_factor,
      FALSE);
    rc = img_cache_ops_external(QIMG_ADDR(p_frame, 0),
      QIMG_STRIDE(p_frame, 0) * QIMG_HEIGHT(p_frame, 0), 0,
      QIMG_FD(p_frame, 0),
      CACHE_CLEAN_INVALIDATE, p_mod->ion_fd);
    IDBG_LOW("%s:%d] Boost luma frame %d value %f rc %d", __func__, __LINE__,
      p_frame->frame_id,
      p_client->p_fd_chromatix->backlite_boost_factor, rc);
  }
  rc = IMG_COMP_Q_BUF(p_comp, p_frame, IMG_IN);
  if (IMG_ERROR(rc)) {
    IDBG_HIGH("%s:%d] Cannot enqueue index %d rc:%d",
        __func__, __LINE__, img_idx, rc);

    /* Unlock the buffer if qbuf fails */
    pthread_mutex_lock(&p_client->divert_holder[img_idx].mutex);
    p_client->divert_holder[img_idx].locked = FALSE;
    pthread_mutex_unlock(&p_client->divert_holder[img_idx].mutex);

    frame_processed = TRUE;
    goto unlock_client_and_exit;
  }

unlock_client_and_exit:
  pthread_mutex_unlock(&p_client->mutex);
  return frame_processed;
}

/**
 * Function: module_faceproc_client_process_buffers
 *
 * Description: This function is processing the buffers
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @buf_idx: index of the buffer to be processed
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_process_buffers(faceproc_client_t *p_client)
{
  img_frame_t *p_frame;
  int rc = IMG_SUCCESS;
  img_component_ops_t *p_comp =
    module_faceproc_client_get_current_comp(p_client);
  uint32_t f_cnt;
  faceproc_stream_t *p_fp_stream = &p_client->fp_stream[0];

  for (f_cnt = 0; f_cnt < p_fp_stream->buffer_info.buf_count; f_cnt++) {
    p_frame = &p_fp_stream->buffer_info.p_buffer[f_cnt].frame;

    rc = IMG_COMP_Q_BUF(p_comp, p_frame, IMG_IN);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] buffer enqueue error %d", __func__, __LINE__, rc);
      break;
    }
    pthread_mutex_lock(&p_client->mutex);
    p_client->state = IMGLIB_STATE_PROCESSING;
    rc = img_wait_for_completion(&p_client->cond, &p_client->mutex, 10000);
    p_client->state = IMGLIB_STATE_STARTED;
    pthread_mutex_unlock(&p_client->mutex);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] buffer Wait timeout %d", __func__, __LINE__, rc);
      break;
    }
  }
  return rc;
}

/**
 * Function: module_faceproc_client_schedule_mode
 *
 * Description: Schedule faceproc library mode
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @new_mode: New faceproc mode
 *   @frame_id: frame id
 *   @type: Type of command
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
static void module_faceproc_client_schedule_mode(faceproc_client_t *p_client,
  faceproc_mode_t new_mode, fd_pfc_data_type_t type,
  int frame_id)
{
  int rc = IMG_SUCCESS;
  fd_pfc_data_t *p_a_data = NULL;
  fd_pfc_data_t *p_r_data = NULL;
  module_faceproc_t *p_mod = (module_faceproc_t *)p_client->p_mod;
  uint32_t a_delay = p_mod->session_data.max_pipeline_frame_applying_delay;
  uint32_t r_delay = p_mod->session_data.max_pipeline_meta_reporting_delay;

  /* Update the queues. Since ON takes more delay,
     delaying the OFF as well */
  int apply_idx = (frame_id % (int32_t)p_client->fpc_q_size);

  int report_idx = (frame_id + (int32_t)a_delay + (int32_t)r_delay)
    % (int32_t)p_client->fpc_q_size;

  IDBG_MED("%s:%d] apply %d report %d new_mode %d", __func__, __LINE__,
    apply_idx, report_idx, new_mode);

  p_a_data = (fd_pfc_data_t *)malloc(sizeof(fd_pfc_data_t));
  if (NULL == p_a_data) {
    IDBG_ERROR("%s:%d] Cannot create pfc apply %d", __func__, __LINE__,
      frame_id);
    goto error;
  }

  /* apply message */
  p_a_data->ctrl_type = IMGLIB_APPLY;
  p_a_data->data_type = type;
  p_a_data->d.mode = new_mode;
  rc = img_q_enqueue(&p_client->p_fpc_q[apply_idx], p_a_data);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] Cannot queue pfc apply %d", __func__, __LINE__,
      frame_id);
    goto error;
  }

  /* post message */
  if (type == MOD_FD_FPC_MODE) {
    p_r_data = (fd_pfc_data_t *)malloc(sizeof(fd_pfc_data_t));
    if (NULL == p_r_data) {
      IDBG_ERROR("%s:%d] Cannot create pfc post %d", __func__, __LINE__,
        frame_id);
      goto error;
    }
    p_r_data->ctrl_type = IMGLIB_POST;
    p_r_data->data_type = type;
    p_r_data->d.mode = new_mode;
    rc = img_q_enqueue(&p_client->p_fpc_q[report_idx], p_r_data);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Cannot queue pfc report %d", __func__, __LINE__,
        frame_id);
      goto error;
    }
  }
  /* Success */
  return;

error:
  if (p_a_data)
    free(p_a_data);

  if (p_r_data)
    free(p_r_data);
}

/**
 * Function: module_faceproc_client_reset_face_roi
 *
 * Description: update upstream module with empty Face ROI when turn off
 *              faceproc, as other modules are not handling faceproc off
 *              command
 *
 * Arguments:
 *   @p_client: faceproc client
 *
 * Return values:
 *   NULL
 *
 * Notes: none
 **/
void module_faceproc_client_reset_face_roi(faceproc_client_t *p_client)
{
  mct_face_info_t face_info;
  uint32_t session_str_idx = faceproc_get_stream_by_type(p_client, CAM_STREAM_TYPE_PARM);
  mct_event_t mct_event;

  IDBG_MED("%s:%d] send empty face proc roi to up stream modules",
    __func__, __LINE__);
  memset(&face_info, 0x0, sizeof(mct_face_info_t));
  mct_event.u.module_event.type = MCT_EVENT_MODULE_FACE_INFO;
  mct_event.u.module_event.module_event_data = (void *)&face_info;
  mct_event.type = MCT_EVENT_MODULE_EVENT;
  mct_event.identity = p_client->fp_stream[session_str_idx].identity;
  mct_event.direction = MCT_EVENT_UPSTREAM;
  mct_port_send_event_to_peer(p_client->fp_stream[session_str_idx].p_sink_port, &mct_event);
}

/**
 * Function: module_faceproc_client_set_mode
 *
 * Description: Set faceproc library mode
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @mode: New faceproc mode
 *   @lock: flag to indicate whether to issue the lock
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_set_mode(faceproc_client_t *p_client,
  faceproc_mode_t mode,
  int8_t lock)
{
  img_component_ops_t *p_comp;
  int status = IMG_SUCCESS;
  module_faceproc_t *p_mod = NULL;
  int reconfig = FALSE;

  if (NULL == p_client)
    return IMG_ERR_INVALID_INPUT;

  if (p_client->mode == mode)
    return IMG_SUCCESS;

  p_mod = (module_faceproc_t *)p_client->p_mod;
  p_comp = module_faceproc_client_get_current_comp(p_client);

  if (!p_comp->handle) {
    p_client->set_mode = TRUE;
    p_client->new_mode = mode;
    return IMG_SUCCESS;
  }

  if (lock) {
    pthread_mutex_lock(&p_client->mutex);
  }

  status = IMG_COMP_SET_PARAM(p_comp, QWD_FACEPROC_MODE, (void *)&mode);
  if (IMG_SUCCESS == status) {
    /* Preserve the mode */
    p_client->mode = mode;
    /* handle FD lite mode */
    reconfig = (mode == FACE_DETECT_LITE) || (mode == FACE_DETECT);
    if (reconfig) {
     /* Select chromatix based on faceproc engine */
      status = module_faceproc_client_update_chromatix(p_client);
      if (IMG_ERROR(status)) {
        IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, status);
        goto out;
      }
    }

    /* Start processing only if faceproc is in started or processing state */
    if ((IMGLIB_STATE_STARTED == p_client->state) ||
      (IMGLIB_STATE_PROCESSING == p_client->state)) {
      p_client->state = (FACE_DETECT_OFF == mode) ? IMGLIB_STATE_STARTED :
        IMGLIB_STATE_PROCESSING;
      p_client->current_frame_cnt = 0;
      memset(&p_client->last_stats, 0x0, sizeof(p_client->last_stats));
      memset(&p_client->stats_filter.mfi_last_updated, 0x0,
        sizeof(p_client->stats_filter.mfi_last_updated));
      p_client->stats_filter.hold_cnt =
        p_client->p_fd_chromatix->stats_filter_max_hold;
      p_client->stats_filter.locked =
        p_client->p_fd_chromatix->stats_filter_lock;
    }
    memset(p_client->result, 0, sizeof(p_client->result));
  }
  IDBG_HIGH("%s:%d] mode %d", __func__, __LINE__, p_client->mode);
  if (mode == FACE_DETECT_OFF) {
    module_faceproc_client_reset_face_roi(p_client);
  }

  if (IMG_SUCCEEDED(status) && p_client->state == IMGLIB_STATE_PROCESSING) {
    int s_idx, rc;
    /* If processing stream is available enable buffer diverts */
    s_idx = faceproc_get_processing_stream_idx(p_client);
    if ((s_idx >= 0) && (s_idx < MAX_NUM_FD_FRAMES)) {
      rc = module_faceproc_client_req_divert(&p_client->fp_stream[s_idx], 1);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Fail to enable diverts", __func__, __LINE__);
      }
    }
  }

  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] Setting Face param Failed", __func__, __LINE__);
  }

out:
  if (lock)
    pthread_mutex_unlock(&p_client->mutex);
  return status;
}

/**
 * Function: module_faceproc_client_set_rotation
 *
 * Description: Set faceproc rotation.
 *
 * Arguments:
 *   @p_client: faceproc client.
 *   @rotation: rotation in degrees.
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_set_rotation(faceproc_client_t *p_client,
  int32_t rotation)
{
  faceproc_orientation_t orientation_hint;
  int ret = IMG_SUCCESS;

  if (NULL == p_client) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  img_component_ops_t *p_comp =
    module_faceproc_client_get_current_comp(p_client);

  if (rotation < 0 || rotation > 360) {
    IDBG_ERROR("%s:%d] Invalid rotation value %d",
      __func__, __LINE__, rotation);
    return IMG_ERR_INVALID_INPUT;
  }
  IDBG_MED("%s:%d] Faceproc rotation %d", __func__, __LINE__, rotation);

  if (p_client->use_device_orientation == FALSE) {
    IDBG_MED("%s:%d] Orientation is ignored", __func__, __LINE__);
    return IMG_SUCCESS;
  }

  /* Faceproc is operation on not rotated frame, make opposite rotation */
  rotation = (360 - rotation);
  if (rotation < 45) {
    orientation_hint = FD_FACE_ORIENTATION_0;
  } else if (rotation < 135) {
    orientation_hint = FD_FACE_ORIENTATION_90;
  } else if (rotation < 225) {
    orientation_hint = FD_FACE_ORIENTATION_180;
  } else if (rotation < 315) {
    orientation_hint = FD_FACE_ORIENTATION_270;
  } else if (rotation <= 360) {
    orientation_hint = FD_FACE_ORIENTATION_0;
  } else {
    IDBG_ERROR("%s:%d] Invalid rotation value %d",
      __func__, __LINE__, rotation);
    return IMG_ERR_INVALID_INPUT;
  }

  pthread_mutex_lock(&p_client->mutex);

  p_client->config.face_cfg.face_orientation_hint = orientation_hint;
  if (p_comp->handle) {
    ret = IMG_COMP_SET_PARAM(p_comp, QWD_FACEPROC_CFG,
      (void *)&p_client->config);
  }

  pthread_mutex_unlock(&p_client->mutex);

  return ret;
}

/**
 * Function: module_faceproc_client_config_streams.
 *
 * Description: configure streams based on stream selection.
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @p_stream_select: Pointer to stream selection.
 *
 * Return values:
 *     imaging error values
 *
 * Notes: Function is not protected.
 **/
static int module_faceproc_client_config_streams(faceproc_client_t *p_client,
  faceproc_stream_select_t const *p_stream_select)
{
  cam_stream_type_t stream_type;
  uint32_t s_idx;
  int rc;

  if (!p_client || !p_stream_select) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  for (s_idx = 0; s_idx < p_client->ref_count; s_idx++) {
    stream_type = p_client->fp_stream[s_idx].stream_info->stream_type;

    if (p_stream_select->processing_stream == stream_type) {
      /* Start buffer diverts when processing stream is set */
      if ((p_client->fp_stream[s_idx].processing_stream == FALSE) &&
        (p_client->state == IMGLIB_STATE_PROCESSING)) {
        rc = module_faceproc_client_req_divert(&p_client->fp_stream[s_idx], 1);
        if (IMG_ERROR(rc)) {
          IDBG_ERROR("%s:%d] Fail buffer divert request", __func__, __LINE__);
          return rc;
        }
      }

      p_client->fp_stream[s_idx].processing_stream = TRUE;
      p_client->fp_stream[s_idx].use_int_bufs =
        p_stream_select->internal_buffers;
    } else {
      /* Stop buffer diverts when processing stream is unset */
      if ((p_client->fp_stream[s_idx].processing_stream == TRUE) &&
        (p_client->state == IMGLIB_STATE_PROCESSING)) {
        rc = module_faceproc_client_req_divert(&p_client->fp_stream[s_idx], 0);
        if (IMG_ERROR(rc)) {
          IDBG_ERROR("%s:%d] Fail buffer divert release", __func__, __LINE__);
          return rc;
        }
      }

      p_client->fp_stream[s_idx].processing_stream = FALSE;
      p_client->fp_stream[s_idx].use_int_bufs = FALSE;
    }

    if (p_stream_select->reporting_stream == stream_type) {
      p_client->fp_stream[s_idx].reporting_stream = TRUE;
    } else {
      p_client->fp_stream[s_idx].reporting_stream = FALSE;
    }

    IDBG_MED("%s:%d] Config s_idx %d id %x type %d proc %d report %d int_b %d",
      __func__, __LINE__, s_idx, p_client->fp_stream[s_idx].identity,
      p_client->fp_stream[s_idx].stream_info->stream_type,
      p_client->fp_stream[s_idx].processing_stream,
      p_client->fp_stream[s_idx].reporting_stream,
      p_client->fp_stream[s_idx].use_int_bufs);
  }

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_map_streams.
 *
 * Description: Map streams based on meta stream info.
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @p_maps_stream: Stream maps
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
static int module_faceproc_client_map_streams(faceproc_client_t *p_client,
  cam_stream_size_info_t *p_maps_stream)
{
  uint32_t def, map, str;
  uint32_t streams_matched = 0;
  uint32_t chosen_matched = 0;
  uint32_t chosen_id = 0;
  int ret;

  if (!p_client || !p_maps_stream) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  pthread_mutex_lock(&p_client->mutex);
  p_client->video_mode = FALSE;

  /* Find stream proper definition */
  for (def = 0; def < IMGLIB_ARRAY_SIZE(faceproc_defs); def++) {
    streams_matched = 0;

    for (map = 0; map < p_maps_stream->num_streams; map++) {
      if (p_maps_stream->type[map] == CAM_STREAM_TYPE_VIDEO) {
        p_client->video_mode = TRUE;
      }

      for (str = 0; str < CAM_STREAM_TYPE_MAX; str++) {
        if (CAM_STREAM_TYPE_MAX == faceproc_defs[def].streams[str]) {
          break;
        }
        if (faceproc_defs[def].streams[str] == p_maps_stream->type[map]) {
          streams_matched++;
          break;
        }
      }

    }
    if (chosen_matched < streams_matched) {
      chosen_matched = streams_matched;
      chosen_id = def;
    }
  }
  p_client->def_id = chosen_id;

  if (faceproc_defs[chosen_id].normal.internal_buffers ||
    (p_client->p_fd_chromatix->enable_low_light &&
    faceproc_defs[chosen_id].low_light.internal_buffers)) {
    p_client->alloc_int_bufs = TRUE;
  } else {
    p_client->alloc_int_bufs = FALSE;
  }
  IDBG_HIGH("%s:%d] Alloc internal buffers %d chosen_id %d",
    __func__, __LINE__,
    p_client->alloc_int_bufs, chosen_id);

  ret = module_faceproc_client_config_streams(p_client,
    &faceproc_defs[chosen_id].normal);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("%s:%d] Fail to configure streams", __func__, __LINE__);
  }

  pthread_mutex_unlock(&p_client->mutex);
  return ret;
}

/**
 * Function: module_faceproc_client_handle_ctrl_parm
 *
 * Description: This function is used to handle the ctrl
 *             commands passed from the MCTL
 *
 * Arguments:
 *   @p_ctrl_event: pointer to mctl ctrl events
 *   @p_client: faceproc client
 *   @frame_id: frame_id
 *
 * Return values:
 *     error/success
 *
 * Notes: none
 **/
int module_faceproc_client_handle_ctrl_parm(faceproc_client_t *p_client,
  mct_event_control_parm_t *param,
  int frame_id)
{
  int status = IMG_SUCCESS;

  if (NULL == param)
    return status;

  IDBG_LOW("%s:%d] param %d", __func__, __LINE__, param->type);
  switch(param->type) {
  case CAM_INTF_PARM_FD: {
    cam_fd_set_parm_t *p_fd_set_parm = (cam_fd_set_parm_t *)param->parm_data;
    faceproc_mode_t new_mode = FACE_DETECT_OFF;

    if (NULL == p_fd_set_parm) {
      IDBG_ERROR("%s:%d] NULL invalid data", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }

    /* Set Face Proc library mode */
    if (CAM_FACE_PROCESS_MASK_RECOGNITION & p_fd_set_parm->fd_mode)
      new_mode = FACE_RECOGNIZE;
    else if (p_fd_set_parm->fd_mode &
      (CAM_FACE_PROCESS_MASK_DETECTION | CAM_FACE_PROCESS_MASK_FOCUS))
      new_mode = FACE_DETECT;

    //Note: if for video HAL wants to turn off UI updates.
    if (CAM_FACE_PROCESS_MASK_DETECTION & p_fd_set_parm->fd_mode) {
      p_client->fd_ui_mode = CAM_FACE_DETECT_MODE_FULL;
    } else {
      p_client->fd_ui_mode = CAM_FACE_DETECT_MODE_OFF;
    }

    /* ToDo: set video lite FD to false */
    // p_client->use_video_chromatix = FALSE;

    IDBG_MED("%s:%d] calling module_faceproc_client_schedule_mode %d, FULL FDMASK %x",
      __func__, __LINE__, new_mode, p_fd_set_parm->fd_mode);

    if ( (CAM_FACE_PROCESS_MASK_GAZE & p_fd_set_parm->fd_mode) ||
      p_client->enable_bsgc) {
      p_client->config.fd_face_info_mask = FACE_INFO_MASK_ALL;
    }

    /* Change the Face proc mode if required */
    module_faceproc_client_schedule_mode(p_client, new_mode, MOD_FD_FPC_MODE,
      frame_id);

    break;
  }
  case CAM_INTF_META_STATS_FACEDETECT_MODE: { // HAL3
    cam_face_detect_mode_t *p_fd_mode =
      (cam_face_detect_mode_t *)param->parm_data;
    faceproc_mode_t new_mode;

    if (NULL == p_fd_mode) {
      IDBG_ERROR("%s:%d] NULL invalid data", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }

    if (*p_fd_mode == CAM_FACE_DETECT_MODE_OFF)
      new_mode = FACE_DETECT_OFF;
    else
      new_mode = FACE_DETECT;

    //Note: if for video HAL wants to turn off UI updates.
    if ((CAM_FACE_PROCESS_MASK_FOCUS & *p_fd_mode) &&
      !(CAM_FACE_PROCESS_MASK_DETECTION & *p_fd_mode)){
      p_client->fd_ui_mode = CAM_FACE_DETECT_MODE_OFF;
    } else {
      p_client->fd_ui_mode = CAM_FACE_DETECT_MODE_FULL;
    }

    IDBG_MED("%s:%d] 2 calling module_faceproc_client_schedule_mode %d",
      __func__, __LINE__, new_mode);

    if (CAM_FACE_PROCESS_MASK_GAZE & *p_fd_mode) {
      p_client->config.fd_face_info_mask = FACE_INFO_MASK_ALL;
    }

    module_faceproc_client_schedule_mode(p_client, new_mode, MOD_FD_FPC_MODE,
      frame_id);
    break;
  }
  case CAM_INTF_PARM_ZOOM: {
    int *p_zoom_val = param->parm_data;
    if (NULL == p_zoom_val) {
      IDBG_ERROR("%s:%d] NULL invalid data", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    p_client->zoom_val = *p_zoom_val;
    IDBG_MED("%s:%d] zoom_val %d", __func__, __LINE__, p_client->zoom_val);
    break;
  }
  case CAM_INTF_PARM_HAL_VERSION: {
    int32_t *hal_version = param->parm_data;
    if (NULL == hal_version) {
      IDBG_ERROR("%s:%d] NULL invalid data", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    p_client->hal_version = *hal_version;
    IDBG_MED("%s:%d] hal_version %d", __func__, __LINE__,
      p_client->hal_version);
    break;
  }
  case CAM_INTF_META_STREAM_INFO: {
    cam_stream_size_info_t *maps_stream =
      (cam_stream_size_info_t *)param->parm_data;

    status = module_faceproc_client_map_streams(p_client, maps_stream);
    if (IMG_ERROR(status)) {
      IDBG_ERROR("%s:%d] Map stream info failed", __func__, __LINE__);
    }
    break;
  }
  case CAM_INTF_PARM_BESTSHOT_MODE: {
    cam_scene_mode_type *p_curr_scene =
      (cam_scene_mode_type*) param->parm_data;
    faceproc_mode_t new_mode;
    if (NULL == p_curr_scene) {
      IDBG_ERROR("%s:%d] NULL invalid data", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    if (*p_curr_scene == CAM_SCENE_MODE_FACE_PRIORITY)
      new_mode = FACE_DETECT;
    else
      new_mode = FACE_DETECT_OFF;

    IDBG_MED("%s: CAM_SCENE_MODE_FACE_PRIORITY %d", __func__, new_mode);

    IDBG_MED("%s:%d] 3 calling module_faceproc_client_schedule_mode %d",
      __func__, __LINE__, new_mode);
    module_faceproc_client_schedule_mode(p_client, new_mode,
      MOD_FD_FPC_SCENE_MODE, frame_id);
    break;
  }
  case CAM_INTF_META_JPEG_ORIENTATION: {
    int32_t *rotation = (int32_t *)param->parm_data;

    status = module_faceproc_client_set_rotation(p_client, *rotation);
    if (IMG_ERROR(status)) {
      IDBG_ERROR("%s:%d] Fail to set faceproc rotation", __func__, __LINE__);
    }
    break;
  }
  default:
    break;
  }
  return status;
}

/**
 * Function: module_faceproc_client_set_lux_idx.
 *
 * Description: Set faceproc lux index.
 *
 * Arguments:
 *   @p_client: faceproc client.
 *   @idx: Lux index.
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_set_lux_idx(faceproc_client_t *p_client, float idx)
{
  faceproc_stream_select_t const *p_stream_select;
  boolean low_light_detected;

  if (!(p_client && p_client->p_fd_chromatix)) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  pthread_mutex_lock(&p_client->mutex);

  if (!p_client->p_fd_chromatix->enable_low_light) {
    IDBG_MED("%s:%d] Low light disabled skip", __func__, __LINE__);
    pthread_mutex_unlock(&p_client->mutex);
    return IMG_SUCCESS;
  }

  low_light_detected = p_client->low_light.low_light_scene;
  if (idx < p_client->low_light.last_lux_index) {
    if (idx < p_client->p_fd_chromatix->low_light_start_lux_idx) {
      low_light_detected = FALSE;
    }
  } else {
    if (idx > p_client->p_fd_chromatix->low_light_end_lux_idx) {
      low_light_detected = TRUE;
    }
  }
  p_client->low_light.last_lux_index = idx;

  if (p_client->low_light.low_light_scene != low_light_detected) {
    if (low_light_detected) {
      p_stream_select = &faceproc_defs[p_client->def_id].low_light;
    } else {
      p_stream_select = &faceproc_defs[p_client->def_id].normal;
    }
    module_faceproc_client_config_streams(p_client, p_stream_select);
    p_client->low_light.low_light_scene = low_light_detected;
  }

  IDBG_MED("%s:%d] lux idx %f low light_scene %d  start %f end %f",
    __func__, __LINE__, idx, low_light_detected,
    p_client->p_fd_chromatix->low_light_start_lux_idx,
    p_client->p_fd_chromatix->low_light_end_lux_idx);

  pthread_mutex_unlock(&p_client->mutex);

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_handle_sof
 *
 * Description: Function to handle ISP SOF (Start of Frame)
 *
 * Arguments:
 *   @p_client: faceproc client pointer
 *   @sof_event: SOF event
 *
 * Return values:
 *     error/success
 *
 * Notes: none
 **/
int module_faceproc_client_handle_sof(faceproc_client_t *p_client,
  unsigned int frame_id)
{
  cam_face_detect_mode_t facedetect_mode;
  int status = IMG_SUCCESS;
  mct_module_t *p_mct_mod;
  unsigned int sessionid;
  fd_pfc_data_t *p_fpcdata;
  img_queue_t *p_q;
  combined_cam_face_detection_data_t lface_data;
  combined_cam_face_detection_data_t *p_face_data = &p_client->last_stats;

  pthread_mutex_lock(&p_client->mutex);

  p_q = &p_client->p_fpc_q[frame_id % p_client->fpc_q_size];
  IDBG_MED("%s:%d] frame_id %d q_cnt %d", __func__, __LINE__,
    frame_id,
    img_q_count(p_q));

  p_client->fpc_mode = p_client->mode;
  while (NULL != (p_fpcdata = img_q_dequeue(p_q))) {

    if (p_fpcdata->ctrl_type == IMGLIB_APPLY) {
      switch (p_fpcdata->data_type) {
      case MOD_FD_FPC_MODE:{
        IMG_CLEAR_BIT(p_client->fd_apply_mask, MOD_FD_FPC_MODE);
        p_client->fd_apply_mask |= (p_fpcdata->d.mode << MOD_FD_FPC_MODE);
        if (IMG_IS_FD_ENABLED(p_client->fd_apply_mask)) {
          module_faceproc_client_set_mode(p_client, FACE_DETECT, FALSE);
          p_client->fpc_mode = FACE_DETECT;
        } else {
          module_faceproc_client_set_mode(p_client, FACE_DETECT_OFF, FALSE);
          p_client->fpc_mode = FACE_DETECT_OFF;
        }
        IDBG_MED("%s:%d:] MOD_FD_FPC_MODE, fd_apply_mode_mask 0x%x,"
          " fpc_mode %d",
          __func__, __LINE__, p_client->fd_apply_mask, p_client->fpc_mode);
        break;
      }
      case MOD_FD_FPC_SCENE_MODE: {
        IMG_CLEAR_BIT(p_client->fd_apply_mask, MOD_FD_FPC_SCENE_MODE);
        p_client->fd_apply_mask |= (p_fpcdata->d.mode << MOD_FD_FPC_SCENE_MODE);
        if (IMG_IS_FD_ENABLED(p_client->fd_apply_mask)) {
          module_faceproc_client_set_mode(p_client, FACE_DETECT, FALSE);
          p_client->fpc_mode = FACE_DETECT;
        } else {
          module_faceproc_client_set_mode(p_client, FACE_DETECT_OFF, FALSE);
          p_client->fpc_mode = FACE_DETECT_OFF;
        }
        IDBG_MED("%s:%d:] MOD_FD_FPC_SCENE_MODE, fd_apply_mode_mask 0x%x, "
          "fpc_mode %d",
          __func__, __LINE__, p_client->fd_apply_mask, p_client->fpc_mode);
        break;
      }
      default:;
      }
    } else if(p_fpcdata->ctrl_type == IMGLIB_POST) {
      switch (p_fpcdata->data_type) {
      case MOD_FD_FPC_MODE:
        IMG_CLEAR_BIT(p_client->fd_post_mask, MOD_FD_FPC_MODE);
        p_client->fd_post_mask |= (p_fpcdata->d.mode << MOD_FD_FPC_MODE);
        break;
      case MOD_FD_FPC_SCENE_MODE:
        IMG_CLEAR_BIT(p_client->fd_post_mask, MOD_FD_FPC_SCENE_MODE);
        p_client->fd_post_mask |= (p_fpcdata->d.mode << MOD_FD_FPC_SCENE_MODE);
        break;
      default:;
      }
    }
    free(p_fpcdata);
  }

  IDBG_MED("%s:%d] fpc_mode %d frame_id %d", __func__, __LINE__,
    p_client->fpc_mode, frame_id);
  if ((p_client->fpc_mode == FACE_DETECT) ||
    (p_client->fpc_mode == FACE_DETECT_LITE)){
    int rc;
    facedetect_mode = CAM_FACE_DETECT_MODE_SIMPLE;
    /* send to metadata buffer */
    if (p_client->state == IMGLIB_STATE_PROCESSING) {
      rc = module_faceproc_client_send_info(p_client, p_face_data);
      if (IMG_ERROR(rc)) {
        IDBG_MED("%s:%d] Face detection is not ready", __func__, __LINE__);
      }
    }

    IDBG_MED("%s:%d]test p_client->fd_ui_mode %d ", __func__, __LINE__,
      p_client->fd_ui_mode);
    if (!IMG_IS_HAL3(p_client) && p_client->fd_ui_mode ==
      CAM_FACE_DETECT_MODE_OFF) {
      facedetect_mode = CAM_FACE_DETECT_MODE_OFF;
      IDBG_MED("%s:%d] HAL Face detection UI Updates turned off",
        __func__, __LINE__);
      lface_data.fd_data.num_faces_detected = lface_data.fd_data.frame_id = 0;
      p_face_data = &lface_data;
    }
  } else {
    facedetect_mode = CAM_FACE_DETECT_MODE_OFF;
    /* update metadata */
    IDBG_MED("%s:%d] Face detection is turned off", __func__, __LINE__);
    lface_data.fd_data.num_faces_detected = lface_data.fd_data.frame_id = 0;
    p_face_data = &lface_data;
  }
  if (IMG_IS_HAL3(p_client) && (!IMG_IS_FD_ENABLED(p_client->fd_post_mask))) {
    facedetect_mode = CAM_FACE_DETECT_MODE_OFF;
    /* update metadata */
    IDBG_MED("%s:%d] HAL3 Face detection is turned off", __func__, __LINE__);
    lface_data.fd_data.num_faces_detected = lface_data.fd_data.frame_id = 0;
    p_face_data = &lface_data;
  }

  IDBG_HIGH("%s:%d] [FD_UI_DBG] facedetect_mode %d num_faces %d",
    __func__, __LINE__,
    facedetect_mode, p_face_data->fd_data.num_faces_detected);
  /* ToDo: update flag on demand */
  p_face_data->fd_data.update_flag = TRUE;

  IDBG_MED("%s:%d] Face detection frame_id %d result_id %d size %d",
    __func__, __LINE__,
    frame_id, p_client->last_stats.fd_data.frame_id,
    sizeof(combined_cam_face_detection_data_t));
  p_mct_mod =
    MCT_MODULE_CAST(MCT_PORT_PARENT(p_client->fp_stream[0].p_sink_port)->data);
  sessionid = IMGLIB_SESSIONID(p_client->fp_stream[0].identity);

  pthread_mutex_unlock(&p_client->mutex);

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_FACE_DETECT) {
    module_faceproc_post_bus_msg(p_mct_mod, sessionid,
      MCT_BUS_MSG_FACE_INFO, &(p_face_data->fd_data),
      sizeof(p_face_data->fd_data));
  }

  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_SMILE) {
    IDBG_MED("%s:%d] SM Coordinates for %d, %d", __func__, __LINE__,
      p_face_data->smile_data.smile[0].smile_degree,
      p_face_data->smile_data.smile[0].smile_confidence
      );
    module_faceproc_post_bus_msg(p_mct_mod, sessionid,
      MCT_BUS_MSG_FACE_INFO_SMILE, &(p_face_data->smile_data),
      sizeof(cam_face_smile_data_t));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_FACE_LANDMARKS) {
    module_faceproc_post_bus_msg(p_mct_mod, sessionid,
      MCT_BUS_MSG_FACE_INFO_FACE_LANDMARKS, &(p_face_data->fp_data),
      sizeof(cam_face_landmarks_data_t));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_BLINK) {
  module_faceproc_post_bus_msg(p_mct_mod, sessionid,
    MCT_BUS_MSG_FACE_INFO_BLINK, &(p_face_data->blink_data),
    sizeof(cam_face_blink_data_t));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_GAZE) {
    module_faceproc_post_bus_msg(p_mct_mod, sessionid,
      MCT_BUS_MSG_FACE_INFO_GAZE, &(p_face_data->gaze_data),
      sizeof(cam_face_gaze_data_t));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_CONTOUR) {
    module_faceproc_post_bus_msg(p_mct_mod, sessionid,
      MCT_BUS_MSG_FACE_INFO_CONTOUR, &(p_face_data->cont_data),
      sizeof(cam_face_contour_data_t));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_RECOG) {
    module_faceproc_post_bus_msg(p_mct_mod, sessionid,
      MCT_BUS_MSG_FACE_INFO_RECOG, &(p_face_data->fr_data),
      sizeof(cam_face_recog_data_t));
  }

  //select mode accordingly.
  module_faceproc_post_bus_msg(p_mct_mod, sessionid,
    MCT_BUS_MSG_SET_FACEDETECT_MODE, &facedetect_mode,
    sizeof(facedetect_mode));

  return status;
}

/**
 * Function: module_faceproc_client_buf_divert_exec
 *
 * Description: This function is for handling the buffers
 *            sent from the peer modules
 *
 * Arguments:
 *   @userdata: faceproc client
 *   @data: buffer divert info
 *
 * Return values:
 *     none
 *
 * Notes: none
 **/
void module_faceproc_client_buf_divert_exec(void *userdata, void *data)
{
  faceproc_client_t *p_client = (faceproc_client_t *)userdata;
  mod_img_msg_buf_divert_t *p_divert = (mod_img_msg_buf_divert_t *)data;
  boolean rc;

  rc = module_faceproc_client_handle_buf_divert(p_client, p_divert);

  /* Send the buffer back to previous module */
  if (rc) {
    module_faceproc_client_send_ack_event(p_client, p_divert);
  }
}

/**
 * Function: module_faceproc_client_flush
 *
 * Description: This function is used to flush the faceproc
 *              buffer events
 *
 * Arguments:
 *   @p_nodedata: faceproc message
 *   @p_userdata:faceproc client
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_flush(void *p_nodedata, void *p_userdata)
{
  mod_img_msg_t *p_msg = (mod_img_msg_t *)p_nodedata;
  faceproc_client_t *p_client = (faceproc_client_t *)p_userdata;

  if (!p_msg || !p_client)
    return IMG_SUCCESS;

  if (p_msg->type == MOD_IMG_MSG_DIVERT_BUF) {
    IDBG_MED("%s:%d] ", __func__, __LINE__);
    module_faceproc_client_send_ack_event(p_client,
      &p_msg->data.buf_divert);
  }

  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_add_stream
 *
 * Description: Function used to add stream to client.
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @p_stream_info: Stream info
 *   @p_port: Port for this stream
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_add_stream(faceproc_client_t *p_client,
  mct_stream_info_t *p_stream_info, mct_port_t *p_port)
{
  int s_idx;
  int def_id;
  int ret;

  pthread_mutex_lock(&p_client->mutex);

  def_id = p_client->def_id;
  s_idx = p_client->ref_count;

  p_client->fp_stream[s_idx].buff_configured = 0;
  p_client->fp_stream[s_idx].mapped = 0;
  p_client->fp_stream[s_idx].identity = p_stream_info->identity;
  p_client->fp_stream[s_idx].stream_info = p_stream_info;
  p_client->fp_stream[s_idx].p_sink_port = p_port;

  p_client->fp_stream[s_idx].processing_stream = FALSE;
  p_client->fp_stream[s_idx].reporting_stream = FALSE;
  p_client->fp_stream[s_idx].use_int_bufs = FALSE;
  p_client->ref_count++;

  /* Reset low light when new stream is added */
  p_client->low_light.low_light_scene = FALSE;
  ret = module_faceproc_client_config_streams(p_client,
    &faceproc_defs[def_id].normal);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("%s:%d] Fail to configure streams", __func__, __LINE__);
  }

  pthread_mutex_unlock(&p_client->mutex);
  return ret;
}

/**
 * Function: module_faceproc_client_remove_stream
 *
 * Description: Function used to remove stream from the client.
 *
 * Arguments:
 *   @p_client: faceproc client
 *   @identity: Identity.
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_faceproc_client_remove_stream(faceproc_client_t *p_client,
  unsigned int identity)
{
  int s_idx;
  int last_idx;

  pthread_mutex_lock(&p_client->mutex);

  s_idx = faceproc_get_stream_by_id(p_client, identity);
  if ((s_idx < 0) || (p_client->ref_count == 0)) {
    IDBG_ERROR("%s:%d] Invalid index for identity %x ref cnt %d",
      __func__, __LINE__, identity, p_client->ref_count);
    pthread_mutex_unlock(&p_client->mutex);
    return IMG_ERR_INVALID_OPERATION;
  }

  last_idx = p_client->ref_count - 1;

  /* Copy last stream in the current and remove last */
  if (last_idx > 0) {
    p_client->fp_stream[s_idx].identity =
      p_client->fp_stream[last_idx].identity;

    p_client->fp_stream[s_idx].stream_info =
      p_client->fp_stream[last_idx].stream_info;

    p_client->fp_stream[s_idx].p_sink_port =
      p_client->fp_stream[last_idx].p_sink_port;

    p_client->fp_stream[s_idx].processing_stream =
      p_client->fp_stream[last_idx].processing_stream;

    p_client->fp_stream[s_idx].reporting_stream =
      p_client->fp_stream[last_idx].reporting_stream;

    p_client->fp_stream[s_idx].use_int_bufs =
      p_client->fp_stream[last_idx].use_int_bufs;

    p_client->fp_stream[s_idx].mapped =
      p_client->fp_stream[last_idx].mapped;

    p_client->fp_stream[s_idx].buff_configured =
      p_client->fp_stream[last_idx].buff_configured;

    p_client->fp_stream[s_idx].buff_divert_requested =
      p_client->fp_stream[last_idx].buff_divert_requested;

    /* p_buffer is pointer with buf count holders */
    memcpy(p_client->fp_stream[s_idx].buffer_info.p_buffer,
      p_client->fp_stream[last_idx].buffer_info.p_buffer,
      sizeof(p_client->fp_stream[last_idx].buffer_info.p_buffer[0]) *
      p_client->fp_stream[last_idx].buffer_info.buf_count);
  }

  /* Remove last stream content */
  p_client->fp_stream[last_idx].identity = 0;
  p_client->fp_stream[last_idx].stream_info = NULL;
  p_client->fp_stream[last_idx].p_sink_port = NULL;
  p_client->fp_stream[last_idx].processing_stream = FALSE;
  p_client->fp_stream[last_idx].reporting_stream = FALSE;
  p_client->fp_stream[last_idx].use_int_bufs = FALSE;
  p_client->fp_stream[last_idx].mapped = 0;
  p_client->fp_stream[last_idx].buff_configured = 0;
  p_client->fp_stream[last_idx].buff_divert_requested = 0;

  p_client->ref_count--;
  pthread_mutex_unlock(&p_client->mutex);
  return IMG_SUCCESS;
}

/**
 * Function: module_faceproc_client_reset_face_info
 *
 * Description: resets the parts of combined face detection data
 *  based on face info mask
 *
 * Arguments:
 *   @p_client: faceproc client.
 *   @p_face_data: face data to be reset
 *
 * Return values:
 *    none
 *
 * Notes: none
 **/
void module_faceproc_client_reset_face_info(faceproc_client_t *p_client,
 combined_cam_face_detection_data_t *p_face_data)
{
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_FACE_DETECT) {
    memset(&(p_face_data->fd_data), 0x0, sizeof(p_face_data->fd_data));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_FACE_LANDMARKS) {
    memset(&(p_face_data->fp_data), 0x0, sizeof(p_face_data->fp_data));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_BLINK) {
    memset(&(p_face_data->blink_data), 0x0, sizeof(p_face_data->blink_data));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_SMILE) {
    memset(&(p_face_data->smile_data), 0x0, sizeof(p_face_data->smile_data));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_GAZE) {
    memset(&(p_face_data->gaze_data), 0x0, sizeof(p_face_data->gaze_data));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_CONTOUR) {
    memset(&(p_face_data->cont_data), 0x0, sizeof(p_face_data->cont_data));
  }
  if (p_client->config.fd_face_info_mask & FACE_INFO_MASK_RECOG) {
    memset(&(p_face_data->fr_data), 0x0, sizeof(p_face_data->fr_data));
  }
}
