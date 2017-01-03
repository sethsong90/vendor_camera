/**********************************************************************
*  Copyright (c) 2015-2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/

.enable = 1,
.min_face_size = 20,
.max_face_size = 1000,
.max_num_face_to_detect = 10,
.angle_front = FD_ANGLE_ALL,
.angle_half_profile = FD_ANGLE_ALL,
.angle_full_profile = FD_ANGLE_NONE,
.frame_skip = 0,
.enable_smile_detection = 1,
.enable_blink_detection = 1,
.enable_gaze_detection = 1,
.direction = 0,
.threshold = 500,
.non_tracking_threshold = 700,
.facial_parts_threshold = 700,
.closed_eye_ratio_threshold = 600,
.initial_frame_no_skip_cnt = 15,
.ct_detection_mode = FD_CONTOUR_MODE_DEFAULT,
.lock_faces = 1,
.speed = 3,
.input_pending_buf = 1,
.enable_facial_parts_assisted_face_filtering = 1,
.assist_below_threshold = 1000,
.assist_facial_discard_threshold = 400,
.assist_facial_weight_mouth = 0.25,
.assist_facial_weight_eyes = 0.25,
.assist_facial_weight_nose = 0.25,
.assist_facial_weight_face = 0.25,
.assist_facial_eyes_filter_type = FD_FILTER_TYPE_MAX,
.assist_facial_nose_filter_type = FD_FILTER_TYPE_MAX,
.assist_facial_min_face_threshold = 400,
.enable_contour_detection = 0,
.assist_sw_detect_threshold = 520,
.assist_sw_detect_box_border_perc = 30,
.assist_sw_detect_search_dens = 20,
.assist_sw_discard_frame_border = 0,
.assist_sw_discard_out_of_border = 0,
.enable_sw_assisted_face_filtering = 1,
.stats_filter_max_hold = 3,
.enable_frame_batch_mode = FACE_FRAME_BATCH_MODE_OFF,
.enable_boost = 0,
.frame_batch_size = 1,
.backlite_boost_factor = 1.0f,
.stats_filter_lock = 0,
.ui_filter_max_hold = 3,

/* Stabilization parameters */
.stab_enable = 1,
.stab_history = 7,
/* Position stabilization tuning params */
.stab_pos = {
  .enable = 1,
  .mode = FD_STAB_CLOSER_TO_REFERENCE,
  .state_cnt = 0,
  .threshold = 15,
  .filter_type = FD_STAB_NO_FILTER,
},
/* Size stabilization tuning params */
.stab_size = {
  .enable = 1,
  .mode = FD_STAB_CLOSER_TO_REFERENCE,
  .state_cnt = 1,
  .threshold = 300,
  .use_reference = 1,
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
/* region filter tuning params */
.region_filter = {
.enable = 0,
.max_face_num = 10,
.p_region = {0.5, 0.8, 1},
.w_region = {100, 0.8, 0.5},
.size_region = {40, 80, 120},
},

