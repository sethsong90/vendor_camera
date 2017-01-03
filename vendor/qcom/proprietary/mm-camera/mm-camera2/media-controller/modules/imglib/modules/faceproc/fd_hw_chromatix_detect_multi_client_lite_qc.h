/**********************************************************************
*  Copyright (c) 2015-2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/
/* Face detection enable */
.enable = 1,
.min_face_size = 40,
.angle_front = FD_ANGLE_45_ALL,
.max_num_face_to_detect = 5,
.angle_half_profile = FD_ANGLE_NONE,
.angle_full_profile = FD_ANGLE_NONE,
.frame_skip = 10,
.enable_smile_detection = 0,
.enable_blink_detection = 0,
.enable_gaze_detection = 0,
.direction = 0,
.threshold = 500,
.facial_parts_threshold = 0,
.closed_eye_ratio_threshold = 1000,
.move_rate_threshold = 8,
.initial_frame_no_skip_cnt = 15,
.ct_detection_mode = FD_CONTOUR_MODE_EYE,
.speed = 2,
.enable_frame_batch_mode = FACE_FRAME_BATCH_MODE_OFF,
.frame_batch_size = 1,
.backlite_boost_factor = 1,
.stats_filter_lock = 0,

/* Stabilization parameters */
.stab_enable = 1,
.stab_history = 3,
/* Confidence tuning params, used to filter false positives */
.stab_conf = {
  .enable = 1,
  .filter_type = FD_STAB_HYSTERESIS,
  .hyst = {
    .start_A = 500,
    .end_A = 700,
  },
},
/* Position stabilization tuning params */
.stab_pos = {
  .enable = 1,
  .mode = FD_STAB_EQUAL,
  .state_cnt = 0,
  .threshold = 15,
  .use_reference = 1,
  .filter_type = FD_STAB_NO_FILTER,
},
/* Size stabilization tuning params */
.stab_size = {
  .enable = 1,
  .mode = FD_STAB_CONTINUES_SMALLER,
  .state_cnt = 3,
  .threshold = 500,
  .use_reference = 0,
  .filter_type = FD_STAB_TEMPORAL,
  .temp = {
    .num = 6,
    .denom = 8,
  },
},
/* Mouth stabilization tuning params */
.stab_mouth = {
  .enable = 1,
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
  .state_cnt = 0,
  .threshold = 4,
  .use_reference = 0,
  .filter_type = FD_STAB_HYSTERESIS,
  .hyst = {
    .start_A = 25,
    .end_A = 35,
    .start_B = 55,
    .end_B = 65,
  },
},
