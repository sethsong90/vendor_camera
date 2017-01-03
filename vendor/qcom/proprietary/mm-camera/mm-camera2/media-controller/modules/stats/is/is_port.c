/* is_port.c
 *
 * Copyright (c) 2013 - 2015 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#include "mct_port.h"
#include "mct_stream.h"
#include "modules.h"
#include "stats_module.h"
#include "is.h"
#include "is_thread.h"
#include "is_port.h"
#include "aec.h"
#include "camera_dbg.h"
#include "c2dExt.h"
#include "stats_util.h"

/* This should be declared in sensor_lib.h */
void poke_gyro_sample(uint64_t t, int32_t gx, int32_t gy, int32_t gz);

#define IS_VIDEO_STREAM_RUNNING (private->video_reserved_id & 0xFFFF)
#define SEC_TO_USEC     (1000000L)


/** is_port_init_thread:
 *    @port: IS port
 *
 *  Returns TRUE IS thread was successfuly started.
 **/
static boolean is_port_init_thread(mct_port_t *port)
{
  boolean rc = FALSE;
  is_port_private_t *private;

  private = (is_port_private_t *)port->port_private;
  private->thread_data = is_thread_init();
  IS_LOW("private->thread_data: %p", private->thread_data);
  if (private->thread_data != NULL) {
    private->thread_data->is_port = port;
    rc = TRUE;
  } else {
    IS_ERR("private->thread_data is NULL");
  }
  return rc;
}


/** is_port_start_thread:
 *    @port: IS port
 *
 *  Returns TRUE IS thread was successfuly started.
 **/
static boolean is_port_start_thread(mct_port_t *port)
{
  boolean rc = FALSE;
  is_port_private_t *private;

  private = (is_port_private_t *)port->port_private;
  if (private->thread_data != NULL) {
     IS_LOW("Start IS thread");
    rc = is_thread_start(private->thread_data);
    if (rc == FALSE) {
      is_thread_deinit(private->thread_data);
      private->thread_data = NULL;
    }
  } else {
    rc = FALSE;
  }
  return rc;
}


/** is_port_handle_stream_config_event:
 *    @private: private port data
 *    @mod_evt: module event
 **/
static boolean is_port_handle_stream_config_event(is_port_private_t *private,
  mct_event_module_t *mod_event)
{
  boolean rc = TRUE;
  sensor_out_info_t *sensor_info =
    (sensor_out_info_t *)mod_event->module_event_data;

   IS_HIGH("w = %u, h = %u, ma = %u, p = %d",
    sensor_info->dim_output.width, sensor_info->dim_output.height,
    sensor_info->sensor_mount_angle, sensor_info->position);

  is_thread_msg_t *msg = (is_thread_msg_t *)
    malloc(sizeof(is_thread_msg_t));

  if (msg != NULL ) {
    memset(msg, 0, sizeof(is_thread_msg_t));
    msg->type = MSG_IS_SET;
    msg->u.is_set_parm.type = IS_SET_PARAM_STREAM_CONFIG;
    msg->u.is_set_parm.u.is_sensor_info.sensor_mount_angle =
      sensor_info->sensor_mount_angle;
    msg->u.is_set_parm.u.is_sensor_info.camera_position =
      sensor_info->position;
    is_thread_en_q_msg(private->thread_data, msg);
  } else {
    IS_ERR("malloc failed!");
    rc = FALSE;
  }

  return rc;
}


/** is_port_handle_set_is_enable:
 *    @private: private port data
 *    @ctrl_evt: control event
 *
 *  Returns TRUE if event was successfuly queued to the IS thread for
 *  processing.
 **/
static boolean is_port_handle_set_is_enable(is_port_private_t *private,
  mct_event_control_t *ctrl_event)
{
  boolean rc = TRUE;
  stats_set_params_type *stats_parm = ctrl_event->control_event_data;

   IS_HIGH("IS enable = %d", stats_parm->u.is_param.u.is_enable);
  is_thread_msg_t *msg = (is_thread_msg_t *)
    malloc(sizeof(is_thread_msg_t));

  if (msg != NULL ) {
    memset(msg, 0, sizeof(is_thread_msg_t));
    msg->type = MSG_IS_SET;
    msg->u.is_set_parm.type = IS_SET_PARAM_IS_ENABLE;
    msg->u.is_set_parm.u.is_enable = stats_parm->u.is_param.u.is_enable;
    is_thread_en_q_msg(private->thread_data, msg);
  } else {
    IS_ERR("malloc failed!");
    rc = FALSE;
  }

  return rc;
}


/** is_port_handle_stream_event:
 *    @private: private port data
 *    @event: event
 *
 *  Returns TRUE if event was successfuly queued to the IS thread for
 *  processing.
 **/
static boolean is_port_handle_stream_event(is_port_private_t *private,
  mct_event_t *event)
{
  boolean rc = TRUE;
  mct_event_control_t *control = &event->u.ctrl_event;

  if (control->type == MCT_EVENT_CONTROL_STREAMON) {
     IS_HIGH("MCT_EVENT_CONTROL_STREAMON, identity = 0x%x",
      event->identity);
  } else {
     IS_HIGH("MCT_EVENT_CONTROL_STREAMOFF, identity = 0x%x",
      event->identity);
  }

  is_thread_msg_t *msg = (is_thread_msg_t *)
    malloc(sizeof(is_thread_msg_t));

  if (msg != NULL ) {
    memset(msg, 0, sizeof(is_thread_msg_t));
    msg->type = MSG_IS_PROCESS;
    msg->u.is_process_parm.type = IS_PROCESS_STREAM_EVENT;
    if (event->identity == private->video_reserved_id) {
      if (control->type == MCT_EVENT_CONTROL_STREAMON) {
        msg->u.is_process_parm.u.stream_event_data.stream_event =
          IS_VIDEO_STREAM_ON;
      } else {
        msg->u.is_process_parm.u.stream_event_data.stream_event =
          IS_VIDEO_STREAM_OFF;
      }
    } else {
      msg->u.is_process_parm.u.stream_event_data.stream_event =
        IS_OTHER_STREAM_ON_OFF;
    }
    msg->u.is_process_parm.u.stream_event_data.is_info = &private->is_info;
    is_thread_en_q_msg(private->thread_data, msg);
  } else {
    IS_ERR("malloc failed!");
    rc = FALSE;
  }

  return rc;
}


/** is_port_handle_stats_event:
 *    @port: IS port
 *    @event: event
 *
 *  Returns TRUE if event was successfully handled.
 **/
static boolean is_port_handle_stats_event(mct_port_t *port, mct_event_t *event)
{
  boolean rc = TRUE;
  is_port_private_t *private = (is_port_private_t *)port->port_private;
  mct_event_stats_ext_t *stats_ext_event =
    (mct_event_stats_ext_t *)event->u.module_event.module_event_data;
  mct_event_stats_isp_t *stats_event = stats_ext_event->stats_data;

  IS_LOW("video_stream_on = %d, stats_mask = %x",
    private->is_info.video_stream_on, stats_event->stats_mask);
  if (private->is_info.video_stream_on == TRUE) {
    if ((stats_event->stats_mask & (1 << MSM_ISP_STATS_RS)) &&
        (stats_event->stats_mask & (1 << MSM_ISP_STATS_CS))) {
      private->RSCS_stats_ready = TRUE;
      if (!private->is_info.is_inited) {
        private->is_info.num_row_sum = ((q3a_rs_stats_t *)
          stats_event->stats_data[MSM_ISP_STATS_RS].stats_buf)->num_v_regions;
        private->is_info.num_col_sum = ((q3a_cs_stats_t *)
          stats_event->stats_data[MSM_ISP_STATS_CS].stats_buf)->num_col_sum;
      }
    }

    if (private->RSCS_stats_ready) {
      is_thread_msg_t *msg;
      private->is_info.timestamp = stats_event->timestamp;
      msg = (is_thread_msg_t *)malloc(sizeof(is_thread_msg_t));
      if (msg != NULL ) {
        memset(msg, 0, sizeof(is_thread_msg_t));
        msg->type = MSG_IS_PROCESS;
        msg->u.is_process_parm.type = IS_PROCESS_RS_CS_STATS;
        msg->u.is_process_parm.u.stats_data.frame_id = stats_event->frame_id;
        msg->u.is_process_parm.u.stats_data.identity = event->identity;
        msg->u.is_process_parm.u.stats_data.port = port;
        msg->u.is_process_parm.u.stats_data.is_info = &private->is_info;
        if (private->is_info.is_mode != IS_TYPE_EIS_2_0 ||
          private->is_info.dis_bias_correction) {
          msg->u.is_process_parm.u.stats_data.yuv_rs_cs_data.p_q3a_rs_stats = \
            stats_event->stats_data[MSM_ISP_STATS_RS].stats_buf;
          msg->u.is_process_parm.u.stats_data.yuv_rs_cs_data.p_q3a_cs_stats = \
            stats_event->stats_data[MSM_ISP_STATS_CS].stats_buf;
        }

        msg->u.is_process_parm.u.stats_data.ack_data = stats_ext_event;
        circular_stats_data_use(stats_ext_event);

        rc = is_thread_en_q_msg(private->thread_data, msg);
        if (!rc) {
          circular_stats_data_done(stats_ext_event, 0, 0, 0);
        }
      } else {
        IS_ERR("malloc failed!");
        rc = FALSE;
      }
      private->RSCS_stats_ready = FALSE;
    }
  } else if (private->stream_type == CAM_STREAM_TYPE_VIDEO) {
    if ((stats_event->stats_mask & (1 << MSM_ISP_STATS_RS)) &&
        (stats_event->stats_mask & (1 << MSM_ISP_STATS_CS))) {
      private->RSCS_stats_ready = TRUE;
    }

    if (private->RSCS_stats_ready) {
      is_thread_msg_t *msg = (is_thread_msg_t *)
        malloc(sizeof(is_thread_msg_t));

      if (msg != NULL ) {
        memset(msg, 0, sizeof(is_thread_msg_t));
        msg->type = MSG_IS_PROCESS;
        msg->u.is_process_parm.type = IS_PROCESS_RS_CS_STATS;
        msg->u.is_process_parm.u.stats_data.frame_id = stats_event->frame_id;
        msg->u.is_process_parm.u.stats_data.identity = event->identity;
        msg->u.is_process_parm.u.stats_data.port = port;
        msg->u.is_process_parm.u.stats_data.is_info = &private->is_info;
        is_thread_en_q_msg(private->thread_data, msg);
      } else {
        IS_ERR("malloc failed!");
        rc = FALSE;
      }
      private->RSCS_stats_ready = FALSE;
    }
  }
  return rc;
}


/** is_port_handle_gyro_stats_event:
 *    @port: IS port
 *    @event: event
 *
 *  Returns TRUE if event was successfully handled.
 **/
static boolean is_port_handle_gyro_stats_event(mct_port_t *port,
  mct_event_t *event)
{
  boolean rc = TRUE;
  is_port_private_t *private = (is_port_private_t *)port->port_private;
  mct_event_gyro_stats_t *gyro_stats_event =
    (mct_event_gyro_stats_t *)event->u.module_event.module_event_data;

  is_thread_msg_t *msg = (is_thread_msg_t *)
    malloc(sizeof(is_thread_msg_t));

   IS_LOW("gyro frame id = %u", gyro_stats_event->is_gyro_data.frame_id);
  if (msg != NULL ) {
    memset(msg, 0, sizeof(is_thread_msg_t));
    msg->type = MSG_IS_PROCESS;
    msg->u.is_process_parm.type = IS_PROCESS_GYRO_STATS;
    msg->u.is_process_parm.u.gyro_data.frame_id =
      gyro_stats_event->is_gyro_data.frame_id;
    msg->u.is_process_parm.u.gyro_data.is_info = &private->is_info;
    memcpy(&msg->u.is_process_parm.u.gyro_data.gyro_data,
      &gyro_stats_event->is_gyro_data, sizeof(mct_event_gyro_data_t));
    is_thread_en_q_msg(private->thread_data, msg);
  } else {
    IS_ERR("malloc failed!");
    rc = FALSE;
  }

  return rc;
}


/** is_port_handle_dis_config_event:
 *    @private: private port data
 *    @mod_evt: module event
 **/
static boolean is_port_handle_dis_config_event(is_port_private_t *private,
  mct_event_module_t *mod_event)
{
  boolean rc = TRUE;
  isp_dis_config_info_t *dis_config;
  cam_stream_type_t stream_type;

  dis_config = (isp_dis_config_info_t *)mod_event->module_event_data;

  if (dis_config->stream_id == (private->video_reserved_id & 0xFFFF)) {
    stream_type = CAM_STREAM_TYPE_VIDEO;
    IS_HIGH("Set stream_type to VIDEO");
  } else if (dis_config->stream_id == (private->reserved_id & 0xFFFF)) {
    stream_type = CAM_STREAM_TYPE_PREVIEW;
    IS_HIGH("Set stream_type to PREVIEW");
  } else {
    IS_HIGH("Junking MCT_EVENT_MODULE_ISP_DIS_CONFIG event");
    return FALSE;
  }

  IS_HIGH("MCT_EVENT_MODULE_ISP_DIS_CONFIG, sid = %u, strid = %x, "
    "vid res id= %x, res id= %x, col_num = %u, row_num = %u, w = %u, h = %u",
    dis_config->session_id, dis_config->stream_id, private->video_reserved_id,
    private->reserved_id, dis_config->col_num, dis_config->row_num,
    dis_config->width, dis_config->height);

  is_thread_msg_t *msg = (is_thread_msg_t *)
    malloc(sizeof(is_thread_msg_t));

  if (msg != NULL ) {
    memset(msg, 0, sizeof(is_thread_msg_t));
    msg->type = MSG_IS_SET;
    msg->u.is_set_parm.type = IS_SET_PARAM_DIS_CONFIG;
    msg->u.is_set_parm.u.is_config_info.stream_type = stream_type;
    msg->u.is_set_parm.u.is_config_info.width = dis_config->width;
    msg->u.is_set_parm.u.is_config_info.height = dis_config->height;
    is_thread_en_q_msg(private->thread_data, msg);
  } else {
    IS_ERR("malloc failed!");
    rc = FALSE;
  }

  return rc;
}


/** is_port_handle_chromatix_event
 *    @private: private port data
 *    @mod_evt: module event
 */
static boolean is_port_handle_chromatix_event(is_port_private_t *private,
  mct_event_module_t *mod_event)
{
  boolean rc = TRUE;
  chromatix_3a_parms_type *chromatix =
    ((modulesChromatix_t *)mod_event->module_event_data)->chromatix3APtr;

  is_thread_msg_t *msg = malloc(sizeof(is_thread_msg_t));
  if (msg != NULL ) {
    memset(msg, 0, sizeof(is_thread_msg_t));
    msg->type = MSG_IS_SET;
    msg->u.is_set_parm.type = IS_SET_PARAM_CHROMATIX;
    msg->u.is_set_parm.u.is_init_param.chromatix = &chromatix->chromatix_EIS_data;

    is_thread_en_q_msg(private->thread_data, msg);
  } else {
    IS_ERR("malloc failed!");
    rc = FALSE;
  }
  return rc;
}


/** is_port_handle_output_dim_event:
 *    @private: private port data
 *    @mod_evt: module event
 **/
static boolean is_port_handle_output_dim_event(is_port_private_t *private,
  mct_event_module_t *mod_event)
{
  boolean rc = TRUE;
  mct_stream_info_t *stream_info = NULL;

  stream_info = (mct_stream_info_t *)mod_event->module_event_data;
  if (stream_info->stream_type != CAM_STREAM_TYPE_VIDEO) {
    return FALSE;
  }

   IS_HIGH("MCT_EVENT_MODULE_ISP_OUTPUT_DIM, steam_type = %d, w = %d, "
    "h = %d, IS mode = %d", stream_info->stream_type,
    stream_info->dim.width, stream_info->dim.height, stream_info->is_type);

  is_thread_msg_t *msg = (is_thread_msg_t *)
    malloc(sizeof(is_thread_msg_t));

  if (msg != NULL ) {
    memset(msg, 0, sizeof(is_thread_msg_t));
    msg->type = MSG_IS_SET;
    msg->u.is_set_parm.type = IS_SET_PARAM_OUTPUT_DIM;
    msg->u.is_set_parm.u.is_output_dim_info.is_mode = stream_info->is_type;
    msg->u.is_set_parm.u.is_output_dim_info.vfe_width = stream_info->dim.width;
    msg->u.is_set_parm.u.is_output_dim_info.vfe_height =
      stream_info->dim.height;
    is_thread_en_q_msg(private->thread_data, msg);
  } else {
    IS_ERR("malloc failed!");
    rc = FALSE;
  }

  return rc;
}


/** is_port_send_is_update:
 *    @port: IS port
 *    @private: private port data
 **/
static void is_port_send_is_update(mct_port_t *port,
  is_port_private_t *private)
{
  mct_event_t is_update_event;
  is_update_t is_update;

  /* Sanity check */
  /* is_enabled is reset to 0 when IS initialization fails.  By checking this
     flag for 0, IS won't send DIS_UPDATE event when it is not operational. */
  if (private->is_output.x < 0 || private->is_output.y < 0 ||
    private->is_info.is_enabled == 0) {
    return;
  }

  is_update.id = private->video_reserved_id;
  is_update.x = private->is_output.x;
  is_update.y = private->is_output.y;
  is_update.width = private->is_info.is_width;
  is_update.height = private->is_info.is_height;
  is_update.frame_id = private->is_output.frame_id;
  if (private->is_info.is_mode == IS_TYPE_EIS_2_0) {
    is_update.use_3d = 1;
    memcpy(is_update.transform_matrix, private->is_output.transform_matrix,
      sizeof(is_update.transform_matrix));
    is_update.transform_type = private->is_info.transform_type;
  }
  else {
    is_update.use_3d = 0;
  }

  if (private->is_info.video_stream_on == FALSE) {
    is_update.id = private->reserved_id;
    is_update.width = private->is_info.preview_width;
    is_update.height = private->is_info.preview_height;
  }

  IS_LOW("fid = %d, x = %d, y = %d, w = %d, h = %d",
    is_update.frame_id, is_update.x, is_update.y,
    is_update.width, is_update.height);
  if (private->is_info.is_mode == IS_TYPE_EIS_2_0) {
    IS_LOW("tt = %u, tm = %f %f %f %f %f %f %f %f %f", is_update.transform_type,
      is_update.transform_matrix[0], is_update.transform_matrix[1],
      is_update.transform_matrix[2], is_update.transform_matrix[3],
      is_update.transform_matrix[4], is_update.transform_matrix[5],
      is_update.transform_matrix[6], is_update.transform_matrix[7],
      is_update.transform_matrix[8]);
  }

  is_update_event.type = MCT_EVENT_MODULE_EVENT;
  is_update_event.identity = is_update.id;
  is_update_event.direction = MCT_EVENT_UPSTREAM;
  is_update_event.u.module_event.type = MCT_EVENT_MODULE_STATS_DIS_UPDATE;
  is_update_event.u.module_event.module_event_data = &is_update;
  mct_port_send_event_to_peer(port, &is_update_event);
}

static void is_port_stats_done_callback(void *port, void *stats)
{
  if (!port || !stats) {
    return;
  }

  is_port_private_t *private = ((mct_port_t *)port)->port_private;
  is_stats_data_t   *is_stats = (is_stats_data_t *)stats;

  if (!private || !is_stats) {
    return;
  }

  IS_LOW("DONE IS STATS ACK back");

  circular_stats_data_done(is_stats->ack_data, port,
                           private->video_reserved_id,
                           private->is_output.frame_id);
}

/** is_port_callback:
 *    @port: IS port
 *    @output: Output from processing IS event
 **/
static void is_port_callback(mct_port_t *port,
  is_process_output_t *output)
{
  is_port_private_t *private = port->port_private;

   IS_LOW("IS process ouput type = %d", output->type);
  switch (output->type) {
  case IS_PROCESS_OUTPUT_RS_CS_STATS:
    is_port_send_is_update(port, port->port_private);
    break;

  case IS_PROCESS_OUTPUT_GYRO_STATS:
    is_port_send_is_update(port, port->port_private);
    break;

  case IS_PROCESS_OUTPUT_STREAM_EVENT:
    if (output->is_stream_event == IS_VIDEO_STREAM_ON) {
      private->is_info.video_stream_on = TRUE;
    } else if (output->is_stream_event == IS_VIDEO_STREAM_OFF) {
      private->is_info.video_stream_on = FALSE;
    }
    if (private->is_info.video_stream_on == FALSE) {
      /* Default offsets to half margin for cropping at center during camcorder
         preview no recording. */
      if (private->is_info.is_mode != IS_TYPE_EIS_2_0) {
        private->is_output.x =
          (private->is_info.vfe_width - private->is_info.preview_width) / 2;
        private->is_output.y =
          (private->is_info.vfe_height - private->is_info.preview_height) / 2;
      } else {
        private->is_output.x = 0;
        private->is_output.y = 0;
      }

      /* For now, front/rear camera has same virutal margin */
      private->is_output.transform_matrix[0] = 1.0 /
        (1 + 2 * private->is_info.is_chromatix_info.virtual_margin);
      private->is_output.transform_matrix[1] = 0.0;
      private->is_output.transform_matrix[2] = 0.0;
      private->is_output.transform_matrix[3] = 0.0;
      private->is_output.transform_matrix[4] = 1.0 /
        (1 + 2 * private->is_info.is_chromatix_info.virtual_margin);
      private->is_output.transform_matrix[5] = 0.0;
      private->is_output.transform_matrix[6] = 0.0;
      private->is_output.transform_matrix[7] = 0.0;
      private->is_output.transform_matrix[8] = 1.0;
    }
    break;
  default:
    break;
  }
}


/** is_port_event:
 *    @port: IS port
 *    @event: event
 *
 *  This function handles events for the IS port.
 *
 *  Returns TRUE on successful event processing.
 **/
static boolean is_port_event(mct_port_t *port, mct_event_t *event)
{
  boolean rc = TRUE;
  is_port_private_t *private;

  /* sanity check */
  if (!port || !event)
    return FALSE;

  private = (is_port_private_t *)(port->port_private);
  if (!private)
    return FALSE;

  /* sanity check: ensure event is meant for port with same identity*/
  if ((private->reserved_id & 0xFFFF0000) !=
      (event->identity & 0xFFFF0000))
  {
    return FALSE;
  }

  switch (MCT_EVENT_DIRECTION(event)) {
  case MCT_EVENT_DOWNSTREAM: {
    switch (event->type) {
    case MCT_EVENT_CONTROL_CMD: {
      mct_event_control_t *control = &event->u.ctrl_event;

      // IS_LOW("Control event type %d", control->type);
      switch (control->type) {
      case MCT_EVENT_CONTROL_STREAMON:
        if (private->thread_data) {
          rc = is_port_handle_stream_event(private, event);
        }
        break;

      case MCT_EVENT_CONTROL_STREAMOFF:
        if (private->thread_data) {
          rc = is_port_handle_stream_event(private, event);
        }
        break;

      case MCT_EVENT_CONTROL_LINK_INTRA_SESSION: {
        cam_sync_related_sensors_event_info_t *link_param = NULL;
        uint32_t                               peer_identity = 0;
        link_param = (cam_sync_related_sensors_event_info_t *)
          (event->u.ctrl_event.control_event_data);
        peer_identity = link_param->related_sensor_session_id;
        private->intra_peer_id = peer_identity;
        private->dual_cam_sensor_info = link_param->type;
      }
        break;

      case MCT_EVENT_CONTROL_UNLINK_INTRA_SESSION: {
        private->dual_cam_sensor_info = CAM_TYPE_MAIN;
        private->intra_peer_id = 0;
      }
        break;

      case MCT_EVENT_CONTROL_SET_PARM: {
        stats_set_params_type *stats_parm = control->control_event_data;
        if (private->thread_data) {
          if (stats_parm->param_type == STATS_SET_IS_PARAM &&
              stats_parm->u.is_param.type == IS_SET_PARAM_IS_ENABLE) {
            rc = is_port_handle_set_is_enable(private, control);
          } else if (stats_parm->param_type == STATS_SET_COMMON_PARAM &&
              stats_parm->u.common_param.type == COMMON_SET_PARAM_STREAM_ON_OFF) {
            stats_common_set_parameter_t *common_param =
              &(stats_parm->u.common_param);

            IS_LOW("COMMON_SET_PARAM_STREAM_ON_OFF %d", common_param->u.stream_on);
            private->thread_data->no_stats_mode = !common_param->u.stream_on;

            // stream off, need to flush existing stats
            // send a sync msg here to flush the stats & other msg
            if (!common_param->u.stream_on) {
              is_thread_msg_t is_msg;
              memset(&is_msg, 0, sizeof(is_thread_msg_t));
              is_msg.type = MSG_IS_STATS_MODE;
              is_msg.sync_flag = TRUE;
              is_thread_en_q_msg(private->thread_data, &is_msg);
              IS_LOW("COMMON_SET_PARAM_STREAM_ON_OFF end");
            }
          }
        }
      }
        break;

      default:
        break;
      }
    } /* case MCT_EVENT_CONTROL_CMD */
      break;

    case MCT_EVENT_MODULE_EVENT: {
      mct_event_module_t *mod_event = &event->u.module_event;

      switch (mod_event->type) {
      case MCT_EVENT_MODULE_STATS_EXT_DATA:
        if (private->is_info.is_enabled && (IS_VIDEO_STREAM_RUNNING)) {
          rc = is_port_handle_stats_event(port, event);
        }
        break;

      case MCT_EVENT_MODULE_STATS_GYRO_STATS:
        MCT_OBJECT_LOCK(port);
        if (private->thread_data && private->is_info.is_inited &&
          private->is_info.is_mode != IS_TYPE_DIS &&
          (IS_VIDEO_STREAM_RUNNING)) {
          rc = is_port_handle_gyro_stats_event(port, event);
        }
        MCT_OBJECT_UNLOCK(port);
        break;

      case MCT_EVENT_MODULE_SET_STREAM_CONFIG: {
          rc = is_port_handle_stream_config_event(private, mod_event);
      }
        break;

      case MCT_EVENT_MODULE_ISP_DIS_CONFIG: {
        if (private->thread_data) {
          rc = is_port_handle_dis_config_event(private, mod_event);
        }
      }
        break;

      case MCT_EVENT_MODULE_MODE_CHANGE: {
        private->stream_type = ((stats_mode_change_event_data *)
          (mod_event->module_event_data))->stream_type;
      }
        break;

      case MCT_EVENT_MODULE_START_STOP_STATS_THREADS: {
        uint8_t *start_flag = (uint8_t*)(mod_event->module_event_data);
        IS_LOW("MCT_EVENT_MODULE_START_STOP_STATS_THREADS start_flag: %d",
          *start_flag);
        if (*start_flag) {
          if (is_port_start_thread(port) == FALSE) {
            IS_ERR("is thread start failed");
            rc = FALSE;
          }
        } else {
          if (private->thread_data) {
            is_thread_stop(private->thread_data);
          }
        }
      }
        break;

      case MCT_EVENT_MODULE_ISP_OUTPUT_DIM: {
        if (private->thread_data) {
          rc = is_port_handle_output_dim_event(private, mod_event);
        }
      }
        break;

      case MCT_EVENT_MODULE_SET_CHROMATIX_PTR: {
        if (private->thread_data) {
          rc = is_port_handle_chromatix_event(private, mod_event);
        }
      }
        break;

      case MCT_EVENT_MODULE_REQUEST_STATS_TYPE: {
        mct_event_request_stats_type *stats_info =
          (mct_event_request_stats_type *)mod_event->module_event_data;

        if (ISP_STREAMING_OFFLINE == stats_info->isp_streaming_type) {
          IS_HIGH("IS doesn't support offline processing yet. Returning.");
          break;
        } else if ( private->dual_cam_sensor_info == CAM_TYPE_AUX) {
          break;
        }
        IS_INFO(" Enable stats mask only when IS is enabled, cur status: %d",
          private->is_info.is_enabled);
        /*Opt: Enable IS stats only when IS is enabled*/
        if(private->is_info.is_enabled) {
          if (stats_info->supported_stats_mask & (1 << MSM_ISP_STATS_RS)) {
            stats_info->enable_stats_mask |= (1 << MSM_ISP_STATS_RS);
          }
          if (stats_info->supported_stats_mask & (1 << MSM_ISP_STATS_CS)) {
            stats_info->enable_stats_mask |= (1 << MSM_ISP_STATS_CS);
          }
        }
      }
        break;

      default:
        break;
      }
    } /* case MCT_EVENT_MODULE_EVENT */
      break;

    default:
      break;
    } /* switch (event->type) */

  } /* case MCT_EVENT_TYPE_DOWNSTREAM */
    break;

  case MCT_EVENT_UPSTREAM: {
    mct_port_t *peer = MCT_PORT_PEER(port);
    MCT_PORT_EVENT_FUNC(peer)(peer, event);
  }
    break;

  default:
    break;
  } /* switch (MCT_EVENT_DIRECTION(event)) */

  return rc;
}


/** is_port_ext_link:
 *    @identity: session id | stream id
 *    @port: IS port
 *    @peer: For IS sink port, peer is most likely stats port
 *
 *  Sets IS port's external peer port.
 *
 *  Returns TRUE on success.
 **/
static boolean is_port_ext_link(unsigned int identity,
  mct_port_t *port, mct_port_t *peer)
{
  boolean rc = FALSE, thread_init = FALSE;
  is_port_private_t *private;
  mct_event_t event;

   IS_LOW("Enter");
  if (strcmp(MCT_OBJECT_NAME(port), "is_sink"))
    return FALSE;

  private = (is_port_private_t *)port->port_private;
  if (!private)
    return FALSE;

  MCT_OBJECT_LOCK(port);
  switch (private->state) {
  case IS_PORT_STATE_RESERVED:
     IS_LOW("IS_PORT_STATE_RESERVED");
    if ((private->reserved_id & 0xFFFF0000) != (identity & 0xFFFF0000)) {
      break;
    }
  /* Fall through */
  case IS_PORT_STATE_UNLINKED:
     IS_LOW("IS_PORT_STATE_UNLINKED");
    if ((private->reserved_id & 0xFFFF0000) != (identity & 0xFFFF0000)) {
      break;
    }

  case IS_PORT_STATE_CREATED:
    if ((private->reserved_id & 0xFFFF0000) == (identity & 0xFFFF0000)) {
      thread_init = TRUE;
    }
    rc = TRUE;
    break;

  case IS_PORT_STATE_LINKED:
     IS_LOW("IS_PORT_STATE_LINKED");
    if ((private->reserved_id & 0xFFFF0000) == (identity & 0xFFFF0000)) {
      rc = TRUE;
    }
    break;

  default:
    break;
  }

  if (rc == TRUE) {
    /* If IS module requires a thread and the port state above warrants one,
       create the thread here */
    if (thread_init == TRUE) {
      if (private->thread_data == NULL) {
        rc = FALSE;
        goto init_thread_fail;
      }
    }
    private->state = IS_PORT_STATE_LINKED;
    MCT_PORT_PEER(port) = peer;
    MCT_OBJECT_REFCOUNT(port) += 1;
  }

init_thread_fail:
  MCT_OBJECT_UNLOCK(port);
  mct_port_add_child(identity, port);
   IS_LOW("rc=%d", rc);
  return rc;
}


/** is_port_unlink:
 *  @identity: session id | stream id
 *  @port: IS port
 *  @peer: IS port's peer port (probably stats port)
 *
 *  This funtion unlinks the IS port from its peer.
 **/
static void is_port_unlink(unsigned int identity,
  mct_port_t *port, mct_port_t *peer)
{
  is_port_private_t *private;

  if (!port || !peer || MCT_PORT_PEER(port) != peer)
    return;

  private = (is_port_private_t *)port->port_private;
  if (!private)
    return;

   IS_LOW("port state = %d, identity = 0x%x",
    private->state, identity);
  MCT_OBJECT_LOCK(port);
  if (private->state == IS_PORT_STATE_LINKED &&
      (private->reserved_id & 0xFFFF0000) == (identity & 0xFFFF0000)) {
    MCT_OBJECT_REFCOUNT(port) -= 1;
    if (!MCT_OBJECT_REFCOUNT(port)) {
      private->state = IS_PORT_STATE_UNLINKED;
       IS_LOW("Stop IS thread, video_reserved_id = %x",
        private->video_reserved_id);
    }
  }
  MCT_OBJECT_UNLOCK(port);
  mct_port_remove_child(identity, port);

  return;
}


/** is_port_set_caps:
 *    @port: port object whose caps are to be set
 *    @caps: this port's capability.
 *
 *  Function overwrites a ports capability.
 *
 *  Returns TRUE if it is valid source port.
 **/
static boolean is_port_set_caps(mct_port_t *port,
  mct_port_caps_t *caps)
{
   IS_LOW("Enter");
  if (strcmp(MCT_PORT_NAME(port), "is_sink"))
    return FALSE;

  port->caps = *caps;
  return TRUE;
}


/** is_port_check_caps_reserve:
 *    @port: this interface module's port
 *    @peer_caps: the capability of peer port which wants to match
 *                interface port
 *    @stream_info: stream information
 *
 *  Returns TRUE on success.
 **/
static boolean is_port_check_caps_reserve(mct_port_t *port, void *caps,
  void *stream_info)
{
  boolean rc = FALSE;
  mct_port_caps_t *port_caps;
  is_port_private_t *private;
  mct_stream_info_t *strm_info = (mct_stream_info_t *)stream_info;

   IS_LOW("Enter");
  MCT_OBJECT_LOCK(port);
  if (!port || !caps || !strm_info ||
      strcmp(MCT_OBJECT_NAME(port), "is_sink")) {
     IS_LOW("Exit unsucessful");
    goto reserve_done;
  }

  port_caps = (mct_port_caps_t *)caps;
  if (port_caps->port_caps_type != MCT_PORT_CAPS_STATS) {
    rc = FALSE;
    goto reserve_done;
  }

  private = (is_port_private_t *)port->port_private;
   IS_LOW("port state = %d, identity = 0x%x, stream_type = %d",
    private->state, strm_info->identity, strm_info->stream_type);
  switch (private->state) {
  case IS_PORT_STATE_LINKED:
  if ((private->reserved_id & 0xFFFF0000) ==
      (strm_info->identity & 0xFFFF0000)) {
    if (strm_info->stream_type == CAM_STREAM_TYPE_VIDEO) {
      private->video_reserved_id = strm_info->identity;
       IS_HIGH("video id = 0x%x", private->video_reserved_id);
      IS_LOW("w = %d, h = %d",strm_info->dim.width,
        strm_info->dim.height);
    } else if (strm_info->stream_type == CAM_STREAM_TYPE_PREVIEW) {
      private->is_info.preview_width = strm_info->dim.width;
      private->is_info.preview_height = strm_info->dim.height;
      private->reserved_id = strm_info->identity;
       IS_LOW("preview id = 0x%x", private->reserved_id);
      IS_LOW("w = %lu, h = %lu",
        private->is_info.preview_width, private->is_info.preview_height);
    } else if (strm_info->stream_type == CAM_STREAM_TYPE_SNAPSHOT) {
       IS_LOW("snapshot id = 0x%x", strm_info->identity);
    }

    rc = TRUE;
  }
  break;

  case IS_PORT_STATE_CREATED:
  case IS_PORT_STATE_UNRESERVED:
    if (strm_info->stream_type == CAM_STREAM_TYPE_VIDEO) {
      private->video_reserved_id = strm_info->identity;
       IS_LOW("reserved_id = 0x%x",private->video_reserved_id);
      IS_LOW("w = %d, h = %d", strm_info->dim.width,
        strm_info->dim.height);
    }
    private->reserved_id = strm_info->identity;
    private->stream_type = strm_info->stream_type;
    private->state       = IS_PORT_STATE_RESERVED;
    rc = TRUE;
    break;

  case IS_PORT_STATE_RESERVED:
    if ((private->reserved_id & 0xFFFF0000) ==
        (strm_info->identity & 0xFFFF0000))
      rc = TRUE;
    break;

  default:
    break;
  }

reserve_done:
  MCT_OBJECT_UNLOCK(port);
  return rc;
}


/** is_port_check_caps_unreserve:
 *    @port: this port object to remove the session/stream
 *    @identity: session+stream identity
 *
 *  This function frees the identity from port's children list.
 *
 *  Returns FALSE if the identity does not exist.
 **/
static boolean is_port_check_caps_unreserve(mct_port_t *port,
  unsigned int identity)
{
  boolean rc = FALSE;
  is_port_private_t *private;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "is_sink"))
    return FALSE;

   IS_LOW("E, identity = 0x%x", identity);
  private = (is_port_private_t *)port->port_private;
  if (!private)
    return FALSE;

   IS_LOW("port state = %d, identity = 0x%x",
    private->state, identity);
  if (private->state == IS_PORT_STATE_UNRESERVED)
    return TRUE;

  MCT_OBJECT_LOCK(port);
  if (private->state == IS_PORT_STATE_LINKED &&
    private->video_reserved_id == identity) {
    private->video_reserved_id = (private->video_reserved_id & 0xFFFF0000);
    IS_HIGH("Reset video_reserved_id to 0x%x", private->video_reserved_id);
  }

  if ((private->state == IS_PORT_STATE_UNLINKED ||
       private->state == IS_PORT_STATE_RESERVED) &&
      ((private->reserved_id & 0xFFFF0000) == (identity & 0xFFFF0000))) {

    if (!MCT_OBJECT_REFCOUNT(port)) {
      private->state = IS_PORT_STATE_UNRESERVED;
      private->video_reserved_id = (private->video_reserved_id & 0xFFFF0000);
      private->reserved_id = (private->reserved_id & 0xFFFF0000);
    }
    rc = TRUE;
  }

unreserve_done:
  MCT_OBJECT_UNLOCK(port);
  return rc;
}


/** is_port_init:
 *    @port: IS port
 *    @session_id: session id
 *
 *  This function initializes the IS port's internal variables.
 *
 *  Returns TRUE on success.
 **/
boolean is_port_init(mct_port_t *port, unsigned int session_id)
{
  mct_port_caps_t caps;
  is_port_private_t *private;

  if (port == NULL || strcmp(MCT_OBJECT_NAME(port), "is_sink"))
    return FALSE;

  private = (void *)malloc(sizeof(is_port_private_t));
  if (!private)
    return FALSE;

  memset(private, 0, sizeof(is_port_private_t));
  private->set_parameters = is_set_parameters;
  private->process = is_process;
  private->callback = is_port_callback;
  private->is_stats_cb = is_port_stats_done_callback;
  private->is_process_output.is_output = &private->is_output;
  private->video_reserved_id = session_id;
  private->reserved_id = session_id;
  private->state = IS_PORT_STATE_CREATED;
  private->dual_cam_sensor_info = CAM_TYPE_MAIN;

  private->is_info.transform_type = C2D_LENSCORRECT_PERSPECTIVE |
    C2D_LENSCORRECT_BILINEAR | C2D_LENSCORRECT_ORIGIN_IN_MIDDLE |
    C2D_LENSCORRECT_SOURCE_RECT;

  /* Explicitly disable DIS bias correction for EIS 2.0 for clarity */
  private->is_info.dis_bias_correction = 0;

  port->port_private = private;
  port->direction = MCT_PORT_SINK;
  caps.port_caps_type = MCT_PORT_CAPS_STATS;
  caps.u.stats.flag   = MCT_PORT_CAP_STATS_CS_RS;

  is_port_init_thread(port);

  mct_port_set_event_func(port, is_port_event);
  /* Accept default int_link function */
  mct_port_set_ext_link_func(port, is_port_ext_link);
  mct_port_set_unlink_func(port, is_port_unlink);
  mct_port_set_set_caps_func(port, is_port_set_caps);
  mct_port_set_check_caps_reserve_func(port, is_port_check_caps_reserve);
  mct_port_set_check_caps_unreserve_func(port, is_port_check_caps_unreserve);

  if (port->set_caps) {
    port->set_caps(port, &caps);
  }
  return TRUE;
}


/** is_port_deinit:
 *    @port: IS port
 *
 * This function frees the IS port's memory.
 **/
void is_port_deinit(mct_port_t *port)
{
  is_port_private_t *private;
  if (!port || strcmp(MCT_OBJECT_NAME(port), "is_sink"))
    return;

  private = port->port_private;

  if (private != NULL) {
    is_thread_deinit(private->thread_data);
    free(port->port_private);
  }
}


/** is_port_find_identity:
 *    @port: IS port
 *    @identity: session id | stream id
 *
 * This function checks for the port with a given session.
 *
 * Returns TRUE if the port is found.
 **/
boolean is_port_find_identity(mct_port_t *port, unsigned int identity)
{
  is_port_private_t *private;

  if (!port)
      return FALSE;

  if (strcmp(MCT_OBJECT_NAME(port), "is_sink"))
    return FALSE;

  private = port->port_private;

  if (private) {
    return ((private->reserved_id & 0xFFFF0000) ==
            (identity & 0xFFFF0000) ? TRUE : FALSE);
  }

  return FALSE;
}

