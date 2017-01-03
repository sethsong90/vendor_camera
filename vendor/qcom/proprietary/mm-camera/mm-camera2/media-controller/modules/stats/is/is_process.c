/* is_process.c
 *
 * Copyright (c) 2013 - 2015 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#include "is.h"
/* This should be declared in sensor_lib.h */
void poke_gyro_sample(uint64_t t, int32_t gx, int32_t gy, int32_t gz);

/** is_process_is_initialize:
 *    @is_info: IS internal variables
 *
 * This function initializes IS.
 **/
static void is_process_is_initialize(is_info_t *is_info)
{
  int rc = 0;
  is_init_data_t is_init_data;

  is_init_data.frame_cfg.frame_fps = 30;
  is_init_data.frame_cfg.dis_frame_width = is_info->is_width;
  is_init_data.frame_cfg.dis_frame_height = is_info->is_height;
  is_init_data.frame_cfg.vfe_output_width = is_info->vfe_width;
  is_init_data.frame_cfg.vfe_output_height = is_info->vfe_height;
  is_init_data.rs_cs_config.num_row_sum = is_info->num_row_sum;
  is_init_data.rs_cs_config.num_col_sum = is_info->num_col_sum;
  is_init_data.is_mode = is_info->is_mode;
  is_init_data.sensor_mount_angle = is_info->sensor_mount_angle;
  is_init_data.camera_position = is_info->camera_position;
  is_init_data.dis_bias_correction = is_info->dis_bias_correction;
  memcpy(&is_init_data.is_chromatix_info, &is_info->is_chromatix_info,
    sizeof(is_chromatix_info_t));

  if (is_info->is_width == 0 || is_info->is_height == 0 ||
    is_info->vfe_width <= is_info->is_width ||
    is_info->vfe_height <= is_info->is_height) {
    IS_ERR("IS CRITICAL ERROR : IS did not get dimensions");
    rc = -1;
  }

  /* For now, DIS and EIS initialization need to succeed */
  memset(&is_info->dis_context, 0, sizeof(dis_context_type));

  /* If IS is enabled but user did not select IS technology preference, default
     to DIS. */
  if (is_info->is_mode == IS_TYPE_NONE) {
    is_info->is_mode = IS_TYPE_DIS;
     IS_HIGH("Default to DIS");
  }

  if ((is_info->is_mode != IS_TYPE_EIS_2_0 || is_info->dis_bias_correction) &&
    (rc == 0)) {
    rc = dis_initialize(&is_info->dis_context, &is_init_data);
  }

  if (rc == 0) {
    if (is_info->is_mode == IS_TYPE_EIS_1_0) {
      memset(&is_info->eis_context, 0, sizeof(eis_context_type));
      rc = eis_initialize(&is_info->eis_context, &is_init_data);
    } else if (is_info->is_mode == IS_TYPE_EIS_2_0) {
      memset(&is_info->eis2_context, 0, sizeof(eis2_context_type));
      rc = eis2_initialize(&is_info->eis2_context, &is_init_data);
    }
    if (rc == 0) {
      is_info->is_inited = 1;
       IS_HIGH("IS inited");
    }
    else if (is_info->is_mode != IS_TYPE_EIS_2_0 ||
      is_info->dis_bias_correction) {
        dis_exit(&is_info->dis_context);
    }
  }

  if (rc == 0 && is_info->is_mode != IS_TYPE_DIS) {
    sns_eis2_init(NULL);
    is_info->sns_lib_offset_set = 0;
  }

  if (rc != 0) {
    IS_ERR("IS initialization failed");
    /* Disable IS so we won't keep initializing and failing */
    is_info->is_enabled = 0;
  }
}


/** is_process is_deinitialize:
 *    @is_info: IS internal variables
 *
 * This function deinits IS.
 **/
static void is_process_is_deinitialize(is_info_t *is_info)
{
  if (is_info->is_mode != IS_TYPE_DIS) {
    sns_eis2_stop();
  }

  if (is_info->is_mode == IS_TYPE_EIS_1_0) {
    eis_deinitialize(&is_info->eis_context);
  } else if (is_info->is_mode == IS_TYPE_EIS_2_0) {
    eis2_deinitialize(&is_info->eis2_context);
  }

  if (is_info->is_mode != IS_TYPE_EIS_2_0 || is_info->dis_bias_correction) {
    dis_exit(&is_info->dis_context);
  }
  is_info->is_inited = 0;
  is_info->gyro_frame_id = 0;
  is_info->rs_cs_frame_id = 0;
   IS_HIGH("IS deinited");
}


/** is_process_run_gyro_dependent_is:
 *    @is_info: IS internal variables
 *    @rs_cs_data: RS/CS stats
 *    @gyro_data: gyro data
 *    @is_output: output of the event processing
 *
 * This function initializes IS.
 **/
static void is_process_run_gyro_dependent_is(is_info_t *is_info,
  rs_cs_data_t *rs_cs_data, mct_event_gyro_data_t *gyro_data,
  is_output_type *is_output)
{
  frame_times_t frame_times;
  unsigned int i;

  memset(&frame_times, 0, sizeof(frame_times_t));
  if (!is_info->sns_lib_offset_set) {
    set_sns_apps_offset(gyro_data->sample[0].timestamp);
    is_info->sns_lib_offset_set = 1;
  }

  /*Update is_output frame ID with gyro arrival fid*/
  is_output->frame_id = is_info->gyro_frame_id;

   IS_LOW("num_samples %d", gyro_data->sample_len);
  /* Update the gyro buffer */
  for (i = 0; i < gyro_data->sample_len; i++) {
    IS_LOW("Poking in gyro sample, %llu, %d, %d, %d",
      gyro_data->sample[i].timestamp, gyro_data->sample[i].value[0],
      gyro_data->sample[i].value[1], gyro_data->sample[i].value[2]);
    poke_gyro_sample(gyro_data->sample[i].timestamp,
      gyro_data->sample[i].value[0],
      gyro_data->sample[i].value[1],
      gyro_data->sample[i].value[2]);
  }

  frame_times.sof = gyro_data->sof;
  frame_times.exposure_time = gyro_data->exposure_time;
  frame_times.frame_time = gyro_data->frame_time;
   IS_LOW("gyro_data.sof = %llu", frame_times.sof);
  if (is_info->is_mode == IS_TYPE_EIS_1_0) {
    eis_process(&is_info->eis_context, &frame_times, is_output);
  } else if (is_info->is_mode == IS_TYPE_EIS_2_0) {
    eis2_process(&is_info->eis2_context, &frame_times, is_output);
  }

  if (is_info->is_mode != IS_TYPE_EIS_2_0 || is_info->dis_bias_correction) {
    dis_process(&is_info->dis_context, rs_cs_data, &frame_times, is_output);
  }
}


/** is_process_stats_event:
 *    @stats_data: RS/CS stats
 *    @is_output: output of the event processing
 *
 * If DIS is the selected IS algorithm, this function runs the DIS algorithm.
 * If the selected IS algorithm depends on gyro data (EIS), this functions runs
 * the IS algorithm only if the frame's gyro data is available.  In other
 * words, two items are are needed to run IS on a frame, frame's RS/CS stats and
 * frame's gyro samples.  This is the function that runs the IS algorithm when
 * the frame's gyro data arrives before the frame's RS/CS stats.
 *
 * Returns TRUE if the IS algorithm ran, FALSE otherwise.
 **/
static boolean is_process_stats_event(is_stats_data_t *stats_data,
  is_output_type *is_output)
{
  int rc = TRUE;
  struct timespec t_now;
  is_info_t *is_info = stats_data->is_info;

  if (!is_info->is_inited) {
    is_process_is_initialize(is_info);
  }

  if (is_info->is_inited) {
    clock_gettime( CLOCK_REALTIME, &t_now );
     IS_LOW("RS(%u) & CS(%u) ready, time = %llu, ts = %llu, id = %u",
      stats_data->is_info->num_row_sum, stats_data->is_info->num_col_sum,
      ((int64_t)t_now.tv_sec * 1000 + t_now.tv_nsec/1000000),
      ((int64_t)stats_data->is_info->timestamp.tv_sec * 1000 +
                stats_data->is_info->timestamp.tv_usec/1000),
      stats_data->frame_id);
    is_output->frame_id = stats_data->frame_id;
    is_info->rs_cs_frame_id = stats_data->frame_id;

    /* We only need rs/cs stats in this case, adding checker here to save CPU */
    if (is_info->is_mode != IS_TYPE_EIS_2_0 || is_info->dis_bias_correction) {
      /* downsample RS stats */
      is_rs_stats_t *to_rs_stats = NULL;
      q3a_rs_stats_t *from_q3a_rs_stats = NULL;
      uint32_t i = 0, j = 0;
      to_rs_stats = &is_info->rs_cs_data.rs_stats;
      from_q3a_rs_stats = stats_data->yuv_rs_cs_data.p_q3a_rs_stats;
      /* there is case the stats event sent to algo, but the stats buffer is NULL
         whose purpose is only to triger the IS processing.So do the stats validiaty
         check here */
      if (from_q3a_rs_stats) {
        /* Convert 8x 1024 RS to 1 x 1024*/
        for(i = 0; i < from_q3a_rs_stats->num_v_regions; i++)
          for(j = 0; j < from_q3a_rs_stats->num_h_regions; j++)
            to_rs_stats->row_sum[i] += from_q3a_rs_stats->row_sum[j][i];

        to_rs_stats->num_row_sum = is_info->num_row_sum;

        /* copy CS stats */
        memcpy(&is_info->rs_cs_data.cs_stats, stats_data->yuv_rs_cs_data.p_q3a_cs_stats,
            sizeof(q3a_cs_stats_t));
      }
    }
    if (is_info->is_mode != IS_TYPE_DIS) {
      if (is_info->gyro_frame_id >= is_info->rs_cs_frame_id) {
         IS_LOW("Gyro is ready, can run IS, gyro_fid = %u, rs_cs_fid = %u",
           is_info->gyro_frame_id, is_info->rs_cs_frame_id);
        is_process_run_gyro_dependent_is(is_info, &is_info->rs_cs_data,
          &is_info->gyro_data, is_output);
      } else {
        rc = FALSE;
         IS_LOW("Gyro not ready, can't run IS, gyro_fid = %u, rs_cs_fid = %u",
           is_info->gyro_frame_id, is_info->rs_cs_frame_id);
      }
    } else {
      frame_times_t frame_times;

      memset(&frame_times, 0, sizeof(frame_times_t));
      dis_process(&is_info->dis_context, &is_info->rs_cs_data,
        &frame_times, is_output);
    }
  }

  return rc;
}


/** is_process_gyro_stats_event:
 *    @gyro_stats_data: gyro data
 *    @is_output: output of the event processing
 *
 * If the selected IS algorithm depends on gyro data (EIS), this functions runs
 * the IS algorithm only if the frame's stats data is available.  In other
 * words, two items are are needed to run IS on a frame, frame's RS/CS stats and
 * frame's gyro samples.  This is the function that runs the IS algorithm when
 * the frame's RS/CS stats arrives before the frame's gyro data.
 *
 * Returns TRUE if the IS algorithm ran, FALSE otherwise.
 **/
static boolean is_process_gyro_stats_event(is_gyro_data_t *gyro_stats_data,
  is_output_type *is_output)
{
  int rc = TRUE;
  is_info_t *is_info = gyro_stats_data->is_info;

  is_info->gyro_frame_id = gyro_stats_data->frame_id;

  if (is_info->gyro_frame_id <= is_info->rs_cs_frame_id) {
     IS_LOW("Frame is ready, can run IS, gyro_fid = %u, rs_cs_fid = %u",
      is_info->gyro_frame_id, is_info->rs_cs_frame_id);
    is_process_run_gyro_dependent_is(is_info, &is_info->rs_cs_data,
      &gyro_stats_data->gyro_data, is_output);
  } else {
    rc = FALSE;
    /* Cache gyro data for when the frame is ready */
    memcpy(&is_info->gyro_data, &gyro_stats_data->gyro_data,
      sizeof(mct_event_gyro_data_t));
     IS_LOW("Frame not ready, can't run IS, gyro_fid = %u, rs_cs_fid = %u",
      is_info->gyro_frame_id, is_info->rs_cs_frame_id);
  }

  return rc;
}


/** is_process:
 *    @param: input event parameters
 *    @output: output of the event processing
 *
 * This function is the top level event handler.
 **/
boolean is_process(is_process_parameter_t *param, is_process_output_t *output)
{
  int rc = TRUE;

  switch (param->type) {
  case IS_PROCESS_RS_CS_STATS:
    IS_LOW("IS_PROCESS_RS_CS_STATS, fid = %u", param->u.stats_data.frame_id);
    output->type = IS_PROCESS_OUTPUT_RS_CS_STATS;
    if (param->u.stats_data.is_info->video_stream_on == TRUE) {
      rc = is_process_stats_event(&param->u.stats_data, output->is_output);
    } else {
      output->is_output->frame_id = param->u.stats_data.frame_id;
    }
    break;

  case IS_PROCESS_GYRO_STATS:
    IS_LOW("IS_PROCESS_GYRO_STATS, fid = %u", param->u.gyro_data.frame_id);
    if (param->u.gyro_data.is_info->is_inited) {
      rc = is_process_gyro_stats_event(&param->u.gyro_data, output->is_output);
    } else {
      IS_HIGH("Skip IS_PROCESS_GYRO_STATS");
      rc = FALSE;
    }
    output->type = IS_PROCESS_OUTPUT_GYRO_STATS;
    break;

  case IS_PROCESS_STREAM_EVENT:
    IS_LOW("IS_PROCESS_STREAM_EVENT, s = %d",
      param->u.stream_event_data.stream_event);
    output->type = IS_PROCESS_OUTPUT_STREAM_EVENT;
    output->is_stream_event = param->u.stream_event_data.stream_event;
    if (param->u.stream_event_data.stream_event == IS_VIDEO_STREAM_OFF) {
      if (param->u.stream_event_data.is_info->is_inited) {
        is_process_is_deinitialize(param->u.stream_event_data.is_info);
      }
    }
    break;

  default:
    break;
  }

  return rc;
} /* is_process */
