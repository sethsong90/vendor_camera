 /* isp_parser.c
 *
 * Copyright (c) 2012-2014 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

/* std headers */
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>

/* kernel headers */
#include "media/msmb_isp.h"

/* mctl headers */
#include "mct_event_stats.h"
#include "media_controller.h"
#include "mct_list.h"

/* isp headers */
#include "isp_module.h"
#include "isp_parser_thread.h"
#include "isp_trigger_thread.h"
#include "isp_log.h"
#include "isp_util.h"
#include "isp_algo.h"
#include "isp_stats_buf_mgr.h"

/** isp_parser_thread_enqueue_event:
 *
 *  @trigger_update_params: trigger update params handle
 *  @data: data to be returned
 *
 *  Push new event to in stats queue
 *  Mutex Lock protection is outside this function.
 *
 *  Return TRUE on success and FALSE on failure
 **/
static boolean isp_parser_thread_enqueue_event(
  isp_parser_params_t *parser_params, void *data)
{
  boolean ret = TRUE;

  if (!parser_params || !parser_params->in_stats_queue) {
    ISP_ERR("failed: %p", parser_params);
    return FALSE;
  }

  mct_queue_push_tail(parser_params->in_stats_queue, (void *)data);
  return ret;
}


/** isp_parser_thread_dequeue_event:
 *
 *  @trigger_update_params: trigger update params handle
 *  @data: data to be returned
 *
 *  If queue is not empty, pop message from queue
 *  Mutex Lock protection is inside this function.
 *  Return TRUE on success and FALSE on failure
 **/
static boolean isp_parser_thread_dequeue_event(
  isp_parser_params_t *parser_params, void **data)
{
  boolean ret = TRUE;

  if (!parser_params || !parser_params->in_stats_queue) {
    ISP_ERR("failed: %p", parser_params);
    return FALSE;
  }

  PTHREAD_MUTEX_LOCK(&parser_params->mutex);
  if (MCT_QUEUE_IS_EMPTY(parser_params->in_stats_queue) == FALSE) {
    *data = mct_queue_pop_head(parser_params->in_stats_queue);
    ret = TRUE;
  } else {
    ret = FALSE;
  }
  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);
  return ret;
} /* isp_trigger_thread_get_event_from_queue */

/** isp_parser_thread_send_divert_ack:
 *
 *  @module: mct module handle
 *  @stats_notify_event: stats notify event
 *
 *  Send upstream event for notify ack
 *
 *  Return TRUE on success and FALSE on failure
 **/
static boolean isp_parser_thread_send_divert_ack(mct_module_t *module,
  mct_event_t *stats_notify_event)
{
  boolean ret = TRUE;

  if (!stats_notify_event) {
    return FALSE;
  }

  stats_notify_event->direction = MCT_EVENT_UPSTREAM;
  stats_notify_event->u.module_event.type =
    MCT_EVENT_MODULE_RAW_STATS_DIVERT_ACK;
  ret = isp_util_forward_event_from_module(module, stats_notify_event);
  if (ret == FALSE) {
    ISP_ERR("failed: isp_util_forward_event_from_module");
  }
  if (stats_notify_event->u.module_event.module_event_data) {
    free(stats_notify_event->u.module_event.module_event_data);
    stats_notify_event->u.module_event.module_event_data = NULL;
  }
  if (stats_notify_event) {
    free(stats_notify_event);
    stats_notify_event = NULL;
  }
  return TRUE;
}

/** isp_parser_thread_process:
 *
 *  @module: mct module handle
 *  @isp_resource: isp resource handle
 *  @session_param: session param
 *
 *  Handle parser update
 *
 *  Return TRUE on success and FALSE on failure
 **/
static boolean isp_parser_thread_process(mct_module_t *module,
  isp_resource_t *isp_resource, isp_session_param_t *session_param)
{
  boolean                     ret = TRUE;
  isp_hw_id_t                 hw_id0 = 0, hw_id1 = 0;
  mct_event_t                *stats_notify_event;
  isp_parser_params_t        *parser_params = NULL;
  mct_event_t                 stats_data_event;
  mct_event_t                 algo_output_event;
  iface_raw_stats_buf_info_t *raw_stats_info = NULL;
  uint32_t                    i = 0;
  mct_event_stats_isp_t      *stats_data = NULL;
  isp_saved_stats_params_t    stats_params[ISP_HW_MAX];
  isp_algo_params_t           algo_parm[ISP_HW_MAX];
  isp_hw_id_t                 hw_id = 0;
  boolean                     is_ack_done = FALSE;

  if (!module || !isp_resource || !session_param) {
    ISP_ERR("failed: %p %p %p", module, isp_resource, session_param);
    return FALSE;
  }

  parser_params = &session_param->parser_params;

  memset(&stats_data_event, 0, sizeof(stats_data_event));
  memset(&algo_output_event, 0, sizeof(algo_output_event));

  PTHREAD_MUTEX_LOCK(&parser_params->mutex);

  stats_notify_event = parser_params->stats_notify_event;
  parser_params->stats_notify_event = NULL;

  for (hw_id = 0; hw_id < ISP_HW_MAX; hw_id++) {
    stats_params[hw_id] = parser_params->stats_params[hw_id];
    algo_parm[hw_id] = parser_params->algo_parm[hw_id];
  }
  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);

  if (stats_notify_event->u.module_event.type !=
    MCT_EVENT_MODULE_RAW_STATS_DIVERT) {
    ISP_ERR("failed: invalid event type %d expected %d",
      stats_notify_event->u.module_event.type,
      MCT_EVENT_MODULE_RAW_STATS_DIVERT);
    ret = FALSE;
    goto ERROR;
  }

  raw_stats_info = (iface_raw_stats_buf_info_t *)
    stats_notify_event->u.module_event.module_event_data;
  if (!raw_stats_info) {
    ISP_ERR("failed: raw_stats_info %p", raw_stats_info);
    ret = FALSE;
    goto ERROR;
  }

  ISP_DBG("raw_stats_info->stats_mask %x frame_id %d",
    raw_stats_info->stats_mask,
    raw_stats_info->frame_id);

  if (!raw_stats_info->stats_mask) {
    ISP_ERR("failed: invalid stats_mask %x", raw_stats_info->stats_mask);
    ret = FALSE;
    goto ERROR;
  }

  if (session_param->state == ISP_STATE_IDLE) {
    ret = TRUE;
    goto ERROR;
  }

  /* Get buffers from buffer maanager */
  if (session_param->offline_num_isp > 0 &&
    raw_stats_info->hw_id == session_param->offline_hw_id[0]) {
    stats_data = isp_stats_buf_mgr_get_buf(
       &parser_params->buf_mgr[ISP_STREAMING_OFFLINE],
      raw_stats_info->stats_mask);
    if (!stats_data) {
      ISP_ERR("failed: get buf failed");
      ret = FALSE;
      goto ERROR;
    }
    stats_data->isp_streaming_type = ISP_STREAMING_OFFLINE;
  } else {
    stats_data = isp_stats_buf_mgr_get_buf(
       &parser_params->buf_mgr[ISP_STREAMING_ONLINE],
      raw_stats_info->stats_mask);
    if (!stats_data) {
      ISP_ERR("failed: get buf failed");
      ret = FALSE;
      goto ERROR;
    }
    stats_data->isp_streaming_type = ISP_STREAMING_ONLINE;
  }

  for (i = 0; i < MSM_ISP_STATS_MAX; i++) {
    if (raw_stats_info->stats_mask & (1 << i)) {
      if (!stats_data->stats_data[i].stats_buf) {
        ISP_ERR("failed: stats_buf NULL for stats_type %d", i);
        ret = FALSE;
        goto ERROR;
      }
      if (session_param->num_isp == 1) {
        hw_id0 = session_param->hw_id[0];
        ret = isp_resource_pipeline_parse(isp_resource, hw_id0, i,
          raw_stats_info->raw_stats_buf_len[i],
          raw_stats_info->raw_stats_buffer[i], stats_data,
          &stats_params[hw_id0], NULL, &parser_params->parser_session_params);
      } else {
        hw_id0 = session_param->hw_id[0];
        hw_id1 = session_param->hw_id[1];
        ret = isp_resource_pipeline_parse(isp_resource, hw_id0, i,
          raw_stats_info->raw_stats_buf_len[i],
          raw_stats_info->raw_stats_buffer[i], stats_data,
          &stats_params[hw_id0],&stats_params[hw_id1],
          &parser_params->parser_session_params);
      }
      if (ret == FALSE) {
        ISP_ERR("failed: isp_resource_pipepine_parse stats type %d", i);
        goto ERROR;
      }
    }
  }

  /* Post BHist stats to mct bus */
  if (raw_stats_info->stats_mask & (1 << MSM_ISP_STATS_BHIST)) {
    q3a_bhist_stats_t *bhist_stats =
      stats_data->stats_data[MSM_ISP_STATS_BHIST].stats_buf;

    if (bhist_stats) {
      int i;
      cam_hist_stats_t hist;
      mct_bus_msg_t bus_msg;

      memset(&bus_msg, 0, sizeof(bus_msg));
      bus_msg.type = MCT_BUS_MSG_HIST_STATS_INFO;
      bus_msg.msg = (void *)&hist;
      bus_msg.size = sizeof(cam_hist_stats_t);
      bus_msg.sessionid = session_param->session_id;

      hist.type = CAM_HISTOGRAM_TYPE_BAYER;

      memset(&hist.bayer_stats, 0, sizeof(hist.bayer_stats));

      if (CAM_HISTOGRAM_STATS_SIZE == bhist_stats->num_bins) {
        memcpy(&hist.bayer_stats.r_stats.hist_buf, &bhist_stats->bayer_r_hist,
          sizeof(hist.bayer_stats.r_stats.hist_buf));
        memcpy(&hist.bayer_stats.b_stats.hist_buf, &bhist_stats->bayer_b_hist,
          sizeof(hist.bayer_stats.b_stats.hist_buf));
        memcpy(&hist.bayer_stats.gr_stats.hist_buf, &bhist_stats->bayer_gr_hist,
          sizeof(hist.bayer_stats.gr_stats.hist_buf));
        memcpy(&hist.bayer_stats.gb_stats.hist_buf, &bhist_stats->bayer_gb_hist,
          sizeof(hist.bayer_stats.gb_stats.hist_buf));
      } else if (bhist_stats->num_bins > CAM_HISTOGRAM_STATS_SIZE &&
                 bhist_stats->num_bins % CAM_HISTOGRAM_STATS_SIZE == 0) {
        /* Stats from HW have more bins and is a multiple of output bin size */
        /* scale bins by collapsing */
        uint32_t factor = bhist_stats->num_bins / CAM_HISTOGRAM_STATS_SIZE;
        uint32_t i, j;
        for (i = 0; i < CAM_HISTOGRAM_STATS_SIZE; i++) {
          for (j = 0; j < factor; j++) {
            hist.bayer_stats.r_stats.hist_buf[i]  +=
              bhist_stats->bayer_r_hist[i * factor + j];
            hist.bayer_stats.b_stats.hist_buf[i]  +=
              bhist_stats->bayer_b_hist[i * factor + j];
            hist.bayer_stats.gr_stats.hist_buf[i] +=
              bhist_stats->bayer_gr_hist[i * factor + j];
            hist.bayer_stats.gb_stats.hist_buf[i] +=
              bhist_stats->bayer_gb_hist[i * factor + j];
          }
        }
      } else {
        ISP_ERR("Size mismatch error bhist_stats->num_bins %d",
          bhist_stats->num_bins);
        ret = FALSE;
        goto ERROR;
      }

      if (TRUE != isp_util_send_metadata_entry(module, &bus_msg,
        raw_stats_info->frame_id)) {
        ISP_ERR("session_id = %d error", session_param->session_id);
      }
    }
  }

  /* Call stats notify event to 3A */
  stats_data->frame_id = raw_stats_info->frame_id;
  stats_data->timestamp = raw_stats_info->timestamp;

  /*send event to 3A*/
  memset(&stats_data_event, 0, sizeof(stats_data_event));

  stats_data_event.direction = MCT_EVENT_DOWNSTREAM;
  stats_data_event.type = MCT_EVENT_MODULE_EVENT;
  stats_data_event.identity = stats_notify_event->identity;
  stats_data_event.u.module_event.type = MCT_EVENT_MODULE_STATS_DATA;
  stats_data_event.u.module_event.module_event_data = (void *)stats_data;

  /* Set ack_flag to TRUE, if 3A need to consume this buffer, it will set
   * to FALSE
   */
  stats_data->ack_flag = TRUE;

  ret =  isp_util_forward_event_downstream_to_type(module, &stats_data_event,
    MCT_PORT_CAPS_STATS);
  if (ret == FALSE) {
    ISP_ERR("failed: isp_util_forward_event_downstream_to_type");
  }

  /* Call stats notify ack to iface to enqueue stats buffer back to kernel*/
  ret = isp_parser_thread_send_divert_ack(module, stats_notify_event);
  if (ret == FALSE) {
    ISP_ERR("failed: isp_parser_thread_send_divert_ack");
  } else {
    is_ack_done = TRUE;
  }

  /* Run ISP internel algo, ex: LA, LTM, tintless, etc*/
  ret = isp_algo_execute_internal_algo(module,
    session_param, stats_data, &algo_parm[session_param->hw_id[0]]);
  if (ret == FALSE) {
    ISP_DBG("failed: isp_parser_thread_execute_internal_algo");
  }

  if (stats_data->ack_flag == TRUE) {
    /* Return buffers to buffer maanager */
    ret = isp_stats_buf_mgr_put_buf(
       &parser_params->buf_mgr[stats_data->isp_streaming_type], stats_data);
    if (ret == FALSE) {
      ISP_ERR("failed: pet buf failed");
    }
  }

  return ret;

ERROR:
  if (stats_data) {
    /* Return buffers to buffer maanager */
    ret = isp_stats_buf_mgr_put_buf(
       &parser_params->buf_mgr[stats_data->isp_streaming_type], stats_data);
    if (ret == FALSE) {
      ISP_ERR("failed: pet buf failed");
    }
  }

  if (is_ack_done == FALSE) {
    /* Call stats notify ack to iface to enqueue stats buffer back to kernel*/
    ret = isp_parser_thread_send_divert_ack(module, stats_notify_event);
    if (ret == FALSE) {
      ISP_ERR("failed: isp_parser_thread_send_divert_ack");
    }
  }
  return FALSE;
} /* isp_parser_thread_process */

/** isp_parser_thread_free_params:
 *
 *  @module: module handle
 *  @parser_params: parser param
 *
 *  Send ack for stats notify event
 *
 *  Return TRUE on success and FALSE on failure
 **/
static boolean isp_parser_thread_free_params(mct_module_t *module,
  isp_parser_params_t *parser_params)
{
  isp_hw_id_t hw_id = 0;

  if (!module || !parser_params) {
    ISP_ERR("failed: module %p parser_params %p", module, parser_params);
    return FALSE;
  }

  PTHREAD_MUTEX_LOCK(&parser_params->mutex);
  isp_parser_thread_send_divert_ack(module,
    parser_params->stats_notify_event);
  parser_params->stats_notify_event = NULL;

  /* Clear stats params */
  for (hw_id = 0; hw_id < ISP_HW_MAX; hw_id++) {
    memset(&parser_params->stats_params[hw_id], 0,
      sizeof(isp_saved_stats_params_t));
  }
  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);

  return TRUE;
}

/** isp_parser_thread_func:
 *
 *  @data: handle to session_param
 *
 *  ISP main thread handler
 *
 *  Returns NULL
 **/
static void *isp_parser_thread_func(void *data)
{
  boolean                      ret = TRUE;
  isp_session_param_t         *session_param;
  struct pollfd                pollfds;
  int32_t                      num_fds = 1, ready = 0, i = 0, read_bytes = 0;
  isp_parser_thread_event_t    event;
  isp_parser_params_t         *parser_params = NULL;
  boolean                      exit_thread = FALSE;
  isp_parser_thread_priv_t    *thread_priv = NULL;
  mct_module_t                *module = NULL;
  isp_resource_t              *isp_resource = NULL;
  void                        *pending_stats_event = NULL;

  if (!data) {
    ISP_ERR("failed: data %p", data);
    return NULL;
  }

  ISP_HIGH("isp_new_thread parser thread start");
  thread_priv = (isp_parser_thread_priv_t *)data;
  module = thread_priv->module;
  isp_resource = thread_priv->isp_resource;
  session_param = thread_priv->session_param;
  if (!module || !isp_resource || !session_param) {
    ISP_ERR("failed: %p %p %p", module, isp_resource, session_param);
    return NULL;
  }

  parser_params = &session_param->parser_params;
  PTHREAD_MUTEX_LOCK(&parser_params->mutex);
  parser_params->is_thread_alive = TRUE;
  pthread_cond_signal(&parser_params->cond);
  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);

  while (exit_thread == FALSE) {
    pollfds.fd = parser_params->pipe_fd[READ_FD];
    pollfds.events = POLLIN|POLLPRI;
    ready = poll(&pollfds, (nfds_t)num_fds, -1);
    if (ready > 0) {
      if (pollfds.revents & (POLLIN|POLLPRI)) {
        read_bytes = read(pollfds.fd, &event,
          sizeof(isp_parser_thread_event_t));
        if ((read_bytes < 0) ||
            (read_bytes != sizeof(isp_parser_thread_event_t))) {
          ISP_ERR("failed: read_bytes %d", read_bytes);
          continue;
        }
        switch (event.type) {
        case ISP_PARSER_EVENT_PROCESS:
          while (1) {
            pending_stats_event = NULL;
            ret = isp_parser_thread_dequeue_event(parser_params,
              &pending_stats_event);
            if (ret == FALSE) {
              ISP_DBG("Queue is empty");
              break;
            }
            if (pending_stats_event != NULL) {
              PTHREAD_MUTEX_LOCK(&parser_params->mutex);
              parser_params->stats_notify_event =
                (mct_event_t *)pending_stats_event;
              PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);
              ret = isp_parser_thread_process(module,
                isp_resource, session_param);
              if (ret == FALSE) {
                ISP_ERR("failed: isp_parser_process");
                break;
              }
            }
          }
          break;
        case ISP_PARSER_THREAD_EVENT_FREE_QUEUE:
          ISP_HIGH("Free stats parsing input queue");
          while (1) {
            pending_stats_event = NULL;
            ret = isp_parser_thread_dequeue_event(parser_params,
              &pending_stats_event);
            if (ret == FALSE) {
              ISP_DBG("Queue is empty");
              break;
            }
            if (pending_stats_event != NULL) {
              PTHREAD_MUTEX_LOCK(&parser_params->mutex);
              parser_params->stats_notify_event =
                (mct_event_t *)pending_stats_event;
              PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);
              ret = isp_parser_thread_free_params(module,
                &session_param->parser_params);
              if (ret == FALSE) {
                ISP_ERR("failed: isp_parser_thread_free_params");
                break;
              }
            }
          }
          /* Unblock caller as parser thread queue is cleared */
          PTHREAD_MUTEX_LOCK(&parser_params->mutex);
          pthread_cond_signal(&parser_params->cond);
          PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);
          ISP_HIGH("Free stats parsing input queue done");
          break;
        case ISP_PARSER_EVENT_ABORT_THREAD:
          exit_thread = TRUE;
          break;
        default:
          ISP_ERR("invalid event type %d", event.type);
          break;
        }
      }
    } else if (ready <= 0) {
      ISP_ERR("failed: exit thread");
      break;
    }
  }

  return NULL;
}

/** isp_parser_thread_post_message:
 *
 *  @parser_params: parser params
 *  @type: message type to be posted
 *
 *  Post event to session thread
 *
 *  Return TRUE on success and FALSE on failure
 **/
boolean isp_parser_thread_post_message(
  isp_parser_params_t *parser_params, isp_parser_event_type_t type)
{
  int32_t                      rc = 0;
  isp_parser_thread_event_t    message;

  if (!parser_params || (type >= ISP_PARSER_EVENT_MAX)) {
    ISP_ERR("failed: %p type %d", parser_params, type);
    return FALSE;
  }

  memset(&message, 0, sizeof(message));
  message.type = type;
  rc = write(parser_params->pipe_fd[WRITE_FD], &message, sizeof(message));
  if(rc < 0) {
    ISP_ERR("failed: rc %d", rc);
    return FALSE;
  }

  return TRUE;
} /* isp_parser_thread_post_message */

/** isp_parser_thread_save_stats_nofity_event:
 *
 *  @module: mct module handle
 *  @session_param: session param handle
 *  @event: event to be stored
 *
 *  If there is pending event to be handled, return it and
 *  store current in parser params
 *
 *  Return TRUE on success and FALSE on failure
 **/
boolean isp_parser_thread_save_stats_nofity_event(mct_module_t *module,
  isp_session_param_t *session_param, mct_event_t *event)
{
  boolean                     ret = TRUE;
  isp_parser_params_t        *parser_params = NULL;
  mct_event_t                *stats_notify_event;
  iface_raw_stats_buf_info_t *copy_raw_stats_info = NULL;
  iface_raw_stats_buf_info_t *raw_stats_info = NULL;
  isp_hw_id_t                 hw_id = 0;
  mct_event_t                *copy_event = NULL;

  RETURN_IF_NULL(module);
  RETURN_IF_NULL(session_param);
  RETURN_IF_NULL(event);

  parser_params = &session_param->parser_params;
  PTHREAD_MUTEX_LOCK(&parser_params->mutex);
  raw_stats_info = event->u.module_event.module_event_data;
  GOTO_ERROR_IF_NULL(raw_stats_info);

  copy_raw_stats_info =
    (iface_raw_stats_buf_info_t *)malloc(sizeof(*copy_raw_stats_info));
  GOTO_ERROR_IF_NULL(copy_raw_stats_info);
  memset(copy_raw_stats_info, 0, sizeof(*copy_raw_stats_info));

  *copy_raw_stats_info = *raw_stats_info;

  copy_event = (mct_event_t *)malloc(sizeof(*copy_event));
  GOTO_ERROR_IF_NULL(copy_event);
  memset(copy_event, 0, sizeof(*copy_event));

  /* Deep copy of incoming stats event */
  *copy_event = *event;
  copy_event->u.module_event.module_event_data =
    copy_raw_stats_info;

  ret = isp_parser_thread_enqueue_event(parser_params, (void *)copy_event);
  GOTO_ERROR_IF_FALSE(ret);

  ret = isp_parser_thread_post_message(parser_params, ISP_PARSER_EVENT_PROCESS);
  GOTO_ERROR_IF_FALSE(ret);

  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);
  return ret;

error:
  if (copy_raw_stats_info)
    free(copy_raw_stats_info);
  if (copy_event)
    free(copy_event);
  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);
  return FALSE;
} /* isp_parser_thread_save_stats_nofity_event */

/** isp_parser_thread_create:
 *
 *  @session_param: isp session param handle
 *
 *  Create new ISP thread
 *
 *  Returns TRUE on success and FALSE on failure
 **/
boolean isp_parser_thread_create(mct_module_t *module,
  isp_resource_t *isp_resource, isp_session_param_t *session_param)
{
  int32_t                      rc = 0;
  boolean                      ret = TRUE;
  isp_parser_params_t         *parser_params = NULL;
  isp_parser_thread_priv_t     thread_priv;

  if (!module || !isp_resource || !session_param) {
    ISP_ERR("failed: %p %p %p", module, isp_resource, session_param);
    return FALSE;
  }

  parser_params = &session_param->parser_params;
  /* Create PIPE to communicate with isp thread */
  rc = pipe(parser_params->pipe_fd);
  if(rc < 0) {
    ISP_ERR("pipe() failed");
    return FALSE;
  }

  /* Create input queue for raw stats*/
  parser_params->in_stats_queue =
    (mct_queue_t *)malloc(sizeof(*parser_params->in_stats_queue));
  if (!parser_params->in_stats_queue) {
    ISP_ERR("failed: no free memory for stats queue");
    ret = FALSE;
    goto ERROR_MALLOC;
  }
  memset(parser_params->in_stats_queue, 0,
    sizeof(*parser_params->in_stats_queue));
  mct_queue_init(parser_params->in_stats_queue);

  thread_priv.module = module;
  thread_priv.isp_resource = isp_resource;
  thread_priv.session_param = session_param;

  PTHREAD_MUTEX_LOCK(&parser_params->mutex);
  parser_params->is_thread_alive = FALSE;
  rc = pthread_create(&parser_params->parser_thread, NULL,
    isp_parser_thread_func, &thread_priv);
  pthread_setname_np(parser_params->parser_thread, "CAM_isp_parser");
  if(rc < 0) {
    ISP_ERR("pthread_create() failed rc= %d", rc);
    ret = FALSE;
    goto ERROR_THREAD;
  }

  while(parser_params->is_thread_alive == FALSE) {
    pthread_cond_wait(&parser_params->cond, &parser_params->mutex);
  }
  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);

  return ret;
ERROR_THREAD:
  mct_queue_free(parser_params->in_stats_queue);
  parser_params->in_stats_queue = NULL;
ERROR_MALLOC:
  close(parser_params->pipe_fd[READ_FD]);
  close(parser_params->pipe_fd[WRITE_FD]);
  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);
  return ret;
}

/** isp_parser_thread_join:
 *
 *  @session_param: ISP session param
 *
 *  Join ISP thread
 *
 *  Returns: void
 **/
void isp_parser_thread_join(isp_session_param_t *session_param)
{
  boolean                 ret = TRUE;
  isp_parser_params_t *parser_params = NULL;

  if (!session_param) {
    ISP_ERR("failed: session_param %p", session_param);
    return;
  }

  parser_params = &session_param->parser_params;

  PTHREAD_MUTEX_LOCK(&parser_params->mutex);
  parser_params->is_thread_alive = FALSE;
  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);

  isp_parser_thread_post_message(parser_params,
    ISP_PARSER_EVENT_ABORT_THREAD);

  /* Join session thread */
  pthread_join(parser_params->parser_thread, NULL);

  PTHREAD_MUTEX_LOCK(&parser_params->mutex);
  if (parser_params->in_stats_queue) {
    mct_queue_free(parser_params->in_stats_queue);
    parser_params->in_stats_queue = NULL;
  }
  ISP_HIGH("isp stats input queue deleted");
  PTHREAD_MUTEX_UNLOCK(&parser_params->mutex);

  close(parser_params->pipe_fd[READ_FD]);
  close(parser_params->pipe_fd[WRITE_FD]);
}
