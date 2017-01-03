/* stats_port.c
 *
 * Copyright (c) 2013-2016 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#include "modules.h"
#include "stats_module.h"
#include "stats_port.h"
#include "q3a_module.h"
#include "mct_controller.h"
#include "mct_profiler.h"

#include "cam_intf.h"
#include "cam_types.h"
#include "aec.h"
#include "awb.h"
#include "af.h"
#include "3AStatsDataTypes.h"
#include "stats_event.h"

#include <utils/Log.h>

#define STATS_PORT_SKIP_STATS_MAX_FPS 30    // if fps exceed will trigger skip stats
#define STATS_PORT_SKIP_STATS_MIN_FPS 20    // if fps below this value won't skip stats
#define Q8                            0x00000100
#define EXIF_DEBUG_MASK_STATS        (0x10000 << 5)


/* Internal function prototypes. */
static void copy_stats_buffer_to_debug_data(
  mct_event_t *event, mct_port_t *port);
static void copy_stats_config_to_debug_data(
  mct_port_t *port, mct_event_t *event);
static void send_stats_buffer_to_debug_data(mct_port_t *port);
boolean is_stats_buffer_debug_data_enable(mct_port_t *port);

static boolean stats_port_handle_stats_data(mct_port_t *port, mct_event_t *event);
static boolean stats_port_set_stream_on_off(mct_port_t *port, boolean stream_on);
volatile uint32_t stats_debug_data_log_level;
volatile uint32_t stats_debug_test;
volatile uint32_t stats_exif_debug_mask;

static char *mct_event_ctrl_strings[MCT_EVENT_CONTROL_MAX+1] = {
  MCT_EVENT_CONTROL_ENUM_LIST(MCT_EVENT_GENERATE_STRING)
};
static char *mct_event_module_strings[MCT_EVENT_MODULE_MAX+1] = {
  MCT_EVENT_MODULE_ENUM_LIST(MCT_EVENT_GENERATE_STRING)
};

/** stats_port_get_mct_event_ctrl_string
 *    @eventId: mct ctrl event num
 *
 *  Return char *
 **/
inline char * stats_port_get_mct_event_ctrl_string(enum _mct_event_control_type eventId)
{
  return mct_event_ctrl_strings[eventId < MCT_EVENT_CONTROL_MAX ?
    eventId : MCT_EVENT_CONTROL_MAX];
}

/** stats_port_get_mct_event_module_string
 *    @eventId: mct module event num
 *
 *  Return char *
 **/
inline char * stats_port_get_mct_event_module_string(enum _mct_event_module_type eventId)
{
  return mct_event_module_strings[eventId < MCT_EVENT_MODULE_MAX ?
    eventId : MCT_EVENT_MODULE_MAX];
}

/** stats_port_handle_stats_skip
 *    @port:  the port instance
 *    @event: the event to be processed
 *
 *  get the fps value from AEC_UPDATE event and request ISP to
 *  skip the stats if necessary
 *
 *  Return void
 **/
static void stats_port_handle_stats_skip(mct_port_t *port,
  mct_event_t *event)
{
  stats_update_t *stats_update =
    (stats_update_t *)event->u.module_event.module_event_data;
  stats_port_private_t *private = (stats_port_private_t *)port->port_private;
  mct_event_t stats_event;
  int skip_count = 0;
  boolean send_event = FALSE;
  int skip_pattern = 0;

  int32_t current_fps =
    (int32_t)MIN(1/stats_update->aec_update.exp_time, private->max_sensor_fps);
  private->current_fps = current_fps;

  /* If current fps is more than the max fps stats support we request ISP to
     skip the stats */
  if (current_fps > STATS_PORT_SKIP_STATS_MAX_FPS) {
    skip_count =
      (private->current_fps / STATS_PORT_SKIP_STATS_MAX_FPS) + 0.5;
    STATS_LOW("Number of stats to be skipped: %d", skip_count);

    switch (skip_count) {
    case 2: //60fps
      skip_pattern = EVERY_2FRAME;
      break;
    case 3: //90fps
      skip_pattern = EVERY_3FRAME;
      break;
    case 4: //120fps
      skip_pattern = EVERY_4FRAME;
      break;
    case 5: //150fps
      skip_pattern = EVERY_5FRAME;
      break;
    case 6: //180fps
      skip_pattern = EVERY_6FRAME;
      break;
    case 7: //210fps
      skip_pattern = EVERY_7FRAME;
      break;
    case 8: //240fps
      skip_pattern = EVERY_8FRAME;
      break;
    default:
      skip_pattern = NO_SKIP;
      break;
    }

    if (private->skip_pattern != skip_pattern) {
      send_event = TRUE;
      private->skip_pattern = skip_pattern;
    }
  } else {
    /* If we had requested ISP to skip the frame, we need to request now to
       stop skipping.*/
    if (private->skip_pattern != NO_SKIP) {
      skip_pattern = NO_SKIP;
      private->skip_pattern = FALSE;
      send_event = TRUE;
    }
  }

  STATS_LOW("exp_time %f sensor_fps %f current_fps %d skip_pattern %d",
    stats_update->aec_update.exp_time, private->max_sensor_fps, current_fps,
    skip_pattern);

  if (send_event == TRUE) {
    stats_event.direction = MCT_EVENT_UPSTREAM;
    stats_event.identity = private->reserved_id;
    stats_event.type = MCT_EVENT_MODULE_EVENT;
    stats_event.u.module_event.type = MCT_EVENT_MODULE_UPDATE_STATS_SKIP;
    stats_event.u.module_event.module_event_data =
      (void *)(&(skip_pattern));
    STATS_LOW("Send event to ISP to skip frame!");
    mct_port_send_event_to_peer(port, &stats_event);
  }

  return;
}

/** stats_port_check_id
 *    @data1: port1's id
 *    @data2: port2's id
 *
 *  Check if two ports have same identity.
 *
 *  Return TRUE if the two ports have same id.
 **/
static boolean stats_port_check_id(void *data1, void *data2)
{
  return ((*((unsigned int *)data1) == *((unsigned int *)data2)) ?
    TRUE : FALSE);
}

/** stats_port_get_msg_q_idx
 *
 *
 **/
static unsigned int stats_port_get_msg_q_idx(unsigned int frameid)
{
  return (frameid % STATS_MAX_FRAME_DELAY);
}

static stats_port_event_t* stats_port_malloc_set_parm_ctrl(mct_event_type type,
  int size)
{
  stats_port_event_t *stats_event =
    (stats_port_event_t *)calloc(1, sizeof(stats_port_event_t));

  if (!stats_event) {
    STATS_ERR("calloc failure Out of memory");
    goto err;
  }

  stats_event->event = (mct_event_t *)calloc(1, sizeof(mct_event_t));
  if (!stats_event->event) {
    STATS_ERR("calloc failure Out of memory");
    goto err1;
  }
  if (type == MCT_EVENT_CONTROL_CMD) {
    stats_event->event->u.ctrl_event.control_event_data = calloc (1, size);
    if (!stats_event->event->u.ctrl_event.control_event_data) {
      STATS_ERR("calloc failure Out of memory");
      goto err2;
    }
  } else if (type == MCT_EVENT_MODULE_EVENT) {
    stats_event->event->u.module_event.module_event_data = calloc (1, size);
    if (!stats_event->event->u.module_event.module_event_data) {
      STATS_ERR("calloc failure Out of memory");
      goto err2;
    }
  } else {
    STATS_LOW("Unknown event type.");
    goto err1;
  }

  return stats_event;
  err2:
    free(stats_event->event);
    stats_event->event = NULL;
  err1:
    free(stats_event);
    stats_event = NULL;
  err:
    return stats_event;
}

static boolean stats_port_free_set_parm_ctrl(void *data, void *user_data)
{
  (void) user_data;
  stats_port_event_t *stats_event = (stats_port_event_t *)data;

  /* Free stats_event->event->u.ptr */
  if (stats_event && stats_event->event) {
    if ((stats_event->event->type == MCT_EVENT_CONTROL_CMD) &&
      stats_event->event->u.ctrl_event.control_event_data) {
      free(stats_event->event->u.ctrl_event.control_event_data);
      stats_event->event->u.ctrl_event.control_event_data = NULL;
    } else if ((stats_event->event->type == MCT_EVENT_MODULE_EVENT) &&
      stats_event->event->u.module_event.module_event_data) {
      switch (stats_event->event->u.module_event.type) {
      /* Handle freeing internal payload memory on event case by case basis*/
      case MCT_EVENT_MODULE_STATS_POST_TO_BUS: {
        mct_bus_msg_t *bus_msg =
          stats_event->event->u.module_event.module_event_data;
        if (bus_msg->msg) {
          free(bus_msg->msg);
        }
      }
        break;

      default: {
      }
        break;
      } /* end switch (stats_event->event->u.module_event.type) */
  }

  free(stats_event->event->u.module_event.module_event_data);
  stats_event->event->u.module_event.module_event_data = NULL;
  }
          /* Free stats_event->event ptr*/
  if (stats_event) {
    if (stats_event->event) {
      free(stats_event->event);
      stats_event->event = NULL;
    }
    free(stats_event);
    stats_event = NULL;
  }
 return TRUE;
}

/** stats_port_save_post_bus_msg
 *
 */
static boolean stats_port_save_post_bus_msg(mct_port_t *port,
  mct_event_t *event)
{
  boolean                   rc = TRUE;
  int                       msg_q_idx;
  /* save to port event to queue */
  stats_port_event_t        *post_event;
  mct_bus_msg_t             *dest_bus_msg;
  mct_bus_msg_t             *orig_bus_msg;
  void                      *bus_payload_ptr;
  stats_port_private_t      *private =
    (stats_port_private_t *)(port->port_private);
  stats_port_setparm_ctrl_t *parm_ctrl =
    (stats_port_setparm_ctrl_t *)&private->parm_ctrl;

  post_event = stats_port_malloc_set_parm_ctrl(MCT_EVENT_MODULE_EVENT,
    sizeof(mct_bus_msg_t) );
  if (post_event == NULL) {
    STATS_ERR("Set param failed to set. Hence Ignored!!");
    return FALSE;
  }
  /* Malloc bus.payload and free in stats_port_free_set_parm_ctrl()*/
  dest_bus_msg =
    (mct_bus_msg_t *)(post_event->event->u.module_event.module_event_data);
  orig_bus_msg = (mct_bus_msg_t *)(event->u.module_event.module_event_data);

  bus_payload_ptr = malloc(orig_bus_msg->size);
  if (!bus_payload_ptr) {
    stats_port_free_set_parm_ctrl(post_event, NULL);
    return FALSE;
  }
  memcpy(post_event->event, event, sizeof(mct_event_t));
  memcpy(dest_bus_msg, orig_bus_msg, sizeof(mct_bus_msg_t));
  memcpy(bus_payload_ptr, orig_bus_msg->msg, orig_bus_msg->size);
  post_event->event->u.module_event.module_event_data = dest_bus_msg;
  dest_bus_msg->msg = bus_payload_ptr;
  STATS_LOW("bus_msg_type = %d frame_id=%d",
    dest_bus_msg->type, event->u.module_event.current_frame_id);

  msg_q_idx = event->u.module_event.current_frame_id
    + private->delay.applying_delay
    + private->delay.meta_reporting_delay;
  msg_q_idx = stats_port_get_msg_q_idx(msg_q_idx);

  pthread_mutex_lock(&parm_ctrl->msg_q_lock[msg_q_idx]);
  mct_queue_push_tail(parm_ctrl->msg_q[msg_q_idx], post_event);
  pthread_mutex_unlock(&parm_ctrl->msg_q_lock[msg_q_idx]);
  /*Sanity check if id = 0 send*/
  return rc;
}

/** stats_port_post_bus_msg
 *
 */
static boolean stats_port_post_bus_msg(mct_port_t *port, mct_event_t *event)
{
 mct_bus_msg_t *bus_msg =
     (mct_bus_msg_t *)event->u.module_event.module_event_data;
  if (!bus_msg) {
    STATS_ERR("Null bus message!");
    return FALSE;
  }

  mct_module_t *module = MCT_MODULE_CAST((MCT_PORT_PARENT(port))->data);
  if (!module) {
    STATS_ERR("Failure getting module info! bus_msg->type=%d",
      bus_msg->type);
    return FALSE;
  }

  STATS_LOW("module: %s bus_msg_type=%d frame_id=%d",
    module->object.name, bus_msg->type, event->u.module_event.current_frame_id);

  /* These are bus messages that need to be post to bus. No need
     to forward this message upstream or downstream */
  if (TRUE != mct_module_post_bus_msg(module, bus_msg)) {
    STATS_ERR("Failure posting bus msg type %d to the bus!",
      bus_msg->type);
    return FALSE;
  }

  return TRUE;
}

/** stats_port_set_pipeline_delay
 *
 */
void stats_port_set_pipeline_delay(mct_port_t *port,
  mct_pipeline_session_data_t *session_data)
{
  stats_port_private_t *private = (stats_port_private_t *)(port->port_private);

  private->delay.applying_delay =
    session_data->max_pipeline_frame_applying_delay;
  private->delay.meta_reporting_delay =
    session_data->max_pipeline_meta_reporting_delay;
}

/** stats_module_set_log_level
 *
 *  Set the appropriate dynamic log level using getprop
 *
 *  Log levels are controlled via "persist.camera.stats.debug"
 *  and "persist.camera.global.debug"
 */
void stats_port_set_log_level () {
  STATS_DEBUG_DATA_LEVEL_MASK(stats_debug_data_log_level);
  STATS_EXIF_DBG_MASK(stats_exif_debug_mask);
}

/** stats_port_handle_enable_meta_channel_event
 *    @port:  the port instance
 *    @event: the event to be processed
 *
 *  Handle meta channel event
 *
 *  Return TRUE on success, FALSE on failure.
 **/
static boolean stats_port_handle_enable_meta_channel_event(mct_port_t *port,
  mct_event_t *event)
{
  boolean              rc = TRUE;
  stats_port_private_t *private = port->port_private;
  mct_event_t          stats_event;
  uint32_t             ch_idx;

  meta_channel_buf_divert_request_t divert_request;
  divert_request.meta_idx_mask =
    *((uint32_t *)event->u.module_event.module_event_data);

  stats_event.direction = MCT_EVENT_UPSTREAM;
  stats_event.identity = event->identity;
  stats_event.type = MCT_EVENT_MODULE_EVENT;
  stats_event.u.module_event.type = MCT_EVENT_MODULE_META_CHANNEL_DIVERT;
  stats_event.u.module_event.module_event_data =
    (void *)(&(divert_request));
  STATS_LOW("Send event to ISP to divert the meta channel!");
  mct_port_send_event_to_peer(port, &stats_event);

  return rc;
}


/** stats_port_send_event_downstream
 *    @data:      a port object of mct_port_t where the event to send from;
 *    @user_data: object of stats_port_private_t which contains capability
 *                to match and event object.
 *
 *  This function should be called by stats src ports to
 *  redirect any event to its other src ports. eg Redirect Gyro
 *  event to Q3a stats src port. It will also redirect the event
 *  to the event originator.
 *
 *  Return TRUE on success, FALSE on failure.
 **/
static boolean stats_port_send_event_downstream(void *data, void *user_data)
{
  mct_port_t         *port = (mct_port_t *)data;
  stats_port_event_t *port_event = (stats_port_event_t *)user_data;
  if(!port || !port_event->event){
    return FALSE;
  }
  /* send to peer with same capability and not to the event originator*/
  if (MCT_PORT_EVENT_FUNC(port)) {
    MCT_PORT_EVENT_FUNC(port)(port, port_event->event);
  }

  return TRUE;
}

/** stats_port_resend_event_downstream
 *    @data:      a port object of mct_port_t where the event to send from;
 *    @user_data: object of stats_port_private_t which contains capability
 *                to match and event object.
 *
 *  Return: Boolean
 *
 *  This function should be called by stats src ports to
 *  redirect any event to its other src ports. eg Redirect Gyro
 *  event to Q3a stats src port. It will ensure it doesn't redirect
 *  the Q3 events back to the Q3A port, because Q3A port handles this
 *  redirection internally.
 **/
static boolean stats_port_redirect_event_downstream(void *data, void *user_data)
{
  mct_port_t         *port = (mct_port_t *)data;
  stats_port_event_t *port_event = (stats_port_event_t *)user_data;

  if((port->caps.u.stats.flag & MCT_PORT_CAP_STATS_Q3A) &&
    ((port_event->cap_flag & MCT_PORT_CAP_STATS_AEC) ||
    (port_event->cap_flag & MCT_PORT_CAP_STATS_AWB) ||
    (port_event->cap_flag & MCT_PORT_CAP_STATS_AF))) {
    /* The event originated from one of the Q3A subports, we don't want to
     * send it to the to the Q3A port again. */
    return TRUE;
  }

  if (MCT_PORT_EVENT_FUNC(port)) {
    MCT_PORT_EVENT_FUNC(port)(port, port_event->event);
  }

  return TRUE;
}

/** stats_port_check_session_id
 *    @d1: session+stream identity
 *    @d2: session+stream identity
 *
 *  To find out if both identities are matching;
 *
 *  Return TRUE if matches.
 **/
static boolean stats_port_check_session_id(void *d1, void *d2)
{
  unsigned int v1, v2;
  v1 = *((unsigned int *)d1);
  v2 = *((unsigned int *)d2);

  return  ((v1 & 0xFFFF0000) == (v2 & 0xFFFF0000) ?
           TRUE : FALSE);
}

/** stats_port_proc_eztune_set_parm
 *    @cap_flag:         the destination port's cap flag;
 *    @ezetune_cmd_data: the command data for the eztune
 *    @stats_parm:       the parameters to be sent to the Q3A module
 *
 *  Translate the HAL eztune cmd param to Stats set param
 *
 *  Return TRUE on success, FALSE on unknown eztune cmd.
 **/
static boolean stats_port_proc_eztune_set_parm(unsigned int *cap_flag,
  cam_eztune_cmd_data_t *ezetune_cmd_data, stats_set_params_type *stats_parm)
{
  boolean rc = TRUE;

  switch (ezetune_cmd_data->cmd) {
  case CAM_EZTUNE_CMD_STATUS: {
    *cap_flag =
      MCT_PORT_CAP_STATS_AWB | MCT_PORT_CAP_STATS_AEC | MCT_PORT_CAP_STATS_AF;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_ALL_SET_PARAM;
    stats_parm->u.q3a_param.u.q3a_all_param.type = Q3A_ALL_SET_EZTUNE_RUNNIG;
    stats_parm->u.q3a_param.u.q3a_all_param.u.ez_runnig =
      ezetune_cmd_data->u.running;
  }
    break;
  case CAM_EZTUNE_CMD_AEC_FORCE_LINECOUNT: {
    aec_ez_force_linecount_t *aec_ez_force_linecount =
      &stats_parm->u.q3a_param.u.aec_param.u.ez_force_linecount;
    *cap_flag = MCT_PORT_CAP_STATS_AEC;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
    stats_parm->u.q3a_param.u.aec_param.type =
      AEC_SET_PARAM_EZ_FORCE_LINECOUNT;
    cam_ez_force_params_t *force_info = &ezetune_cmd_data->u.ez_force_param;
    aec_ez_force_linecount->forced = force_info->forced;
    aec_ez_force_linecount->force_linecount_value = force_info->u.force_linecount_value;
  }
    break;
  case CAM_EZTUNE_CMD_AEC_FORCE_GAIN: {
    aec_ez_force_gain_t *aec_ez_force_gain =
      &stats_parm->u.q3a_param.u.aec_param.u.ez_force_gain;
    *cap_flag = MCT_PORT_CAP_STATS_AEC;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
    stats_parm->u.q3a_param.u.aec_param.type =
      AEC_SET_PARAM_EZ_FORCE_GAIN;
    cam_ez_force_params_t *force_info = &ezetune_cmd_data->u.ez_force_param;
    aec_ez_force_gain->forced = force_info->forced ;
    aec_ez_force_gain->force_gain_value = force_info->u.force_gain_value;
  }
    break;
  case CAM_EZTUNE_CMD_AEC_FORCE_EXP: {
    aec_ez_force_exp_t *aec_ez_force_exp =
      &stats_parm->u.q3a_param.u.aec_param.u.ez_force_exp;
    *cap_flag = MCT_PORT_CAP_STATS_AEC;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
    stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_EZ_FORCE_EXP;
    cam_ez_force_params_t *force_info = &ezetune_cmd_data->u.ez_force_param;
    aec_ez_force_exp->forced = force_info->forced;
    aec_ez_force_exp->force_exp_value = force_info->u.force_exp_value;
  }
    break;
  case CAM_EZTUNE_CMD_AEC_FORCE_SNAP_LC: {
    aec_ez_force_snap_linecount_t *aec_ez_force_snap_linecount =
      &stats_parm->u.q3a_param.u.aec_param.u.ez_force_snap_linecount;
    *cap_flag = MCT_PORT_CAP_STATS_AEC;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
    stats_parm->u.q3a_param.u.aec_param.type =
      AEC_SET_PARAM_EZ_FORCE_SNAP_LINECOUNT;
    cam_ez_force_params_t *force_info = &ezetune_cmd_data->u.ez_force_param;
    aec_ez_force_snap_linecount->forced = force_info->forced;
    aec_ez_force_snap_linecount->force_snap_linecount_value =
      force_info->u.force_snap_linecount_value;
  }
    break;
  case CAM_EZTUNE_CMD_AEC_FORCE_SNAP_GAIN: {
    aec_ez_force_snap_gain_t *aec_ez_force_snap_gain =
      &stats_parm->u.q3a_param.u.aec_param.u.ez_force_snap_gain;
    *cap_flag = MCT_PORT_CAP_STATS_AEC;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
    stats_parm->u.q3a_param.u.aec_param.type =
      AEC_SET_PARAM_EZ_FORCE_SNAP_GAIN;
    cam_ez_force_params_t *force_info = &ezetune_cmd_data->u.ez_force_param;
    aec_ez_force_snap_gain->forced = force_info->forced;
    aec_ez_force_snap_gain->force_snap_gain_value =
      force_info->u.force_snap_gain_value;
  }
    break;
  case CAM_EZTUNE_CMD_AEC_FORCE_SNAP_EXP: {
    aec_ez_force_snap_exp_t *aec_ez_force_snap_exp =
      &stats_parm->u.q3a_param.u.aec_param.u.ez_force_snap_exp;
    *cap_flag = MCT_PORT_CAP_STATS_AEC;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
    stats_parm->u.q3a_param.u.aec_param.type =
      AEC_SET_PARAM_EZ_FORCE_SNAP_EXP;
    cam_ez_force_params_t *force_info = &ezetune_cmd_data->u.ez_force_param;
    aec_ez_force_snap_exp->forced = force_info->forced;
    aec_ez_force_snap_exp->force_snap_exp_value =
      force_info->u.force_snap_exp_value;
  }
    break;
  case CAM_EZTUNE_CMD_AEC_ENABLE: {
    *cap_flag = MCT_PORT_CAP_STATS_AEC;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
    stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_ENABLE;
    stats_parm->u.q3a_param.u.aec_param.u.aec_enable =
      ezetune_cmd_data->u.aec_enable;
  }
    break;
  case CAM_EZTUNE_CMD_AWB_MODE: {
    *cap_flag = MCT_PORT_CAP_STATS_AWB;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AWB_PARAM;
    stats_parm->u.q3a_param.u.awb_param.type = AWB_SET_PARAM_WHITE_BALANCE;
    stats_parm->u.q3a_param.u.awb_param.u.awb_current_wb =
      ezetune_cmd_data->u.awb_mode;
  }
    break;
  case CAM_EZTUNE_CMD_AWB_ENABLE: {
    *cap_flag = MCT_PORT_CAP_STATS_AWB;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AWB_PARAM;
    stats_parm->u.q3a_param.u.awb_param.type = AWB_SET_PARAM_ENABLE;
    stats_parm->u.q3a_param.u.awb_param.u.awb_enable =
      ezetune_cmd_data->u.awb_enable;
  }
    break;
  case CAM_EZTUNE_CMD_AF_ENABLE: {
    *cap_flag = MCT_PORT_CAP_STATS_AF;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
    stats_parm->u.q3a_param.u.af_param.type = AF_SET_PARAM_EZ_ENABLE;
    stats_parm->u.q3a_param.u.af_param.u.af_ez_enable =
      ezetune_cmd_data->u.af_enable;
  }
    break;
  case CAM_EZTUNE_CMD_AWB_FORCE_DUAL_LED_IDX: {
    *cap_flag = MCT_PORT_CAP_STATS_AWB;
    stats_parm->param_type = STATS_SET_Q3A_PARAM;
    stats_parm->u.q3a_param.type = Q3A_SET_AWB_PARAM;
    stats_parm->u.q3a_param.u.awb_param.type = AWB_SET_PARAM_EZ_DUAL_LED_FORCE_IDX;
    stats_parm->u.q3a_param.u.awb_param.u.ez_force_dual_led_idx =
      ezetune_cmd_data->u.ez_force_dual_led_idx;
  }
    break;
  default: {
    /* Return FALSE if case of unknown or unhandled eztune cmd */
    rc = FALSE;
  }
    break;
  }

  return rc;
}

/** stats_port_is_transiently_event
 *    @idx: This value correspond to the parameter
 *
 *  Some parameters  have only a transitorily value that is valid only at the time it was originally
 *  set. This function helps to determine if the passed parameter is one of these transiently
 *  parameters.
 *
 *  Return TRUE if the parameter/event is transiently.
 **/
 static boolean stats_port_is_transiently_event(cam_intf_parm_type_t idx)
 {
   boolean is_transiently_param = FALSE;
   switch (idx) {
   case CAM_INTF_META_AF_TRIGGER:
   case CAM_INTF_META_AEC_PRECAPTURE_TRIGGER:
   case CAM_INTF_PARM_MANUAL_FOCUS_POS:
   case CAM_INTF_META_CAPTURE_INTENT:
     is_transiently_param = TRUE;
   break;
   default:
     is_transiently_param = FALSE;
     break;
   }
   return is_transiently_param;
}

/** stats_port_proc_set_parm
 *    @port:       a port instance where the event to send from;
 *    @event:      the event to be processed
 *    @sent_done:  a flag to indicate if the event is processed
 *
 *  Translate the HAL set param to Stats set param
 *
 *  Return TRUE on success, FALSE on failure.
 **/
static boolean stats_port_proc_downstream_set_parm(mct_port_t *port,
  mct_event_t *event, boolean *sent_done) {
  boolean rc = TRUE;
  *sent_done = FALSE;

  boolean                  send_internal = FALSE;
  boolean                  force_to_send_internal = FALSE;
  mct_event_control_parm_t *ui_parm =
    (mct_event_control_parm_t *)event->u.ctrl_event.control_event_data;
  if (!ui_parm || !ui_parm->parm_data) {
    STATS_ERR("failed NULL");
    return FALSE;
  }
  stats_port_private_t     *private;
  stats_port_event_t       port_event;

  private = (stats_port_private_t *)(port->port_private);
  stats_port_setparm_ctrl_t *parm_ctrl =
    (stats_port_setparm_ctrl_t *)&private->parm_ctrl;

  if (event->type == MCT_EVENT_CONTROL_CMD &&
    event->u.ctrl_event.type == MCT_EVENT_CONTROL_SET_PARM) {
    mct_event_t new_event;
    port_event.event = &new_event;
    stats_set_params_type *stats_parm = malloc(sizeof(stats_set_params_type));
    q3a_set_params_type *q3a_param;
    if (stats_parm != NULL) {
      new_event.direction = MCT_EVENT_DOWNSTREAM;
      new_event.timestamp = event->timestamp;
      new_event.identity = event->identity;
      new_event.type = event->type;
      new_event.u.ctrl_event.type = event->u.ctrl_event.type;
      new_event.u.ctrl_event.control_event_data = stats_parm;
      *sent_done  = TRUE;
      switch (ui_parm->type) {
      case CAM_INTF_PARM_ZSL_MODE: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_ZSL_OP;
        stats_parm->u.q3a_param.u.aec_param.u.zsl_op =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_BRIGHTNESS: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_BRIGHTNESS_LVL;
        stats_parm->u.q3a_param.u.aec_param.u.brightness =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_INSTANT_AEC: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_INSTANT_AEC_DATA;

        uint8 instant_aec_type = *((uint8_t*)ui_parm->parm_data);

        send_internal = TRUE;
        if (CAM_AEC_NORMAL_CONVERGENCE == instant_aec_type) {
          stats_parm->u.common_param.u.instant_aec_type =
            AEC_CONVERGENCE_NORMAL;
        } else if (CAM_AEC_AGGRESSIVE_CONVERGENCE == instant_aec_type) {
            stats_parm->u.common_param.u.instant_aec_type =
              AEC_CONVERGENCE_AGGRESSIVE;
        } else if (CAM_AEC_FAST_CONVERGENCE == instant_aec_type) {
            stats_parm->u.common_param.u.instant_aec_type =
              AEC_CONVERGENCE_FAST;
            /* Incase of Fast AEC, Set Param event will come from sensor to AEC module
             * to enable Fast AEC convergence. No need to recieve set param again from HAL.
             * So Do not send this set param internal to AEC.
             */
            send_internal = FALSE;
            STATS_HIGH("Received Fast AEC set param.No Action required");
        } else {
            stats_parm->u.common_param.u.instant_aec_type =
              AEC_CONVERGENCE_NORMAL;
            STATS_ERR("Invalid instant aec type, use default convergence");
        }
      }
        break;
      case CAM_INTF_PARM_WHITE_BALANCE: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AWB;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AWB_PARAM;
        stats_parm->u.q3a_param.u.awb_param.type =  AWB_SET_PARAM_WHITE_BALANCE;

        cam_wb_mode_type  wb_mode_type =
          *((cam_wb_mode_type *)ui_parm->parm_data);
        awb_config3a_wb_t *pConfig3aWb =
          &stats_parm->u.q3a_param.u.awb_param.u.awb_current_wb;
        send_internal = TRUE;

        switch (wb_mode_type) {
        case CAM_WB_MODE_AUTO:
          *pConfig3aWb = CAMERA_WB_AUTO;
          break;
        case CAM_WB_MODE_CUSTOM:
          *pConfig3aWb = CAMERA_WB_CUSTOM;
          break;
        case CAM_WB_MODE_INCANDESCENT:
          *pConfig3aWb = CAMERA_WB_INCANDESCENT;
          break;
        case CAM_WB_MODE_FLUORESCENT:
          *pConfig3aWb = CAMERA_WB_FLUORESCENT;
          break;
        case CAM_WB_MODE_WARM_FLUORESCENT:
          *pConfig3aWb = CAMERA_WB_WARM_FLUORESCENT;
          break;
        case CAM_WB_MODE_DAYLIGHT:
          *pConfig3aWb = CAMERA_WB_DAYLIGHT;
          break;
        case CAM_WB_MODE_CLOUDY_DAYLIGHT:
          *pConfig3aWb = CAMERA_WB_CLOUDY_DAYLIGHT;
          break;
        case CAM_WB_MODE_TWILIGHT:
          *pConfig3aWb = CAMERA_WB_TWILIGHT;
          break;
        case CAM_WB_MODE_SHADE:
          *pConfig3aWb = CAMERA_WB_SHADE;
          break;
        case CAM_WB_MODE_OFF:
          *pConfig3aWb = CAMERA_WB_OFF;
          break;
        case CAM_WB_MODE_MANUAL:
          *pConfig3aWb = CAMERA_WB_MANUAL;
          break;
        default:
          STATS_ERR("WB format %d not supported!", wb_mode_type);
          send_internal = FALSE;
          break;
        }
      }
        break;
      case CAM_INTF_PARM_WB_MANUAL: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AWB;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AWB_PARAM;
        stats_parm->u.q3a_param.u.awb_param.type = AWB_SET_PARAM_MANUAL_WB;
        manual_wb_parm_t manual_parm;
        cam_manual_wb_parm_t *manual_info =
          (cam_manual_wb_parm_t *) ui_parm->parm_data;
        cam_manual_wb_mode_type  wb_mode_type = manual_info->type;
        send_internal = TRUE;

        switch (wb_mode_type) {
          case CAM_MANUAL_WB_MODE_CCT:
            manual_parm.type  = MANUAL_WB_MODE_CCT;
            manual_parm.u.cct = manual_info->cct;
            stats_parm->u.q3a_param.u.awb_param.u.manual_wb_params = manual_parm;
            break;

          case CAM_MANUAL_WB_MODE_GAIN:
            manual_parm.type = MANUAL_WB_MODE_GAIN;
            manual_parm.u.gains.r_gain = manual_info->gains.r_gain;
            manual_parm.u.gains.g_gain = manual_info->gains.g_gain;
            manual_parm.u.gains.b_gain = manual_info->gains.b_gain;
            stats_parm->u.q3a_param.u.awb_param.u.manual_wb_params = manual_parm;
            break;

          default:
            STATS_ERR("Manual format %d not supported!", wb_mode_type);
            send_internal = FALSE;
            break;
         }

      }
        break;
      case CAM_INTF_PARM_ISO: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_ISO_MODE;
        stats_parm->u.q3a_param.u.aec_param.u.iso =
          *((cam_intf_parm_manual_3a_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_DUAL_LED_CALIBRATION: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AWB;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_ALL_SET_PARAM;
        stats_parm->u.q3a_param.u.q3a_all_param.type = Q3A_ALL_SET_DUAL_LED_CALIB_MODE;
        stats_parm->u.q3a_param.u.q3a_all_param.u.dual_led_calib_mode =
          *((uint32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_EXPOSURE_TIME: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_EXP_TIME;
        stats_parm->u.q3a_param.u.aec_param.u.manual_exposure_time =
          *((cam_intf_parm_manual_3a_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_ANTIBANDING: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AFD;
        stats_parm->param_type = STATS_SET_AFD_PARAM;
        stats_parm->u.afd_param =
          *((cam_antibanding_mode_type *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_FPS_RANGE: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_FPS;
        if (CAM_STREAM_TYPE_VIDEO != private->stream_type) {
          stats_parm->u.q3a_param.u.aec_param.u.fps.max_fps =
            ((cam_fps_range_t *)ui_parm->parm_data)->max_fps * 256;
          stats_parm->u.q3a_param.u.aec_param.u.fps.min_fps =
            ((cam_fps_range_t *)ui_parm->parm_data)->min_fps * 256;
        } else {
          stats_parm->u.q3a_param.u.aec_param.u.fps.max_fps =
            ((cam_fps_range_t *)ui_parm->parm_data)->video_max_fps * 256;
          stats_parm->u.q3a_param.u.aec_param.u.fps.min_fps =
            ((cam_fps_range_t *)ui_parm->parm_data)->video_min_fps * 256;
        }
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_EXPOSURE_COMPENSATION: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type =
          AEC_SET_PARAM_EXP_COMPENSATION;
        stats_parm->u.q3a_param.u.aec_param.u.exp_comp =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_LED_MODE: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_LED_MODE;
        stats_parm->u.q3a_param.u.aec_param.u.led_mode =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_AEC_ALGO_TYPE: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_METERING_MODE;
        stats_parm->u.q3a_param.u.aec_param.u.aec_metering =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_FOCUS_ALGO_TYPE: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.type = AF_SET_PARAM_METERING_MODE;
        stats_parm->u.q3a_param.u.af_param.u.af_metering_mode =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_AEC_ROI: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_ROI;
        cam_set_aec_roi_t roi_info =
          *((cam_set_aec_roi_t *)ui_parm->parm_data);
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.enable =
          roi_info.aec_roi_enable;
        /* Only send a single touch ROI */
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.r[0].x =
          roi_info.cam_aec_roi_position.coordinate[0].x;
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.r[0].y =
          roi_info.cam_aec_roi_position.coordinate[0].y;
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.r[0].dx = 0;
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.r[0].dy = 0;
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.num_regions = 1;
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_AF_ROI: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.type = AF_SET_PARAM_ROI;
        stats_parm->u.q3a_param.u.af_param.current_frame_id =
            event->u.ctrl_event.current_frame_id;
        cam_roi_info_t cam_roi_info =
          *((cam_roi_info_t *)ui_parm->parm_data);
        int            i;

        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi_updated = TRUE;

        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.num_roi =
          cam_roi_info.num_roi;
        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.type =
          AF_ROI_TYPE_TOUCH;
        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.frm_id =
          cam_roi_info.frm_id;
        for(i = 0; i < cam_roi_info.num_roi; i++) {
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[i].x =
          cam_roi_info.roi[i].left;
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[i].y =
          cam_roi_info.roi[i].top;
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[i].dx =
          cam_roi_info.roi[i].width;
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[i].dy =
          cam_roi_info.roi[i].height;
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.weight[i] = 0;
        }
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_FOCUS_MODE: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.type = AF_SET_PARAM_FOCUS_MODE;
        stats_parm->u.q3a_param.u.af_param.u.af_mode =
          *((int32_t *)ui_parm->parm_data);

        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_BESTSHOT_MODE: {
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
          MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB |
          MCT_PORT_CAP_STATS_ASD);
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_BESTSHOT;
        stats_parm->u.common_param.u.bestshot_mode =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_LOCK_CAF: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.type = AF_SET_PARAM_LOCK_CAF;
        stats_parm->u.q3a_param.u.af_param.current_frame_id =
          event->u.ctrl_event.current_frame_id;
        stats_parm->u.q3a_param.u.af_param.u.af_lock_caf =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_AEC_LOCK: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type =  AEC_SET_PARAM_LOCK;
        stats_parm->u.q3a_param.u.aec_param.u.aec_lock =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_EFFECT: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type =  AEC_SET_PARAM_EFFECT;
        stats_parm->u.q3a_param.u.aec_param.u.effect_mode =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_AWB_LOCK: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AWB;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AWB_PARAM;
        stats_parm->u.q3a_param.u.awb_param.type = AWB_SET_PARAM_LOCK;
        stats_parm->u.q3a_param.u.awb_param.u.awb_lock =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_EZTUNE_CMD: {
        cam_eztune_cmd_data_t *ezetune_cmd_data =
          (cam_eztune_cmd_data_t *)ui_parm->parm_data;

        stats_port_proc_eztune_set_parm(&port_event.cap_flag, ezetune_cmd_data,
          stats_parm);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_REDEYE_REDUCTION: {
        /* Do nothing */
      }
        break;
      case CAM_INTF_PARM_ASD_ENABLE: {
        /* Do nothing */
      }
        break;
      case CAM_INTF_PARM_DIS_ENABLE: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_IS;
        stats_parm->param_type = STATS_SET_IS_PARAM;
        stats_parm->u.is_param.type = IS_SET_PARAM_IS_ENABLE;
        stats_parm->u.is_param.u.is_enable = *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_HDR: {
        cam_exp_bracketing_t exp;
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_BRACKET;
        exp = *((cam_exp_bracketing_t *)ui_parm->parm_data);
        if(exp.mode == CAM_EXP_BRACKETING_OFF)
          stats_parm->u.q3a_param.u.aec_param.u.aec_bracket[0] = '\0';
        else
          strlcpy(stats_parm->u.q3a_param.u.aec_param.u.aec_bracket,
            exp.values, MAX_EXP_BRACKETING_LENGTH);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_SENSOR_HDR:{
      /* Sensor HW HDR: both preview and snapshot are running in HDR mode */
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB);
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_SNAPSHOT_HDR;
        stats_parm->u.common_param.u.snapshot_hdr =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_VIDEO_HDR: {
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB);
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_VIDEO_HDR;
        stats_parm->u.common_param.u.video_hdr =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_META_SENSOR_EXPOSURE_TIME: { // HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type =
          AEC_SET_PARAM_MANUAL_EXP_TIME;
        stats_parm->u.q3a_param.u.aec_param.u.manual_expTime =
          *((int64_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
      break;
      case CAM_INTF_META_SENSOR_SENSITIVITY: { // HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type =
          AEC_SET_PARAM_MANUAL_GAIN;
        stats_parm->u.q3a_param.u.aec_param.u.manual_gain =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
      break;
      case CAM_INTF_META_LENS_FOCUS_DISTANCE: { // HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.type =
          AF_SET_PARAM_FOCUS_MANUAL_POSITION;
        stats_parm->u.q3a_param.u.af_param.u.af_manual_focus_info.flag =
            AF_MANUAL_FOCUS_MODE_DIOPTER;
        stats_parm->u.q3a_param.u.af_param.u.af_manual_focus_info.u.
             af_manual_diopter =
          *((float *)ui_parm->parm_data);
        send_internal = TRUE;
      }
      break;
      case CAM_INTF_PARM_MANUAL_FOCUS_POS: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.type =
          AF_SET_PARAM_FOCUS_MANUAL_POSITION;
        cam_manual_focus_parm_t* manual_info =
          (cam_manual_focus_parm_t *)ui_parm->parm_data;
        if (manual_info->flag == CAM_MANUAL_FOCUS_MODE_DIOPTER) {
          stats_parm->u.q3a_param.u.af_param.u.af_manual_focus_info.flag =
            AF_MANUAL_FOCUS_MODE_DIOPTER;
          stats_parm->u.q3a_param.u.af_param.u.af_manual_focus_info.u.
            af_manual_diopter = manual_info->af_manual_diopter;
          send_internal = TRUE;
        } else if (manual_info->flag == CAM_MANUAL_FOCUS_MODE_RATIO) {
          stats_parm->u.q3a_param.u.af_param.u.af_manual_focus_info.flag =
            AF_MANUAL_FOCUS_MODE_POS_RATIO;
          stats_parm->u.q3a_param.u.af_param.u.af_manual_focus_info.
            u.af_manual_lens_position_ratio =
            manual_info->af_manual_lens_position_ratio;
          send_internal = TRUE;
        } else {
          STATS_LOW("Dac value and index are not supported");
          rc = FALSE;
        }
      }
        break;
      case CAM_INTF_META_MODE :{
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
          MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB |
          MCT_PORT_CAP_STATS_ASD);
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_META_MODE;
        stats_parm->u.common_param.u.meta_mode =
          *((uint8_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_META_AEC_MODE: { // HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_ON_OFF;
        cam_ae_mode_type ae_mode_type =
          *((cam_ae_mode_type *)ui_parm->parm_data);

        switch(ae_mode_type) {
        case CAM_AE_MODE_OFF: {
          stats_parm->u.q3a_param.u.aec_param.u.enable_aec = FALSE;
          send_internal = TRUE;
        }
          break;

        case CAM_AE_MODE_ON: {
          stats_parm->u.q3a_param.u.aec_param.u.enable_aec = TRUE;
          send_internal = TRUE;
        }
          break;

        default: {
          STATS_ERR("Error: Unrecognized AEC mode configuration!");
        }
          break;
        }
      }
        break;

      case CAM_INTF_META_AEC_ROI: { // HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_SENSOR_ROI;
        cam_area_t area_info = *((cam_area_t *)ui_parm->parm_data);

        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.enable = 0;
        // per spec HAL 3.2 if weight == 0, disable the ROI
        if (area_info.weight != 0 && (area_info.rect.left || area_info.rect.top ||
              area_info.rect.width || area_info.rect.height)) {
          stats_parm->u.q3a_param.u.aec_param.u.aec_roi.enable = 1;
        }
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.weight =
          area_info.weight;
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.r[0].x =
          area_info.rect.left;
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.r[0].y =
          area_info.rect.top;
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.r[0].dx =
          area_info.rect.width;
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.r[0].dy =
          area_info.rect.height;
        stats_parm->u.q3a_param.u.aec_param.u.aec_roi.num_regions = 1;
        send_internal = TRUE;
      }
        break;

      case CAM_INTF_META_AF_ROI: { // HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.type =  AF_SET_PARAM_SENSOR_ROI;
        stats_parm->u.q3a_param.u.af_param.current_frame_id =
          event->u.ctrl_event.current_frame_id;
        cam_area_t area_info = *((cam_area_t *)ui_parm->parm_data);

        STATS_LOW("AF ROI sent x: %d ,y: %d dx: %d dy: %d, weight: %d ",
          area_info.rect.left,
          area_info.rect.top,
          area_info.rect.width,
          area_info.rect.height,
          area_info.weight);

        if (area_info.weight == 0) {
          // weight 0 means cancel touch AF
          // clear the value in rect
          memset(&area_info.rect, 0, sizeof(area_info.rect));
        }

        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.type =
          AF_ROI_TYPE_GENERAL;
        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.num_roi = 0;

        if (area_info.rect.left || area_info.rect.top ||
          area_info.rect.width || area_info.rect.height) {
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.type =
            AF_ROI_TYPE_TOUCH;
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.num_roi = 1;
        }

        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi_updated = FALSE;
        if (stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[0].x !=
            area_info.rect.left ||
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[0].y !=
            area_info.rect.top ||
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[0].dx !=
            area_info.rect.width ||
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[0].dy !=
            area_info.rect.height) {
          stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi_updated = TRUE;
        }

        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.frm_id += 1;
        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[0].x =
          area_info.rect.left;
        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[0].y =
          area_info.rect.top;
        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[0].dx =
          area_info.rect.width;
        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.roi[0].dy =
          area_info.rect.height;
        stats_parm->u.q3a_param.u.af_param.u.af_roi_info.weight[0] =
          area_info.weight;
        send_internal = TRUE;
      }
        break;

      case CAM_INTF_META_AF_TRIGGER: { // HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.current_frame_id =
          event->u.ctrl_event.current_frame_id;

        cam_trigger_t af_trigger = *((cam_trigger_t *)ui_parm->parm_data);
        stats_parm->u.q3a_param.u.af_param.u.af_trigger_id =
          af_trigger.trigger_id;

        if (private->legacy_hal_cmd == TRUE) {
          /* For AF it does not matter how the command is issued, so just
           * reset the flag for the current HAL command */
          private->legacy_hal_cmd = FALSE;
        }

        send_internal = TRUE;
        if (af_trigger.trigger == CAM_AF_TRIGGER_START) {
          stats_parm->u.q3a_param.u.af_param.type =
            AF_SET_PARAM_START;
          STATS_LOW("CAM_AF_TRIGGER_START");
        } else if (af_trigger.trigger == CAM_AF_TRIGGER_CANCEL) {
          STATS_LOW("CAM_AF_TRIGGER_CANCEL");
          stats_parm->u.q3a_param.u.af_param.type = AF_SET_PARAM_CANCEL_FOCUS;
        } else {
          send_internal = FALSE;
        }
      }
        break;

      case CAM_INTF_META_SCENE_FLICKER: { //HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AFD;
        stats_parm->param_type = STATS_SET_AFD_PARAM;
        stats_parm->u.afd_param =
          *((cam_antibanding_mode_type *)ui_parm->parm_data);
        send_internal = FALSE;
      }
        break;

      case CAM_INTF_META_AEC_PRECAPTURE_TRIGGER: { //HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        if (private->legacy_hal_cmd == TRUE) {
          /* HAL1 to HAL3 style */
          stats_parm->u.q3a_param.u.aec_param.type =
            AEC_SET_PARAM_PREP_FOR_SNAPSHOT_LEGACY;
          /* Reset the flag, we need it only for the current HAL command. */
          private->legacy_hal_cmd = FALSE;
        } else {
          stats_parm->u.q3a_param.u.aec_param.type =
            AEC_SET_PARAM_PREPARE_FOR_SNAPSHOT;
        }
        cam_trigger_t recvd_aec_trigger =
          *((cam_trigger_t *)ui_parm->parm_data);

        stats_parm->u.q3a_param.u.aec_param.u.aec_trigger.trigger =
          recvd_aec_trigger.trigger;
        stats_parm->u.q3a_param.u.aec_param.u.aec_trigger.trigger_id =
          recvd_aec_trigger.trigger_id;
        send_internal = TRUE;
      }
        break;

      case CAM_INTF_META_COLOR_CORRECT_GAINS: {
         cam_color_correct_gains_t *ui_gains =
           (cam_color_correct_gains_t *)ui_parm->parm_data;
         port_event.cap_flag = MCT_PORT_CAP_STATS_AWB;
         stats_parm->param_type = STATS_SET_Q3A_PARAM;
         stats_parm->u.q3a_param.type = Q3A_SET_AWB_PARAM;
         stats_parm->u.q3a_param.u.awb_param.type =
           AWB_SET_PARAM_MANUAL_WB;
         stats_parm->u.q3a_param.u.awb_param.u.manual_wb_params.type
           = MANUAL_WB_MODE_GAIN;
         stats_parm->u.q3a_param.u.awb_param.u.manual_wb_params.u.gains.r_gain
           = ui_gains->gains[0];
         stats_parm->u.q3a_param.u.awb_param.u.manual_wb_params.u.gains.g_gain
           = ui_gains->gains[1];
         stats_parm->u.q3a_param.u.awb_param.u.manual_wb_params.u.gains.b_gain
           = ui_gains->gains[2];
         send_internal = TRUE;
      }
        break;

      case CAM_INTF_META_CAPTURE_INTENT: {
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
          MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB |
          MCT_PORT_CAP_STATS_ASD);
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_CAPTURE_INTENT;
        stats_parm->u.common_param.u.capture_type =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_META_SCALER_CROP_REGION: {
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
          MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB |
          MCT_PORT_CAP_STATS_ASD);
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_CROP_REGION;
        stats_parm->u.common_param.u.crop_region =
          *((cam_crop_region_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;

      /*
      Set param will trigger log level change in port side and
      also in algorithm.
      */
      case CAM_INTF_PARM_UPDATE_DEBUG_LEVEL: {
        stats_port_set_log_level();

        port_event.cap_flag = (MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB | MCT_PORT_CAP_STATS_AF);

        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_STATS_DEBUG_MASK;
        send_internal = TRUE;
      }
        break;

      case CAM_INTF_PARM_STATS_AF_PAAF: {
        int32_t data = *((int32_t *)ui_parm->parm_data);

        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;

        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.type =  AF_SET_PARAM_PAAF;
        stats_parm->u.q3a_param.u.af_param.u.paaf_mode =
          *((int32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;

      case CAM_INTF_META_AWB_REGIONS: { // HAL3
        port_event.cap_flag = MCT_PORT_CAP_STATS_AWB;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AWB_PARAM;
        stats_parm->u.q3a_param.u.awb_param.type = AWB_SET_PARAM_ROI;

        cam_area_t area_info = *((cam_area_t *)ui_parm->parm_data);

        STATS_LOW("AWB ROI sent x: %d ,y: %d dx: %d dy: %d, weight: %d",
          area_info.rect.left,
          area_info.rect.top,
          area_info.rect.width,
          area_info.rect.height,
          area_info.weight);

        if (area_info.weight == 0) {
          break;
        }

        stats_parm->u.q3a_param.u.awb_param.u.awb_roi_info.num_roi = 0;
        if (area_info.rect.left || area_info.rect.top ||
          area_info.rect.width || area_info.rect.height) {
          stats_parm->u.q3a_param.u.awb_param.u.awb_roi_info.enable = TRUE;

        }

        stats_parm->u.q3a_param.u.awb_param.u.awb_roi_info.roi[0].x =
          area_info.rect.left;
        stats_parm->u.q3a_param.u.awb_param.u.awb_roi_info.roi[0].y =
          area_info.rect.top;
        stats_parm->u.q3a_param.u.awb_param.u.awb_roi_info.roi[0].dx =
          area_info.rect.width;
        stats_parm->u.q3a_param.u.awb_param.u.awb_roi_info.roi[0].dy =
          area_info.rect.height;
        stats_parm->u.q3a_param.u.awb_param.u.awb_roi_info.weight[0] =
          area_info.weight;
        stats_parm->u.q3a_param.u.awb_param.u.awb_roi_info.num_roi = 1;
        send_internal = TRUE;
      }
        break;

      case CAM_INTF_PARM_HAL_VERSION: {
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
          MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB |
          MCT_PORT_CAP_STATS_ASD);
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_HAL_VERSION;
        stats_parm->u.common_param.u.hal_version =
          *((int32_t *)ui_parm->parm_data);
        private->hal_version =  stats_parm->u.common_param.u.hal_version;
        send_internal = TRUE;
      }
        break;
      case CAM_INTF_PARM_LONGSHOT_ENABLE: {
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB);

        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_LONGSHOT_MODE;
        stats_parm->u.common_param.u.longshot_mode =
          *((int8_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
      break;

      case CAM_INTF_PARM_HFR: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AF_PARAM;
        stats_parm->u.q3a_param.u.af_param.type = AF_SET_PARAM_HFR_MODE;
        stats_parm->u.q3a_param.u.af_param.u.hfr_mode =
          *((int32_t *)ui_parm->parm_data);

        send_internal = TRUE;
      }
        break;

      case CAM_INTF_PARM_FD: {
        cam_fd_set_parm_t *fd_set_parm =
          (cam_fd_set_parm_t *)ui_parm->parm_data;
        boolean fd_enabled =
          ((CAM_FACE_PROCESS_MASK_DETECTION & fd_set_parm->fd_mode)||
          (CAM_FACE_PROCESS_MASK_FOCUS & fd_set_parm->fd_mode)) ?
          TRUE : FALSE;

        port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
          MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB |
          MCT_PORT_CAP_STATS_ASD);
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_FD;
        stats_parm->u.common_param.u.fd_enabled = fd_enabled;
        send_internal = TRUE;
      }
         break;

     case CAM_INTF_PARM_CAPTURE_FRAME_CONFIG:{
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB);

        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_UNIFIED_FLASH;
        stats_parm->u.common_param.u.frame_info =
          *((cam_capture_frame_config_t *)ui_parm->parm_data);

        private->frame_capture_mode_in_progress =
          stats_parm->u.common_param.u.frame_info.num_batch != 0 ? TRUE : FALSE;

        STATS_HIGH("Set frame_capture_mode_in_progress = %s",
          private->frame_capture_mode_in_progress ? "TRUE" : "FALSE");
        force_to_send_internal = TRUE; /* This information is needed even in stream-off */
      }
      break;

      case CAM_INTF_PARM_CUSTOM: {
        port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
          MCT_PORT_CAP_STATS_AEC |
          MCT_PORT_CAP_STATS_AWB |
          MCT_PORT_CAP_STATS_ASD |
          MCT_PORT_CAP_STATS_GYRO |
          MCT_PORT_CAP_STATS_IS |
          MCT_PORT_CAP_STATS_AFD);
        stats_parm->param_type = STATS_SET_COMMON_PARAM;
        stats_parm->u.common_param.type = COMMON_SET_PARAM_CUSTOM;
        stats_parm->u.common_param.u.custom_param.size =
          get_size_of(CAM_INTF_PARM_CUSTOM);
        stats_parm->u.common_param.u.custom_param.data =
          (custom_parm_buffer_t *)ui_parm->parm_data;
        force_to_send_internal = TRUE; /* Bypass back-up of parameters for custom type */
      }
      break;

      case CAM_INTF_PARM_INITIAL_EXPOSURE_INDEX: {
        port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
        stats_parm->param_type = STATS_SET_Q3A_PARAM;
        stats_parm->u.q3a_param.type = Q3A_SET_AEC_PARAM;
        stats_parm->u.q3a_param.u.aec_param.type = AEC_SET_PARAM_INIT_EXPOSURE_INDEX;
        stats_parm->u.q3a_param.u.aec_param.u.init_exposure_index =
          *((uint32_t *)ui_parm->parm_data);
        send_internal = TRUE;
      }
        break;

      default: {
      }
        break;
      }

      if (force_to_send_internal) {
        /* Use this flag to send parameters that are not overwritten by Chromatix */
        rc = mct_list_traverse((mct_list_t *)private->sub_ports,
          stats_port_send_event_downstream, &port_event);
      } else if (send_internal) {
        int     idx = ui_parm->type;
        MCT_OBJECT_LOCK(port);

        if (!parm_ctrl->is_initialised[idx] &&
          parm_ctrl->evt[idx].event == NULL) {
          parm_ctrl->evt[idx].event = malloc(sizeof(mct_event_t));
          parm_ctrl->evt[idx].event->u.ctrl_event.control_event_data =
            (void *)malloc(sizeof(stats_set_params_type));
        }
        /* make a copy of control_event_data before it's been overwriteen*/
        void* p = parm_ctrl->evt[idx].event->u.ctrl_event.control_event_data;
        memcpy(parm_ctrl->evt[idx].event, port_event.event,
               sizeof(mct_event_t));
        parm_ctrl->evt[idx].event->u.ctrl_event.control_event_data = p;
        memcpy((stats_set_params_type *)parm_ctrl->evt[idx].event->
               u.ctrl_event.control_event_data,
               stats_parm, sizeof(stats_set_params_type));

        parm_ctrl->evt[idx].cap_flag = port_event.cap_flag;
        parm_ctrl->is_initialised[idx] = TRUE;

        if (parm_ctrl->has_chromatix_set) {
          rc = mct_list_traverse((mct_list_t *)private->sub_ports,
            stats_port_send_event_downstream, &port_event);
        }
        MCT_OBJECT_UNLOCK(port);

      }
      free(stats_parm);
    }
  }
  return rc;
}

/** stats_port_transform_af_cmd_to_set_parm
 *    @port:       a port instance where the event to send from;
 *    @event:      the event to be processed
 *    @sent_done:  a flag to indicate if the event is processed
 *
 *  Translate the HAL set param to Stats set param
 *
 *  Return TRUE on success, FALSE on failure.
 **/
static boolean stats_port_transform_af_cmd_to_set_parm(mct_port_t *port,
  mct_event_t *event, boolean *sent_done)
{
  boolean                  rc = TRUE;
  mct_event_control_parm_t fake_ui_parm;
  cam_trigger_t            fake_trigger;
  stats_port_private_t     *private;
  mct_event_t              stats_event;

  stats_event = *event;
  private = (stats_port_private_t *)(port->port_private);
  stats_port_setparm_ctrl_t *parm_ctrl =
    (stats_port_setparm_ctrl_t *)&private->parm_ctrl;

  if (event->u.ctrl_event.type == MCT_EVENT_CONTROL_DO_AF) {
    fake_trigger.trigger = CAM_AF_TRIGGER_START;
    STATS_LOW("Do_AF call -> CAM_AF_TRIGGER_START");
  } else if (event->u.ctrl_event.type == MCT_EVENT_CONTROL_CANCEL_AF) {
    fake_trigger.trigger = CAM_AF_TRIGGER_CANCEL;
    STATS_LOW("Cancel_AF call -> CAM_AF_TRIGGER_CANCEL");
  } else {
    *sent_done = FALSE;
    return rc;
  }

  fake_trigger.trigger_id = ++private->fake_trigger_id;
  fake_ui_parm.type = CAM_INTF_META_AF_TRIGGER;
  fake_ui_parm.parm_data = &fake_trigger;

  /* change the type of the event */
  stats_event.u.ctrl_event.type = MCT_EVENT_CONTROL_SET_PARM;
  /* Set the event data pointer */
  stats_event.u.ctrl_event.control_event_data = &fake_ui_parm;
  /* Set the flag to distinguish the way we should handle the set parameter */
  private->legacy_hal_cmd = TRUE;
  /* Send the faked set_param event downstream */
  rc = stats_port_proc_downstream_set_parm(port, &stats_event, sent_done);

  return rc;
}

/** stats_port_transform_prepsnap_cmd_to_set_parm
 *    @port:       a port instance where the event to send from;
 *    @event:      the event to be processed
 *    @sent_done:  a flag to indicate if the event is processed
 *
 *  Translate the HAL prepare snapshot cmd param to Stats set param
 *
 *  Return TRUE on success, FALSE on failure.
 **/
static boolean stats_port_transform_prepsnap_cmd_to_set_parm(mct_port_t *port,
  mct_event_t *event, boolean *sent_done)
{
  boolean                  rc = TRUE;
  mct_event_control_parm_t fake_ui_parm;
  cam_trigger_t            fake_trigger;
  stats_port_private_t     *private;
  mct_event_t              stats_event;

  stats_event = *event;
  private = (stats_port_private_t *)(port->port_private);
  stats_port_setparm_ctrl_t *parm_ctrl =
    (stats_port_setparm_ctrl_t *)&private->parm_ctrl;

  fake_ui_parm.parm_data = &fake_trigger;

  /* change the type of the event */
  stats_event.u.ctrl_event.type = MCT_EVENT_CONTROL_SET_PARM;
  /* Set the event data pointer */
  stats_event.u.ctrl_event.control_event_data = &fake_ui_parm;

  /* Now send a precapture trigger */
  fake_ui_parm.type = CAM_INTF_META_AEC_PRECAPTURE_TRIGGER;
  fake_trigger.trigger = CAM_AEC_TRIGGER_START;
  fake_trigger.trigger_id = ++private->fake_trigger_id;

  /* Send the faked set_param event downstream */
  STATS_LOW("E");
  /* Set the flag to distinguish the way we should handle the set parameter */
  private->legacy_hal_cmd = TRUE;
  rc = stats_port_proc_downstream_set_parm(port, &stats_event, sent_done);

  return rc;
}

/** stats_port_handle_sof_set_parm
 *    @port: this port from where the event should go
 *    @event: event object to send upstream or downstream
 *
 *  As part of the HAL3 architecture, ISP sends the super event
 *  which bundles all the set parameters. This needs to be
 *  iterated and individual set params needs to be set.
 *
 *  Return TRUE for successful event processing.
 **/
static boolean stats_port_handle_sof_set_parm(mct_port_t *port, mct_event_t *event)
{
  boolean                        ret = TRUE;
  mct_event_super_control_parm_t *param = NULL;
  uint32_t                       index = 0;
  boolean                        sent = FALSE;
  mct_event_t                    sub_event;

  if (!port || !event) {
    STATS_ERR("failed: invalid params port %p event%p ", port, event);
    return FALSE;
  }

  stats_port_private_t *private = (stats_port_private_t *)(port->port_private);
  param =
    (mct_event_super_control_parm_t *)event->u.ctrl_event.control_event_data;
  if (!param) {
    STATS_ERR("failed: param %p", param);
    return FALSE;
  }
  /* Populate sub_event*/
  sub_event.direction = event->direction;
  sub_event.identity = event->identity;
  sub_event.timestamp = event->timestamp;
  sub_event.type = MCT_EVENT_CONTROL_CMD;
  sub_event.u.ctrl_event.type = MCT_EVENT_CONTROL_SET_PARM;
  sub_event.u.ctrl_event.current_frame_id =
    event->u.ctrl_event.current_frame_id;

  /* Handle all set params */
  for (index = 0; index < param->num_of_parm_events; index++) {
    sub_event.u.ctrl_event.control_event_data = &param->parm_events[index];
    stats_port_proc_downstream_set_parm(port, &sub_event, &sent);
  }
  return TRUE;
}

/** stats_port_event
 *    @port:  this port from where the event should go
 *    @event: event object to send upstream or downstream
 *
 *  Because stats interface module works no more than a event pass through
 *  module, hence its downstream event handling should be fairly
 *  straight-forward, but upstream event will need a little
 *  bit processing.
 *
 *  Return TRUE for successful event processing.
 **/
static boolean stats_port_event(mct_port_t *port, mct_event_t *event)
{
  boolean              rc = TRUE;
  stats_port_private_t *private;
  stats_port_event_t   port_event;

  /* sanity check */
  if (!port || !event) {
    return FALSE;
  }

  private = (stats_port_private_t *)(port->port_private);
  if (!private) {
    STATS_ERR("Private port is NULL");
    return FALSE;
  }

  /* sanity check: ensure event is meant for port with same identity*/

  if (mct_list_find_custom(MCT_OBJECT_CHILDREN(port), &(event->identity),
    stats_port_check_session_id) == NULL) {
    STATS_HIGH("sanity fail event id=%d", event->identity);
    return FALSE;
  }

  /* Save the event for later use */
  port_event.event = event;

  STATS_LOW("STAT_EVENT: %s Dir %d",
    event->type == MCT_EVENT_CONTROL_CMD ?
    stats_port_get_mct_event_ctrl_string(event->u.ctrl_event.type):
    (event->type == MCT_EVENT_MODULE_EVENT ?
    stats_port_get_mct_event_module_string(event->u.module_event.type):
    "INVALID EVENT"), MCT_EVENT_DIRECTION(event));

  switch (MCT_EVENT_DIRECTION(event)) {
  case MCT_EVENT_UPSTREAM: {
    /* The upstream events could come from Q3A, DIS, AFD or sensor
     * module.
     *
     * Need to check event and see if it has to redirect the event
     * to downstream, for example GYRO event needs to redirect to
     * Q3A and DIS modules */
    mct_event_direction redirect;
    mct_list_t *list;

    /* Currently we are just sending module event, instead of going
       one level down to stats event */

    /* check to see if need to redirect this event to sub-modules */
    STATS_LOW("Received event type=%d", event->u.module_event.type);
    switch (event->u.module_event.type) {
    case MCT_EVENT_MODULE_IMGLIB_AF_CONFIG: {
      mct_imglib_af_config_t *cfg =
       (mct_imglib_af_config_t *)event->u.module_event.module_event_data;
      event->identity = private->sessionStreamId;
      redirect = MCT_EVENT_UPSTREAM;
    }
      break;

    case MCT_EVENT_MODULE_STATS_AEC_CONFIG_UPDATE:
    case MCT_EVENT_MODULE_STATS_AWB_CONFIG_UPDATE: {
      if (is_stats_buffer_debug_data_enable(port)) {
        /* Copy stats buffers to debug data structures. */
        copy_stats_config_to_debug_data(port, event);
      }
      redirect = MCT_EVENT_UPSTREAM;
    }
      break;

    case MCT_EVENT_MODULE_STATS_GYRO_STATS: {
      /* GRYO STATS should be redirected downstream to both Q3A
       * and EIS modules */
      port_event.cap_flag = MCT_PORT_CAP_STATS_GYRO;
      redirect = MCT_EVENT_DOWNSTREAM;
    }
      break;

    case MCT_EVENT_MODULE_STATS_AEC_UPDATE: {
      port_event.cap_flag = MCT_PORT_CAP_STATS_AEC;
      if (private->parm_ctrl.stream_on_counter > 0) {
        /* AEC UPDATE should be redirected downstream to ASD module and
         * also sent to upsteam */
        redirect = MCT_EVENT_BOTH;
      } else {
        // don't send stats to UPSTREAM if all of the streams were off
        STATS_LOW("stop sending AEC_UPDATE");
        redirect = MCT_EVENT_BOTH;
      }

      stats_port_handle_stats_skip(port, event);
    }
      break;

    case MCT_EVENT_MODULE_STATS_AWB_UPDATE: {
      port_event.cap_flag = MCT_PORT_CAP_STATS_AWB;
      if (private->parm_ctrl.stream_on_counter > 0) {
        /* AWB UPDATE should be redirected downstream to ASD module and
         * also sent to upsteam */
        redirect = MCT_EVENT_BOTH;
      } else {
        // don't send stats to UPSTREAM if all of the streams were off
        STATS_LOW("stop sending AWB_UPDATE");
        redirect = MCT_EVENT_BOTH;
      }
    }
      break;
    case MCT_EVENT_MODULE_STATS_AF_UPDATE: {
      port_event.cap_flag = MCT_PORT_CAP_STATS_AF;
      /* AWB UPDATE should be redirected downstream to ASD module and
       * also sent to upsteam */
      redirect = MCT_EVENT_BOTH;
    }
      break;

    case MCT_EVENT_MODULE_STATS_ASD_UPDATE: {
      /* ASD event should be redirected downstream to Q3A module and
       * also sent to upstream */
      port_event.cap_flag = MCT_PORT_CAP_STATS_ASD;
      redirect = MCT_EVENT_BOTH;
    }
      break;

    case MCT_EVENT_MODULE_STATS_AFD_UPDATE: {
      /* AFD event should be redirected downstream to Q3A module and
       * used by AEC */
      port_event.cap_flag = MCT_PORT_CAP_STATS_AFD;
      redirect = MCT_EVENT_DOWNSTREAM;
    }
      break;

    case MCT_EVENT_MODULE_STATS_POST_TO_BUS: {
      mct_bus_msg_t *bus_msg =
        (mct_bus_msg_t *)event->u.module_event.module_event_data;
      if (MCT_BUS_OFFLINE_METADATA == bus_msg->metadata_collection_type) {
        /* For offline stats, just post the metadata immediately */
        rc = stats_port_post_bus_msg(port, event);
      } else {
        if (CAM_HAL_V3 == private->hal_version) {
          /* Save the post_to_bus msg in resp q*/
          int32_t frame_id = (int32_t)event->u.module_event.current_frame_id +
            private->delay.applying_delay + private->delay.meta_reporting_delay;

          if (frame_id  < (int32_t) private->sof_id) {
            rc = stats_port_post_bus_msg(port, event);
          } else {
            rc = stats_port_save_post_bus_msg(port, event);
          }
        } else {
          rc = stats_port_post_bus_msg(port, event);
        }
      }
      redirect = MCT_EVENT_NONE;
    }
      break;

    case MCT_EVENT_MODULE_GET_GYRO_DATA: {
      port_event.cap_flag = 0;
      redirect = MCT_EVENT_DOWNSTREAM;
    }
      break;

    case MCT_EVENT_MODULE_GRAVITY_VECTOR_UPDATE: {
      port_event.cap_flag = 0;
      redirect = MCT_EVENT_DOWNSTREAM;
    }
      break;

    case MCT_EVENT_MODULE_TOF_UPDATE: {
      port_event.cap_flag = 0;
      redirect = MCT_EVENT_DOWNSTREAM;
    }
      break;

    default: {
      redirect = MCT_EVENT_UPSTREAM;
    }
      break;
    }

    /* always forward the event to upstream(sink port) first */
    if (redirect == MCT_EVENT_UPSTREAM || redirect == MCT_EVENT_BOTH) {
      rc = mct_port_send_event_to_peer(port, event);
    }

    if (redirect == MCT_EVENT_DOWNSTREAM || redirect == MCT_EVENT_BOTH) {
      /* redirect the event to sub-modules' ports */
      event->direction = MCT_EVENT_DOWNSTREAM;

      rc = mct_list_traverse((mct_list_t *)private->sub_ports,
        stats_port_redirect_event_downstream, &port_event);
    }
  } /* case MCT_EVENT_TYPE_UPSTREAM */
    break;

  case MCT_EVENT_DOWNSTREAM: {
    /* In case of sink port, no need to peek into the event,
     * instead just simply forward the event and let downstream
     * modules process them and take action accordingly */
    boolean sent = 0;

    if (event->type == MCT_EVENT_CONTROL_CMD &&
      event->u.ctrl_event.type == MCT_EVENT_CONTROL_SET_PARM) {
      rc = stats_port_proc_downstream_set_parm(port, event, &sent);
    } else if (event->type == MCT_EVENT_CONTROL_CMD &&
      (event->u.ctrl_event.type == MCT_EVENT_CONTROL_DO_AF ||
      event->u.ctrl_event.type == MCT_EVENT_CONTROL_CANCEL_AF)) {
      rc = stats_port_transform_af_cmd_to_set_parm(port, event, &sent);
    } else if (event->type == MCT_EVENT_CONTROL_CMD &&
      event->u.ctrl_event.type == MCT_EVENT_CONTROL_PREPARE_SNAPSHOT) {
      rc = stats_port_transform_prepsnap_cmd_to_set_parm(port, event, &sent);
    } else if (event->type == MCT_EVENT_MODULE_EVENT &&
      ((event->u.module_event.type == MCT_EVENT_MODULE_SET_CHROMATIX_PTR) ||
      (event->u.module_event.type == MCT_EVENT_MODULE_SET_RELOAD_CHROMATIX) ||
      (event->u.module_event.type == MCT_EVENT_MODULE_SET_AF_TUNE_PTR) ||
      (event->u.module_event.type == MCT_EVENT_MODULE_SET_RELOAD_AFTUNE))) {

      if (TRUE == private->frame_capture_mode_in_progress) {
        STATS_HIGH("frame_capture_mode_in_progress, skip chromatix update");
        break;
      }

      STATS_HIGH("Got Chromatix or new tune values, type: %d",
        event->u.module_event.type);

      int                       idx;
      stats_port_setparm_ctrl_t *parm_ctrl =
        (stats_port_setparm_ctrl_t *)&private->parm_ctrl;

      /* Set exposure index before loading the chromatix */
      if (parm_ctrl->is_initialised[CAM_INTF_PARM_INITIAL_EXPOSURE_INDEX] == TRUE &&
        !stats_port_is_transiently_event(CAM_INTF_PARM_INITIAL_EXPOSURE_INDEX)) {
        rc = mct_list_traverse((mct_list_t *)private->sub_ports,
          stats_port_send_event_downstream, &parm_ctrl->evt[CAM_INTF_PARM_INITIAL_EXPOSURE_INDEX]);
      }
      /* Set instant capture before loading the chromatix */
     if (parm_ctrl->is_initialised[CAM_INTF_PARM_INSTANT_AEC] == TRUE &&
        !stats_port_is_transiently_event(CAM_INTF_PARM_INSTANT_AEC)) {
        rc = mct_list_traverse((mct_list_t *)private->sub_ports,
          stats_port_send_event_downstream, &parm_ctrl->evt[CAM_INTF_PARM_INSTANT_AEC]);
      }

      if (CAM_STREAM_TYPE_RAW != private->stream_type) {
        rc = mct_list_traverse((mct_list_t *)private->sub_ports,
          stats_port_send_event_downstream, &port_event);
      }

      MCT_OBJECT_LOCK(port);
      for (idx = 0; idx < CAM_INTF_PARM_MAX; idx++) {
        if (parm_ctrl->is_initialised[idx] == TRUE &&
          !stats_port_is_transiently_event(idx)) {
          rc = mct_list_traverse((mct_list_t *)private->sub_ports,
            stats_port_send_event_downstream, &parm_ctrl->evt[idx]);
        }
      } /* end of for */
      parm_ctrl->has_chromatix_set = TRUE;
      MCT_OBJECT_UNLOCK(port);

      sent = TRUE;
    } else if (event->type == MCT_EVENT_MODULE_EVENT &&
      event->u.module_event.type == MCT_EVENT_MODULE_SENSOR_META_CONFIG) {
      rc = stats_port_handle_enable_meta_channel_event(port, event);
      sent = TRUE;
    } else if (event->type == MCT_EVENT_MODULE_EVENT &&
      event->u.module_event.type == MCT_EVENT_MODULE_ISP_STATS_INFO) {
      if (is_stats_buffer_debug_data_enable(port)) {
        /* set stats config depth info to debug data. */
        BayerGridConfigType *bg_config = &(private->bg_config_debug_data);
        BayerGridConfigType *aec_bg_config = &(private->bg_aec_config_debug_data);
        mct_stats_info_t *stats_info =
          (mct_stats_info_t *)event->u.module_event.module_event_data;
        bg_config->bitDepth = (uint8)stats_info->stats_depth;
        /* Same bit-depth, save value now, in case aec_bg is enable */
        aec_bg_config->bitDepth = bg_config->bitDepth;
      }
    } else if (event->type == MCT_EVENT_CONTROL_CMD &&
      event->u.ctrl_event.type == MCT_EVENT_CONTROL_STREAMOFF) {
      int i = 0;
      stats_port_setparm_ctrl_t *parm_ctrl =
        (stats_port_setparm_ctrl_t *)&private->parm_ctrl;
      STATS_LOW("STREAMOFF event received stream_on_counter=%d!",
        private->parm_ctrl.stream_on_counter);
      MCT_OBJECT_LOCK(port);
      private->parm_ctrl.stream_on_counter--;
      if (0 == private->parm_ctrl.stream_on_counter) {
        parm_ctrl->has_chromatix_set = FALSE;

        /*
        * Clear cached params on stream_off. Otherwise, the old params will
        * be reused which will have issues as certain param(like manual exp/gain)
        * are only applied to current session.
        */
        int32_t idx = 0;
        for (idx = 0; idx < CAM_INTF_PARM_MAX; idx++) {
          parm_ctrl->is_initialised[idx] = FALSE;
        }
      }
      MCT_OBJECT_UNLOCK(port);

      if (0 == private->parm_ctrl.stream_on_counter) {
        stats_port_set_stream_on_off(port, FALSE);

        for (i = 0; i < STATS_MAX_FRAME_DELAY; i++) {
          pthread_mutex_lock(&parm_ctrl->msg_q_lock[i]);
          mct_queue_flush(parm_ctrl->msg_q[i], stats_port_free_set_parm_ctrl);
          pthread_mutex_unlock(&parm_ctrl->msg_q_lock[i]);
        }
      }
      /* Reset the preview-stream-on flag from here */
      private->preview_stream_on = 0;
    } else if (event->type == MCT_EVENT_CONTROL_CMD &&
        event->u.ctrl_event.type == MCT_EVENT_CONTROL_STREAMON) {
      stats_port_setparm_ctrl_t *parm_ctrl =
        (stats_port_setparm_ctrl_t *)&private->parm_ctrl;
      STATS_LOW("STREAMON event received!");

      MCT_OBJECT_LOCK(port);
      private->parm_ctrl.stream_on_counter++;
      MCT_OBJECT_UNLOCK(port);
      if (1 == private->parm_ctrl.stream_on_counter) {
        stats_port_set_stream_on_off(port, TRUE);
      }
    } else if (event->type == MCT_EVENT_MODULE_EVENT &&
        event->u.module_event.type == MCT_EVENT_MODULE_SET_STREAM_CONFIG) {

       // save the sensor fps, used in skip stats feature
       sensor_out_info_t *sensor_info =
         (sensor_out_info_t *)(event->u.module_event.module_event_data);
       private->max_sensor_fps = sensor_info->max_fps;
    } else if (event->type == MCT_EVENT_MODULE_EVENT &&
        event->u.module_event.type == MCT_EVENT_MODULE_STATS_DATA){

      if (is_stats_buffer_debug_data_enable(port)) {

        /* Copy stats buffers to debug data structures. */
        copy_stats_buffer_to_debug_data(event, port);
      }

      stats_port_handle_stats_data(port, event);
      sent = TRUE;
    } else if(event->type == MCT_EVENT_CONTROL_CMD &&
      event->u.ctrl_event.type == MCT_EVENT_CONTROL_SET_SUPER_PARM) {
      MCT_PROF_LOG_BEG(PROF_3A_SP);
      stats_port_handle_sof_set_parm(port, event);
      MCT_PROF_LOG_END();
      sent = FALSE;
    } else if (event->type == MCT_EVENT_CONTROL_CMD &&
      event->u.ctrl_event.type == MCT_EVENT_CONTROL_SOF) {
      if (is_stats_buffer_debug_data_enable(port)) {

        /* send stats buffers to exif debug data */
        send_stats_buffer_to_debug_data(port);
      }
    }
    if (!sent) {
      /* We don't want to handle these events for the SNAPSHOT stream */
      if (event->type == MCT_EVENT_MODULE_EVENT &&
        ((event->u.module_event.type == MCT_EVENT_MODULE_STREAM_CROP) ||
        (event->u.module_event.type == MCT_EVENT_MODULE_ISP_OUTPUT_DIM))) {
        int     i;
        boolean case_break = FALSE;

        for (i = 0; i < MAX_NUM_STREAMS; i++) {
          if (private->streams_info[i].used_flag == TRUE &&
            private->streams_info[i].identity == event->identity &&
            (private->streams_info[i].stream_type == CAM_STREAM_TYPE_SNAPSHOT ||
            private->streams_info[i].stream_type == CAM_STREAM_TYPE_RAW ||
            private->streams_info[i].stream_type == CAM_STREAM_TYPE_POSTVIEW)) {
            case_break = TRUE;
            /* prevent reporting an error to the caller */
            rc = TRUE;
            break;
          }
        }
        if (case_break) {
          STATS_LOW("SNAPSHOT stream event: %d - DISCARD",
            event->u.module_event.type);
          break;
        }
      }

      if (CAM_HAL_V3 == private->hal_version) {
        /* Per frame control */
        if (event->type == MCT_EVENT_CONTROL_CMD &&
          event->u.ctrl_event.type == MCT_EVENT_CONTROL_SOF) {
          int msg_q_idx;

          MCT_PROF_LOG_BEG(PROF_3A_SOF);
          private->sof_id = event->u.ctrl_event.current_frame_id;
          msg_q_idx =
            event->u.ctrl_event.current_frame_id % STATS_MAX_FRAME_DELAY;

          stats_port_setparm_ctrl_t *parm_ctrl =
          (stats_port_setparm_ctrl_t *)&private->parm_ctrl;

          pthread_mutex_lock(&parm_ctrl->msg_q_lock[msg_q_idx]);
          while (parm_ctrl->msg_q[msg_q_idx]->length != 0) {
            stats_port_event_t *port_event = (stats_port_event_t *)

            mct_queue_pop_head(parm_ctrl->msg_q[msg_q_idx]);
            if (port_event && MCT_EVENT_DIRECTION(port_event->event) ==
              MCT_EVENT_DOWNSTREAM) {
              rc = mct_list_traverse((mct_list_t *)private->sub_ports,
                stats_port_send_event_downstream, port_event);
            } else if (port_event && MCT_EVENT_DIRECTION(port_event->event) ==
              MCT_EVENT_UPSTREAM) {
              if (port_event->event->u.module_event.type ==
                MCT_EVENT_MODULE_STATS_POST_TO_BUS) {
                stats_port_post_bus_msg(port, port_event->event);
              } else {
                mct_port_send_event_to_peer(port, port_event->event);
              }
            }
            stats_port_free_set_parm_ctrl(port_event, NULL);
          } /* end of while */
          pthread_mutex_unlock(&parm_ctrl->msg_q_lock[msg_q_idx]);
          MCT_PROF_LOG_END();
        } /* Per frame control X */
      }

      rc = mct_list_traverse((mct_list_t *)private->sub_ports,
        stats_port_send_event_downstream, &port_event);
    }
  } /* case MCT_EVENT_TYPE_DOWNSTREAM */
    break;

  default: {
    STATS_ERR("Unknown event");
    rc = FALSE;
  }
    break;
  }

  return rc;
}

static boolean stats_port_start_stop_stats_thread(
mct_port_t *port, uint8_t start_flag)
{
  boolean                rc = TRUE;
  stats_port_private_t   *private = (stats_port_private_t *)port->port_private;
  stats_port_event_t     port_event;
  mct_event_t            event;

  /* This event handling in each submodule should be a blocking call */
  STATS_LOW("start_flag: %d", start_flag);
  port_event.event = &event;
  port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
    MCT_PORT_CAP_STATS_AEC |
    MCT_PORT_CAP_STATS_AWB |
    MCT_PORT_CAP_STATS_IS  |
    MCT_PORT_CAP_STATS_AFD |
    MCT_PORT_CAP_STATS_ASD);
  event.type = MCT_EVENT_MODULE_EVENT;
  event.identity = private->reserved_id;
  event.direction = MCT_EVENT_DOWNSTREAM;
  event.u.module_event.type = MCT_EVENT_MODULE_START_STOP_STATS_THREADS;
  event.u.module_event.module_event_data = (void *)(&start_flag);
  rc = mct_list_traverse((mct_list_t *)private->sub_ports,
     stats_port_send_event_downstream, &port_event);

 return rc;
}

/** stats_port_set_caps
 *    @port: port object which the caps to be set
 *    @caps: this port's capability
 *
 *  Function overwrites a ports capability.
 *
 *  Return TRUE if it is valid source port.
 **/
static boolean stats_port_set_caps(mct_port_t *port, mct_port_caps_t *caps)
{
  if (strcmp(MCT_PORT_NAME(port), "stats_sink")) {
    return FALSE;
  }

  port->caps = *caps;

  return TRUE;
}

/** stats_port_sub_caps_reserve
 *    @data:      mct_port_t object
 *    @user_data: stats_port_caps_reserve_t object
 *
 *  To reserve port's capability
 *
 *  Return TRUE on success, FALSE on failure.
 **/
static boolean stats_port_sub_caps_reserve(void *data, void *user_data)
{
  boolean                   rc = FALSE;
  mct_port_t                *port = (mct_port_t *)data;
  stats_port_caps_reserve_t *reserve = (stats_port_caps_reserve_t *)user_data;

  if (port->check_caps_reserve) {
    rc = port->check_caps_reserve(port, &reserve->caps, reserve->stream_info);
  }

  return rc;
}

/** stats_port_sub_unreserve
 *    @data:      mct_port_t object
 *    @user_data: identity
 *
 *  To unreserve port's capability
 *
 *  Return TRUE.
 **/
static boolean stats_port_sub_unreserve(void *data, void *user_data)
{
  mct_port_t   *port = (mct_port_t *)data;
  unsigned int id = *((unsigned int *)user_data);

  if (port->check_caps_unreserve) {
    port->check_caps_unreserve(port, id);
  }

  return TRUE;
}

/** stats_port_add_reserved_stream
 *    @private:     Private info of the stats port.
 *    @stream_info: Stream info of the reserving stream.
 *
 * This function adds the stream to the stream list whenever a new stream
 * is reserved.
 *
 * Return FALSE if adding the stream fails.
 **/

static boolean stats_port_add_reserved_stream(stats_port_private_t *private,
  mct_stream_info_t *stream_info)
{
  boolean rc = FALSE;
  int     i = 0;

  STATS_LOW("Adding stream to reserved list, identity=%d",
    stream_info->identity);

  for (i = 0; i < MAX_NUM_STREAMS; i++) {
    if ((private->streams_info[i].used_flag == TRUE) &&
      (private->streams_info[i].identity == stream_info->identity)) {
      STATS_HIGH("Stream with identity=%d already reserved",
        stream_info->identity);
      rc = TRUE;
      return rc;
    }
  }

  for (i = 0; i < MAX_NUM_STREAMS; i++) {
    if (private->streams_info[i].used_flag == FALSE) {
      private->streams_info[i].identity = stream_info->identity;
      private->streams_info[i].stream_type = stream_info->stream_type;
      private->streams_info[i].streaming_mode = stream_info->streaming_mode;
      private->streams_info[i].used_flag = TRUE;

      if (stream_info->stream_type == CAM_STREAM_TYPE_VIDEO) {
        private->video_stream_cnt++;
        private->stream_type = CAM_STREAM_TYPE_VIDEO;
        private->reserved_id = stream_info->identity;
      } else if (stream_info->stream_type == CAM_STREAM_TYPE_PREVIEW ||
        stream_info->stream_type == CAM_STREAM_TYPE_CALLBACK) {
        private->preview_stream_cnt++;
        if (!private->video_stream_cnt) {
          private->stream_type = CAM_STREAM_TYPE_PREVIEW;
          private->reserved_id = stream_info->identity;
        }
      } else if (stream_info->stream_type == CAM_STREAM_TYPE_SNAPSHOT ||
        stream_info->stream_type == CAM_STREAM_TYPE_RAW ||
        stream_info->stream_type == CAM_STREAM_TYPE_POSTVIEW) {
        private->snapshot_stream_cnt++;
        if (!private->video_stream_cnt && (!private->preview_stream_cnt ||
          (private->preview_stream_cnt && stream_info->streaming_mode))) {
          private->stream_type = CAM_STREAM_TYPE_SNAPSHOT;
          private->reserved_id = stream_info->identity;
        }
      } else if (stream_info->stream_type == CAM_STREAM_TYPE_PARM){
          private->param_stream_cnt++;
          private->stream_type = CAM_STREAM_TYPE_PARM;
          private->reserved_id = stream_info->identity;
      } else {
        STATS_ERR("Invalid stream type=%d",
          stream_info->stream_type);
        rc = FALSE;
        break;
      }
      rc = TRUE;
      break;
    }
  }

  if (!rc) {
    STATS_ERR("Adding stream with identity=%d, to reserved list failed",
      stream_info->identity);
  }

  return rc;
}

/** stats_port_delete_reserved_stream
 *    @private: Private info of the stats port.
 *    @identity: session+stream identity.
 *
 * This function deletes the stream from the stream list whenever the stream
 * is unreserved.
 *
 * Return FALSE if deleting the stream fails.
 **/
static boolean stats_port_delete_reserved_stream(stats_port_private_t *private,
  unsigned int identity, mct_port_t *port)
{
  boolean rc = FALSE;
  int     i = 0;
  int     j = 0;
  uint32_t num_streams;

  STATS_LOW("Deleting stream from reserved list, identity=%d",
    identity);

  for (i = 0; i < MAX_NUM_STREAMS; i++) {
    if (identity == private->streams_info[i].identity) {
      mct_list_traverse(private->sub_ports, stats_port_sub_unreserve,
        &identity);
      private->streams_info[i].identity  = 0;
      private->streams_info[i].used_flag = FALSE;

      if (private->streams_info[i].stream_type == CAM_STREAM_TYPE_VIDEO) {
        private->video_stream_cnt--;
      } else if (private->streams_info[i].stream_type ==
        CAM_STREAM_TYPE_SNAPSHOT) {
        private->snapshot_stream_cnt--;
      } else if (private->streams_info[i].stream_type ==
        CAM_STREAM_TYPE_PREVIEW||
        private->streams_info[i].stream_type ==
        CAM_STREAM_TYPE_CALLBACK) {
        private->preview_stream_cnt--;
      }

      private->streams_info[i].stream_type = CAM_STREAM_TYPE_MAX;
      private->streams_info[i].streaming_mode = 0;

      if (private->video_stream_cnt) {
        for (j = MAX_NUM_STREAMS - 1; j >= 0; j--) {
          if (private->streams_info[j].used_flag &&
            private->streams_info[j].stream_type == CAM_STREAM_TYPE_VIDEO) {

            private->stream_type = CAM_STREAM_TYPE_VIDEO;
            private->reserved_id = private->streams_info[j].identity;

            rc = TRUE;
            return rc;
          }
        }
      }

      if (private->snapshot_stream_cnt) {
        for (j = MAX_NUM_STREAMS - 1; j >= 0; j--) {
          if (private->streams_info[j].used_flag &&
            (private->streams_info[j].stream_type == CAM_STREAM_TYPE_SNAPSHOT ||
            private->streams_info[i].stream_type == CAM_STREAM_TYPE_RAW ||
            private->streams_info[i].stream_type == CAM_STREAM_TYPE_POSTVIEW)) {

            private->stream_type = CAM_STREAM_TYPE_SNAPSHOT;
            private->reserved_id = private->streams_info[j].identity;

            rc = TRUE;
            return rc;
          }
        }
      }

      if (private->preview_stream_cnt) {
        for (j = MAX_NUM_STREAMS - 1; j >= 0; j--) {
          if (private->streams_info[j].used_flag &&
            (private->streams_info[j].stream_type == CAM_STREAM_TYPE_PREVIEW||
            private->streams_info[j].stream_type == CAM_STREAM_TYPE_CALLBACK)) {

            private->stream_type = CAM_STREAM_TYPE_PREVIEW;
            private->reserved_id = private->streams_info[j].identity;

            rc = TRUE;
            return rc;
          }
        }
      }
      if (private->param_stream_cnt) {
        for (j = MAX_NUM_STREAMS - 1; j >= 0; j--) {
          if (private->streams_info[j].used_flag &&
            (private->streams_info[j].stream_type == CAM_STREAM_TYPE_PARM)) {

            private->stream_type = CAM_STREAM_TYPE_PARM;
            private->reserved_id = private->streams_info[j].identity;
            STATS_LOW("CAM_STREAM_TYPE_PARM\n");
            rc = TRUE;
            return rc;
          }
        }
      }

      private->reserved_id = (private->reserved_id & 0xFFFF0000);

      rc = TRUE;
      break;
    }
  }

  if (!rc) {
    STATS_ERR("Stream with identity=%d, not found in the reserved list",
      identity);
  } else {
    num_streams = private->preview_stream_cnt + private->snapshot_stream_cnt +
        private->video_stream_cnt;
    if (!num_streams) {
      MCT_DBG_LOW(MODULE_STATS, "going to NULL peer");
      MCT_PORT_PEER(port) = NULL;
    }
  }

  return rc;
}


/** stats_port_check_caps_reserve
 *    @port: this interface module's port;
 *    @peer_caps: the capability of peer port which wants to match
 *                interface port;
 *    @info:
 *
 *  Stats modules are pure s/w software modules, and every port can
 *  support one identity. If the identity is different, support
 *  can be provided via create a new port. Regardless source or
 *  sink port, once capabilities are matched,
 *  - If this port has not been used, it can be supported;
 *  - If the requested stream is in existing identity, return
 *    failure
 *  - If the requested stream belongs to a different session, the port
 *  can not be used.
 *
 *  Return TRUE on success, FALSE on failure.
 **/
static boolean stats_port_check_caps_reserve(mct_port_t *port, void *caps,
  void *info)
{
  boolean              rc = FALSE;
  mct_port_caps_t      *port_caps;
  stats_port_private_t *private;
  cam_stream_type_t    old_stream_type;
  mct_event_t          cmd_event;
  mct_event_module_t   event_data;
  stats_port_event_t   stats_event;
  mct_stream_info_t    *stream_info = info;

  memset(&(event_data), 0, sizeof(mct_event_module_t));
  MCT_OBJECT_LOCK(port);
  if (!port || !caps || !stream_info ||
    strcmp(MCT_OBJECT_NAME(port), "stats_sink")) {

    STATS_ERR("Invalid parameters!\n");
    rc = FALSE;
    goto reserve_done;
  }

  port_caps = (mct_port_caps_t *)caps;

  if (port_caps->port_caps_type != MCT_PORT_CAPS_STATS) {
    STATS_ERR("Invalid Port capability type!");
    rc = FALSE;
    goto reserve_done;
  }

  private = (stats_port_private_t *)port->port_private;
  old_stream_type = private->stream_type;

  if (!stats_port_check_session_id(&(private->reserved_id),
    &(stream_info->identity))) {
    STATS_ERR("session id not match.");
    rc = FALSE;
    goto reserve_done;
  }

  if(stream_info->stream_type == CAM_STREAM_TYPE_PARM){
    private->sessionStreamId = stream_info->identity;
    STATS_LOW("sessionID=%d",private->sessionStreamId);
    rc = TRUE;
  }

  STATS_LOW("state %d\n", private->state);
  /* Hack: Keep preview stream info for AFS */
  if (stream_info->stream_type == CAM_STREAM_TYPE_PREVIEW)
    private->preview_stream_info = *stream_info;
  switch (private->state) {
  case STATS_PORT_STATE_LINKED: {
    if (mct_list_find_custom(MCT_OBJECT_CHILDREN(port),
      &(stream_info->identity), stats_port_check_session_id) != NULL) {
      stats_port_caps_reserve_t reserve;

      reserve.caps = *port_caps;
      reserve.stream_info = stream_info;
      STATS_LOW("Calling stats_port_sub_caps_reserve");
      if (mct_list_traverse(private->sub_ports, stats_port_sub_caps_reserve,
        &reserve) == TRUE) {
        rc = stats_port_add_reserved_stream(private, stream_info);
      }
    }
  }
    break;

  case STATS_PORT_STATE_CREATED:
  case STATS_PORT_STATE_UNRESERVED: {
    stats_port_caps_reserve_t reserve;

    reserve.caps = *port_caps;
    reserve.stream_info = stream_info;

    private->preview_stream_width = 0;
    private->preview_stream_height = 0;

    if (mct_list_traverse(private->sub_ports, stats_port_sub_caps_reserve,
      &reserve) == TRUE) {
      private->reserved_id = stream_info->identity;
      private->state       = STATS_PORT_STATE_RESERVED;
      private->stream_type = stream_info->stream_type;
      rc = stats_port_add_reserved_stream(private, stream_info);
    }
  }
    break;

  case STATS_PORT_STATE_RESERVED: {
    if ((private->reserved_id & 0xFFFF0000) ==
      (stream_info->identity & 0xFFFF0000)) {
      rc = TRUE;
    }
  }
    break;

  default: {
    rc = FALSE;
  }
    break;
  }

  MCT_OBJECT_UNLOCK(port);
  /* Set flag to indicate preview stream is running */
  if (CAM_STREAM_TYPE_PREVIEW == stream_info->stream_type) {
    private->preview_stream_on = 1;
  }

  if ((CAM_STREAM_TYPE_PREVIEW == stream_info->stream_type) ||
  ((CAM_STREAM_TYPE_CALLBACK == stream_info->stream_type) &&
  !private->preview_stream_on &&
   stream_info->dim.width > private->preview_stream_width)) {
    /* in multiple preview/callback stream, take the maximum dimension
     * we shall remove such hacky assumption once our algorithm removes
     * the dependency on the preview stream. For now, given the algorithm
     * need the information of preview stream, and there is no way to identify
     * which one is for the preview if there are multiple preview/callback streams
     * the only way here is to use the stream with maximum dimension.
     */
    unsigned int identity;

    private->preview_stream_width = stream_info->dim.width;
    private->preview_stream_height = stream_info->dim.height;
    identity = (stream_info->identity & 0x0000FFFF);
    cmd_event.type = MCT_EVENT_MODULE_EVENT;
    cmd_event.identity = private->reserved_id;
    cmd_event.direction = MCT_EVENT_DOWNSTREAM;
    cmd_event.u.module_event = event_data;
    cmd_event.u.module_event.type = MCT_EVENT_MODULE_PREVIEW_STREAM_ID;
    cmd_event.u.module_event.module_event_data = (void *)stream_info;
    stats_event.event = &cmd_event;
    rc = mct_list_traverse((mct_list_t *)private->sub_ports,
      stats_port_send_event_downstream, &stats_event);
  }

  if (old_stream_type != private->stream_type) {
    STATS_LOW("Changing stream from %d to %d", old_stream_type,
      private->stream_type);

     /* Store event data into struct */
     stats_mode_change_event_data module_event_data;
     module_event_data.reserved_id = private->reserved_id;
     module_event_data.stream_type = private->stream_type;

    cmd_event.type = MCT_EVENT_MODULE_EVENT;
    cmd_event.identity = private->reserved_id;
    cmd_event.direction = MCT_EVENT_DOWNSTREAM;
    cmd_event.u.module_event = event_data;
    cmd_event.u.module_event.type = MCT_EVENT_MODULE_MODE_CHANGE;
    cmd_event.u.module_event.module_event_data = (void *) &module_event_data;

    stats_event.cap_flag = 0;
    stats_event.event = &cmd_event;

    /* Send event to the stats sub-ports to change stream */
    rc = mct_list_traverse((mct_list_t *)private->sub_ports,
      stats_port_send_event_downstream, &stats_event);
  }
  if(stream_info->stream_type == CAM_STREAM_TYPE_SNAPSHOT ||
    stream_info->stream_type == CAM_STREAM_TYPE_RAW ||
    stream_info->stream_type == CAM_STREAM_TYPE_POSTVIEW) {
    private->snap_stream_id = (stream_info->identity & 0x0000FFFF);
  }

  return rc;

reserve_done:
  MCT_OBJECT_UNLOCK(port);
  return rc;
}

/** module_stats_check_caps_unreserve
 *    @port: this port object to remove the session/stream;
 *    @identity: session+stream identity.
 *
 *  This function frees the identity from port's children list.
 *
 *  Return FALSE if the identity is not existing.
 **/
boolean stats_port_check_caps_unreserve(mct_port_t *port,
  unsigned int identity)
{
  stats_port_private_t *private;
  boolean              rc = FALSE;
  cam_stream_type_t    old_stream_type;
  mct_event_t          cmd_event;
  mct_event_module_t   event_data;
  stats_port_event_t   stats_event;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "stats_sink")) {
    return FALSE;
  }

  private = (stats_port_private_t *)port->port_private;
  if (!private) {
    return FALSE;
  }

  old_stream_type = private->stream_type;

  if (private->state == STATS_PORT_STATE_UNRESERVED) {
    return TRUE;
  }

  if ((private->state == STATS_PORT_STATE_UNLINKED ||
    private->state == STATS_PORT_STATE_RESERVED) &&
    ((private->reserved_id & 0xFFFF0000) == (identity & 0xFFFF0000))) {

    MCT_OBJECT_LOCK(port);
    mct_list_traverse(private->sub_ports, stats_port_sub_unreserve, &identity);
    rc = stats_port_delete_reserved_stream(private, identity, port);
    private->state       = STATS_PORT_STATE_UNRESERVED;
    MCT_OBJECT_UNLOCK(port);
  } else if (MCT_OBJECT_REFCOUNT(port)) {
    /* This case is raised in case of multiple streams when the state is
     * already unreserved. Delete stream and unreserve them with the sub-ports
     * till all streams are done with. */
    if ((private->reserved_id & 0xFFFF0000) == (identity & 0xFFFF0000)) {
      MCT_OBJECT_LOCK(port);
      rc = stats_port_delete_reserved_stream(private, identity, port);
      MCT_OBJECT_UNLOCK(port);
    }
  }

  if (old_stream_type != private->stream_type) {
    STATS_LOW("Changing stream from %d to %d", old_stream_type,
      private->stream_type);

     /* Store event data into struct */
     stats_mode_change_event_data module_event_data;
     module_event_data.reserved_id  = private->reserved_id;
     module_event_data.stream_type   = private->stream_type;

    cmd_event.type = MCT_EVENT_MODULE_EVENT;
    cmd_event.identity = private->reserved_id;
    cmd_event.direction = MCT_EVENT_DOWNSTREAM;
    cmd_event.u.module_event = event_data;
    cmd_event.u.module_event.type = MCT_EVENT_MODULE_MODE_CHANGE;
    cmd_event.u.module_event.module_event_data = (void *)& module_event_data;

    stats_event.cap_flag = 0;
    stats_event.event = &cmd_event;

    /* Send event to the stats sub-ports to change stream */
    rc = mct_list_traverse((mct_list_t *)private->sub_ports,
      stats_port_send_event_downstream, &stats_event);
  }

  return rc;
}

/** stats_port_sub_ports_ext_link
 *    @data:      mct_port_t object
 *    @user_data: stats_port_sub_link_t object
 *
 *  Call the extlink function of a stats subport.
 *
 *  Return TRUE on success, FALSE on failure.
 **/
static boolean stats_port_sub_ports_ext_link(void *data, void *user_data)
{
  boolean               rc = TRUE;
  mct_port_t            *port = (mct_port_t *)data;
  stats_port_sub_link_t *ext  = (stats_port_sub_link_t *)user_data;

  if (MCT_PORT_EXTLINKFUNC(port)) {
    rc = MCT_PORT_EXTLINKFUNC(port)(ext->id, port, ext->peer);
  } else {
    rc = FALSE;
  }

  return rc;
}

/** stats_port_ext_link
 *    @identity:  Identity of session/stream
 *    @port: SRC/SINK of stats ports
 *    @peer: For stats sink- peer is most likely isp port
 *           For src module - peer is submodules sink.
 *
 *  Set stats port's external peer port.
 *
 *  Return TRUE on success, FALSE on failure.
 **/
static boolean stats_port_ext_link(unsigned int identity,
  mct_port_t *port, mct_port_t *peer)
{
  boolean               rc = FALSE;
  stats_port_private_t  *private;
  stats_port_sub_link_t ext_link;

  if (strcmp(MCT_OBJECT_NAME(port), "stats_sink")) {
    STATS_ERR("Stats port name does not match!");
    return FALSE;
  }

  private = (stats_port_private_t *)port->port_private;
  if (!private) {
    STATS_ERR("Private port NULL!");
    return FALSE;
  }

  STATS_LOW("state %d\n", private->state);
  MCT_OBJECT_LOCK(port);
  switch (private->state) {
  case STATS_PORT_STATE_RESERVED: {
    if ((private->reserved_id & 0xFFFF0000) == (identity & 0xFFFF0000)) {
      STATS_LOW("Identity matches reserved_id!");
      rc = TRUE;
    }
  }
    break;

  case STATS_PORT_STATE_CREATED:
  case STATS_PORT_STATE_UNLINKED: {
    rc = TRUE;
  }
    break;

  case STATS_PORT_STATE_LINKED: {
    if (mct_list_find_custom(MCT_OBJECT_CHILDREN(port), &identity,
      stats_port_check_session_id) != NULL) {
      rc = TRUE;
    }
  }
    break;

  default: {
    rc = FALSE;
  }
    break;
  }

  if (rc == TRUE) {
    ext_link.id   = identity;
    ext_link.peer = port;
    STATS_LOW("Invoke sub-ports ext link");
    /* Invoke sub ports' ext link  */
    rc = mct_list_traverse(private->sub_ports, stats_port_sub_ports_ext_link,
      &ext_link);
    if (rc == TRUE) {
      private->state = STATS_PORT_STATE_LINKED;
      MCT_PORT_PEER(port) = peer;
      MCT_OBJECT_REFCOUNT(port) += 1;
      if (1 == MCT_OBJECT_REFCOUNT(port)) {
        /* Send event to start all 3A threads from here after linking first stream */
        stats_port_start_stop_stats_thread(port, TRUE);
      }
    }
  }
  MCT_OBJECT_UNLOCK(port);

  STATS_LOW("X rc=%d", rc);
  return rc;
}

/** stats_port_sub_unlink
 *    @data: mct_port_t object
 *    @user_data: stats_port_sub_link_t object
 *
 *  Unlink stats subport.
 *
 *  Return TRUE.
 **/
static boolean stats_port_sub_unlink(void *data, void *user_data)
{
  mct_port_t            *port = (mct_port_t *)data;
  stats_port_sub_link_t *sub_link = (stats_port_sub_link_t *)user_data;

  if (port->un_link) {
    port->un_link(sub_link->id, port, sub_link->peer);
  }

  return TRUE;
}

/** stats_port_unlink
 *
 *    @identity: Identity of session/stream
 *    @port:     stats sink port (called from mct)
 *               stats src port called from stats sink port.
 *    @peer:     peer of stats sink port
 *
 * This funtion unlink the peer ports of stats sink, src ports
 * and its peer submodule's port
 *
 * Return void.
 **/
static void stats_port_unlink(unsigned int identity, mct_port_t *port,
  mct_port_t *peer)
{
  stats_port_private_t  *private;
  stats_port_sub_link_t sub_link;

  if (!port || !peer) {
    return;
  }

  private = (stats_port_private_t *)port->port_private;
  if (!private) {
    return;
  }

  MCT_OBJECT_LOCK(port);
  if (private->state == STATS_PORT_STATE_LINKED &&
    mct_list_find_custom(MCT_OBJECT_CHILDREN(port), &identity,
      stats_port_check_id) != NULL) {

    sub_link.id   = identity;
    sub_link.peer = port;
    if (1 == MCT_OBJECT_REFCOUNT(port)) {
      /* Send event to stop all stats threads from here before unlinking last stream */
      stats_port_start_stop_stats_thread(port, FALSE);
    }
    STATS_LOW("Invoke sub-ports ext un link");
    mct_list_traverse(private->sub_ports, stats_port_sub_unlink, &sub_link);

    MCT_OBJECT_REFCOUNT(port) -= 1;
    if (!MCT_OBJECT_REFCOUNT(port)) {
      private->state = STATS_PORT_STATE_UNLINKED;
    }
  }
  MCT_OBJECT_UNLOCK(port);

  return;
}

/** stats_port_check_port
 *    @port:     the port to be checked
 *    @identity: the identity to be checked against the port
 *
 *  Check if the port matches the given identity.
 *
 *  Return TRUE if the identity matches, otherwise return FALSE.
 **/
boolean stats_port_check_port(mct_port_t *port, unsigned int identity)
{
  stats_port_private_t *private;

  STATS_LOW("E");

  if (!port || ((private = port->port_private) == NULL) ||
    strcmp(MCT_OBJECT_NAME(port), "stats_sink")) {
    STATS_LOW("ERROR PORT NULL ");
    return FALSE;
  }
  return ((private->reserved_id & 0xFFFF0000) ==
    (identity & 0xFFFF0000) ? TRUE : FALSE);
}

/** stats_port_deinit
 *    @port: the port to be destroyed
 *
 *  Deinit & destroy port instance
 *
 *  Return void.
 **/
void stats_port_deinit(mct_port_t *port)
{
  stats_port_private_t *private;
  int                  i;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "stats_sink")) {
    return;
  }

  private = port->port_private;
  if (private) {
    pthread_mutex_destroy(&private->stats_buf_mutex);
    stats_port_setparm_ctrl_t *parm_ctrl =
      (stats_port_setparm_ctrl_t *)&private->parm_ctrl;
    /* Free allocated memory used for handling set params before
     chromatix is set*/
    for (i = 0; i < CAM_INTF_PARM_MAX; i++) {
      if (parm_ctrl->evt[i].event) {
        if (parm_ctrl->evt[i].event->u.ctrl_event.control_event_data) {
          free(parm_ctrl->evt[i].event->u.ctrl_event.control_event_data);
        }
        free(parm_ctrl->evt[i].event);
      }
    }

    for (i = 0; i < STATS_MAX_FRAME_DELAY; i++) {
      mct_queue_flush(parm_ctrl->msg_q[i], stats_port_free_set_parm_ctrl);
      mct_queue_free(parm_ctrl->msg_q[i]);
      pthread_mutex_destroy(&parm_ctrl->msg_q_lock[i]);
    }
    /* Release the sub ports list */
    mct_list_free_list(private->sub_ports);

    free(private);
    private = NULL;
  }
}

/** stats_port_init
 *    @port:      port object to be initialized
 *    @identity:  the identity of port
 *    @sub_ports: the sub ports list
 *
 *  Port initialization, use this function to overwrite
 *  default port methods and install capabilities. Stats
 *  module should have ONLY sink port.
 *
 *  Return TRUE on success, FALSE on failure.
 **/
boolean stats_port_init(mct_port_t *port, unsigned int identity,
  mct_list_t *sub_ports)
{
  mct_port_caps_t      caps;
  stats_port_private_t *private;
  int                  i;

  private = malloc(sizeof(stats_port_private_t));
  if (private == NULL) {
    return FALSE;
  }
  memset(private, 0, sizeof(stats_port_private_t));

  private->state       = STATS_PORT_STATE_CREATED;
  private->sub_ports   = sub_ports;
  private->reserved_id = identity;
  /* Default pipeline delay 1*/
  private->delay.applying_delay = 0;
  private->delay.meta_reporting_delay = 0;
  private->skip_pattern = NO_SKIP;

  port->port_private   = private;
  port->direction      = MCT_PORT_SINK;

  stats_port_setparm_ctrl_t *parm_ctrl =
    (stats_port_setparm_ctrl_t *)&private->parm_ctrl;

  for (i = 0; i < STATS_MAX_FRAME_DELAY; i++) {
    pthread_mutex_init(&parm_ctrl->msg_q_lock[i], NULL);
    parm_ctrl->msg_q[i] = (mct_queue_t *)mct_queue_new;
    if (!parm_ctrl->msg_q[i]) {
      goto err;
    }
    mct_queue_init(parm_ctrl->msg_q[i]);
  }
  pthread_mutex_init(&private->stats_buf_mutex, NULL);

  caps.port_caps_type  = MCT_PORT_CAPS_STATS;
  caps.u.stats.flag    = (MCT_PORT_CAP_STATS_Q3A | MCT_PORT_CAP_STATS_CS_RS |
    MCT_PORT_CAP_STATS_HIST);

  mct_port_set_event_func(port, stats_port_event);
  mct_port_set_set_caps_func(port, stats_port_set_caps);
  mct_port_set_ext_link_func(port, stats_port_ext_link);
  mct_port_set_unlink_func(port, stats_port_unlink);
  mct_port_set_check_caps_reserve_func(port, stats_port_check_caps_reserve);
  mct_port_set_check_caps_unreserve_func(port, stats_port_check_caps_unreserve);

  private->set_pipeline_delay = stats_port_set_pipeline_delay;
  if (port->set_caps) {
    port->set_caps(port, &caps);
  }

  return TRUE;
err:
  stats_port_deinit(port);
  return FALSE;
}

/* This function simply returns the TRUE or FALSE, based on debug mask */
boolean is_stats_buffer_debug_data_enable(mct_port_t *port)
{
  boolean exif_dbg_enable = FALSE;

  if (!port) {
    STATS_ERR("Null pointer");
    return 0;
  }
  if (stats_exif_debug_mask & EXIF_DEBUG_MASK_STATS) {
    exif_dbg_enable = TRUE;
  }
  return (exif_dbg_enable);
}

/* Function to copy BG stats only */
static void copy_stats_bg_to_debug_data(stats_port_private_t *private,
  enum msm_isp_stats_type bg_stats_type, const mct_event_stats_isp_t *const stats_event)
{

  uint32 index = 0;
  BayerGridStatsType* pBgDebugStats = NULL;
  int32 debug_data_size = 0;
  if (!(bg_stats_type == MSM_ISP_STATS_BG ||
    bg_stats_type == MSM_ISP_STATS_AEC_BG)) {
    STATS_ERR("Can't copy this stats type: %d", bg_stats_type);
    return;
  }

  const q3a_bg_stats_t* const p_bg_stats =
    (q3a_bg_stats_t*)stats_event->stats_data[bg_stats_type].stats_buf;

  if (bg_stats_type == MSM_ISP_STATS_BG) {
    pBgDebugStats = &(private->bg_stats_debug_Data);
    debug_data_size = sizeof(private->bg_stats_debug_Data.redChannelSum);
    private->bg_stats_buffer_size = sizeof(private->bg_stats_debug_Data);
  } else if (bg_stats_type == MSM_ISP_STATS_AEC_BG) {
    pBgDebugStats = &(private->bg_aec_stats_debug_Data);
    debug_data_size = sizeof(private->bg_aec_stats_debug_Data.redChannelSum);
    private->bg_aec_stats_buffer_size = sizeof(private->bg_aec_stats_debug_Data);
  }

  const int32 q3a_data_size = sizeof(p_bg_stats->bg_r_sum);
  const int32 copy_size = (q3a_data_size > debug_data_size) ?
    debug_data_size : q3a_data_size;

  /* Horizontal and vertical regions */
  pBgDebugStats->bgStatsNumHorizontalRegions =
    (uint16)p_bg_stats->bg_region_h_num;
  pBgDebugStats->bgStatsNumVerticalRegions =
    (uint16)p_bg_stats->bg_region_v_num;

  /* Instead of loop, used memcopy for better performance. */
  /* Copy BG stats sum array. */
  memcpy(pBgDebugStats->redChannelSum, p_bg_stats->bg_r_sum, copy_size);
  memcpy(pBgDebugStats->grChannelSum, p_bg_stats->bg_gr_sum, copy_size);
  memcpy(pBgDebugStats->gbChannelSum, p_bg_stats->bg_gb_sum, copy_size);
  memcpy(pBgDebugStats->blueChannelSum, p_bg_stats->bg_b_sum, copy_size);

  /* Copy BG stats count array. */
  for (index = 0;
      (index < BAYER_GRID_NUM_REGIONS) && (index < MAX_BG_STATS_NUM);
      index++) {
    pBgDebugStats->redChannelCount[index] = (uint16)p_bg_stats->bg_r_num[index];
    pBgDebugStats->grChannelCount[index] = (uint16)p_bg_stats->bg_gr_num[index];
    pBgDebugStats->gbChannelCount[index] = (uint16)p_bg_stats->bg_gb_num[index];
    pBgDebugStats->blueChannelCount[index] = (uint16)p_bg_stats->bg_b_num[index];
  }

  return;
}

/* Function to copy BG stats and Hist stats into debug data structure. */
static void copy_stats_buffer_to_debug_data(mct_event_t *event,
  mct_port_t *port)
{
  if (!event || !port) {
    STATS_ERR("Null pointer");
    return;
  }
  stats_port_private_t *private = (stats_port_private_t *)port->port_private;
  const mct_event_stats_isp_t *const stats_event =
    (mct_event_stats_isp_t *)event->u.module_event.module_event_data;

  if (stats_event->stats_mask & (1 << MSM_ISP_STATS_BG)) {
    copy_stats_bg_to_debug_data(private, MSM_ISP_STATS_BG, stats_event);
  }
  if (stats_event->stats_mask & (1 << MSM_ISP_STATS_AEC_BG)) {
    copy_stats_bg_to_debug_data(private, MSM_ISP_STATS_AEC_BG, stats_event);
  }
  if ((stats_event->stats_mask & (1 << MSM_ISP_STATS_HDR_BHIST)) ||
    (stats_event->stats_mask & (1 << MSM_ISP_STATS_BHIST))) {
    const q3a_bhist_stats_t* const p_hist_stats =
      (stats_event->stats_mask & (1 << MSM_ISP_STATS_HDR_BHIST)) ?
      (q3a_bhist_stats_t*)stats_event->stats_data[MSM_ISP_STATS_HDR_BHIST].stats_buf :
      (q3a_bhist_stats_t*)stats_event->stats_data[MSM_ISP_STATS_BHIST].stats_buf;
    BayerHistogramStatsType* const pHistDebugStats = &(private->hist_stats_debug_Data);
    const int32 debug_data_size = sizeof(private->hist_stats_debug_Data.grChannel);
    const int32 q3a_data_size = sizeof(p_hist_stats->bayer_gr_hist);
    const int32 copy_size = (q3a_data_size > debug_data_size) ?
      debug_data_size : q3a_data_size;

    /* Instead of loop, used memcopy for better performance. */
    /* Copy Gr histogram stats. */
    memcpy(pHistDebugStats->grChannel, p_hist_stats->bayer_gr_hist, copy_size);
    private->bhist_stats_buffer_size = sizeof(private->hist_stats_debug_Data);
  }
}

static void copy_stats_bg_config_to_debug_data(BayerGridConfigType *bg_config_dbg,
  const aec_bg_config_t *const bg_config, int32_t *dbg_config_size, char *const str_stats_tag)
{
  uint16 config_width = bg_config->roi.width;
  uint16 config_height = bg_config->roi.height;
  uint16 config_left = bg_config->roi.left;
  uint16 config_top = bg_config->roi.top;

  uint16 camif_width = 2 * config_left + config_width;
  uint16 camif_height = 2 * config_top + config_height;
  bg_config_dbg->horizonOffsetRatio =
    (uint32)(((float)config_left / camif_width) * (1 << 20));
  bg_config_dbg->verticalOffsetRatio =
    (uint32)(((float)config_top / camif_height) * (1 << 20));
  bg_config_dbg->horizonWindowRatio =
    (uint32)(((float)config_width / camif_width) * (1 << 20));
  bg_config_dbg->verticalWindowRatio =
    (uint32)(((float)config_height / camif_height) * (1 << 20));

  *dbg_config_size = sizeof(BayerGridConfigType);

  STATS_LOW("[STATS-DEBUG]:%s: HOffsetRatio: %d, VOffsetRatio: %d,"
    " HWinRatio: %d, VWinRatio: %d, debug-size: %d",
    str_stats_tag,
    bg_config_dbg->horizonOffsetRatio, bg_config_dbg->verticalOffsetRatio,
    bg_config_dbg->horizonWindowRatio, bg_config_dbg->horizonWindowRatio,
    *dbg_config_size);

}
static void copy_stats_config_to_debug_data(mct_port_t *port,
  mct_event_t *event)
{
  const aec_bg_config_t *bg_config = NULL;
  const aec_bg_config_t *aec_bg_config = NULL;
  stats_port_private_t *private = NULL;

  if (!event || !port || !port->port_private) {
    STATS_ERR("Null pointer, event: %p, port: %p", event, port);
    return;
  }
  private = (stats_port_private_t *)port->port_private;

  switch (event->u.module_event.type) {
  case MCT_EVENT_MODULE_STATS_AEC_CONFIG_UPDATE: {
    const aec_config_t *const p_aec_config =
      (aec_config_t *)event->u.module_event.module_event_data;

    if (p_aec_config->aec_bg_config.is_valid) {
      aec_bg_config = &(p_aec_config->aec_bg_config);
    }
    if (p_aec_config->bg_config.is_valid) {
      bg_config = &(p_aec_config->bg_config);
    }
  }
    break;
  case MCT_EVENT_MODULE_STATS_AWB_CONFIG_UPDATE: {
    const awb_config_t *const p_awb_config =
      (awb_config_t *)event->u.module_event.module_event_data;
    if (p_awb_config->bg_config.is_valid) {
      bg_config = &(p_awb_config->bg_config);
    }
  }
    break;
  default: {
    STATS_ERR("Not supported");
  }
    break;
  }


  if (!bg_config && !aec_bg_config) {
    STATS_ERR("Nothing to be done: bg_config: %p, aec_bg_config: %p",
      bg_config, aec_bg_config);
    return;
  }

  if (bg_config) {
    copy_stats_bg_config_to_debug_data(&(private->bg_config_debug_data),
      bg_config, &(private->bg_config_buffer_size), "BG");
  }
  if (aec_bg_config) {
    copy_stats_bg_config_to_debug_data(&(private->bg_aec_config_debug_data),
      aec_bg_config, &(private->bg_aec_config_buffer_size), "AEC_BG");
  }
}

static void send_stats_buffer_to_debug_data(mct_port_t *port)
{
  mct_event_t event;
  mct_bus_msg_t bus_msg;
  cam_stats_buffer_exif_debug_t stats_buffer_info;
  stats_port_private_t *private;
  int size = 0;
  int total_stats_buff_size = 0;
  int aec_bg_stats_size = 0;

  if (!port) {
    STATS_ERR("input error");
    return;
  }

#ifdef _STATS_DUMP_AEC_BG_DBG_DATA_
  aec_bg_stats_size = private->bg_aec_stats_buffer_size + private->bg_aec_config_buffer_size;
#endif
  private = (stats_port_private_t *)(port->port_private);
  total_stats_buff_size = private->bg_stats_buffer_size +
    private->bhist_stats_buffer_size + private->bg_config_buffer_size +
    aec_bg_stats_size;

  if (STATS_BUFFER_DEBUG_DATA_SIZE < total_stats_buff_size ||
      0 == total_stats_buff_size) {
    STATS_ERR("Stats buffer debug data send error. buffer size mismatch buffer size: %d,"
      " BG stats size: %d, bg_config size: %d"
      " bhist stats size %d"
      " aec_bg_stats_size %d",
      STATS_BUFFER_DEBUG_DATA_SIZE,
      private->bg_stats_buffer_size, private->bg_config_buffer_size,
      private->bhist_stats_buffer_size,
      aec_bg_stats_size);
#ifdef _STATS_DUMP_AEC_BG_DBG_DATA_
    STATS_ERR("Stats debug data error: BG AEC stats size: %d, bg_aec_config size: %d",
      private->bg_aec_stats_buffer_size, private->bg_aec_config_buffer_size);
#endif
    return;
  }
  bus_msg.sessionid = (private->reserved_id >> 16);
  bus_msg.type = MCT_BUS_MSG_STATS_EXIF_DEBUG_INFO;
  bus_msg.msg = (void *)&stats_buffer_info;
  size = (int)sizeof(cam_stats_buffer_exif_debug_t);
  bus_msg.size = size;
  memset(&stats_buffer_info, 0, size);
  stats_buffer_info.bg_stats_buffer_size =
    private->bg_stats_buffer_size;
  stats_buffer_info.bhist_stats_buffer_size =
    private->bhist_stats_buffer_size;
  stats_buffer_info.bg_config_buffer_size =
    private->bg_config_buffer_size;
  STATS_LOW("Stats buffer debug data size. bg: %d, bg_config: %d bhist: %d",
    private->bg_stats_buffer_size,
    private->bg_config_buffer_size,
    private->bhist_stats_buffer_size);

#ifdef _STATS_DUMP_AEC_BG_DBG_DATA_
  STATS_LOW("Stats buffer debug data size for AEC_BG: bg_aec: %d, bg_aec_config: %d",
    private->bg_aec_stats_buffer_size,
    private->bg_aec_config_buffer_size);
#endif


  /* Copy the bg stats debug data if data size is valid */
  if (private->bg_stats_buffer_size) {
    memcpy(&(stats_buffer_info.stats_buffer_private_debug_data[0]),
      &(private->bg_stats_debug_Data), private->bg_stats_buffer_size);
  }

  /* Copy the bg config debug data if data size is valid */
  if (private->bg_config_buffer_size) {
    memcpy(&(stats_buffer_info.stats_buffer_private_debug_data[\
       private->bg_stats_buffer_size]),
      &(private->bg_config_debug_data), private->bg_config_buffer_size);
  }

  /* Copy the bhist stats debug data if data size is valid */
  if (private->bhist_stats_buffer_size) {
    memcpy(&(stats_buffer_info.stats_buffer_private_debug_data[\
      private->bg_stats_buffer_size + private->bg_config_buffer_size]),
      &(private->hist_stats_debug_Data), private->bhist_stats_buffer_size);
  }

#ifdef _STATS_DUMP_AEC_BG_DBG_DATA_
  /* Copy the bg AEC stats debug data if data size is valid */
  if (private->bg_aec_stats_buffer_size) {
    memcpy(&(stats_buffer_info.stats_buffer_private_debug_data[\
      private->bg_stats_buffer_size + private->bg_config_buffer_size +
      private->bhist_stats_buffer_size]),
      &(private->bg_aec_stats_debug_Data), private->bg_aec_stats_buffer_size);
  }

  /* Copy the bg AEC config debug data if data size is valid */
  if (private->bg_aec_config_buffer_size) {
    memcpy(&(stats_buffer_info.stats_buffer_private_debug_data[\
      private->bg_stats_buffer_size + private->bg_config_buffer_size +
      private->bhist_stats_buffer_size +
      private->bg_aec_stats_buffer_size]),
      &(private->bg_aec_config_debug_data), private->bg_aec_config_buffer_size);
  }
#endif

  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = private->reserved_id;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.u.module_event.type = MCT_EVENT_MODULE_STATS_POST_TO_BUS;
  event.u.module_event.module_event_data = (void *)(&bus_msg);
  MCT_PORT_EVENT_FUNC(port)(port, &event);
}

static boolean stats_port_handle_stats_data(mct_port_t *port, mct_event_t *stats_event)
{
  boolean                rc = TRUE;
  stats_port_private_t   *private = NULL;
  stats_port_event_t     port_event;
  mct_event_t            event;
  mct_event_stats_ext_t  *stats_ext = NULL;

  if (!port || !stats_event) {
    STATS_ERR("NULL parameter");
    return FALSE;
  }

  private = (stats_port_private_t *)port->port_private;

  if (!private) {
    STATS_ERR("Null pointer");
    return FALSE;
  }

  if (0 == private->parm_ctrl.stream_on_counter) {
    STATS_ERR("skip stats, 0 stream");
    return FALSE;
  }

  stats_ext = calloc(1, sizeof(mct_event_stats_ext_t));
  if (!stats_ext) {
    STATS_ERR("malloc buffer for mct_event_stats_ext_t failed");
    return FALSE;
  }

  mct_event_stats_isp_t * stats =
    (mct_event_stats_isp_t *)stats_event->u.module_event.module_event_data;

  stats_ext->ref_count = 1;
  stats_ext->stats_data = stats;
  stats_ext->stats_mutex = &private->stats_buf_mutex;

  /* This event handling in each submodule should be a blocking call */
  port_event.event = &event;
  port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
    MCT_PORT_CAP_STATS_AEC |
    MCT_PORT_CAP_STATS_AWB |
    MCT_PORT_CAP_STATS_IS  |
    MCT_PORT_CAP_STATS_AFD |
    MCT_PORT_CAP_STATS_ASD);
  event.type = MCT_EVENT_MODULE_EVENT;
  event.identity = private->reserved_id;
  event.direction = MCT_EVENT_DOWNSTREAM;
  event.u.module_event.type = MCT_EVENT_MODULE_STATS_EXT_DATA;
  event.u.module_event.module_event_data = (void *)(stats_ext);
  rc = mct_list_traverse((mct_list_t *)private->sub_ports,
     stats_port_send_event_downstream, &port_event);

  int32_t count = 0;
  pthread_mutex_lock(stats_ext->stats_mutex);
  count = --stats_ext->ref_count;
  pthread_mutex_unlock(stats_ext->stats_mutex);

  if (count == 0) {
    free(stats_ext);
    stats_ext = NULL;
  } else {
    // we are using the stats buffer
    // per protocol, set the flag to FALSE
    stats->ack_flag = FALSE;
  }
  return rc;
}

static boolean stats_port_set_stream_on_off(mct_port_t *port, boolean stream_on)
{
  boolean                rc = TRUE;
  stats_port_private_t   *private = NULL;
  stats_port_event_t     port_event;
  mct_event_t            event;
  stats_set_params_type  stats_parm;

  if (!port) {
    STATS_ERR("NULL parameter");
    return FALSE;
  }

  private = (stats_port_private_t *)port->port_private;

  if (!private) {
    STATS_ERR("Null pointer");
    return FALSE;
  }

  port_event.event = &event;
  port_event.cap_flag = (MCT_PORT_CAP_STATS_AF |
    MCT_PORT_CAP_STATS_AEC |
    MCT_PORT_CAP_STATS_AWB |
    MCT_PORT_CAP_STATS_IS  |
    MCT_PORT_CAP_STATS_AFD |
    MCT_PORT_CAP_STATS_ASD);
  event.type = MCT_EVENT_CONTROL_CMD;
  event.identity = private->reserved_id;
  event.direction = MCT_EVENT_DOWNSTREAM;
  event.u.ctrl_event.type = MCT_EVENT_CONTROL_SET_PARM;
  event.u.ctrl_event.control_event_data = &stats_parm;

  stats_parm.param_type = STATS_SET_COMMON_PARAM;
  stats_parm.u.common_param.u.stream_on = stream_on;
  stats_parm.u.common_param.type = COMMON_SET_PARAM_STREAM_ON_OFF;
  rc = mct_list_traverse((mct_list_t *)private->sub_ports,
     stats_port_send_event_downstream, &port_event);

  STATS_LOW("COMMON_SET_PARAM_STREAM_ON_OFF %d", stream_on);
  return rc;
}
