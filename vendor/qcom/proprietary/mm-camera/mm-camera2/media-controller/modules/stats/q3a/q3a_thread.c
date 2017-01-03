/* q3a_thread.c
 *
 * Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#include <pthread.h>
#include "mct_queue.h"
#include "q3a_thread.h"
#include "camera_dbg.h"


/** q3a_thread_aecawb_free_msg
 *    @msg_pp: Address to the pointer of the msg to free
 *
 *  Free the msg and set it to NULL.
 *  This function free all structures related the the msg, including non-flat
 *  structures that have been allocated by the caller.
 *
 *  Return void
 **/
void q3a_thread_aecawb_free_msg(q3a_thread_aecawb_msg_t **msg_pp)
{
  q3a_thread_aecawb_msg_t *msg = *msg_pp;
  if (NULL == msg) {
    return;
  }

  /* Stats messages are not synced, so we have to free the payload here */
  switch (msg->type) {
    case MSG_AEC_STATS:
    case MSG_BG_AEC_STATS:
    case MSG_BG_AWB_STATS:
    case MSG_AWB_STATS:
    case MSG_BE_AEC_STATS:
    case MSG_HDR_BE_AEC_STATS: {
      /* enq-msg has struct of pointers to stats buffer. Free up the memory
       * allocated for struct of pointers.
       * enq-msg carrying this pointer's struct will be deallocated just before
       * exiting this function
       * No need to free the actual stats buffers allocated by ISP. That will be
       * handled in circular_stats_data_ack call */
      if (msg->u.stats) {
        free(msg->u.stats);
        msg->u.stats = NULL;
      }
    }
      break;
    case MSG_AEC_STATS_HDR: {
      /* Allocated locally freeing buffer */
      if (msg->u.stats && msg->u.stats->yuv_stats.p_q3a_aec_stats) {
        free(msg->u.stats->yuv_stats.p_q3a_aec_stats);
        msg->u.stats->yuv_stats.p_q3a_aec_stats = NULL;
      }
      if (msg->u.stats) {
        free(msg->u.stats);
        msg->u.stats = NULL;
      }
    }
      break;
    default:
      break;
  }

  /* Freeing AEC msg */
  if (msg->type == MSG_AEC_SET) {
    switch (msg->u.aec_set_parm.type) {
      case AEC_SET_PARM_CUSTOM_EVT_MOD:
      case AEC_SET_PARM_CUSTOM_EVT_CTRL:
      case AEC_SET_PARM_CUSTOM_EVT_HAL: {
        if (msg->u.aec_set_parm.u.aec_custom_data.data) {
          free(msg->u.aec_set_parm.u.aec_custom_data.data);
        }
      }
        break;
      case AEC_SET_PARAM_AWB_PARM: {
        if (msg->u.aec_set_parm.u.awb_param.awb_custom_param_update.data) {
          free(msg->u.aec_set_parm.u.awb_param.awb_custom_param_update.data);
        }
      }
        break;
      default:
        break;
    }
  }

  /* Freeing AWB msg */
  if (msg->type == MSG_AWB_SET) {
    switch (msg->u.awb_set_parm.type) {
      case AWB_SET_PARM_CUSTOM_EVT_MOD:
      case AWB_SET_PARM_CUSTOM_EVT_CTRL:
      case AWB_SET_PARM_CUSTOM_EVT_HAL: {
        if (msg->u.awb_set_parm.u.awb_custom_data.data) {
          free(msg->u.awb_set_parm.u.awb_custom_data.data);
        }
      }
        break;
      case AWB_SET_PARAM_AEC_PARM: {
        if (msg->u.awb_set_parm.u.aec_parms.custom_param_awb.data) {
          free(msg->u.awb_set_parm.u.aec_parms.custom_param_awb.data);
        }
      }
        break;
      default:
        break;
    }
  }

  free(msg);
  *msg_pp = NULL;
}

/** q3a_thread_aecawb_init
 *
 *  Initializes the AECAWB thread data and creates the queues.
 *
 *  Return the the AECAWB thread data object, on failure return NULL.
 **/
q3a_thread_aecawb_data_t* q3a_thread_aecawb_init(void)
{
  q3a_thread_aecawb_data_t *aecawb;

  aecawb = malloc(sizeof(q3a_thread_aecawb_data_t));
  if (aecawb == NULL) {
    return NULL;
  }
  memset(aecawb, 0 , sizeof(q3a_thread_aecawb_data_t));

  aecawb->thread_data = malloc(sizeof(q3a_thread_data_t));
  if (aecawb->thread_data == NULL) {
    free(aecawb);
    return NULL;
  }
  memset(aecawb->thread_data, 0 , sizeof(q3a_thread_data_t));

  aecawb->thread_data->msg_q = (mct_queue_t *)mct_queue_new;
  aecawb->thread_data->p_msg_q = (mct_queue_t *)mct_queue_new;

  if (!aecawb->thread_data->msg_q || !aecawb->thread_data->p_msg_q) {
    if(aecawb->thread_data->msg_q) {
      mct_queue_free(aecawb->thread_data->msg_q);
    }
    if(aecawb->thread_data->p_msg_q) {
      mct_queue_free(aecawb->thread_data->p_msg_q);
    }
    free(aecawb->thread_data);
    aecawb->thread_data = NULL;
    free(aecawb);
    aecawb = NULL;
    return NULL;
  }

  pthread_mutex_init(&aecawb->thread_data->msg_q_lock, NULL);
  mct_queue_init(aecawb->thread_data->msg_q);
  mct_queue_init(aecawb->thread_data->p_msg_q);
  sem_init(&aecawb->thread_data->sem_launch, 0, 0);
  pthread_cond_init(&(aecawb->thread_data->thread_cond), NULL);
  pthread_mutex_init(&(aecawb->thread_data->thread_mutex), NULL);
  Q3A_LOW("private->thread_data: %p", aecawb->thread_data);

  return aecawb;
} /* q3a_thread_aecawb_init */

/** q3a_thread_aecawb_deinit
 *    @aecawb_data: The pointer to the aecawb thread data
 *
 *  Deinitializes the AECAWB thread data - frees the queues, destroys the
 *  sync variables and frees the thread data object.
 *
 *  Return void.
 **/
void q3a_thread_aecawb_deinit(q3a_thread_aecawb_data_t *aecawb)
{
  Q3A_LOW("thread_data: %p", aecawb->thread_data);
  pthread_mutex_destroy(&aecawb->thread_data->thread_mutex);
  pthread_cond_destroy(&aecawb->thread_data->thread_cond);
  mct_queue_free(aecawb->thread_data->msg_q);
  mct_queue_free(aecawb->thread_data->p_msg_q);
  pthread_mutex_destroy(&aecawb->thread_data->msg_q_lock);
  sem_destroy(&aecawb->thread_data->sem_launch);
  free(aecawb->thread_data);
  aecawb->thread_data = NULL;
  free(aecawb);
  aecawb = NULL;
} /* q3a_thread_aecawb_deinit */

/** q3a_aecawb_thread_en_q_msg
 *    @aecawb_data: The pointer to the aecawb thread data
 *    @msg:         The message to be put in the queue
 *
 *  Enqueues the sent message into the thread's queue. If the message has the
 *  priority flag set, it will be put in the priority queue. Upon receiving the
 *  MSG_STOP_THREAD type of message, the queue will no longer be active and
 *  no one will be able to enqueue messages in it.
 *
 *  Return TRUE on success, FALSE on failure.
 **/
boolean q3a_aecawb_thread_en_q_msg(void *aecawb_data,
  q3a_thread_aecawb_msg_t *msg)
{
  q3a_thread_data_t *thread_data = (q3a_thread_data_t *)aecawb_data;
  boolean           rc = FALSE;
  boolean           sync_flag_set = FALSE;
  msg_sync_t        msg_sync;

  pthread_mutex_lock(&thread_data->msg_q_lock);
  if (TRUE == msg->sync_flag) {
    sync_flag_set = TRUE;
  }
  /* Verify that thread is active and ISP stats are allow to be process */
  if (thread_data->active &&
      !((msg->type == MSG_BG_AEC_STATS || msg->type == MSG_BG_AWB_STATS ||
        msg->type == MSG_BE_AEC_STATS || msg->type == MSG_HDR_BE_AEC_STATS)
        && thread_data->no_stats_mode)) {
    rc = TRUE;
    Q3A_LOW("type=%d, sync_falg=%d",
      msg->type, sync_flag_set);
    if (TRUE == sync_flag_set) {
      msg->sync_obj = &msg_sync;
      sem_init(&msg_sync.msg_sem, 0, 0);
    }
    Q3A_LOW("lock Q");

    /* Increment reference counter for ISP STATS buffer */
    if (msg->type == MSG_BG_AEC_STATS || msg->type == MSG_BE_AEC_STATS ||
      msg->type == MSG_HDR_BE_AEC_STATS) {
      thread_data->aec_bg_be_stats_cnt++;
    } else if (msg->type == MSG_BG_AWB_STATS) {
      thread_data->awb_bg_stats_cnt++;
    }

    /* If its a priority event, queue to priority queue, else to normal queue */
    if (msg->is_priority) {
      mct_queue_push_tail(thread_data->p_msg_q, msg);
    } else {
      mct_queue_push_tail(thread_data->msg_q, msg);
    }

    if (msg->type == MSG_STOP_THREAD) {
      thread_data->active = 0;
      Q3A_LOW("active is zero");
    }
  }
  pthread_mutex_unlock(&thread_data->msg_q_lock);

  if (rc) {
    pthread_mutex_lock(&thread_data->thread_mutex);
    pthread_cond_signal(&thread_data->thread_cond);
    pthread_mutex_unlock(&thread_data->thread_mutex);

    if (TRUE == sync_flag_set) {
      sem_wait(&msg_sync.msg_sem);
      sem_destroy(&msg_sync.msg_sem);
    }
    Q3A_LOW(" Signalled AEC/AWB thread handler");
  } else {
    Q3A_LOW("AEC/AWB thread_data not active: %d", thread_data->active);
    /* Only free if sync flag is not set, caller must free */
    if (FALSE == sync_flag_set) {
      q3a_thread_aecawb_free_msg(&msg);
    }
  }
  return rc;
} /* q3a_aecawb_thread_en_q_msg */

/** aecawb_thread_handler
 *    @aecawb_data: The pointer to the aecawb thread data
 *
 *  This is the aecawb thread that will run until it receives the STOP message.
 *  While running, it will dequeue messages from the thread's queue and process
 *  them. If there are no messages to process (queue is empty), the thread will
 *  sleep until it gets signaled.
 *
 *  Return NULL
 **/
static void* aecawb_thread_handler(void *aecawb_data)
{
  q3a_thread_aecawb_data_t *aecawb = (q3a_thread_aecawb_data_t *)aecawb_data;
  q3a_thread_aecawb_msg_t  *msg = NULL;
  int                      exit_flag = 0;
  int                      rc;

  aecawb->thread_data->active = 1;
  sem_post(&aecawb->thread_data->sem_launch);

  do {
    pthread_mutex_lock(&aecawb->thread_data->thread_mutex);
    while ((aecawb->thread_data->msg_q->length == 0) &&
      (aecawb->thread_data->p_msg_q->length == 0)) {
      pthread_cond_wait(&aecawb->thread_data->thread_cond,
        &aecawb->thread_data->thread_mutex);
    }
    pthread_mutex_unlock(&aecawb->thread_data->thread_mutex);

    /* Get the message */
    pthread_mutex_lock(&aecawb->thread_data->msg_q_lock);
    /*Pop from priority queue first and if its empty pop from normal queue*/
    msg = (q3a_thread_aecawb_msg_t *)
      mct_queue_pop_head(aecawb->thread_data->p_msg_q);

    if (!msg) {
      msg = (q3a_thread_aecawb_msg_t *)
        mct_queue_pop_head(aecawb->thread_data->msg_q);
    }
    pthread_mutex_unlock(&aecawb->thread_data->msg_q_lock);
    if (!msg) {
      Q3A_ERR(" msg NULL");
      continue;
    }

    /* Flush the queue if it is stopping. Free the enqueued messages and
     * signal the sync message owners to release their resources */
    if(aecawb->thread_data->active == 0) {
      if(msg->type != MSG_STOP_THREAD) {
        if (msg->sync_flag == TRUE) {
          sem_post(&msg->sync_obj->msg_sem);
          /* Don't free msg, the sender will do */
          msg = NULL;
        } else {
          /* ACK the unused the STATS buffer from ISP */
          switch (msg->type) {
            case MSG_BG_AEC_STATS:
            case MSG_BE_AEC_STATS:
            case MSG_HDR_BE_AEC_STATS:
              aecawb->aec_stats_cb(aecawb->aec_port, msg->u.stats);
              break;
            case MSG_BG_AWB_STATS:
              aecawb->awb_stats_cb(aecawb->awb_port, msg->u.stats);
              /* For offline stats processing, we may need to post semaphore
                 so that mediacontroller thread won't block waiting for semaphore.
                 Since sem_post is handled in offline awb callback, we'll call
                 callback with dummy output so that we do sem_post before flushing
                 this message. */
              if (Q3A_STATS_STREAM_OFFLINE == msg->u.stats->isp_stream_type) {
                awb_output_data_t output;
                memset(&output, 0, sizeof(awb_output_data_t));
                output.type = AWB_UPDATE_OFFLINE;
                aecawb->awb_cb(&output, aecawb->awb_port);
              }
              break;
            default:
              break;
          }

          /* Free memory allocated by caller inside the message */
          q3a_thread_aecawb_free_msg(&msg);
        }
        continue;
      }
    }

    /* Process message accordingly */
    Q3A_LOW("wake up type=%d, flag=%d",msg->type, msg->sync_flag);
    switch (msg->type) {
    case MSG_AEC_SET: {
      if(aecawb->aec_obj->set_parameters){
        aecawb->aec_obj->set_parameters(&msg->u.aec_set_parm,
          NULL, aecawb->aec_obj->aec);
      } else {
        Q3A_ERR(" Error: set_parameters is null");
      }
    }
      break;

    case MSG_AEC_GET: {
      if (aecawb->aec_obj->get_parameters) {
        aecawb->aec_obj->get_parameters(&msg->u.aec_get_parm,
          aecawb->aec_obj->aec);
      }
    }
      break;

    case MSG_AEC_STATS: {
        // TODO: shall remove the legacy yuv handling in port
    }
      break;

    case MSG_BE_AEC_STATS:
    case MSG_HDR_BE_AEC_STATS:
    case MSG_AEC_STATS_HDR:
    case MSG_BG_AEC_STATS: {
      /* Send local output copy to avoid reset of custom data
         and map custom parameters data in local output variable */
      aec_output_data_t output;
      memset(&output, 0, sizeof(aec_output_data_t));
      output.aec_custom_param = aecawb->aec_obj->output.aec_custom_param;

      if ((aecawb->thread_data->aec_bg_be_stats_cnt < 3) ||
        (msg->type == MSG_AEC_STATS_HDR)) {
        if (!aecawb->thread_data->no_stats_mode) {
          ATRACE_BEGIN("Camera:AEC");
          rc = aecawb->aec_obj->process(msg->u.stats, aecawb->aec_obj->aec,
            &output);
          ATRACE_END();

          /* Copy back output data */
          output.type = AEC_UPDATE;
          output.aec_custom_param = aecawb->aec_obj->output.aec_custom_param;
          aecawb->aec_obj->output = output;
          aecawb->aec_cb(&(aecawb->aec_obj->output), aecawb->aec_port);
        } else {
          rc = TRUE;
          Q3A_HIGH("  no_stats_mode awb");
        }
      }

      if (msg->type == MSG_BG_AEC_STATS ||
        msg->type == MSG_BE_AEC_STATS ||
        msg->type == MSG_HDR_BE_AEC_STATS) {
        aecawb->aec_stats_cb(aecawb->aec_port, msg->u.stats);
      }

      if (msg->u.stats) {
        switch (msg->type) {
          case MSG_AEC_STATS:
          case MSG_BG_AEC_STATS:
          case MSG_BE_AEC_STATS:
          case MSG_HDR_BE_AEC_STATS:
            /* Stats used by AEC with buffer allocated by ISP */
            if (aecawb->thread_data->aec_bg_be_stats_cnt) {
              pthread_mutex_lock(&aecawb->thread_data->msg_q_lock);
              aecawb->thread_data->aec_bg_be_stats_cnt--;
              pthread_mutex_unlock(&aecawb->thread_data->msg_q_lock);
            }
            break;
          default:
            break;
        }
      }
    }
      break;

    case MSG_AWB_SEND_EVENT: {
      /* Send local output copy to avoid reset of custom data
         and map custom parameters data in local output variable */
      awb_output_data_t output;
      memset(&output, 0, sizeof(awb_output_data_t));
      output.awb_custom_param = aecawb->awb_obj->output.awb_custom_param;

      if(aecawb->awb_obj->awb_ops.set_parameters) {
        /* Query AWB output */
        msg->type = MSG_AWB_SET;
        aecawb->awb_obj->awb_ops.set_parameters(&msg->u.awb_set_parm,
          &output, aecawb->awb_obj->awb);
        /* Copy back output data */
        output.type = AWB_SEND_OUTPUT_EVENT;
        output.awb_custom_param = aecawb->awb_obj->output.awb_custom_param;

        aecawb->awb_cb(&output, aecawb->awb_port);
      }
    }
      break;

    case MSG_AEC_SEND_EVENT: {
      /* Send local output copy to avoid reset of custom data
         and map custom parameters data in local output variable */
      aec_output_data_t output;
      memset(&output, 0, sizeof(aec_output_data_t));
      output.aec_custom_param = aecawb->aec_obj->output.aec_custom_param;

      if (aecawb->aec_obj->set_parameters) {
        /* Query AEC output and sent it out*/
        msg->type = MSG_AEC_SET;
        aecawb->aec_obj->set_parameters(&msg->u.aec_set_parm,
          &output, aecawb->aec_obj->aec);
        /* Copy back output data */
        output.type = AEC_SEND_EVENT;
        output.aec_custom_param = aecawb->aec_obj->output.aec_custom_param;

        aecawb->aec_cb(&output, aecawb->aec_port);
      }
    }
      break;

    case MSG_AWB_SET: {
      if (aecawb->awb_obj->awb_ops.set_parameters) {
        aecawb->awb_obj->awb_ops.set_parameters(&msg->u.awb_set_parm,
          NULL, aecawb->awb_obj->awb);
      }
    }
      break;
    case MSG_AWB_GET: {
      aecawb->awb_obj->awb_ops.get_parameters(&msg->u.awb_get_parm,
        aecawb->awb_obj->awb);
    }
      break;

    case MSG_BG_AWB_STATS: {
      /* Send local output copy to avoid reset of custom data
         and map custom parameters data in local output variable */
      awb_output_data_t output;
      memset(&output, 0, sizeof(awb_output_data_t));
      output.awb_custom_param = aecawb->awb_obj->output.awb_custom_param;

      /* For offline stats processing */
      if (Q3A_STATS_STREAM_OFFLINE == msg->u.stats->isp_stream_type) {
        aecawb->awb_obj->awb_ops.process(
          msg->u.stats, aecawb->awb_obj->awb, &output);
        output.type = AWB_UPDATE_OFFLINE;
      } else {
        if (aecawb->thread_data->awb_bg_stats_cnt < 3) {
          if (!aecawb->thread_data->no_stats_mode) {
            ATRACE_BEGIN("Camera:AWB");
            aecawb->awb_obj->awb_ops.process(
              msg->u.stats, aecawb->awb_obj->awb, &output);
            ATRACE_END();
          } else {
            Q3A_HIGH("no_stats_mode awb");
          }
        }
        /* Copy back output data */
        output.type = AWB_UPDATE;
      }

      output.awb_custom_param = aecawb->awb_obj->output.awb_custom_param;
      aecawb->awb_obj->output = output;

      aecawb->awb_cb(&(aecawb->awb_obj->output), aecawb->awb_port);
      aecawb->awb_stats_cb(aecawb->awb_port, msg->u.stats);
      if (aecawb->thread_data->awb_bg_stats_cnt) {
        pthread_mutex_lock(&aecawb->thread_data->msg_q_lock);
        aecawb->thread_data->awb_bg_stats_cnt--;
        pthread_mutex_unlock(&aecawb->thread_data->msg_q_lock);
      }

    }
      break;

   case MSG_STOP_THREAD: {
     exit_flag = 1;
   }
     break;

    default: {
    }
      break;
    } /* end switch (msg->type) */
    if (msg->sync_flag == TRUE) {
      sem_post(&msg->sync_obj->msg_sem);
      /*don't free msg, the sender will do*/
      msg = NULL;
    } else {
      q3a_thread_aecawb_free_msg(&msg);
    }
  } while (!exit_flag);

  return NULL;
} /* aecawb_thread_handler */

/** q3a_thread_aecawb_start
 *    @aecawb_data: The pointer to the aecawb thread data
 *
 *  Called to create the aecawb thread. It will wait on a semaphore until the
 *  thread is created and running.
 *
 *  Return TRUE
 **/
boolean q3a_thread_aecawb_start(q3a_thread_aecawb_data_t *aecawb_data)
{
  int32_t ret;
  ret = pthread_create(&aecawb_data->thread_data->thread_id, NULL,
    aecawb_thread_handler, aecawb_data);
  if (ret < 0) {
    Q3A_ERR("Failed to create aecawb thread");
    return FALSE;
  }
  pthread_setname_np(aecawb_data->thread_data->thread_id, "CAM_AECAWB");

  aecawb_data->thread_data->aec_bg_be_stats_cnt = 0;
  aecawb_data->thread_data->awb_bg_stats_cnt = 0;
  return TRUE;
} /* q3a_thread_aecawb_start */

/** q3a_thread_aecawb_stop
 *    @aecawb_data: The pointer to the aecawb thread data
 *
 *  Called to stop the aecawb thread. It will send a MSG_STOP_THREAD message to
 *  the queue and will wait for the thread to join if the message is enqueued
 *  successfully.
 *
 *  Return TRUE on success, FALSE on failure.
 **/
boolean q3a_thread_aecawb_stop(q3a_thread_aecawb_data_t *aecawb_data)
{
  boolean                 rc ;
  q3a_thread_aecawb_msg_t *msg;

  // before stopping the thread just wait for thread to stop.
  // This is to avoid race condition.
  sem_wait(&aecawb_data->thread_data->sem_launch);

  msg = malloc(sizeof(q3a_thread_aecawb_msg_t));

  if (msg) {
    Q3A_HIGH("MSG_STOP_THREAD");
    memset(msg, 0, sizeof(q3a_thread_aecawb_msg_t));
    msg->type = MSG_STOP_THREAD;

    aecawb_data->thread_data->aec_bg_be_stats_cnt = 0;
    aecawb_data->thread_data->awb_bg_stats_cnt = 0;
    rc = q3a_aecawb_thread_en_q_msg(aecawb_data->thread_data,msg);

    if (rc) {
      pthread_join(aecawb_data->thread_data->thread_id, NULL);
      Q3A_LOW("pthread_join");
    }
  } else {
    rc = FALSE;
  }
  return rc;
} /* q3a_thread_aecawb_stop */

/** q3a_thread_af_free_msg
 *    @msg_pp: Address to the pointer of the msg to free
 *
 *  Free the msg and set it to NULL.
 *  This function free all structures related the the msg, including non-flat
 *  structures that have been allocated by the caller.
 *
 *  Return void
 **/
void q3a_thread_af_free_msg(q3a_thread_af_msg_t **msg_pp)
{
  q3a_thread_af_msg_t *msg = *msg_pp;
  if (NULL == msg) {
    return;
  }

  /* Stats messages are not synced, so we have to free the payload here */
  switch (msg->type) {
    case MSG_AF_STATS:
    case MSG_BF_STATS: {
      if(msg->u.stats) {
       free(msg->u.stats);
       msg->u.stats = NULL;
      }
    }
      break;
    default:
      break;
  }

  /* Freeing AF msg */
  if (msg->type == MSG_AF_SET) {
    switch (msg->u.af_set_parm.type) {
      case AF_SET_PARM_CUSTOM_EVT_MOD:
      case AF_SET_PARM_CUSTOM_EVT_CTRL:
      case AF_SET_PARM_CUSTOM_EVT_HAL: {
        if (msg->u.af_set_parm.u.af_custom_data.data) {
          free(msg->u.af_set_parm.u.af_custom_data.data);
          msg->u.af_set_parm.u.af_custom_data.data = NULL;
        }
      }
        break;
      case AF_SET_PARAM_UPDATE_AEC_INFO: {
        if (msg->u.af_set_parm.u.aec_info.custom_param_af.data) {
          free(msg->u.af_set_parm.u.aec_info.custom_param_af.data);
          msg->u.af_set_parm.u.aec_info.custom_param_af.data = NULL;
        }
      }
        break;
      case AF_SET_PARAM_UPDATE_AWB_INFO: {
        if (msg->u.af_set_parm.u.awb_info.custom_param_af.data) {
          free(msg->u.af_set_parm.u.awb_info.custom_param_af.data);
          msg->u.af_set_parm.u.awb_info.custom_param_af.data = NULL;
        }
      }
        break;
      default:
        break;
    }
  }

  free(msg);
  *msg_pp = NULL;
}

/** af_thread_handler
 *    @af_data: The pointer to the af thread data
 *
 *  This is the af thread that will run until it receives the STOP message.
 *  While running, it will dequeue messages from the thread's queue and process
 *  them. If there are no messages to process (queue is empty), the thread will
 *  sleep until it gets signaled.
 *
 *  Return NULL
 **/
static void* af_thread_handler(void *af_data)
{
  q3a_thread_af_data_t *af = (q3a_thread_af_data_t *)af_data;
  q3a_thread_af_msg_t  *msg = NULL;
  int                  exit_flag = 0;

  af->thread_data->active = 1;
  sem_post(&af->thread_data->sem_launch);
  Q3A_LOW("Starting AF thread handler");

  do {
    Q3A_LOW(" Waiting for message");

    pthread_mutex_lock(&af->thread_data->thread_mutex);
    while ((af->thread_data->msg_q->length == 0) &&
      (af->thread_data->p_msg_q->length == 0)) {
      pthread_cond_wait(&af->thread_data->thread_cond,
        &af->thread_data->thread_mutex);
    }
    pthread_mutex_unlock(&af->thread_data->thread_mutex);
    /* Get the message */
    pthread_mutex_lock(&af->thread_data->msg_q_lock);
    /*Pop from priority queue first and if its empty pop from normal queue*/
    msg = (q3a_thread_af_msg_t *) mct_queue_pop_head(af->thread_data->p_msg_q);

    if (!msg) {
      msg = (q3a_thread_af_msg_t *) mct_queue_pop_head(af->thread_data->msg_q);
    }
    pthread_mutex_unlock(&af->thread_data->msg_q_lock);

    if (!msg) {
      Q3A_ERR(" msg NULL");
      continue;
    }

    /* Flush the queue if it is stopping. Free the enqueued messages and
     * signal the sync message owners to release their resources */
    if (af->thread_data->active == 0) {
      if (msg->type != MSG_AF_STOP_THREAD) {
        if ((msg->type == MSG_AF_STATS) || (msg->type == MSG_BF_STATS)) {
          af->af_stats_cb(af->af_port, msg->u.stats);
        }
        if (msg->sync_flag == TRUE) {
           sem_post(&msg->sync_obj->msg_sem);
           /*don't free msg, the sender will do*/
           msg = NULL;
        } else {
          q3a_thread_af_free_msg(&msg);
        }
        continue;
      }
    }

    /* Process message accordingly */
    Q3A_LOW(" Got the message of type: %d", msg->type);
    switch (msg->type) {
    case MSG_AF_START: {
      /* Send local output copy to avoid reset of custom data
         and map custom parameters data in local output variable */
      af_output_data_t output;
      output.af_custom_param = af->af_obj->output.af_custom_param;

      ATRACE_BEGIN("AF_START");
      af->af_obj->af_ops.set_parameters(&msg->u.af_set_parm,
        &output, af->af_obj->af);
      /* Copy back output data */
      output.af_custom_param = af->af_obj->output.af_custom_param;
      af->af_obj->output = output;

      af->af_cb(&(af->af_obj->output), af->af_port);
      ATRACE_END();
    }
      break;

    case MSG_AF_CANCEL: {
      /* Send local output copy to avoid reset of custom data
         and map custom parameters data in local output variable */
      af_output_data_t output;
      output.af_custom_param = af->af_obj->output.af_custom_param;

      af->af_obj->af_ops.set_parameters(&msg->u.af_set_parm,
        &output, af->af_obj->af);
      /* Copy back output data */
      output.af_custom_param = af->af_obj->output.af_custom_param;
      af->af_obj->output = output;

      af->af_cb(&(af->af_obj->output), af->af_port);
    }
      break;
    case MSG_AF_SEND_EVENT: {
      /* Send local output copy to avoid reset of custom data
         and map custom parameters data in local output variable */
      af_output_data_t output;
      output.af_custom_param = af->af_obj->output.af_custom_param;

      output.type = AF_OUTPUT_SEND_EVENT;
      af->af_cb(&output, af->af_port);
    }
      break;

    case MSG_AF_GET: {
      af->af_obj->af_ops.get_parameters(&msg->u.af_get_parm, af->af_obj->af);
    }
      break;

    case MSG_AF_SET: {
      /* Send local output copy to avoid reset of custom data
         and map custom parameters data in local output variable */
      af_output_data_t output;
      output.af_custom_param = af->af_obj->output.af_custom_param;

      ATRACE_BEGIN("AF_SET");
      if (af->af_obj->af_ops.set_parameters(&msg->u.af_set_parm,
        &output, af->af_obj->af)) {
        /* Copy back output data */
        output.af_custom_param = af->af_obj->output.af_custom_param;
        af->af_obj->output = output;

        af->af_cb(&(af->af_obj->output), af->af_port);
      }
      ATRACE_END();
    }
      break;

    case MSG_AF_STATS:
    case MSG_BF_STATS: {

      /*AF_OUTPUT_EZ_METADATA help to stop MSG_AF_SET case,when update af info to metadata*/
      if (!af->thread_data->no_stats_mode) {
        ATRACE_BEGIN("Camera:AF");
        /* Send local output copy to avoid reset of custom data
           and map custom parameters data in local output variable */
        af_output_data_t output;
        output.af_custom_param = af->af_obj->output.af_custom_param;

        af->af_obj->af_ops.process(msg->u.stats, &output, af->af_obj->af);
        /* Copy back output data */
        output.af_custom_param = af->af_obj->output.af_custom_param;
        af->af_obj->output = output;
        ATRACE_END();
      } else {
        Q3A_HIGH("  no_stats_mode");
      }

      af->af_cb(&(af->af_obj->output), af->af_port);
      af->af_stats_cb(af->af_port, msg->u.stats);
    }
      break;

    case MSG_AF_STOP_THREAD: {
      exit_flag = 1;
    }
      break;

    default: {
    }
      break;
    }
    if (msg->sync_flag == TRUE) {
      sem_post(&msg->sync_obj->msg_sem);
      /* Don't free msg, the sender will do */
      msg = NULL;
    } else {
      q3a_thread_af_free_msg(&msg);
    }
  } while (!exit_flag);
  return NULL;
} /* af_thread_handler */

/** q3a_thread_af_start
 *    @af_data: The pointer to the af thread data
 *
 *  Called to create the af thread. It will wait on a semaphore until the
 *  thread is created and running.
 *
 *  Return TRUE
 **/
boolean q3a_thread_af_start(q3a_thread_af_data_t *af_data)
{
  int32_t ret = TRUE;
  ret = pthread_create(&af_data->thread_data->thread_id, NULL,
    af_thread_handler, af_data);
  if (ret < 0) {
    Q3A_ERR("Failed to create af thread");
    return FALSE;
  }
  pthread_setname_np(af_data->thread_data->thread_id, "CAM_AF");
  return TRUE;
} /* q3a_thread_af_start */

/** q3a_thread_af_stop
 *    @af_data: The pointer to the af thread data
 *
 *  Called to stop the af thread. It will send a MSG_STOP_THREAD message to
 *  the queue and will wait for the thread to join if the message is enqueued
 *  successfully.
 *
 *  Return TRUE on success, FALSE on failure.
 **/
boolean q3a_thread_af_stop(q3a_thread_af_data_t *af_data)
{
  boolean             rc;
  q3a_thread_af_msg_t *msg;

  // before stopping the thread just wait for thread to stop.
  // This is to avoid race condition.
  sem_wait(&af_data->thread_data->sem_launch);

  msg = malloc(sizeof(q3a_thread_af_msg_t));

  if (msg) {
    Q3A_LOW("MSG_STOP_THREAD");
    memset(msg, 0, sizeof(q3a_thread_af_msg_t));
    msg->type = MSG_AF_STOP_THREAD;
    rc = q3a_af_thread_en_q_msg(af_data->thread_data, msg);

    if (rc) {
      pthread_join(af_data->thread_data->thread_id, NULL);
      Q3A_LOW("pthread_join");
    }
  } else {
    rc = FALSE;
  }
  return rc;
} /* q3a_thread_af_stop */

/** q3a_af_thread_en_q_msg
 *    @af_data: The pointer to the af thread data
 *    @msg:     The message to be put in the queue
 *
 *  Enqueues the sent message into the thread's queue. If the message has the
 *  priority flag set, it will be put in the priority queue. Upon receiving the
 *  MSG_STOP_THREAD type of message, the queue will no longer be active and
 *  no one will be able to enqueue messages in it.
 *
 *  Return TRUE on success, FALSE on failure.
 **/
boolean q3a_af_thread_en_q_msg(void *af_data, q3a_thread_af_msg_t *msg)
{
  q3a_thread_data_t *thread_data = (q3a_thread_data_t *)af_data;
  boolean           rc = FALSE;
  boolean           sync_flag_set = FALSE;
  msg_sync_t        msg_sync;

  if (!msg || !thread_data) {
    Q3A_ERR(" Invalid Parameters!");
    q3a_thread_af_free_msg(&msg);
    return FALSE;
  }
  Q3A_LOW(" Enqueue AF message of type: %d", msg->type);

  pthread_mutex_lock(&thread_data->msg_q_lock);
  if (TRUE == msg->sync_flag) {
    sync_flag_set = TRUE;
  }
  if (thread_data->active &&
       !(msg->type == MSG_BF_STATS && thread_data->no_stats_mode)) {
    if (TRUE == sync_flag_set) {
      msg->sync_obj = &msg_sync;
      sem_init(&msg_sync.msg_sem, 0, 0);
    }
    Q3A_LOW("lock Q");
   //If its a priority event queue to priority queue else to normal queue
    if(msg->is_priority) {
      mct_queue_push_tail(thread_data->p_msg_q, msg);
    } else {
      mct_queue_push_tail(thread_data->msg_q, msg);
    }

    if (msg->type == MSG_AF_STOP_THREAD) {
      thread_data->active = 0;
    }
    rc = TRUE;
  }
  pthread_mutex_unlock(&thread_data->msg_q_lock);

  if (rc) {
    pthread_mutex_lock(&thread_data->thread_mutex);
    pthread_cond_signal(&thread_data->thread_cond);
    pthread_mutex_unlock(&thread_data->thread_mutex);

    if (TRUE == sync_flag_set) {
      sem_wait(&msg_sync.msg_sem);
      sem_destroy(&msg_sync.msg_sem);
    }
  } else {
    Q3A_ERR(" Failure adding AF message - handler inactive ");
    /* Only free if sync flag is not set, caller must free */
    if (FALSE == sync_flag_set) {
      q3a_thread_af_free_msg(&msg);
    }
  }

  return rc;
} /* q3a_af_thread_en_q_msg */

/** q3a_thread_af_init
 *
 *  Initializes the AF thread data and creates the queues.
 *
 *  Return the the AF thread data object, on failure return NULL.
 **/
q3a_thread_af_data_t* q3a_thread_af_init(void)
{
  q3a_thread_af_data_t *af;

  Q3A_LOW(" Allocate memory for AF thread!");
  af = malloc(sizeof(q3a_thread_af_data_t));
  if (af == NULL) {
    return NULL;
  }
  memset(af, 0, sizeof(q3a_thread_af_data_t));

  Q3A_LOW(" Allocate memory for q3a thread data");
  af->thread_data = malloc(sizeof(q3a_thread_data_t));
  if (af->thread_data == NULL) {
    free(af);
    af = NULL;
    return NULL;
  }
  memset(af->thread_data, 0, sizeof(q3a_thread_data_t));

  Q3A_LOW(" Create AF queue ");
  af->thread_data->msg_q = (mct_queue_t *)mct_queue_new;
  af->thread_data->p_msg_q = (mct_queue_t *)mct_queue_new;

  if (!af->thread_data->msg_q || !af->thread_data->p_msg_q) {
    if(af->thread_data->msg_q) {
      mct_queue_free(af->thread_data->msg_q);
    }
    if(af->thread_data->p_msg_q) {
      mct_queue_free(af->thread_data->p_msg_q);
    }
    free(af->thread_data);
    af->thread_data = NULL;
    free(af);
    af = NULL;
    return NULL;
  }

  Q3A_LOW(" Initialize the AF queue! ");
  pthread_mutex_init(&af->thread_data->msg_q_lock, NULL);
  mct_queue_init(af->thread_data->msg_q);
  mct_queue_init(af->thread_data->p_msg_q);
  pthread_cond_init(&af->thread_data->thread_cond, NULL);
  pthread_mutex_init(&af->thread_data->thread_mutex, NULL);
  sem_init(&af->thread_data->sem_launch, 0, 0);
  Q3A_LOW("private->thread_data: %p", af->thread_data);

  return af;
} /* q3a_thread_af_init */

/** q3a_thread_af_deinit
 *    @af_data: The pointer to the af thread data
 *
 *  Deinitializes the AF thread data - frees the queues, destroys the
 *  sync variables and frees the thread data object.
 *
 *  Return void.
 **/
void q3a_thread_af_deinit(q3a_thread_af_data_t *af)
{
  Q3A_LOW("thread_data: %p", af->thread_data);
  pthread_mutex_destroy(&af->thread_data->thread_mutex);
  pthread_cond_destroy(&af->thread_data->thread_cond);
  mct_queue_free(af->thread_data->msg_q);
  mct_queue_free(af->thread_data->p_msg_q);
  pthread_mutex_destroy(&af->thread_data->msg_q_lock);
  sem_destroy(&af->thread_data->sem_launch);

  free(af->thread_data);
  af->thread_data = NULL;
  free(af);
  af = NULL;
} /* q3a_thread_af_deinit */
