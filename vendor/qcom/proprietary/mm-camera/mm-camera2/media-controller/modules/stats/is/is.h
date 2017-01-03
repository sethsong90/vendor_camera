/* is.h
 *
 * Copyright (c) 2013 - 2014 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __IS_H__
#define __IS_H__
#include "dis_interface.h"
#include "eis_interface.h"
#include "eis2_interface.h"
#include "stats_debug.h"
#include "q3a_stats.h"


/** _is_info:
 *    @video_stream_on: Indicates whether video stream (stream with type
 *       CAM_STREAM_TYPE_VIDEO) is on.
 *    @is_mode: IS method (DIS, gyro-assisted DIS, EIS 1.0)
 *    @rs_cs_frame_id: latest frame id for which RS/CS stats was received
 *    @timestamp: RS/CS stats timestamp
 *    @rs_cs_data: RS/CS stats cache
 *    @gyro_frame_id: latest frame id for which gyro data was received
 *    @gyro_data: gyro data cache
 *    @dis_context: DIS context
 *    @eis_context: EIS 1.0 context
 *    @eis2_context: EIS 2.0 context
 *    @vfe_width: VFE width (image width + margin)
 *    @vfe_height: VFE height (image height + margin)
 *    @width: image width
 *    @height: image height
 *    @preview_width: preview width
 *    @preview_height: preview height
 *    @num_row_sum: number of row sums
 *    @num_col_sum: number of column sums
 *    @sensor_mount_angle: sensor mount angle (0, 90, 180, 270)
 *    @camera_position: camera position (front or back)
 *    @is_enabled: indicates whether IS is enabled
 *    @is_inited: indicates whether IS has been initialized
 *    @sns_lib_offset_set: Indicates whether time offset of gyro sensor library
 *       has been set
 *    @dis_bias_correction: Indicates whether DIS bias correction is enabled
 *       (applies only to EIS 2)
 *    @is_chromatix_info: IS tuning parameters
 *
 * This structure represents the IS internal variables.
 **/
typedef struct _is_info {
  boolean video_stream_on;
  cam_is_type_t is_mode;
  unsigned int rs_cs_frame_id;
  struct timeval timestamp;
  rs_cs_data_t rs_cs_data;
  unsigned int gyro_frame_id;
  mct_event_gyro_data_t gyro_data;
  dis_context_type dis_context;
  eis_context_type eis_context;
  eis2_context_type eis2_context;
  unsigned int transform_type;
  long vfe_width;
  long vfe_height;
  long is_width;
  long is_height;
  long preview_width;
  long preview_height;
  unsigned int num_row_sum;
  unsigned int num_col_sum;
  unsigned int sensor_mount_angle;
  enum camb_position_t camera_position;
  unsigned int is_enabled;
  unsigned int is_inited;
  unsigned int sns_lib_offset_set;
  uint32_t dis_bias_correction;
  is_chromatix_info_t is_chromatix_info;
} is_info_t;


typedef enum {
  IS_PROCESS_STREAM_EVENT = 1,
  IS_PROCESS_RS_CS_STATS,
  IS_PROCESS_GYRO_STATS
} is_process_parameter_type;


typedef enum {
  IS_VIDEO_STREAM_OFF = 1,
  IS_VIDEO_STREAM_ON,
  IS_OTHER_STREAM_ON_OFF
} is_stream_event_t;


typedef struct _is_stream_event_data {
  is_stream_event_t stream_event;
  is_info_t *is_info;
} is_stream_event_data_t;


typedef struct _is_stats_data {
  unsigned int frame_id;
  unsigned int identity;
  mct_port_t *port;
  is_info_t *is_info;
  stats_yuv_stats_t yuv_rs_cs_data;
  void*             ack_data;
} is_stats_data_t;


typedef struct _is_gyro_data {
  unsigned int frame_id;
  is_info_t *is_info;
  mct_event_gyro_data_t gyro_data;
} is_gyro_data_t;


typedef struct _is_process_parameter {
  is_process_parameter_type type;

  union {
    is_stream_event_data_t stream_event_data;
    is_stats_data_t stats_data;
    is_gyro_data_t gyro_data;
  } u;
} is_process_parameter_t;


/** is_process_output_type
 **/
typedef enum {
  IS_PROCESS_OUTPUT_STREAM_EVENT = 1,
  IS_PROCESS_OUTPUT_RS_CS_STATS,
  IS_PROCESS_OUTPUT_GYRO_STATS,
} is_process_output_type_t;


typedef struct _is_process_output {
  is_process_output_type_t  type;
  is_stream_event_t         is_stream_event;
  is_output_type *          is_output;
} is_process_output_t;


typedef boolean (*is_set_parameters_func)(is_set_parameter_t *param,
  is_info_t *is_info);

typedef boolean (*is_process_func)(is_process_parameter_t *param,
  is_process_output_t *output);

typedef void (*is_callback_func)(mct_port_t *port,
  is_process_output_t *output);

typedef void (*is_stats_done_callback_func)(void* port, void *stats);

boolean is_set_parameters(is_set_parameter_t *param, is_info_t *is_info);
boolean is_process(is_process_parameter_t *param, is_process_output_t *output);

#endif /* __IS_H__ */
