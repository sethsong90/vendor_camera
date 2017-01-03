/* mct_controller.c
 *
 * This file contains the media controller implementation. All commands coming
 * from the server arrive here first. There is one media controller per
 * session.
 *
 * Copyright (c) 2013-2016 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#include "cam_ker_headers.h"
#include "media_controller.h"
#include "mct_controller.h"
#include "mct_profiler.h"
#include "mct_pipeline.h"
#include "mct_bus.h"
#include "cam_intf.h"
#include "camera_dbg.h"

#include <cutils/properties.h>
#include <server_debug.h>

volatile uint32_t mct_debug_level = MCT_DEBUG_PRIO_HIGH;
volatile uint32_t kpi_debug_level = 0;
volatile uint32_t mct_debug_module = MODULE_MCT;

mct_list_t *mcts = NULL;

static void* mct_controller_thread_run(void *data);
static void* mct_bus_handler_thread_run(void *data);
static boolean mct_controller_handle_SOF_proc(
  mct_pipeline_t *pipeline, mct_bus_msg_t *bus_msg);

/** mct_controller_new:
 *   @mods: modules list
 *   @session_idx: session index
 *   @serv_fd: file descriptor for MCT to communicate
 *             back to imaging server
 *
 *    Return: Status of back-end.
 *    Description:
 *    create a new Media Controller object. This creates
 *    a new pipeline and starts a session on all inited modules
 *
 * This function executes in SERVER context
 **/
mct_status_ret_type mct_controller_new(mct_list_t *mods,
  unsigned int session_idx, int serv_fd)
{
  mct_controller_t *mct = NULL;
  pthread_t         tid;
  pthread_t         bus_hdl_tid;
  mct_status_ret_type ret_type = MCT_STATUS_RET_FAILURE;

  cam_set_dbg_log_properties();

  CAMSCOPE_INIT(CAMSCOPE_SECTION_MCT);

  mct = (mct_controller_t *)malloc(sizeof(mct_controller_t));
  if (!mct)
    goto mct_error;
  CLOGH(CAM_MCT_MODULE,
        "Creating new mct_controller with session-id %d", session_idx);

  mct->pipeline = mct_pipeline_new(session_idx, mct);
  if (!mct->pipeline)
    goto pipeline_error;

  mct->pipeline->modules = mods;

  ret_type = mct_pipeline_start_session(mct->pipeline);
  if (MCT_STATUS_RET_SUCCESS != ret_type) {
    CLOGE(CAM_MCT_MODULE, "Start session failed with status %d", ret_type);
    goto start_session_error;
  }

  mct->serv_cmd_q = mct_queue_new;
  if (!mct->serv_cmd_q)
    goto start_session_error;

  mct_queue_init(mct->serv_cmd_q);

  pthread_mutex_init(&mct->mctl_thread_started_mutex, NULL);
  pthread_cond_init(&mct->mctl_thread_started_cond, NULL);

  mct->serv_cmd_q_counter = 0;

  pthread_mutex_init(&mct->mctl_mutex, NULL);
  pthread_cond_init(&mct->mctl_cond, NULL);

  pthread_mutex_init(&mct->serv_msg_q_lock, NULL);

  pthread_mutex_init (&mct->mctl_state_mutex, NULL);
  pthread_cond_init(&mct->mctl_state_cond, NULL);
  mct->bus_thread_state = MCT_THREAD_STATE_IDLE;
  mct->serv_fd  = serv_fd;

  pthread_mutex_lock(&mct->mctl_thread_started_mutex);

  if (pthread_create(&tid, NULL, mct_controller_thread_run, mct)) {
    pthread_mutex_unlock(&mct->mctl_thread_started_mutex);
    goto main_thread_error;
  }
  pthread_setname_np(tid, "CAM_MctServ");

  pthread_cond_wait(&mct->mctl_thread_started_cond,
    &mct->mctl_thread_started_mutex);
  pthread_mutex_unlock(&mct->mctl_thread_started_mutex);
  mct->mct_tid = tid;

  pthread_mutex_init(&mct->mctl_bus_handle_mutex, NULL);
  pthread_cond_init(&mct->mctl_bus_handle_cond, NULL);
  pthread_mutex_lock(&mct->mctl_bus_handle_mutex);
  if (pthread_create(&bus_hdl_tid, NULL, mct_bus_handler_thread_run, mct)) {
    pthread_mutex_unlock(&mct->mctl_bus_handle_mutex);
    goto bus_handle_thread_error;
  }
  pthread_setname_np(bus_hdl_tid, "CAM_MctBus");
  pthread_mutex_unlock(&mct->mctl_bus_handle_mutex);
  mct->mct_bus_handle_tid = bus_hdl_tid;
  mct->pipeline->bus->mct_mutex = &mct->mctl_bus_handle_mutex;
  mct->pipeline->bus->mct_cond  = &mct->mctl_bus_handle_cond;

  if (!(mcts = mct_list_append(mcts, mct, NULL, NULL)))
    goto all_error;

  return MCT_STATUS_RET_SUCCESS;

all_error:
  mct_bus_destroy(mct->pipeline->bus);
bus_handle_thread_error:
  pthread_cond_destroy(&mct->mctl_bus_handle_cond);
  pthread_mutex_destroy(&mct->mctl_bus_handle_mutex);
main_thread_error:
  pthread_cond_destroy(&mct->mctl_thread_started_cond);
  pthread_mutex_destroy(&mct->mctl_thread_started_mutex);
  pthread_mutex_destroy(&mct->serv_msg_q_lock);
  pthread_cond_destroy(&mct->mctl_cond);
  pthread_mutex_destroy(&mct->mctl_mutex);
  pthread_mutex_destroy(&mct->mctl_state_mutex);
  pthread_cond_destroy(&mct->mctl_state_cond);
  mct_queue_free(mct->serv_cmd_q);
  mct->serv_cmd_q = NULL;
start_session_error:
  mct_pipeline_stop_session(mct->pipeline);
  free(mct->pipeline);
  mct->pipeline = NULL;
pipeline_error:
  free(mct);
  mct = NULL;
mct_error:
  return ret_type;
}

/** mct_controller_find_session:
 *    @d1: media controller objective
 *    @d2: session index
 *
 * To find a MCT from MCTs list based on session index.
 *
 *  Return TRUE if MCT exists.
 **/
static boolean mct_controller_find_session(void *d1, void *d2)
{
  return ((((mct_controller_t *)d1)->pipeline->session) ==
          *(unsigned int *)d2 ? TRUE : FALSE);
}


/** mct_controller_destroy:
 *    @sessionIdx: the corresponding session index's
 *       MCT to be removed
 *
 **/
boolean mct_controller_destroy(unsigned int session_idx)
{
  mct_serv_msg_t   *msg;
  mct_controller_t *mct;
  mct_list_t       *mct_list;
  mct_bus_msg_t *bus_msg_destroy;

  mct_list = mct_list_find_custom(mcts, &session_idx,
        mct_controller_find_session);
  if (!mct_list) {
    return FALSE;
  }
  mct = (mct_controller_t *)mct_list->data;
  CLOGH(CAM_MCT_MODULE,
        "Initiating destroy sequence for session = %d", session_idx);

  msg = calloc(1, sizeof(mct_serv_msg_t));
  if (!msg)
    return FALSE;

  msg->msg_type = SERV_MSG_HAL;
  msg->u.hal_msg.id = MSM_CAMERA_DEL_SESSION;

  pthread_mutex_lock(&mct->serv_msg_q_lock);
  mct_queue_push_tail(mct->serv_cmd_q, msg);
  pthread_mutex_unlock(&mct->serv_msg_q_lock);

  pthread_mutex_lock(&mct->mctl_mutex);
  mct->serv_cmd_q_counter++;
  pthread_cond_signal(&mct->mctl_cond);
  pthread_mutex_unlock(&mct->mctl_mutex);
  pthread_join(mct->mct_tid, NULL);
  CLOGH(CAM_MCT_MODULE, "serv_thread closed");

  bus_msg_destroy = (mct_bus_msg_t*)malloc(sizeof(mct_bus_msg_t));
  if(!bus_msg_destroy)
    return FALSE;
  memset(bus_msg_destroy, 0, sizeof(mct_bus_msg_t));
  bus_msg_destroy->type = MCT_BUS_MSG_CLOSE_CAM;
  pthread_mutex_lock(&mct->pipeline->bus->priority_q_lock);
  mct_queue_push_tail(mct->pipeline->bus->priority_queue, bus_msg_destroy);
  pthread_mutex_unlock(&mct->pipeline->bus->priority_q_lock);
  /*Signal bus_handle thread to terminate */
  pthread_mutex_lock(&mct->mctl_bus_handle_mutex);
  pthread_cond_signal(&mct->mctl_bus_handle_cond);
  pthread_mutex_unlock(&mct->mctl_bus_handle_mutex);
  pthread_join(mct->mct_bus_handle_tid, NULL);
  CLOGH(CAM_MCT_MODULE, "bus_handler thread closed");

  CAMSCOPE_DESTROY(CAMSCOPE_SECTION_MCT);

  /*Destroying Pipeline */
  mct_pipeline_stop_session(mct->pipeline);
  mct_pipeline_destroy(mct->pipeline);

  pthread_cond_destroy(&mct->mctl_thread_started_cond);
  pthread_mutex_destroy(&mct->mctl_thread_started_mutex);
  pthread_mutex_destroy(&mct->serv_msg_q_lock);
  pthread_mutex_destroy(&mct->mctl_mutex);
  pthread_mutex_destroy(&mct->mctl_bus_handle_mutex);
  pthread_mutex_destroy(&mct->mctl_state_mutex);
  pthread_cond_destroy(&mct->mctl_state_cond);
  if (!MCT_QUEUE_IS_EMPTY(mct->serv_cmd_q)) {
    mct_queue_free(mct->serv_cmd_q);
  }else
    free(mct->serv_cmd_q);

  mcts = mct_list_remove(mcts, mct);
  free(mct);
  mct = NULL;
  CLOGH(CAM_MCT_MODULE, "X Successfully closed mct_controller session %d",
    session_idx);
  return TRUE;
}

/** mct_controller_proc_servmsg:
 *    @servMsg: the message to be posted
 *
 * Post imaging server message to Media Controller's message
 *    message queue.
 *
 * This function executes in Imaging Server context
 **/
boolean mct_controller_proc_serv_msg(mct_serv_msg_t *serv_msg)
{
  mct_controller_t *mct;
  mct_list_t       *mct_list;
  mct_serv_msg_t   *msg;
  unsigned int     session;

  switch (serv_msg->msg_type) {
  case SERV_MSG_DS:
    session = serv_msg->u.ds_msg.session;
    break;

  case SERV_MSG_HAL: {
    struct msm_v4l2_event_data *data =
      (struct msm_v4l2_event_data *)(serv_msg->u.hal_msg.u.data);
    session = data->session_id;
  }
    break;

  default:
    return FALSE;
  }

  mct_list = mct_list_find_custom(mcts, &session,
    mct_controller_find_session);
  if (!mct_list) {
    return FALSE;
  }

  mct = (mct_controller_t *)mct_list->data;

  msg = malloc(sizeof(mct_serv_msg_t));
  if (!msg)
    return FALSE;

  *msg = *serv_msg;

  /* Push message to Media Controller Message Queue
   * and post signal to Media Controller */
  pthread_mutex_lock(&mct->serv_msg_q_lock);
  mct_queue_push_tail(mct->serv_cmd_q, msg);
  pthread_mutex_unlock(&mct->serv_msg_q_lock);

  pthread_mutex_lock(&mct->mctl_mutex);
  mct->serv_cmd_q_counter++;
  pthread_cond_signal(&mct->mctl_cond);
  pthread_mutex_unlock(&mct->mctl_mutex);

  return TRUE;
}

/** mct_controller_check_pipeline
 *    @pipeline: pipeline objective
 *
 * Check the pipeline's vadility
 **/
static boolean mct_controller_check_pipeline(mct_pipeline_t *pipeline)
{
  return ((pipeline->add_stream    &&
           pipeline->remove_stream &&
           pipeline->send_event    &&
           pipeline->set_bus       &&
           pipeline->get_bus) ? TRUE : FALSE);
}

/** mct_controller_proc_servmsg_internal:
 *    @mct: Media Controller Object
 *    @msg: message object from imaging server
 *
 * Media Controller process Imaging Server messages
 * Return: mct_process_ret_t
 *
 * This function executes in Media Controller's thread context
 **/
static mct_process_ret_t mct_controller_proc_serv_msg_internal(
  mct_controller_t *mct, mct_serv_msg_t *msg)
{
  mct_process_ret_t ret;
  mct_pipeline_t    *pipeline;

  memset(&ret, 0x00, sizeof(mct_process_ret_t));
  ret.type = MCT_PROCESS_RET_SERVER_MSG;
  ret.u.serv_msg_ret.error = TRUE;

  if (!mct || !msg || !mct->pipeline) {
    ret.u.serv_msg_ret.error = TRUE;
    return ret;
  }

  ret.u.serv_msg_ret.msg = *msg;
  pipeline = mct->pipeline;

  if (!mct_controller_check_pipeline(pipeline)) {
    ret.u.serv_msg_ret.error = TRUE;
    return ret;
  }

  switch (msg->msg_type) {
  case SERV_MSG_DS: {
    if ((msg->u.ds_msg.operation == CAM_MAPPING_TYPE_FD_BUNDLED_MAPPING ||
       msg->u.ds_msg.operation == CAM_MAPPING_TYPE_FD_MAPPING)
       && pipeline->map_buf) {
      ret.u.serv_msg_ret.error = pipeline->map_buf(&msg->u.ds_msg, pipeline);
    } else if (msg->u.ds_msg.operation == CAM_MAPPING_TYPE_FD_UNMAPPING &&
        pipeline->unmap_buf) {
      ret.u.serv_msg_ret.error = pipeline->unmap_buf(&msg->u.ds_msg, pipeline);
    }
  }
    break;

  case SERV_MSG_HAL:
    if (pipeline->process_serv_msg)
      ret.u.serv_msg_ret.error = pipeline->process_serv_msg(&msg->u.hal_msg,
        pipeline);
    break;

  default:
    break;
  }

  return ret;
}

/** mct_controller_handle_offline_meta_proc:
 *    @pipeline: structure of mct_controller_t
 *    @bus_msg: Incoming bus_msg to process
 *
 *  This function handle offline metadata bus message
 **/

/**Name: mct_controller_handle_offline_meta_proc:
 *
 * Arguments/Fields:
 *    @Pipeline: structure of mct_pipeline_t
 *    @bus_msg: bus messages to process
 *
 *  Return: boolean;
 *    TRUE on success; FALSE on failure
 *
 *  Description:
 *    This function handle offline metadata bus message
 **/
static boolean mct_controller_handle_offline_meta_proc(
   mct_pipeline_t *pipeline, mct_bus_msg_t *bus_msg)
{
  struct msm_buf_mngr_info buf_info;
  mct_stream_t *offline_raw_stream = NULL;
  mct_pipeline_get_stream_info_t info;
  mct_stream_t *output_stream = NULL;
  boolean ret = TRUE;
  mct_bus_msg_isp_rd_done_t *isp_rd_done_msg = NULL;
  int status = 0;

  /* Obtain Raw stream to issue commands on */
  if (MCT_PIPELINE_NUM_CHILDREN(pipeline) > 0) {
    info.check_type   = CHECK_FEATURE_MASK;
    info.stream_type = CAM_STREAM_TYPE_OFFLINE_PROC;
    info.feature_mask  = CAM_QCOM_FEATURE_RAW_PROCESSING;
    offline_raw_stream = mct_pipeline_get_stream(pipeline, &info);
    if (!offline_raw_stream) {
      CLOGE(CAM_MCT_MODULE, "No offline stream available at SOF");
      return FALSE;
    }
  } else {
    CLOGE(CAM_MCT_MODULE, "No child present for pipeline");
    return FALSE;
  }
  isp_rd_done_msg = (mct_bus_msg_isp_rd_done_t *)bus_msg->msg;
  /* Sending Offline proc command to modules */
  ret = mct_stream_send_offline_meta(offline_raw_stream, isp_rd_done_msg);

  /*Buf done for Offline metadata buffer */
  buf_info.index = pipeline->offline_meta.output_buf_idx;
  buf_info.frame_id = isp_rd_done_msg->frame_id;
  buf_info.stream_id = pipeline->offline_meta.stream_id;
  buf_info.session_id = MCT_PIPELINE_SESSION(
  MCT_PIPELINE_CAST(MCT_OBJECT_PARENT(offline_raw_stream)->data));

  CLOGD(CAM_MCT_MODULE,
        "Sending offline metadata buffer index %d", buf_info.index);
  status = ioctl(pipeline->offline_meta.buf_mgr_fd,
      VIDIOC_MSM_BUF_MNGR_BUF_DONE, &buf_info);
  if (status < 0) {
    CLOGE(CAM_MCT_MODULE, "Failed to do buf_done errno: %s!!! for buffer with"
        "index: %d, stream id: %x, session id: %d", strerror(errno),
        buf_info.index, buf_info.stream_id, buf_info.session_id);
    ret = FALSE;
  }
  return ret;
}

/**Name: mct_controller_handle_HW_error:
 *
 * Arguments/Fields:
 *    @Pipeline: structure of mct_pipeline_t
 *
 *  Return: boolean;
 *    TRUE on success; FALSE on failure
 *
 *  Description:
 *    This function handle hw error bus message
 **/

static boolean mct_controller_handle_HW_error(mct_pipeline_t *pipeline)
{
  boolean ret = TRUE;
  int status = 0;
  mct_stream_t *parm_stream;
  mct_pipeline_get_stream_info_t info;

  if (!pipeline) {
    CLOGE(CAM_MCT_MODULE, "Pipeline not present");
    return FALSE;
  }
  /* Sending on Session based stream */
  if (MCT_PIPELINE_NUM_CHILDREN(pipeline) > 0) {
    info.check_type   = CHECK_TYPE;
    info.stream_type  = CAM_STREAM_TYPE_PARM;
    parm_stream = mct_pipeline_get_stream(pipeline, &info);
    if (!parm_stream) {
      CLOGE(CAM_MCT_MODULE, "No parm stream available");
      return FALSE;
    }
  } else {
    CLOGE(CAM_MCT_MODULE, "No child present for pipeline");
    return FALSE;
  }
  /* Sending HW error to modules */
  ret = mct_stream_send_hw_error(parm_stream);

  return ret;
}


/** mct_controller_proc_bus_msg_internal:
 *    Media Controller process Bus messages
 *
 *    @mct: Media Controller Object
 *    @msg: message object from bus
 *
 * Return: mct_process_ret_t
 *
 * This function executes in Media Controller's thread context
 **/
static mct_process_ret_t mct_controller_proc_bus_msg_internal(
  mct_controller_t *mct, mct_bus_msg_t *bus_msg)
{
  mct_process_ret_t ret;
  mct_pipeline_t    *pipeline;
  pthread_attr_t attr;

  ret.u.bus_msg_ret.error = TRUE;
  ret.type = MCT_PROCESS_RET_BUS_MSG;

  if (!mct || !bus_msg || !mct->pipeline) {
    return ret;
  }

  if (!mct_controller_check_pipeline(mct->pipeline)) {
    return ret;
  }
  mct_controller_set_thread_state(mct, MCT_THREAD_STATE_RUNNING);
  ret.u.bus_msg_ret.error = FALSE;
  ret.u.bus_msg_ret.msg_type = bus_msg->type;
  ret.u.bus_msg_ret.session = bus_msg->sessionid;
  pipeline = mct->pipeline;

  if (bus_msg->type == MCT_BUS_MSG_NOTIFY_KERNEL) {
    ret.u.bus_msg_ret.sof_debug = *(int *)bus_msg->msg;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&pipeline->thread_data.pid, &attr,
      &server_debug_dump_data_for_sof_freeze, &ret.u.bus_msg_ret.sof_debug);
    pthread_setname_np(pipeline->thread_data.pid, "CAM_mct_freeze");
    ret.type = MCT_PROCESS_DUMP_INFO;
    return ret;
  }
  if (bus_msg->type == MCT_BUS_MSG_SEND_HW_ERROR) {
    CLOGE(CAM_MCT_MODULE, "MCT_BUS_MSG_SEND_HW_ERROR");
    mct_controller_handle_HW_error(pipeline);
    ret.type = MCT_PROCESS_RET_ERROR_MSG;
    return ret;
  }
  if (bus_msg->type == MCT_BUS_MSG_EZTUNE_JPEG ||
      bus_msg->type == MCT_BUS_MSG_EZTUNE_RAW) {
    ret.type = MCT_PROCESS_RET_BUS_MSG;
    return ret;
  }

  pipeline = mct->pipeline;
  if(MCT_BUS_MSG_ISP_SOF == bus_msg->type) {
    if(!mct_controller_handle_SOF_proc(pipeline, bus_msg)) {
      ret.u.bus_msg_ret.error = FALSE;
      CLOGE(CAM_MCT_MODULE, "Failed to issue SOF cmd to all modules.");
      return ret;
    }
  }

  if(MCT_BUS_MSG_ISP_RD_DONE == bus_msg->type) {
   if(!mct_controller_handle_offline_meta_proc(pipeline, bus_msg)) {
     ret.u.bus_msg_ret.error = FALSE;
     CLOGE(CAM_MCT_MODULE, "ISP RD done control event failed");
     return ret;
   }
  }

  ATRACE_BEGIN("MCT_BUS_PROC");
  if (pipeline->process_bus_msg)
    ret.u.bus_msg_ret.error = pipeline->process_bus_msg(bus_msg, pipeline);
  ATRACE_END();

  mct_controller_set_thread_state(mct, MCT_THREAD_STATE_IDLE);
  return ret;
}


/** mct_controller_notify_hal_internal_event:
 *   @pipeline: Pointer to pipeline object
 *   @command: Command to be notified to HAL
 *
 *   Open up a channel for posting asynchronous notifications
 *   to HAL by MCT. It packs the command in a v4l2 event
 *   structure and writes into mct_msg_wt_fd pipe.
 *   return: Success/Failure
 **/
static boolean mct_controller_notify_hal_internal_event(
 mct_pipeline_t *pipeline, cam_event_type_t command)
{
  mct_process_ret_t  proc_ret;
  struct v4l2_event *event;
  if (!pipeline) {
    CLOGE(CAM_MCT_MODULE, "Pipeline ptr NULL");
    return FALSE;
  }
  proc_ret.type = MCT_PROCESS_NOTIFY_SERVER_MSG;
  proc_ret.u.serv_msg_ret.error = TRUE;

  /* Populate v4l2 event with necessary arguments */
  event = &proc_ret.u.serv_msg_ret.msg.u.hal_msg;
  struct msm_v4l2_event_data *data =
    (struct msm_v4l2_event_data *)(&(event->u.data[0]));
  data->session_id = pipeline->session;
  data->command    = command;

  /* Now post notify event on pipe */
  if (pipeline->controller) {
    write(pipeline->controller->serv_fd, &proc_ret, sizeof(mct_process_ret_t));
  } else {
    CLOGE(CAM_MCT_MODULE,
          "NULL mct_controller object. Not found serv_fd to write on");
    return FALSE;
  }
  return TRUE;
}

/** Name: mct_controller_send_superparam:
 *  Arguments/Fields:
 *    @pipeline: structure of mct_controller_t
 *    @parm_stream: Session stream on which super events are sent
 *  Description:
 *     Routine to process super-param events in pipeline queue.
 *     It de-queues super-params based on the per-frame-control logic
 *     and sends them to downstream modules while recording frame number
 *     mapping and notifying HAL when queue becomes empty.
 **/

static boolean mct_controller_send_superparam(mct_pipeline_t *pipeline,
  mct_stream_t *parm_stream)
{
  boolean ret = TRUE;
  mct_event_super_control_parm_t *super_event = NULL;
  uint32_t i, queue_length =0;

  if (!pipeline || !parm_stream) {
    CLOGE(CAM_MCT_MODULE, "NULL ptr detected: pipeline [%p], parm_stream [%p]",
      pipeline, parm_stream);
    return FALSE;
  }

  if(pipeline->delay_dequeue_count)
  {
    CLOGH(CAM_MCT_MODULE, "Bypass super-param settings on this frame");
    pipeline->delay_dequeue_count--;
  } else {
    /* First send out super param settings if any */
    /* if HAL1 send all superparams in queue */
    do {
      pthread_mutex_lock(&pipeline->super_param_q_lock);
      super_event = (mct_event_super_control_parm_t *)
        mct_queue_pop_head(pipeline->super_param_queue);
      queue_length = pipeline->super_param_queue->length;

      pthread_mutex_unlock(&pipeline->super_param_q_lock);
      if(super_event) {
        /* Unblock HAL to send new super params
        when queue becomes empty */
        if (0 == queue_length) {
          if (pipeline->hal_version != CAM_HAL_V1) {
            ret &= mct_controller_notify_hal_internal_event(pipeline,
              CAM_EVENT_TYPE_DAEMON_PULL_REQ);
          }
        }

        /* Book-keep frame number for reference */
        if (pipeline->hal_version == CAM_HAL_V3) {
          ret &= mct_stream_map_frame_number(pipeline, super_event,
            parm_stream->current_frame_id);
        }
        /*Apply super param settings to all modules */
        ret &= mct_stream_send_super_event(parm_stream, super_event);

        for (i = 0; i < super_event->num_of_parm_events; i++) {
          if (super_event->parm_events[i].parm_data) {
            free(super_event->parm_events[i].parm_data);
            super_event->parm_events[i].parm_data = NULL;
          }
        }
        if (super_event->parm_events) {
          free(super_event->parm_events);
          super_event->parm_events = NULL;
        }
        free(super_event);
        super_event = NULL;
      } else {
        break;
      }
      if(CAM_HAL_V3 == pipeline->hal_version) {
        break;
      }
    } while (1);
  }
  return ret;
}

/** mct_controller_handle_SOF_proc:
 *    @pipeline: structure of mct_controller_t
 *    @bus_msg: Incoming bus_msg to process
 * Media Controller SOF processing routine
 **/
static boolean mct_controller_handle_SOF_proc(
  mct_pipeline_t *pipeline, mct_bus_msg_t *bus_msg)
{
  boolean ret = TRUE;
  mct_stream_t *parm_stream = NULL;
  mct_pipeline_get_stream_info_t info;
  mct_event_control_t event_data;
  mct_event_t cmd_event;
  mct_bus_msg_isp_sof_t *isp_sof_bus_msg = NULL;
  mct_stream_t *meta_stream;
  metadata_buffer_t *metadata_buffer = NULL;

  CAMSCOPE_UPDATE_FLAGS(CAMSCOPE_SECTION_MCT, kpi_camscope_flags);

  if (!pipeline || !bus_msg) {
    CLOGE(CAM_MCT_MODULE, "Invalid ptr detected: pipeline: %p, bus_msg: %p",
      pipeline, bus_msg);
    return FALSE;
  }
  isp_sof_bus_msg = bus_msg->msg;
  /* Obtain session stream to issue commands on */
  if (MCT_PIPELINE_NUM_CHILDREN(pipeline) > 0) {
    info.check_type   = CHECK_INDEX;
    info.stream_index  = MCT_SESSION_STREAM_ID;
    parm_stream = mct_pipeline_get_stream(pipeline, &info);
    if (!parm_stream) {
      CLOGE(CAM_MCT_MODULE, "No session stream available at SOF");
      return FALSE;
    }
  } else {
    CLOGE(CAM_MCT_MODULE, "No child present for pipeline");
    return FALSE;
  }
  parm_stream->current_frame_id = isp_sof_bus_msg->frame_id;
  /* Validate incoming frame id */
  ret = mct_bus_sof_tracker(pipeline->bus, parm_stream->current_frame_id);
  /* No action currently taken on results of tracker */

  /* Send super-params queued in super_param_queue */
  ATRACE_BEGIN(PROF_MCT_SP);
  ret &= mct_controller_send_superparam(pipeline, parm_stream);
  ATRACE_END();

  /* Send any special events queued in param_queue */
  ATRACE_BEGIN(PROF_MCT_SPL_EVT);
  ret &= mct_pipeline_send_special_event(pipeline, parm_stream);
  ATRACE_END();

  /*Obtain the metadata stream */
  info.check_type   = CHECK_TYPE;
  info.stream_type  = CAM_STREAM_TYPE_METADATA;
  meta_stream = mct_pipeline_get_stream(pipeline, &info);

  if (meta_stream) {
    /* Get the metadata buffer before sending the control */
    /* SOF to other modules */
    ret &= mct_stream_get_metadata_buffer(meta_stream, &metadata_buffer);

    /* If Buffer present fill up the metadata bus_messages */
    /* already in bus_queue */
    if (meta_stream->metadata_stream.get_buf_err == FALSE) {
      ret &= mct_stream_update_metadata_buffer(meta_stream,
             metadata_buffer);
    }
  } else {
    CLOGE(CAM_MCT_MODULE, "Metadata stream not present");
  }


  /*Then send the SOF event down to modules*/
  CAMSCOPE_LOG(CAMSCOPE_SECTION_MCT, CAMSCOPE_ASYNC_BEGIN,
               CAMSCOPE_MCT_SOF, isp_sof_bus_msg->frame_id);
  ATRACE_BEGIN(PROF_MCT_SOF);
  ret &= mct_stream_send_ctrl_sof(parm_stream, isp_sof_bus_msg);
  ATRACE_END();
  CAMSCOPE_LOG(CAMSCOPE_SECTION_MCT, CAMSCOPE_ASYNC_END,
               CAMSCOPE_MCT_SOF, isp_sof_bus_msg->frame_id);

  if (ret == FALSE && meta_stream) {
    if (!mct_stream_put_metadata_buffer(meta_stream)) {
      CLOGE(CAM_MCT_MODULE, "Put_buf metadata to kernel Buf idx =%d failed",
        meta_stream->metadata_stream.current_buf_idx);
    }
  }

  CAMSCOPE_TIME_CONV();

  return ret;
}


/** mct_controller_thread_run:
 *    @data: structure of mct_controller_t
 *
 * Media Controller Thread
 **/
static void* mct_controller_thread_run(void *data)
{
  mct_controller_t  *mct_this;
  mct_process_ret_t  proc_ret;
  mct_serv_msg_t    *msg;


  mct_this = (mct_controller_t *)data;
  mct_this->mct_tid = pthread_self();

  /* signal condition variable */
  pthread_mutex_lock(&mct_this->mctl_thread_started_mutex);
  pthread_cond_signal(&mct_this->mctl_thread_started_cond);
  pthread_mutex_unlock(&mct_this->mctl_thread_started_mutex);

  do {
    pthread_mutex_lock(&mct_this->mctl_mutex);
    if (!mct_this->serv_cmd_q_counter) {
      pthread_cond_wait(&mct_this->mctl_cond, &mct_this->mctl_mutex);
    }
    pthread_mutex_unlock(&mct_this->mctl_mutex);
    /* Received Signal from HAL to process HAL/DS Message */

    while (1) {
      pthread_mutex_lock(&mct_this->serv_msg_q_lock);
      msg = (mct_serv_msg_t *)mct_queue_pop_head(mct_this->serv_cmd_q);
      pthread_mutex_unlock(&mct_this->serv_msg_q_lock);

      if (!msg) {
        break;
      }
      pthread_mutex_lock(&mct_this->mctl_mutex);
      mct_this->serv_cmd_q_counter--;
      pthread_mutex_unlock(&mct_this->mctl_mutex);

      proc_ret = mct_controller_proc_serv_msg_internal(mct_this, msg);
      free(msg);
      msg = NULL;

      if (proc_ret.type == MCT_PROCESS_RET_SERVER_MSG           &&
          proc_ret.u.serv_msg_ret.msg.msg_type == SERV_MSG_HAL  &&
          proc_ret.u.serv_msg_ret.msg.u.hal_msg.id == MSM_CAMERA_DEL_SESSION) {
        goto close_mct;
      }
      /* Based on process result, need to send event to server */
      write(mct_this->serv_fd, &proc_ret, sizeof(mct_process_ret_t));
    }

  } while(1);
close_mct:
  return NULL;
}

/** Name: mct_controller_get_thread_state
 *
 *  Arguments/Fields:
 *    @mct: Pointer to mct_controller object
 *    @state: State of SOF_proc thread
 *
 *  Return:
 *    TRUE on success, FALSE on failure
 *
 *  Description:
 *    Retreives current state of sof_processing thread.
 *
 **/
boolean mct_controller_get_thread_state (mct_controller_t *mct,
  mct_thread_state_type *state)
{
  if (!mct || !state) {
    CLOGE(CAM_MCT_MODULE, "NULL ptr detected!mct: %p, state: %p", mct, state);
    return FALSE;
  }
  pthread_mutex_lock(&mct->mctl_state_mutex);
  *state = mct->bus_thread_state;
  pthread_mutex_unlock(&mct->mctl_state_mutex);

  return TRUE;
}

/** Name: mct_controller_set_thread_state
 *
 *  Arguments/Fields:
 *    @mct: Pointer to mct_controller object
 *    @state: Thread state to be assigned.
 *
 *  Return:
 *    TRUE on success, FALSE on failure
 *
 *  Description:
 *    Sets current state of sof_processing thread.
 *    Additionally signals listeners when thread goes to IDLE state
 *
 **/
boolean mct_controller_set_thread_state (mct_controller_t *mct,
  mct_thread_state_type state)
{
  if (!mct) {
    CLOGE(CAM_MCT_MODULE, "NULL controller object!");
    return FALSE;
  } else {
    pthread_mutex_lock(&mct->mctl_state_mutex);
    mct->bus_thread_state = state;
    if (MCT_THREAD_STATE_IDLE == mct->bus_thread_state) {
      /* Signal the end of processing current SOF */
      pthread_cond_signal(&mct->mctl_state_cond);
    }
    pthread_mutex_unlock(&mct->mctl_state_mutex);
  }
  return TRUE;
}

/** mct_bus_handler_thread_run:
 *    @data: structure of mct_controller_t
 *
 * Media Controller Bus message processing Thread
 **/
static void* mct_bus_handler_thread_run(void *data)
{
  mct_process_ret_t  proc_ret;
  mct_bus_msg_t     *bus_msg;
  mct_controller_t* mct_obj = (mct_controller_t *)data;
  if(!mct_obj) {
    CLOGE(CAM_MCT_MODULE, "Invalid pointer for mct object");
    return NULL;
  }
  /* This worker thread is invoked at frame SOF.
  *  It offloads processing of HAL super_param settings
  *  and SOF processing from the main mct thread.
  */
  do {
    pthread_mutex_lock(&mct_obj->mctl_bus_handle_mutex);
    if(!mct_obj->pipeline->bus->priority_queue->length) {
      pthread_cond_wait(&mct_obj->mctl_bus_handle_cond,
        &mct_obj->mctl_bus_handle_mutex);
    }
    pthread_mutex_unlock(&mct_obj->mctl_bus_handle_mutex);

    /* Received Signal from Pipeline Bus */
    while (1) {
      pthread_mutex_lock(&mct_obj->pipeline->bus->priority_q_lock);
      bus_msg = (mct_bus_msg_t *)mct_queue_pop_head
        (mct_obj->pipeline->bus->priority_queue);
      pthread_mutex_unlock(&mct_obj->pipeline->bus->priority_q_lock);

      if (!bus_msg) {
        break;
      }
      if (bus_msg->type == MCT_BUS_MSG_CLOSE_CAM) {
        goto thread_exit;
      }
      proc_ret = mct_controller_proc_bus_msg_internal(mct_obj, bus_msg);

      if (bus_msg->msg) {
        free(bus_msg->msg);
        bus_msg->msg = NULL;
      }

      if (bus_msg) {
        free(bus_msg);
        bus_msg = NULL;
      }

      if (proc_ret.type == MCT_PROCESS_RET_ERROR_MSG ||
          proc_ret.type == MCT_PROCESS_DUMP_INFO ||
          (proc_ret.type == MCT_PROCESS_RET_BUS_MSG &&
          proc_ret.u.bus_msg_ret.msg_type == MCT_BUS_MSG_CAC_STAGE_DONE) ||
          (proc_ret.type == MCT_PROCESS_RET_BUS_MSG &&
          proc_ret.u.bus_msg_ret.msg_type == MCT_BUS_MSG_EZTUNE_JPEG) ||
          (proc_ret.type == MCT_PROCESS_RET_BUS_MSG &&
          proc_ret.u.bus_msg_ret.msg_type == MCT_BUS_MSG_EZTUNE_RAW)) {
        write(mct_obj->serv_fd, &proc_ret, sizeof(mct_process_ret_t));
      }
    }
  } while(1);

thread_exit:
  CLOGH(CAM_MCT_MODULE, "Force Exit");
  if (bus_msg->msg) {
    free(bus_msg->msg);
    bus_msg->msg = NULL;
  }

  if (bus_msg) {
    free(bus_msg);
    bus_msg = NULL;
  }
  return NULL;
}
