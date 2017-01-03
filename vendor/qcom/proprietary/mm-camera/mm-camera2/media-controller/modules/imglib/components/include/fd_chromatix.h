/***************************************************************************
* Copyright (c) 2013-2016 Qualcomm Technologies, Inc.
* All Rights Reserved.
* Confidential and Proprietary - Qualcomm Technologies, Inc.
***************************************************************************/


#ifndef __FD_CHROMATIX_H__
#define __FD_CHROMATIX_H__

/**
 *   face detection modes
 **/
#define FD_CHROMATIX_MODE_MOTION_DEFAULT 1
#define FD_CHROMATIX_MODE_MOTION_FULL 1
#define FD_CHROMATIX_MODE_MOTION_PARTITION 2
#define FD_CHROMATIX_MODE_MOTION_PROGRESS 3

/**
 *   region definition for dymanic face detection
 *   Currently we impelent 3 region to filter face
 **/
#define FD_CHROMATIX_MAX_REGION_NUM 3

/** fd_chromatix_angle_t
 *   FD_FACE_ADJ_FIXED: Use fixed minface size
 *   FD_FACE_ADJ_FLOATING: apply the ratio for the face size
 *
 *   face angles for detection
 **/
typedef enum {
  FD_FACE_ADJ_FIXED,
  FD_FACE_ADJ_FLOATING,
} fd_face_adj_type_t;

/** fd_chromatix_angle_t
 *   FD_ANGLE_NONE: disable angle detection
 *   FD_ANGLE_15_ALL: 15 degree detection in all angles
 *   FD_ANGLE_45_ALL: 45 degree detection in all angles
 *   FD_ANGLE_135_ALL: 135 degree detection in all angles
 *   FD_ANGLE_ALL: detection in all angles
 *
 *   face angles for detection
 **/
typedef enum {
  FD_ANGLE_NONE,
  FD_ANGLE_15_ALL,
  FD_ANGLE_45_ALL,
  FD_ANGLE_135_ALL,
  FD_ANGLE_ALL,
  FD_ANGLE_MANUAL,
} fd_chromatix_angle_t;

/** fd_chromatix_ct_detection_mode_t
 *   FD_CONTOUR_MODE_DEFAULT: All counters are detected
 *   FD_CONTOUR_MODE_EYE: Only eyes are detected
 *
 *   Contour detection mode structure
 **/
typedef enum {
  FD_CONTOUR_MODE_DEFAULT = 0,
  FD_CONTOUR_MODE_EYE = 1,
} fd_chromatix_ct_detection_mode_t;

/** fd_face_stab_filter_t
 *   FD_STAB_NO_FILTER: Without stabilization filter.
 *   FD_STAB_TEMPORAL: Temporal filter.
 *   FD_STAB_HYSTERESIS: Hysteresis.
 *   FD_STAB_AVERAGE: Average filter.
 *   FD_STAB_MEDIAN: Median filter.
 *
 *   Face stabilization filter type.
 **/
typedef enum {
  FD_STAB_NO_FILTER,
  FD_STAB_TEMPORAL,
  FD_STAB_HYSTERESIS,
  FD_STAB_AVERAGE,
  FD_STAB_MEDIAN,
} fd_face_stab_filter_t;

/** fd_chromatix_angle_t
 *   FD_STAB_EQUAL: Values will be marked as stable when two consecutive
 *     values are equal.
 *   FD_STAB_BIGGER: Values will be marked as stable if new values are
 *     bigger then old ones.
 *   FD_STAB_SMALLER: Values will be marked as stable if new values are
 *     smaller then old ones:
 *   FD_STAB_CLOSER_TO_REFERENCE:  Values will be marked as stable when
 *     the distance to reference is smaller.
 *   FD_STAB_CONTINUES_EQUAL: The same as FD_STAB_EQUAL but it works in continues mode.
 *   FD_STAB_CONTINUES_BIGGER: The same as FD_STAB_BIGGER but it works in continues
 *     mode, if values are stable and new bigger values are received.
 *     they will be updated.
 *   FD_STAB_CONTINUES_SMALLER: The same as FD_STAB_SMALLER but it works in continues
 *     mode, if values are stable and new smaller values are received.
 *     they will be updated.
 *   FD_STAB_CONTINUES_CLOSER_TO_REFERENCE: The same as FD_STAB_BIGGER but it
 *     works in continues mode, if values are stable and new closer to reference
 *     values are received they will be updated.
 *
 *   Face stabilization mode
 **/
typedef enum {
  FD_STAB_EQUAL,
  FD_STAB_SMALLER,
  FD_STAB_BIGGER,
  FD_STAB_CLOSER_TO_REFERENCE,
  FD_STAB_CONTINUES_EQUAL,
  FD_STAB_CONTINUES_SMALLER,
  FD_STAB_CONTINUES_BIGGER,
  FD_STAB_CONTINUES_CLOSER_TO_REFERENCE,
} fd_face_stab_mode_t;


/** fd_face_stab_params_t
 *   @enable: Enable stabilization.
 *   @mode: Stabilization mode.
 *   @threshold: Stabilization threshold (Within threshold new values will not
 *     be accepted).
 *   @state_cnt: Number of consecutive frames to wait
 *     for entry to became stable.
 *   @use_reference: Stabilize entry by reference
 *       (Current reference are eyes).
 *   @filter_type: Filter type to be used for stabilization
 *     @temp: Temporal filter:
 *       @num: Strength numerator.
 *       @denom: Strength denominator.
 *     @hyst: Hysteresis - Two rectangular zones available Zone A and Zone B.
 *       Requirement - zone A < zone B. Fields:
 *       @start_A: A Start point.
 *       @end_A: A end point.
 *       @start_B: B Start point.
 *       @end_B: B end point.
 *     @average: Average filter
 *       @history_length: History length of the filter.
 *     @median: Median filter
 *       @history_length: History length of the filter.
 *
 *   Structure which holds face stabilization tuning parameters.
 **/
typedef struct {
  uint32_t enable;
  fd_face_stab_mode_t mode;
  uint32_t threshold;
  uint32_t state_cnt;
  uint32_t use_reference;
  fd_face_stab_filter_t filter_type;
  union {
    struct {
      uint32_t num;
      uint32_t denom;
    } temp;
    struct {
      uint32_t start_A;
      uint32_t end_A;
      uint32_t start_B;
      uint32_t end_B;
    } hyst;
    struct {
      uint32_t history_length;
    } average;
    struct {
      uint32_t history_length;
    } median;
  };
} fd_face_stab_params_t;

/** fd_chromatix_region_filter_t
*   @enable: Enable region filter.
*   @max_face_num: max face num to detect
*   @p_region[N]: pecentage of region N
*   @w_region[N]: weight of the region N
*   @size_region[N]: minimun face size of region N
*
*   structure which holds the region filter tuning params
**/
typedef struct {
  uint32_t enable;
  uint32_t max_face_num;
  float p_region[FD_CHROMATIX_MAX_REGION_NUM];
  float w_region[FD_CHROMATIX_MAX_REGION_NUM];
  uint32_t size_region[FD_CHROMATIX_MAX_REGION_NUM];
} fd_face_region_filter_t;


/** fd_chromatix_filter_type_t
 *   FD_FILTER_TYPE_AVG: Average filter
 *   FD_FILTER_TYPE_MAX: Max filter
 *   FD_FILTER_TYPE_MIN: Min filter
 *   FD_FILTER_TYPE_MEDIAN: Median filter
 *   FD_FILTER_TYPE_TEMPORAL: Temporal filter
 *
 *   Filter type
 **/
typedef enum {
  FD_FILTER_TYPE_AVG = 0,
  FD_FILTER_TYPE_MAX = 1,
  FD_FILTER_TYPE_MIN = 2,
  FD_FILTER_TYPE_MEDIAN = 3,
  FD_FILTER_TYPE_TEMPORAL = 4,
} fd_chromatix_filter_type_t;

/** fd_chromatix_t
 *   @enable: flag to enable face detection
 *   @min_face_adj_type: minimum face size adjustment type
 *   @min_face_size: minimum face size
 *   @min_face_size_ratio: ratio of minimum face size w.r.t
 *                       image size
 *   @max_face_size: maximum face size
 *   &max_num_face_to_detect: maximum number of faces to detect
 *   @angle_front: angles to detect from front pose
 *   @angle_half_profile: angles to detect from front pose
 *   @angle_full_profile: angles to detect from front pose
 *   @detection_mode: detection mode
 *   @frame_skip: number of frames to skip
 *   @enable_smile_detection: flag to enable smile detection
 *   @enable_blink_detection: flag to enable blink detection
 *   @enable_gaze_detection: flag to enable gaze detection
 *   @search_density_nontracking: search density for face proc
 *                  algorithm for non-tracking mode 20-40.
 *   @search_density_tracking: search density for face proc
 *                  algorithm for tracking mode 20-40.
 *   @direction: enable/disable face tracking direction
 *   @refresh_count: interval count after which fresh face
 *                 search is done
 *   @threshold: detection threshold
 *   @non_tracking_threshold: Threshold for non-tracking faces first detected
 *   @facial_parts_threshold: Facial parts threshold
 *   @face_retry_count: Face retry count (when face is lost during motion)
 *   @head_retry_count: Head retry count (when head is lost during motion)
 *   @hold_count: Hold count (output previous face when face is lost)
 *   @lock_faces: lock Faces Enable
 *               (Faces are locked not allow other faces to replace them)
 *   @move_rate_threshold: Position Modify rate threshold
 *                (in percents) how smoothly face changes coordinates
 *   @initial_frame_no_skip_cnt: initial number of frame which
 *                             should not be skipped. This field
 *                             is needed depending on initial
 *                             detection latency. Note that the
 *                             field is valid only when
 *                             frame_skip > 0.
 *   @speed: Used only for HW FD. If set to 0 this parameter is ignored.
 *     Range: starting from 1 in incremental order (slower->faster).
 *   @input_pending_buf: Max number of allowed input pending buffers.
 *   @enable_facial_parts_assisted_face_filtering: flag to
 *                                               enable/disable
 *                                               facial parts
 *                                               assisted false
 *                                               positive
 *                                               filtering
 *   @enable_sw_assisted_face_filtering: flag to enable/disable
 *                                     SW assisted false
 *                                     positive filtering
 *   @assist_facial_weight_nose: weight for the nose confidence
 *                             to be considered for false
 *                             positive filtering
 *   @assist_facial_weight_face: weight for the face confidence
 *                             to be considered for false
 *                             positive filtering
 *   @assist_facial_eyes_filter_type: type of filter to be used
 *                                  for getting combined
 *                                  confidence for left/right
 *                                  eyes
 *   @assist_facial_nose_filter_type: type of filter to be used
 *                                  for getting combined
 *                                  confidence for left/right
 *                                  nose
 *   @assist_facial_min_face_threshold: minimum threshold to
 *                                    scale teh face confidence
 *                                    values
 *   @assist_sw_detect_threshold: Assisted sw face detection threshold
 *     0..1000
 *   @assist_sw_detect_box_border_perc: Assisted sw face detection
 *     border out of the face detection box in percents. 0..100.
 *   @assist_sw_detect_search_dens: Assisted sw face detection
 *    search density 20-40.
 *   @assist_sw_discard_frame_border: Border of the frame used for sw face
 *     detection.
 *   @assist_sw_discard_out_of_border: Assisted sw face detection
 *     discard faces if they are out of frame boundary.
 *
 *   @enable_contour_detection: flag to enable/disable contour
 *                            detection.
 *   @enable_low_light: Enable low light face detection.
 *   @low_light_start_lux_idx: Low light hysteresis region start.
 *   @low_light_end_lux_idx:  Low light hysteresis region end.
 *   @stab_enable: enable face stabilization
 *   @stab_history: Stabilization history depth (min is 2)
 *   @stab_conf: Face confidence stabilization parameters.
 *   @stab_pos: Face position stabilization parameters.
 *   @stab_size: Face size stabilization parameters.
 *   @stab_mouth: Mouth position stabilization parameters.
 *   @stab_smile: Smile degree stabilization parameters.
 *   @region_filter: filter the face in different region
 *   @enable_frame_batch_mode: Flag to enable frame batch mode
 *   @frame_batch_size: Batch size
 *   @backlite_boost_factor: Boost factor for backlite filtering
 *   @stats_filter_max_hold: Max frames to hold for stats
 *                         temporal filter
 *   @stats_filter_lock: Lock the stats filter
 *
 *   faceproc chromatix header
 **/
typedef struct {
  int8_t enable;
  fd_face_adj_type_t min_face_adj_type;
  uint32_t min_face_size;
  float min_face_size_ratio;
  uint32_t max_face_size;
  uint32_t max_num_face_to_detect;
  fd_chromatix_angle_t angle_front;
  fd_chromatix_angle_t angle_half_profile;
  fd_chromatix_angle_t angle_full_profile;
  uint32_t detection_mode;
  uint32_t frame_skip;
  uint32_t enable_smile_detection;
  uint32_t enable_blink_detection;
  uint32_t enable_gaze_detection;
  uint32_t search_density_nontracking;
  uint32_t search_density_tracking;
  uint32_t direction;
  uint32_t refresh_count;
  uint32_t threshold;
  uint32_t non_tracking_threshold;
  uint32_t facial_parts_threshold;
  uint32_t closed_eye_ratio_threshold;
  uint32_t face_retry_count;
  uint32_t head_retry_count;
  uint32_t hold_count;
  uint32_t lock_faces;
  uint32_t move_rate_threshold;
  uint32_t initial_frame_no_skip_cnt;
  fd_chromatix_ct_detection_mode_t ct_detection_mode;
  uint32_t speed;
  uint32_t input_pending_buf;
  uint32_t enable_facial_parts_assisted_face_filtering;
  uint32_t enable_sw_assisted_face_filtering;
  uint32_t assist_below_threshold;
  uint32_t assist_facial_discard_threshold;
  float assist_facial_weight_mouth;
  float assist_facial_weight_eyes;
  float assist_facial_weight_nose;
  float assist_facial_weight_face;
  fd_chromatix_filter_type_t assist_facial_eyes_filter_type;
  fd_chromatix_filter_type_t assist_facial_nose_filter_type;
  uint32_t assist_facial_min_face_threshold;
  uint32_t assist_sw_detect_threshold;
  uint32_t assist_sw_detect_box_border_perc;
  uint32_t assist_sw_detect_search_dens;
  uint32_t assist_sw_discard_frame_border;
  uint32_t assist_sw_discard_out_of_border;
  uint32_t enable_contour_detection;
  uint32_t enable_low_light;
  float low_light_start_lux_idx;
  float low_light_end_lux_idx;
  int8_t stab_enable;
  uint32_t stab_history;
  fd_face_stab_params_t stab_conf;
  fd_face_stab_params_t stab_pos;
  fd_face_stab_params_t stab_size;
  fd_face_stab_params_t stab_mouth;
  fd_face_stab_params_t stab_smile;
  fd_face_region_filter_t region_filter;
  uint32_t enable_frame_batch_mode;
  uint32_t frame_batch_size;
  uint32_t enable_boost;
  float backlite_boost_factor;
  uint32_t stats_filter_max_hold;
  uint32_t stats_filter_lock;
  uint32_t ui_filter_max_hold;

} fd_chromatix_t;

#endif //__FD_CHROMATIX_H__
