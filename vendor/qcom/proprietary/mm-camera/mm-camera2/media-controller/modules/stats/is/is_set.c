/* is_set.c
 *
 * Copyright (c) 2013 - 2014 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#include "is.h"
#include "chromatix_3a.h"

static boolean is_set_init_chromatix(is_info_t *is_info,
  is_set_parameter_init_t *init_param)
{
  if (is_info == NULL || init_param == NULL
      || init_param->chromatix == NULL) {
    return FALSE;
  }

  Chromatix_EIS_algo_type *chromatix_EIS =
    (Chromatix_EIS_algo_type *)init_param->chromatix;

  is_info->is_chromatix_info.focal_length =
    chromatix_EIS->focal_length;
  is_info->is_chromatix_info.gyro_pixel_scale =
    chromatix_EIS->gyro_pixel_scale;
  is_info->is_chromatix_info.gyro_noise_floor =
    chromatix_EIS->gyro_noise_floor;
  is_info->is_chromatix_info.gyro_frequency =
    chromatix_EIS->gyro_frequency;
  is_info->is_chromatix_info.virtual_margin =
    chromatix_EIS->virtual_margin;
  is_info->is_chromatix_info.rs_offset_1 =
    chromatix_EIS->rs_offset_1;
  is_info->is_chromatix_info.rs_offset_2 =
    chromatix_EIS->rs_offset_2;
  is_info->is_chromatix_info.rs_offset_3 =
    chromatix_EIS->rs_offset_3;
  is_info->is_chromatix_info.rs_offset_4 =
    chromatix_EIS->rs_offset_4;
  is_info->is_chromatix_info.s3d_offset_1 =
    chromatix_EIS->s3d_offset_1;
  is_info->is_chromatix_info.s3d_offset_2 =
    chromatix_EIS->s3d_offset_2;
  is_info->is_chromatix_info.s3d_offset_3 =
    chromatix_EIS->s3d_offset_3;
  is_info->is_chromatix_info.s3d_offset_4 =
    chromatix_EIS->s3d_offset_4;
  is_info->is_chromatix_info.rs_threshold_1 =
    chromatix_EIS->rs_threshold_1;
  is_info->is_chromatix_info.rs_threshold_2 =
    chromatix_EIS->rs_threshold_2;
  is_info->is_chromatix_info.rs_threshold_3 =
    chromatix_EIS->rs_threshold_3;
  is_info->is_chromatix_info.s3d_threshold_1 =
    chromatix_EIS->s3d_threshold_1;
  is_info->is_chromatix_info.s3d_threshold_2 =
    chromatix_EIS->s3d_threshold_2;
  is_info->is_chromatix_info.s3d_threshold_3 =
    chromatix_EIS->s3d_threshold_3;
  is_info->is_chromatix_info.rs_time_interval_1 =
    chromatix_EIS->rs_time_interval_1;
  is_info->is_chromatix_info.rs_time_interval_2 =
    chromatix_EIS->rs_time_interval_2;
  is_info->is_chromatix_info.rs_time_interval_3 =
    chromatix_EIS->rs_time_interval_3;
  is_info->is_chromatix_info.rs_time_interval_4 =
    chromatix_EIS->rs_time_interval_4;
  is_info->is_chromatix_info.reserve_1 =
    chromatix_EIS->reserve_1;
  is_info->is_chromatix_info.reserve_2 =
    chromatix_EIS->reserve_2;
  is_info->is_chromatix_info.reserve_3 =
    chromatix_EIS->reserve_3;
  is_info->is_chromatix_info.reserve_4 =
    chromatix_EIS->reserve_4;

  return TRUE;
}

/** is_set_parameters:
 *    @param: information about parameter to be set
 *    @is_info: IS information
 *
 * Returns TRUE ons success
 **/
boolean is_set_parameters(is_set_parameter_t *param, is_info_t *is_info)
{
  boolean rc = TRUE;

  switch (param->type) {
  case IS_SET_PARAM_STREAM_CONFIG:
     IS_LOW("IS_SET_PARAM_STREAM_CONFIG, ma = %u, p = %d",
      param->u.is_sensor_info.sensor_mount_angle,
      param->u.is_sensor_info.camera_position);
    is_info->sensor_mount_angle = param->u.is_sensor_info.sensor_mount_angle;
    is_info->camera_position = param->u.is_sensor_info.camera_position;
    break;

  case IS_SET_PARAM_DIS_CONFIG:
    if (param->u.is_config_info.stream_type == CAM_STREAM_TYPE_VIDEO) {
      is_info->is_width = param->u.is_config_info.width;
      is_info->is_height = param->u.is_config_info.height;
      IS_HIGH("IS_SET_PARAM_DIS_CONFIG, vwidth = %ld, vheight = %ld",
        is_info->is_width, is_info->is_height);
    } else {
      /* Must be CAM_STREAM_TYPE_PREVIEW because other types are filtered out
         by poster of this event */
      is_info->preview_width = param->u.is_config_info.width;
      is_info->preview_height = param->u.is_config_info.height;
      IS_HIGH("IS_SET_PARAM_DIS_CONFIG, pwidth = %ld, pheight = %ld",
        is_info->preview_width, is_info->preview_height);
    }
    break;

  case IS_SET_PARAM_OUTPUT_DIM:
    is_info->is_mode = param->u.is_output_dim_info.is_mode;
    is_info->vfe_width = param->u.is_output_dim_info.vfe_width;
    is_info->vfe_height = param->u.is_output_dim_info.vfe_height;
     IS_LOW("IS_SET_PARAM_OUTPUT_DIM, is mode = %d, w = %ld, h = %ld",
      is_info->is_mode, is_info->is_width, is_info->is_height);
    break;

  case IS_SET_PARAM_CHROMATIX: {
    is_set_init_chromatix(is_info, &param->u.is_init_param);

     IS_LOW("IS_SET_PARAM_CHROMATIX\nvirtual margin = %f\n"
      "gyro noise floor = %f\ngyro pixel scale = %d",
      is_info->is_chromatix_info.virtual_margin,
      is_info->is_chromatix_info.gyro_noise_floor,
      is_info->is_chromatix_info.gyro_pixel_scale);
    }
    break;

  case IS_SET_PARAM_IS_ENABLE:
    is_info->is_enabled = param->u.is_enable;
     IS_LOW("IS_SET_PARAM_IS_ENABLE, IS enable = %u",
      is_info->is_enabled);
    break;

  default:
    rc = FALSE;
    break;
  }

  return rc;
} /* is_set_parameters */

