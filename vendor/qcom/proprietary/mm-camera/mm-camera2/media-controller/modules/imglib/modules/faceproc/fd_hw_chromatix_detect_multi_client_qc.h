/**********************************************************************
*  Copyright (c) 2015-2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/

/* Face detection enable */
.enable = 1,
.min_face_size = 20,
.max_face_size = 1000,
.max_num_face_to_detect = 5,
.angle_front = FD_ANGLE_45_ALL,
.angle_half_profile = FD_ANGLE_15_ALL,
.angle_full_profile = FD_ANGLE_NONE,
.frame_skip = 2,
.enable_smile_detection = 0,
.enable_blink_detection = 0,
.enable_gaze_detection = 0,
.direction = 0,
.threshold = 500,
.non_tracking_threshold = 700,
.facial_parts_threshold = 700,
.closed_eye_ratio_threshold = 600,
.initial_frame_no_skip_cnt = 15,
.ct_detection_mode = FD_CONTOUR_MODE_EYE,
.lock_faces = 1,
.speed = 2,
.enable_facial_parts_assisted_face_filtering = 1,
.assist_below_threshold = 900,
.assist_facial_discard_threshold = 500,
.assist_facial_weight_mouth = 0.25,
.assist_facial_weight_eyes = 0.25,
.assist_facial_weight_nose = 0.25,
.assist_facial_weight_face = 0.25,
.assist_facial_eyes_filter_type = FD_FILTER_TYPE_MAX,
.assist_facial_nose_filter_type = FD_FILTER_TYPE_MAX,
.assist_facial_min_face_threshold = 400,
.enable_contour_detection = 0,
.stats_filter_max_hold = 10,
.enable_frame_batch_mode = FACE_FRAME_BATCH_MODE_OFF,
.frame_batch_size = 1,
.backlite_boost_factor = 1,
.stats_filter_lock = 0,

/* Stabilization parameters */
.stab_enable = 1,
.stab_history = 3,
/* Position stabilization tuning params */
.stab_pos = {
  .enable = 1,
  .mode = FD_STAB_EQUAL,
  .state_cnt = 0,
  .threshold = 15,
  .filter_type = FD_STAB_NO_FILTER,
},
/* Size stabilization tuning params */
.stab_size = {
  .enable = 1,
  .mode = FD_STAB_CONTINUES_SMALLER,
  .state_cnt = 3,
  .threshold = 250,
  .use_reference = 0,
  .filter_type = FD_STAB_TEMPORAL,
  .temp = {
    .num = 8,
    .denom = 6,
  },
},
/* Mouth stabilization tuning params */
.stab_mouth = {
  .enable = 0,
  .mode = FD_STAB_CONTINUES_CLOSER_TO_REFERENCE,
  .state_cnt = 1,
  .threshold = 10,
  .use_reference = 1,
  .filter_type = FD_STAB_NO_FILTER,
},
/* Smile stabilization tuning params */
.stab_smile = {
  .enable = 1,
  .mode = FD_STAB_EQUAL,
  .state_cnt = 3,
  .threshold = 0,
  .use_reference = 0,
  .filter_type = FD_STAB_HYSTERESIS,
  .hyst = {
    .start_A = 25,
    .end_A = 35,
    .start_B = 55,
    .end_B = 65,
  },
},
