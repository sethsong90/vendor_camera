/**********************************************************************
*  Copyright (c) 2013-2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/

#include <linux/media.h>
#include "mct_module.h"
#include "module_imgbase.h"
#include "mct_stream.h"
#include "pthread.h"
#include "chromatix.h"
#include "mct_stream.h"
#include "module_imglib_common.h"

#define DEBUG_BATCH 0
#define DEBUG_INTERNAL_BUF_LIST 0

/**
 * MACRO: PRINT_BUF_LIST
 *
 * Description:
 *   Print contents of overlap buffers list
 *
 * Arguments:
 *   @p_client: imgbase client
 *
 **/
#define PRINT_BUF_LIST(p_client) ({ \
if (DEBUG_BATCH) { \
  const int size = 500; \
  char dst [size]; \
  char scratch [size]; \
  dst[0] = '\0'; \
  imgbase_buf_t *p_buf = NULL; \
  strlcat(dst, "BUF LIST ", size); \
  for (i = 0; i < MAX_IMGLIB_BATCH_SIZE; i++) { \
    p_buf = p_client->buf_list[i]; \
    if (p_buf) { \
      snprintf (scratch, size, "[%d, %d %d %d, %d]", \
      p_buf->frame.frame_id, \
      p_buf->queued_cnt, \
      p_buf->dequeued_cnt, \
      p_buf->divert_done, \
      p_buf->ack.ack_received_cnt); \
    } else { \
      snprintf (scratch, size, "[0, 0 0 0, 0]"); \
    } \
    strlcat(dst, scratch, size); \
  } \
  IDBG_MED("%s:%d] %s \n", __func__, __LINE__, dst); \
}\
})

/**
 * MACRO: PRINT_INTERNAL_BUF_LIST
 *
 * Description:
 *   Print contents of internal buf list
 *
 * Arguments:
 *   @p_client: imgbase client
 *
 **/
#define PRINT_INTERNAL_BUF_LIST(p_client) ({ \
if (DEBUG_INTERNAL_BUF_LIST) { \
  const int size = 500; \
  char dst [size]; \
  char scratch [size]; \
  dst[0] = '\0'; \
  strlcat(dst, "INTERNAL BUF ", size); \
  for (i = 0; i < MAX_IMGLIB_INTERNAL_BUFS; i++) { \
    snprintf (scratch, size, "[%d, %d %d]", \
    p_client->internal_buf.buf[i].frame_info.frame_id, \
    p_client->internal_buf.buf[i].ref_cnt, \
    p_client->internal_buf.buf[i].input_ack_held); \
    strlcat(dst, scratch, size); \
  } \
  IDBG_MED("%s \n", dst); \
}\
})

/**
 *  STATIC FUNCTIONS
 */
static int module_imgbase_client_handle_src_divert_wrapper(
  void *userdata);

/**
 * Function: module_imgbase_client_preload_exec
 *
 * Description: function called to execute the preload
 *
 * Arguments:
 *   @p_appdata - pointer to base module
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
static int32_t module_imgbase_client_preload_exec(void *p_appdata)
{
  imgbase_session_data_t *p_session_data = (imgbase_session_data_t *)p_appdata;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_session_data->p_mod;
  IDBG_MED(" E ");

  int32_t rc = IMG_CORE_PRELOAD(&p_mod->core_ops,
    &p_session_data->preload_params.max_dim);
  if (IMG_ERROR(rc)) {
    p_mod->last_error = IMG_ERR_GENERAL;
    IDBG_ERROR("IMG_COMP_MEM_ALLOC failed %d", rc);
  } else {
    p_session_data->preload_params.preload_done = true;
    p_mod->preload_done = true;
  }

  IDBG_MED("X rc %d", rc);
  return IMG_SUCCESS;
}

/**
 * Function: module_imgbase_client_preload
 *
 * Description: function called for base client preload
 *
 * Arguments:
 *   @p_session_data - Sesion data
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int32_t module_imgbase_client_preload(imgbase_session_data_t *p_session_data)
{
  module_imgbase_t *p_mod;
  img_core_type_t thread_affinity[1] = { IMG_CORE_ARM };
  img_thread_job_params_t thread_job;
  uint32_t job_id;
  int32_t rc = IMG_SUCCESS;

  p_mod = (module_imgbase_t *)p_session_data->p_mod;
  p_mod->last_error = IMG_SUCCESS;

  if (!IMG_COMP_PRELOAD_SUPPORTED(&p_mod->core_ops)) {
    p_session_data->preload_params.preload_done = true;
    p_mod->preload_done = true;
    p_mod->th_client_id = 0;
    IDBG_HIGH("[%s] Preload not needed", p_mod->name);
    return IMG_SUCCESS;
  }

  IDBG_MED("Preload needed %dx%d",
    p_session_data->preload_params.max_dim.width,
    p_session_data->preload_params.max_dim.height);

  /* reserve thread */
  if (!p_mod->th_client_id) {
    p_mod->th_client_id =
      img_thread_mgr_reserve_threads(1, thread_affinity);
  }

  if (!p_mod->th_client_id) {
    IDBG_ERROR("Error reserve thread ");
    rc = IMG_ERR_GENERAL;
    goto end;
  }
  thread_job.client_id = p_mod->th_client_id;
  thread_job.core_affinity = IMG_CORE_ARM;
  thread_job.delete_on_completion = TRUE;
  thread_job.execute = module_imgbase_client_preload_exec;
  thread_job.dep_job_count = 0;
  thread_job.args = p_session_data;
  thread_job.dep_job_ids = NULL;
  job_id = img_thread_mgr_schedule_job(&thread_job);
  if (job_id) {
    IDBG_MED("scheduled job id %x client %x", job_id, p_mod->th_client_id);
  } else {
    IDBG_ERROR("Error cannot schedule job ");
    rc = IMG_ERR_GENERAL;
  }

end:
  return rc;
}

/**
 * Function: module_imgbase_client_forward_ack
 *
 * Description: Function to forward the ack upstream
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *   @p_ack - ack to be forwarded
 *
 * Return values:
 *   imaging error values
 *
 * Notes: none
 **/
static int module_imgbase_client_forward_ack(imgbase_client_t *p_client,
  isp_buf_divert_ack_t *p_ack)
{
  int rc = IMG_SUCCESS;
  int stream_idx;
  imgbase_stream_t *p_stream;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;

  if (!p_ack) {
    IDBG_ERROR("%s:%d] Error, null ack ptr %p", __func__, __LINE__,
      p_ack);
    return IMG_ERR_GENERAL;
  }

  stream_idx = module_imgbase_find_stream_by_identity(p_client,
    p_client->divert_identity);
  if (stream_idx < 0) {
    IDBG_ERROR("%s:%d] Cannot find stream mapped to idx %x", __func__,
      __LINE__, p_client->divert_identity);
    return IMG_ERR_GENERAL;
  }
  p_stream = &p_client->stream[stream_idx];

  IDBG_MED("[%s] [IMG_BUF_DBG_ACK] GEN identity %x frame_id %d buf_id %d",
    p_mod->name,
    p_ack->identity,
    p_ack->frame_id,
    p_ack->buf_idx)

  isp_buf_divert_ack_t divert_ack;
  memset(&divert_ack, 0, sizeof(divert_ack));
  divert_ack.buf_idx = p_ack->buf_idx;
  divert_ack.is_buf_dirty = 1;
  divert_ack.identity = p_ack->identity;
  divert_ack.frame_id = p_ack->frame_id;
  divert_ack.channel_id = p_ack->channel_id;
  divert_ack.meta_data = p_ack->meta_data;
  rc = mod_imgbase_send_event(p_client->divert_identity, TRUE,
    MCT_EVENT_MODULE_BUF_DIVERT_ACK, divert_ack);
  if (!rc) {
    IDBG_ERROR("%s:%d] Sending ACK upstream failed %d",
      __func__, __LINE__, rc);
    return IMG_ERR_GENERAL;
  }

  return IMG_SUCCESS;
}

/**
 * Function: module_imgbase_client_forward_buffer_divert
 *
 * Description: Function to forward the buffer divert downstream
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *   @p_buf_divert - divert to be forwarded
 *   @p_stream - stream info on which to send divert
 *   @is_input_buf - flag to indicate if buffer was
 *     imglib input buffer or imglib output buffer
 *
 * Return values:
 *   imaging error values
 *
 * Notes: none
 **/
static int module_imgbase_client_forward_buffer_divert(
  imgbase_client_t *p_client, isp_buf_divert_t *p_buf_divert,
  imgbase_stream_t *p_stream, boolean is_input_buf)
{
  int rc = IMG_SUCCESS;
  boolean brc = TRUE;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;

  /* divert buffer downsteam */
  p_buf_divert->ack_flag = FALSE;
  brc = mod_imgbase_send_event(p_client->divert_identity, FALSE,
    MCT_EVENT_MODULE_BUF_DIVERT, *p_buf_divert);
  if (!brc) {
    IDBG_ERROR("%s_%s:%d] Error, send divert event failed %d",
      __func__, p_mod->name, __LINE__, brc);
    rc = IMG_ERR_GENERAL;
    goto end;
  }

  /* check if inplace ack was issued for divert */
  if (!p_buf_divert->ack_flag) {
    rc = IMG_SUCCESS;
    goto end;
  }

  IDBG_MED("%s_%s:%d] Inplace ack received for %d", __func__,
    p_mod->name, __LINE__, p_buf_divert->buffer.sequence);

  if (is_input_buf) {
    /* buffer was imglib input buffer */
    /* generate ACK. Hold if overlap buffers are used. Forward if not */
    rc = module_imgbase_client_handle_buffer_ack(p_client,
      IMG_EVT_ACK_FORCE_RELEASE, p_buf_divert);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s_%s:%d] Error in handling ack", __func__,
        p_mod->name, __LINE__);
    }
  } else {
    /* buffer was imglib output buffer */
    /* if internal buffers are used for the output buf, free it now */
    if (p_mod->caps.use_internal_bufs) {
      rc = module_imgbase_client_handle_buffer_ack(p_client,
        IMG_EVT_ACK_FREE_INTERNAL_BUF, &p_buf_divert->buffer.sequence);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s_%s:%d] Error, IMG_EVT_ACK_FREE_INTERNAL_BUF failed %d",
          __func__, p_mod->name, __LINE__, rc);
      }
    }
  }

end:
  return rc;
}

/**
 * Function: module_imgbase_client_handle_buf_done
 *
 * Description: Function to handle input buf done event
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *   @p_frame - frame for buf done
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
static void module_imgbase_client_handle_buf_done(
  imgbase_client_t *p_client,
  img_frame_t *p_frame)
{
  isp_buf_divert_t *p_buf_divert;
  int rc, i;
  imgbase_buf_t *p_imgbase_buf = NULL;
  module_imgbase_t *p_mod;
  imgbase_stream_t *p_stream = NULL;
  int32_t free_frame = TRUE;
  int stream_idx;
  img_core_type_t thread_affinity[1] = {
    IMG_CORE_ARM,
  };
  int client_id;
  bool release_ack = FALSE;

  if (!p_client || !p_frame) {
    IDBG_ERROR("%s:%d] Error", __func__, __LINE__);
    return;
  }

  p_mod = (module_imgbase_t *)p_client->p_mod;
  if (!p_mod) {
    IDBG_ERROR("%s:%d] Error", __func__, __LINE__);
    return;
  }

  IDBG_MED("%s_%s:%d] bufdone, buffer idx %d, frameid %d, streamon %d",
    __func__, p_mod->name, __LINE__, p_frame->idx, p_frame->frame_id,
    p_client->stream_on);

  stream_idx = module_imgbase_find_stream_by_identity(p_client,
    p_client->divert_identity);
  if (stream_idx < 0) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to idx %x", __func__,
      p_mod->name, __LINE__, p_client->divert_identity);
    goto end;
  }

  p_stream = &p_client->stream[stream_idx];
  if (NULL == p_stream) {
    IDBG_ERROR("%s_%s:%d] Cannot find stream mapped to client %d", __func__,
      p_mod->name, __LINE__, p_client->stream_cnt);
    goto end;
  }

  p_imgbase_buf = (imgbase_buf_t *)p_frame->private_data;
  if (!p_imgbase_buf) {
    IDBG_HIGH("%s:%d] imgbasebuf NULL, dummy frame", __func__, __LINE__);
    goto end;
  }

  p_buf_divert = &p_imgbase_buf->buf_divert;

  IDBG_MED("%s:%d] buffer idx %d port %p %p frame_id %d", __func__, __LINE__,
    p_frame->idx, p_stream->p_sinkport, p_stream->p_srcport,
    p_buf_divert->buffer.sequence);

  if (p_mod->caps.num_overlap) {
    pthread_mutex_lock(&p_client->buf_mutex);
    p_imgbase_buf->dequeued_cnt++;
    PRINT_BUF_LIST(p_client);

    /* Release ACK if it is no longer needed for a batch process */
    if ((p_imgbase_buf->queued_cnt == p_mod->caps.num_input) &&
      (p_imgbase_buf->dequeued_cnt == p_mod->caps.num_input) &&
      (p_imgbase_buf->ack.ack_received_cnt == p_client->num_ack)) {
      release_ack = TRUE;
    }
    pthread_mutex_unlock(&p_client->buf_mutex);

    if (release_ack) {
      rc = module_imgbase_client_handle_buffer_ack(p_client,
        IMG_EVT_ACK_TRY_RELEASE, p_imgbase_buf);
      if (IMG_ERROR(rc)) {
       IDBG_ERROR("%s:%d] Error in releasing ack", __func__, __LINE__);
      }
      goto end;
    }
  }

  /* release buffer to kernel directly on streamoff */
  if (!p_client->stream_on) {
    rc = module_imglib_common_release_buffer(p_mod->subdevfd,
      p_stream->identity,
      p_frame->idx,
      p_frame->frame_id,
      FALSE);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Error in releasing buffer", __func__, __LINE__);
    }
    goto end;
  }

  if (p_stream->p_srcport && !p_mod->caps.use_internal_bufs) {
    isp_buf_divert_t lbuf_divert;
    memcpy(&lbuf_divert, p_buf_divert, sizeof(isp_buf_divert_t));
    if (!p_client->rate_control) { /*FIXME*/
      IDBG_MED("%s:%d] forwarding from src port", __func__, __LINE__);

      /* divert the buffer only if not already forwarded */
      if (p_mod->caps.num_overlap && p_imgbase_buf->divert_done == TRUE) {
        IDBG_HIGH("%s:%d] frame %d was already diverted", __func__,
          __LINE__, p_frame->frame_id);
        goto end;
      }

      if (p_mod->modparams.cache_ops != CACHE_NO_OP) {
        uint32_t len = IMG_FRAME_LEN(p_frame);
        IDBG_MED("[%s] Cache ops %d frame_id %d identity %x "
          "v_addr %p fd %d buffer_size %d ion_fd %d",
          p_mod->name,
          p_mod->modparams.cache_ops,
          p_frame->frame_id,
          p_buf_divert->identity,
          IMG_ADDR(p_frame),
          IMG_FD(p_frame),
          len,
          p_mod->ion_fd);
        img_cache_ops_external(IMG_ADDR(p_frame),
          len, 0, IMG_FD(p_frame),
          p_mod->modparams.cache_ops, p_mod->ion_fd);
      }
      rc = module_imgbase_client_forward_buffer_divert(p_client,
        &lbuf_divert, p_stream, TRUE);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Error, send divert event from src port failed %d",
          __func__, __LINE__, rc);
      }
      p_imgbase_buf->divert_done = TRUE;
    } else {
      if (!p_client->thread_job.client_id) {
        client_id = img_thread_mgr_reserve_threads(1, thread_affinity);
        if (client_id == 0) {
          IDBG_ERROR("%s:%d] Error reserve thread ", __func__, __LINE__);
          goto end;
        }
        p_client->thread_ops.client_id = client_id;
        IDBG_MED("%s:%d] Reserved thread with client id %d ", __func__,
          __LINE__, p_client->thread_ops.client_id);
      }
      imgbase_client_divert_wrapper_params_t
        *p_src_divert_wrapper_params =
        (imgbase_client_divert_wrapper_params_t *)calloc(1,
        sizeof(imgbase_client_divert_wrapper_params_t));
      if (NULL == p_src_divert_wrapper_params) {
        IDBG_ERROR("%s:%d] Error alloc", __func__, __LINE__);
        goto end;
      }
      p_src_divert_wrapper_params->p_client = p_client;
      p_src_divert_wrapper_params->p_frame = p_frame;
      p_client->thread_job.client_id = p_client->thread_ops.client_id;
      p_client->thread_job.core_affinity = IMG_CORE_ARM;
      p_client->thread_job.delete_on_completion = TRUE;
      p_client->thread_job.execute =
        module_imgbase_client_handle_src_divert_wrapper;
      p_client->thread_job.dep_job_count = 0;
      p_client->thread_job.args = (void *)p_src_divert_wrapper_params;
      p_client->thread_job.dep_job_ids = NULL;

      uint32_t job_id =
        p_client->thread_ops.schedule_job(&p_client->thread_job);
      IDBG_MED("%s:%d] scheduled job id %d", __func__, __LINE__, job_id);
      if (!job_id) {
        IDBG_ERROR("%s:%d] Error schedule job", __func__, __LINE__);
      }
      free_frame = FALSE;
    }
  } else if (p_stream->p_sinkport) {
    if (p_mod->caps.num_output || p_mod->caps.ack_required) {
      /* Generate ACK */
      rc = module_imgbase_client_handle_buffer_ack(p_client,
        IMG_EVT_ACK_FORCE_RELEASE, p_buf_divert);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Error in sending ack", __func__, __LINE__);
      }
    } else {
      rc = module_imglib_common_release_buffer(p_mod->subdevfd,
        p_client->divert_identity,
        p_frame->idx,
        p_frame->frame_id,
        TRUE);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Error in releasing buffer", __func__, __LINE__);
      }
    }
  }

end:
  /* if overlap batch processing, this is freed in handle_overlap_input */
  if (!p_mod->caps.num_overlap && p_imgbase_buf && free_frame) {
    free(p_imgbase_buf);
    p_imgbase_buf = NULL;
  }
}

/**
 * Function: module_imgbase_client_handle_src_divert_wrapper
 *
 * Description: Function to forward divert after it is posted
 *   to the msg thread
 *
 * Arguments:
 *   @p_userdata - Pointer to src divert wrapper params
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
int module_imgbase_client_handle_src_divert_wrapper(
  void *p_userdata)
{
  imgbase_client_divert_wrapper_params_t *p_params =
    (imgbase_client_divert_wrapper_params_t *)p_userdata;
  img_frame_t *p_frame = p_params->p_frame;
  imgbase_client_t *p_client = p_params->p_client;
  imgbase_stream_t *p_stream = NULL;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  uint64_t time_delta;
  int stream_idx;
  isp_buf_divert_t l_buf_divert;
  imgbase_buf_t *p_imgbase_buf = NULL;
  int rc;

  stream_idx = module_imgbase_find_stream_by_identity(p_client,
    p_client->divert_identity);
  if (stream_idx < 0) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to idx %x", __func__,
      p_mod->name, __LINE__, p_client->divert_identity);
    goto end;
  }

  p_stream = &p_client->stream[stream_idx];
  if (NULL == p_stream) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to client", __func__,
      p_mod->name, __LINE__);
    goto end;
  }

  if (!p_client->first_frame) {
    time_delta = IMG_TIMER_END(p_client->start_time, p_client->end_time,
      p_mod->name, IMG_TIMER_MODE_US);
  } else {
    p_client->first_frame = FALSE;
    time_delta = p_client->exp_frame_delay;
  }

  if (time_delta < p_client->exp_frame_delay) {
    usleep(p_client->exp_frame_delay - time_delta);
  }

  if (p_mod->modparams.imgbase_client_process_done) {
    p_mod->modparams.imgbase_client_process_done(p_client, p_frame);
  }

  if (p_mod->modparams.cache_ops != CACHE_NO_OP) {
    IDBG_MED("[%s] Cache ops %d", p_mod->name, p_mod->modparams.cache_ops);
    img_cache_ops_external(IMG_ADDR(p_frame),
      IMG_FRAME_LEN(p_frame), 0, IMG_FD(p_frame),
      p_mod->modparams.cache_ops, p_mod->ion_fd);
  }

  p_imgbase_buf = p_frame->private_data;

  /* divert the buffer only if not already forwarded */
  if (p_mod->caps.num_overlap && p_imgbase_buf->divert_done == TRUE) {
    IDBG_HIGH("%s:%d] frame %d was already diverted", __func__,
      __LINE__, p_frame->frame_id);
    goto end;
  }

  IDBG_HIGH("%s:%d] forward frame %d downstream", __func__,
    __LINE__, p_frame->frame_id);
  p_imgbase_buf->divert_done = TRUE;
  l_buf_divert = p_imgbase_buf->buf_divert;

  /* divert the buffer downstream */
  rc = module_imgbase_client_forward_buffer_divert(p_client,
    &l_buf_divert, p_stream, TRUE);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] Error, send divert event failed %d",
      __func__, __LINE__, rc);
  }

  /* store new time */
  IMG_TIMER_START(p_client->start_time);

end:
  /* if overlap batch processing, this is freed in handle_overlap_input */
  if (!p_mod->caps.num_overlap && p_imgbase_buf) {
    free(p_imgbase_buf);
    p_imgbase_buf = NULL;
  }
  free(p_params);
  return IMG_SUCCESS;
}

/**
 * Function: module_imgbase_client_store_frame_id
 *
 * Description: Function to store the frameId of all the input
 *   buffers. The frameIds will be added to the output buffers
 *   released to HAL.
 *
 *   If the num of bufs released to HAL is greater than the num
 *   of input bufs, we need to fabricate frameIds for the extra
 *   buffers to ensure that a 0 frameId zero is not sent to HAL.
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *   @frame_id - frameId of input buf
 *   @input_buf_cnt - input buf count
 *
 * Return values:
 *   inaging error values
 *
 * Notes: none
 **/
static int module_imgbase_client_store_frame_id(imgbase_client_t *p_client,
  uint32_t frame_id, int input_buf_cnt)
{
  int rc = IMG_SUCCESS;
  int extra_cnt = 1;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;

  if (input_buf_cnt >= MAX_IMGLIB_BASE_RELEASED_BUFS) {
    rc = IMG_ERR_INVALID_INPUT;
    goto error;
  }

  /* Store the frameId of input buffers */
  p_client->input_frame_id[input_buf_cnt] = frame_id;
  IDBG_MED("%s:%d] Store frameId %d at idx %d", __func__,
    __LINE__, p_client->input_frame_id[input_buf_cnt], input_buf_cnt);
  input_buf_cnt++;

  /* If num bufs to be released is greater than num input bufs, fabricate
    frameIds; create a frameId which is incrementally greater than the
    last input buffer's frameId */
  if (input_buf_cnt >= p_mod->caps.num_input &&
    input_buf_cnt < p_mod->caps.num_release_buf) {
    while ((input_buf_cnt < p_mod->caps.num_release_buf) &&
      (input_buf_cnt < MAX_IMGLIB_BASE_RELEASED_BUFS)) {
      p_client->input_frame_id[input_buf_cnt] = frame_id + extra_cnt;
      extra_cnt++;
      IDBG_MED("%s:%d] Store frameId %d at idx %d", __func__,
        __LINE__, p_client->input_frame_id[input_buf_cnt], input_buf_cnt);
      input_buf_cnt++;
    }
  }
  return rc;

error:
  return rc;
}

/**
 * Function: module_imgbase_client_retrieve_frame_id
 *
 * Description: Function to retrieve the frameId to associate
 *   with the output buffer which is sent to HAL
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *   @p_frame: frame pointer
 *
 * Return values:
 *   frameId of buffer
 *
 * Notes: none
 **/
static uint32_t module_imgbase_client_retrieve_frame_id(
  imgbase_client_t *p_client,
  img_frame_t *p_frame)
{
  uint32_t frame_id = 0;
  int release_index = 0;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;

  if (p_mod->caps.num_release_buf > p_mod->caps.num_input) {
    /* If the number of released bufs is greater than the number
    of input bufs, set the released bufs frameId to the input buf's
    frameIds and fabricated frameIds */
    release_index = p_client->release_buf_cnt;
    if (release_index >= p_mod->caps.num_release_buf ||
      release_index >= MAX_IMGLIB_BASE_RELEASED_BUFS) {
      IDBG_ERROR("%s:%d] Invalid index %d", __func__, __LINE__, release_index);
      goto error;
    }
    frame_id = p_client->input_frame_id[release_index];
  } else if (!p_frame->frame_id) {
    /* If the number of released bufs is <= the number of input
      bufs, set the released buffer's frameId to the most recently
      received input buf's frameIds */
    release_index = p_mod->caps.num_input -
      p_mod->caps.num_output + p_client->release_buf_cnt;

    if (release_index >= p_mod->caps.num_input ||
      release_index >= MAX_IMGLIB_BASE_RELEASED_BUFS) {
      IDBG_ERROR("%s:%d] Invalid index %d", __func__, __LINE__, release_index);
      goto error;
    }
    frame_id = p_client->input_frame_id[release_index];
  } else {
    frame_id = p_frame->frame_id;
  }

  p_client->release_buf_cnt++;

  /* Ensure that released buf cnt does not exceed the intended
    number of buffers to be released */
  if (p_client->release_buf_cnt >= p_mod->caps.num_release_buf) {
    p_client->release_buf_cnt = 0;
  }

  return frame_id;

error:
  return 0;
}

/**
 * Function: module_imgbase_client_handle_outbuf_done
 *
 * Description: Function to handle output buf done event
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *   @p_frame - frame for buf done
 *   @buf_done - flag to indicate if the buf done needs to be
 *             called
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
static void module_imgbase_client_handle_outbuf_done(
  imgbase_client_t *p_client,
  img_frame_t *p_frame,
  int8_t buf_done)
{
  int rc = IMG_SUCCESS;
  uint32_t frame_id = 0;
  imgbase_stream_t *p_stream = NULL;
  isp_buf_divert_t *p_buf_divert;
  int stream_idx;
  imgbase_buf_t *p_imgbase_buf = NULL;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  IDBG_MED("%s_%s:%d] E", __func__, p_mod->name, __LINE__);

  if (!p_frame) {
    IDBG_ERROR("%s:%d] Error, p_frame null", __func__, __LINE__);
    return;
  }

  stream_idx = module_imgbase_find_stream_by_identity(p_client,
    p_client->divert_identity);
  if (stream_idx < 0) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to idx %x", __func__,
      p_mod->name, __LINE__, p_client->divert_identity);
    goto end;
  }

  p_stream = &p_client->stream[stream_idx];
  if (NULL == p_stream) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to client", __func__,
      p_mod->name, __LINE__);
    goto end;
  }

  if (p_stream->p_srcport) {
    /* If src port mapped, send op buffer downstream via buf divert */
    IDBG_MED("%s_%s:%d] forwarding from src port", __func__, p_mod->name,
      __LINE__);

    /* Retrieve divert info from private data */
    p_imgbase_buf = (imgbase_buf_t *)p_frame->private_data;
    p_buf_divert = &p_imgbase_buf->buf_divert;
    frame_id = p_buf_divert->buffer.sequence;

    p_buf_divert->fd = p_frame->frame[0].plane[0].fd;
    p_buf_divert->vaddr =
      (void*)(unsigned long *)&(p_frame->frame[0].plane[0].addr);

    IDBG_MED("%s_%s:%d] outbufdone, send buf divert %p for frame %d "
      "idx %d, %x", __func__, p_mod->name, __LINE__, p_buf_divert,
      frame_id, p_frame->idx, p_stream->identity);

    /* Send divert downstream */
    rc = module_imgbase_client_forward_buffer_divert(p_client,
       p_buf_divert, p_stream, FALSE);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Error, send divert event for op buf failed %d",
        __func__, __LINE__, rc);
    }
  } else if (!p_mod->caps.inplace_algo) {
    /* Release output buffer back to kernel */
    frame_id = module_imgbase_client_retrieve_frame_id(p_client, p_frame);
    if (frame_id == 0) {
      IDBG_ERROR("%s_%s:%d] Error frameId 0",__func__, p_mod->name, __LINE__);
      goto end;
    }

    IDBG_MED("%s_%s:%d] outbufdone, release buffer for frame %d idx %d, %d %x",
      __func__, p_mod->name, __LINE__, frame_id, p_frame->idx,
      p_mod->subdevfd, p_stream->identity);

    rc = module_imglib_common_release_buffer(p_mod->subdevfd,
      p_stream->identity, p_frame->idx, frame_id, buf_done);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s_%s:%d] Error getting output buffer %d",
        __func__, p_mod->name, __LINE__, p_frame->idx);
    }
  }

end:
  if (p_imgbase_buf && !p_mod->caps.use_internal_bufs) {
    free(p_imgbase_buf);
    p_imgbase_buf = NULL;
  }
  if (p_frame && !p_mod->caps.use_internal_bufs) {
    free(p_frame);
    p_frame = NULL;
  }

}

/**
 * Function: module_imgbase_client_enqueue_streambuf
 *
 * Description: function to enqueue stream buffer
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *   @p_meta - pointer to the meta
 *   @prop: image properties
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
static void module_imgbase_client_enqueue_streambuf(
  imgbase_client_t *p_client,
  img_meta_t *p_meta,
  cam_stream_img_prop_t *prop)
{
  if (!p_client->stream_on) {
    IDBG_LOW("%s:%d] skip create streambuf", __func__, __LINE__);
    return;
  }

  cam_stream_parm_buffer_t *p_stream_buf =
    calloc(1, sizeof(cam_stream_parm_buffer_t));
  if (p_stream_buf && p_meta) {
    p_stream_buf->imgProp = *prop;
    p_stream_buf->type = CAM_STREAM_PARAM_TYPE_GET_IMG_PROP;
    IDBG_HIGH("%s:%d] (%d %d %d %d) (%d %d) (%d %d)",
      __func__, __LINE__,
      p_stream_buf->imgProp.crop.left,
      p_stream_buf->imgProp.crop.top,
      p_stream_buf->imgProp.crop.width,
      p_stream_buf->imgProp.crop.height,
      p_stream_buf->imgProp.input.width,
      p_stream_buf->imgProp.input.height,
      p_stream_buf->imgProp.output.width,
      p_stream_buf->imgProp.output.height);
    if (IMG_ERROR(img_q_enqueue(&p_client->stream_parm_q, p_stream_buf))) {
      free(p_stream_buf);
      p_stream_buf = NULL;
    }
  }
}

/**
 * Function: module_imgbase_client_event_handler
 *
 * Description: event handler for Imglib base client
 *
 * Arguments:
 *   @p_appdata - IMGLIB_BASE client
 *   @p_event - pointer to the event
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
static int module_imgbase_client_event_handler(void* p_appdata,
  img_event_t *p_event)
{
  imgbase_client_t *p_client;
  img_component_ops_t *p_comp;
  int rc = IMG_SUCCESS;

  IDBG_MED("%s:%d] ", __func__, __LINE__);
  if ((NULL == p_event) || (NULL == p_appdata)) {
    IDBG_ERROR("%s:%d] invalid event", __func__, __LINE__);
    return IMG_ERR_GENERAL;
  }

  p_client = (imgbase_client_t *)p_appdata;
  p_comp = &p_client->comp;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  IDBG_LOW("%s:%d] type %d", __func__, __LINE__, p_event->type);

  p_mod = (module_imgbase_t *)p_client->p_mod;
  if (p_event->type < QIMG_EVT_MAX) {
    if (p_mod->modparams.imgbase_client_event_handler[p_event->type]) {
      rc =
        p_mod->modparams.imgbase_client_event_handler[p_event->type](
        p_client, p_event);
      //If the submodule has over ridden the execution, return
      if (rc != IMG_ERR_EAGAIN) {
        return rc;
      }
    }
  }

  switch (p_event->type) {
  case QIMG_EVT_IMG_BUF_DONE: {
    module_imgbase_client_handle_buf_done(p_client, p_event->d.p_frame);
    break;
  }
  case QIMG_EVT_IMG_OUT_BUF_DONE: {
    module_imgbase_client_handle_outbuf_done(p_client, p_event->d.p_frame,
      TRUE);
    break;
  }
  case QIMG_EVT_META_BUF_DONE: {
    img_meta_t *p_meta = p_event->d.p_meta;
    IDBG_MED("%s:%d] QIMG_EVT_META_BUF_DONE p_meta %p",
      __func__, __LINE__, p_meta);

    p_client->num_meta_queued--;

    if (p_meta) {
      /* Only enqueue the last metadata buf since the relevant
        information is only available within this */
      if (p_client->num_meta_queued == 0) {
        cam_stream_img_prop_t prop;
        memset(&prop, 0x0, sizeof(cam_stream_img_prop_t));
        cam_rect_t *p_rect = &prop.crop;
        img_rect_t *output_crop = (img_rect_t *)img_get_meta(p_meta,
          IMG_META_OUTPUT_ROI);
        if (output_crop) {
          p_rect->left = (int32_t)output_crop->pos.x;
          p_rect->top = (int32_t)output_crop->pos.y;
          p_rect->width = (int32_t)output_crop->size.width;
          p_rect->height = (int32_t)output_crop->size.height;
        }

        img_misc_t *misc_data = (img_misc_t *)img_get_meta(p_meta,
          IMG_META_MISC_DATA);
        if (p_client->p_current_misc_data && misc_data) {
          p_client->p_current_misc_data->result = misc_data->result;
          p_client->p_current_misc_data->header_size = misc_data->header_size;
          p_client->p_current_misc_data->width = misc_data->width;
          p_client->p_current_misc_data->height = misc_data->height;
        }

        module_imgbase_client_enqueue_streambuf(p_client, p_meta, &prop);
      }
      free(p_event->d.p_meta);
    }
    p_client->p_current_meta = NULL;
    break;
  }
  case QIMG_EVT_ERROR: {
    IDBG_HIGH("%s %d: IMGLIB_BASE Error", __func__, __LINE__);
    bool error_handled = FALSE;

    switch (p_event->d.status) {
    case IMG_ERR_SSR:
      IDBG_HIGH("%s:%d] SSR occured", __func__, __LINE__);
      if (p_mod->modparams.imgbase_client_handle_ssr) {
        p_mod->modparams.imgbase_client_handle_ssr(p_client);
        error_handled = TRUE;
      } else {
        IDBG_HIGH("%s:%d] Error, SSR is not handled", __func__, __LINE__ );
      }
      break;
    default:
      break;
    }

    if (!error_handled) {
      /* flush any outstanding acks */
      if (p_mod->caps.num_overlap) {
        rc = module_imgbase_client_handle_buffer_ack(p_client,
          IMG_EVT_ACK_FLUSH, NULL);
        if (IMG_ERROR(rc)) {
          IDBG_ERROR("%s:%d] Error in flushing acks", __func__, __LINE__);
        }
      }
      p_client->state = IMGLIB_STATE_ERROR;
    }
    break;
  }
  default:
    break;
  }
  IDBG_LOW("%s:%d] type %d X", __func__, __LINE__, p_event->type);
  return rc;
}

/**
 * Function: module_imgbase_client_extract_isp_stream_info
 *
 * Description: This function is exercised if the module is
 *           before cpp, in which case the frame dimensions,
 *           padding, and offset need to be extracted from isp's
 *           output dimension
 * Arguments:
 *   @p_client: imgbase client
 *   @pframe: frame pointer
 *   @stride: frame's stride
 *   @scanline: frame's scanline
 *   @offset: frame's offset
 *
 * Return values:
 *     none
 *
 * Notes: none
 **/
static void module_imgbase_client_get_isp_stream_info(
  imgbase_client_t *p_client, img_frame_t *pframe, int *stride,
  int *scanline, int *offset)
{
  int dis_x;
  int dis_y;
  int dis_width;
  int dis_height;
  int crop_x;
  int crop_y;
  int crop_width;
  int crop_height;
  int start_x;
  int start_y;

  if (p_client->dis_enable && p_client->is_update_valid) {
    dis_width = p_client->is_update.width;
    dis_height = p_client->is_update.height;
    dis_x = p_client->is_update.x;
    dis_y = p_client->is_update.y;
  } else {
    dis_width = p_client->isp_output_dim_stream_info.dim.width;
    dis_height = p_client->isp_output_dim_stream_info.dim.height;
    dis_x = 0;
    dis_y = 0;
  }

  if (p_client->stream_crop_valid && p_client->stream_crop.x &&
      p_client->stream_crop.y && p_client->stream_crop.crop_out_x &&
      p_client->stream_crop.crop_out_y && p_client->crop_required) {
    crop_width = p_client->stream_crop.crop_out_x;
    crop_height = p_client->stream_crop.crop_out_y;
    crop_x = p_client->stream_crop.x;
    crop_y = p_client->stream_crop.y;
  } else {
    crop_width = p_client->isp_output_dim_stream_info.dim.width;
    crop_height = p_client->isp_output_dim_stream_info.dim.height;
    crop_x = 0;
    crop_y = 0;
  }

  pframe->info.width = crop_width * dis_width /
      p_client->isp_output_dim_stream_info.dim.width;
  pframe->info.height = crop_height * dis_height /
      p_client->isp_output_dim_stream_info.dim.height;

  *stride =
    p_client->isp_output_dim_stream_info.buf_planes.plane_info.mp[0].stride;
  *scanline =
    p_client->isp_output_dim_stream_info.buf_planes.plane_info.mp[0].scanline;

  start_x = dis_x + (crop_x * dis_width /
      p_client->isp_output_dim_stream_info.dim.width);
  start_y = dis_y + (crop_y * dis_height /
      p_client->isp_output_dim_stream_info.dim.height);

  *offset = start_y * (*stride) + start_x;
}

/**
 * Function: module_imgbase_client_getbuf
 *
 * Description: This function is to fetching the input buffer
 *           based in buffer info
 * Arguments:
 *   @p_client: imgbase client
 *   @p_buf_divert: ISP buffer divert event structure
 *   @pframe: frame pointer
 *   @native_buf: flag to indicate if its a native buffer
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
static int module_imgbase_client_getbuf(imgbase_client_t *p_client,
  isp_buf_divert_t *p_buf_divert,
  img_frame_t *pframe,
  int native_buf)
{
  int rc = IMG_SUCCESS;
  uint32_t i = 0;
  uint32_t buf_idx;
  uint32_t size;
  uint8_t *p_addr = NULL;
  mct_module_t *p_mct_mod;
  uint32_t padded_size;
  int fd = -1;
  int stride = 0;
  int scanline = 0;
  int offset = 0;
  imgbase_stream_t *p_stream = NULL;
  mct_stream_map_buf_t *p_buf_holder;
  int stream_idx;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  int32_t *p_rotation = (int32_t *)img_get_meta(&p_client->current_meta,
    IMG_META_ROTATION);
  int32_t rotation;

  if (!p_rotation) {
    IDBG_HIGH("%s:%d] Rotation not set", __func__, __LINE__);
    rotation = 0;
  } else {
    rotation = *p_rotation;
  }

  stream_idx = module_imgbase_find_stream_by_identity(p_client,
    p_client->divert_identity);
  if (stream_idx < 0) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to idx %x", __func__,
      p_mod->name, __LINE__, p_client->divert_identity);
    return IMG_ERR_GENERAL;
  }

  p_stream = &p_client->stream[stream_idx];
  if (NULL == p_stream) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to client", __func__,
      p_mod->name, __LINE__);
    return IMG_ERR_GENERAL;
  }

  if (NULL == p_stream->p_sinkport) {
    IDBG_ERROR("%s:%d] NULL Sink port", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  if (p_client->before_cpp) {
    IDBG_LOW("%s:%d] mod %s is before cpp", __func__, __LINE__, p_mod->name);
    if (!p_client->isp_output_dim_stream_info_valid) {
      IDBG_ERROR("%s:%d] ISP output dim info required", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }

    module_imgbase_client_get_isp_stream_info(p_client, pframe,
      &stride, &scanline, &offset);
  } else {
    IDBG_HIGH("%s:%d] rotation %d", __func__, __LINE__, rotation);
    if ((CAM_STREAM_TYPE_OFFLINE_PROC == p_stream->stream_info->stream_type) &&
        ((0 == rotation) || (180 == rotation))) {
      pframe->info.width = (uint32_t)p_stream->stream_info->dim.width;
      pframe->info.height = (uint32_t)p_stream->stream_info->dim.height;
      stride = p_stream->stream_info->buf_planes.plane_info.mp[0].stride;
      scanline = p_stream->stream_info->buf_planes.plane_info.mp[0].scanline;
    } else {
      pframe->info.width = (uint32_t)p_stream->stream_info->dim.height;
      pframe->info.height = (uint32_t)p_stream->stream_info->dim.width;
      stride = p_stream->stream_info->buf_planes.plane_info.mp[0].scanline;
      scanline = p_stream->stream_info->buf_planes.plane_info.mp[0].stride;
    }
    offset = 0;
  }

  pframe->frame_cnt = 1;
  size = pframe->info.width * pframe->info.height;
  pframe->frame[0].plane_cnt = 2;
  pframe->idx = buf_idx = p_buf_divert->buffer.index;
  padded_size = (uint32_t)stride * (uint32_t)scanline;
  pframe->frame_id = p_buf_divert->buffer.sequence;
  pframe->timestamp = (uint64_t)p_buf_divert->buffer.timestamp.tv_sec
    * 1000000LL + (uint64_t)p_buf_divert->buffer.timestamp.tv_usec;

  p_mct_mod = MCT_MODULE_CAST((MCT_PORT_PARENT(p_stream->p_sinkport))->data);
  IDBG_MED("%s:%d] Dimension %dx%d, buf_idx %d, %x, mod %p, port %p, pproc %p,"
    " pad %dx%d, offset %d, frame_id %d",
    __func__, __LINE__,
    pframe->info.width, pframe->info.height, buf_idx,
    p_stream->identity,
    p_mct_mod,
    p_stream->p_sinkport,
    p_client->parent_mod,
    stride,
    scanline,
    offset,
    p_buf_divert->buffer.sequence);

  if (!native_buf) {
    p_buf_holder = mct_module_get_buffer(buf_idx,
      (p_client->parent_mod) ? p_client->parent_mod : p_mct_mod,
      IMGLIB_SESSIONID(p_stream->identity),
      IMGLIB_STREAMID(p_stream->identity));
    if (!p_buf_holder) {
      IDBG_ERROR("%s:%d] Buffer with id %d from stream 0x%x is not found",
        __func__, __LINE__, buf_idx, p_stream->identity);
      return IMG_ERR_INVALID_INPUT;
    }
    p_addr = p_buf_holder->buf_planes[0].buf;
    fd = p_buf_holder->buf_planes[0].fd;
  } else {
    p_addr = p_buf_divert->vaddr ?
        (uint8_t*)*(unsigned long*)p_buf_divert->vaddr : NULL;
    fd = p_buf_divert->fd;

    IDBG_MED("%s:%d] Native Buffer addr = %p, fd = %d",
     __func__, __LINE__, p_addr, fd);
  }

  if (NULL == p_addr) {
    IDBG_ERROR("%s:%d] NULL address", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  for (i = 0; i < pframe->frame[0].plane_cnt; i++) {
    pframe->frame[0].plane[i].fd = fd;
    pframe->frame[0].plane[i].offset = offset;
    if (i == 0) { /* Y plane */
      pframe->frame[0].plane[i].plane_type = p_client->plane_type[0];
      pframe->frame[0].plane[i].addr = p_addr;
      pframe->frame[0].plane[i].width = pframe->info.width;
      pframe->frame[0].plane[i].height = pframe->info.height;
      pframe->frame[0].plane[i].stride = (uint32_t)stride;
      pframe->frame[0].plane[i].scanline = (uint32_t)scanline;
      pframe->frame[0].plane[i].fd = fd;
    } else { /* Chroma plane */
      pframe->frame[0].plane[i].addr = p_addr + padded_size;
      pframe->frame[0].plane[i].width = pframe->info.width;
      pframe->frame[0].plane[i].height = pframe->info.height/2;
      pframe->frame[0].plane[i].stride = (uint32_t)stride;
      pframe->frame[0].plane[i].scanline = (uint32_t)scanline/2;
    }
    if (native_buf) {
      pframe->frame[0].plane[i].length = pframe->frame[0].plane[i].stride *
        pframe->frame[0].plane[i].scanline;
    } else {
      pframe->frame[0].plane[i].length =
        p_stream->stream_info->buf_planes.plane_info.mp[i].len;
    }
  }

  return rc;
}

/**
 * Function: module_imgbase_client_get_outputbuf
 *
 * Description: This function is to fetching the output buffer
 *           based in buffer info
 * Arguments:
 *   @p_client: imgbase client
 *   @pframe: frame pointer
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_imgbase_client_get_outputbuf(imgbase_client_t *p_client,
  img_frame_t *pframe)
{
  int rc = IMG_SUCCESS;
  uint32_t i = 0;
  uint32_t buf_idx;
  uint32_t size;
  uint8_t *p_addr;
  mct_module_t *p_mct_mod;
  uint32_t padded_size;
  int fd = -1;
  int stride, scanline;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  int32_t out_idx;
  imgbase_stream_t *p_stream = NULL;
  int stream_idx;
  int32_t *p_rotation;
  int32_t  rotation;
  mct_stream_map_buf_t *p_buf_holder;

  stream_idx = module_imgbase_find_stream_by_identity(p_client,
    p_client->divert_identity);
  if (stream_idx < 0) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to idx %x", __func__,
      p_mod->name, __LINE__, p_client->divert_identity);
    goto error;
  }

  p_rotation = (int32_t *)img_get_meta(&p_client->current_meta,
    IMG_META_ROTATION);

  if (!p_rotation) {
    IDBG_HIGH("%s:%d] Rotation not set", __func__, __LINE__);
    rotation = 0;
  } else {
    rotation = *p_rotation;
  }

  p_stream = &p_client->stream[stream_idx];
  if (NULL == p_stream) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to client", __func__,
      p_mod->name, __LINE__);
    goto error;
  }

  /* get output buffer */
  out_idx = module_imglib_common_get_buffer(p_mod->subdevfd,
    p_stream->identity);
  if (out_idx < 0) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    rc = IMG_ERR_GENERAL;
    goto error;
  }

  if ((CAM_STREAM_TYPE_OFFLINE_PROC == p_stream->stream_info->stream_type) &&
      ((0 == rotation) || (180 == rotation))) {
    pframe->info.width = (uint32_t)p_stream->stream_info->dim.width;
    pframe->info.height = (uint32_t)p_stream->stream_info->dim.height;
    stride = p_stream->stream_info->buf_planes.plane_info.mp[0].stride;
    scanline = p_stream->stream_info->buf_planes.plane_info.mp[0].scanline;
  } else {
    pframe->info.width = (uint32_t)p_stream->stream_info->dim.height;
    pframe->info.height = (uint32_t)p_stream->stream_info->dim.width;
    stride = p_stream->stream_info->buf_planes.plane_info.mp[0].scanline;
    scanline = p_stream->stream_info->buf_planes.plane_info.mp[0].stride;
  }

  pframe->frame_id = p_client->p_current_buf_div->buffer.sequence;
  pframe->frame_cnt = 1;
  size = pframe->info.width * pframe->info.height;
  pframe->frame[0].plane_cnt = 2;
  pframe->idx = buf_idx = (uint32_t)out_idx;
  padded_size = (uint32_t)stride * scanline;

  if (NULL == p_stream->p_sinkport) {
    IDBG_ERROR("%s:%d] NULL Sink port", __func__, __LINE__);
    rc = IMG_ERR_INVALID_INPUT;
    goto error;
  }

  p_mct_mod = MCT_MODULE_CAST((MCT_PORT_PARENT(p_stream->p_sinkport))->data);
  IDBG_MED("%s:%d] Dimension %dx%d buf_idx %d %x mod %p port %p pproc %p"
    " pad %dx%d frame_id %d",
    __func__, __LINE__,
    pframe->info.width, pframe->info.height, buf_idx,
    p_stream->identity,
    p_mct_mod,
    p_stream->p_sinkport,
    p_client->parent_mod,
    stride,
    scanline,
    pframe->frame_id);

  p_buf_holder = mct_module_get_buffer(buf_idx,
    (p_client->parent_mod) ? p_client->parent_mod : p_mct_mod,
    IMGLIB_SESSIONID(p_stream->identity),
    IMGLIB_STREAMID(p_stream->identity));

  if (NULL == p_buf_holder) {
    IDBG_ERROR("%s:%d] NULL address", __func__, __LINE__);
    rc = IMG_ERR_INVALID_INPUT;
    goto error;
  }
  p_addr = p_buf_holder->buf_planes[0].buf;
  fd = p_buf_holder->buf_planes[0].fd;

  for (i = 0; i < pframe->frame[0].plane_cnt; i++) {
    pframe->frame[0].plane[i].fd = fd;
    pframe->frame[0].plane[i].offset = 0;
    if (i == 0) { /* Y plane */
      pframe->frame[0].plane[i].addr = p_addr;
      pframe->frame[0].plane[i].width = pframe->info.width;
      pframe->frame[0].plane[i].height = pframe->info.height;
      pframe->frame[0].plane[i].stride = (uint32_t)stride;
      pframe->frame[0].plane[i].scanline = (uint32_t)scanline;
      pframe->frame[0].plane[i].length =
        IMG_LENGTH(pframe->frame[0].plane[i]);
    } else { /* Chroma plane */
      pframe->frame[0].plane[i].addr = p_addr + padded_size;
      pframe->frame[0].plane[i].width = pframe->info.width;
      pframe->frame[0].plane[i].height = pframe->info.height/2;
      pframe->frame[0].plane[i].stride = (uint32_t)stride;
      pframe->frame[0].plane[i].scanline = (uint32_t)scanline/2;
      pframe->frame[0].plane[i].length =
        IMG_LENGTH(pframe->frame[0].plane[i]);
    }
  }

  return rc;

error:
  IDBG_ERROR("%s:%d] Cannot get output buffer", __func__, __LINE__);
  return rc;
}

/**
 * Function: module_imgbase_client_handle_meta
 *
 * Description: This function is used to queue the metadata
 *   buffer
 *
 * Arguments:
 *   @p_client: IMGLIB_BASE client
 *   @p_stream: stream
 *   @current_frame_id: current frame id
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
static int module_imgbase_client_handle_meta(imgbase_client_t *p_client,
  imgbase_stream_t *p_stream, uint32_t current_frame_id)
{
  int rc = IMG_SUCCESS;
  img_meta_t             *p_meta;
  img_meta_t             *p_frame_meta;
  img_aec_info_t         *p_aec_info;
  img_awb_info_t         *p_awb_info;
  uint32_t               session_id = 0;
  float                  zoom_factor = 0;
  img_component_ops_t    *p_comp;
  module_imgbase_t       *p_mod;
  cam_pp_feature_config_t *p_config;

  p_comp = &p_client->comp;
  p_mod = (module_imgbase_t *)p_client->p_mod;
  p_config = &p_stream->stream_info->reprocess_config.pp_feature_config;

  p_meta = (img_meta_t *)calloc(1, sizeof(img_meta_t));
  if (!p_meta) {
    IDBG_ERROR("%s:%d] Error: Cannot allocate memory", __func__, __LINE__);
    rc = IMG_ERR_NO_MEMORY;
    goto error;
  }
  IDBG_MED("%s:%d] p_meta %p", __func__, __LINE__, p_meta);

  /* get zoom factor from session data */
  session_id = IMGLIB_SESSIONID(p_stream->stream_info->identity);
  zoom_factor = module_imgbase_get_zoom_ratio(p_client->parent_mod,
    session_id, p_config->zoom_level);

  rc = img_set_meta(&p_client->current_meta, IMG_META_ZOOM_FACTOR,
    &zoom_factor);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    goto error;
  }

  rc = img_set_meta(&p_client->current_meta, IMG_META_NUM_INPUT,
    &p_mod->caps.num_input);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    goto error;
  }

  *p_meta = p_client->current_meta;
  p_client->p_current_meta = p_meta;

  if (p_mod->modparams.imgbase_client_update_meta) {
    p_mod->modparams.imgbase_client_update_meta(p_client, p_meta);
    p_meta->frame_id = current_frame_id;
  }

  p_client->p_current_misc_data = module_imglib_common_get_miscdata(
    p_stream->stream_info, p_config->misc_buf_param.misc_buffer_index);

  if (p_client->p_current_misc_data) {
    img_misc_t misc_data;
    misc_data.data = p_client->p_current_misc_data->data;
    rc = img_set_meta(p_meta, IMG_META_MISC_DATA, &misc_data);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
      goto error;
    }
  }

  if (CAM_STREAM_TYPE_OFFLINE_PROC == p_stream->stream_info->stream_type) {
    /* retreive per-frame meta */
    rc = module_imglib_common_meta_get_by_frameid(current_frame_id,
      &p_client->meta_data_list, &p_frame_meta);

    p_aec_info = (img_aec_info_t *)img_get_meta(p_frame_meta,
      IMG_META_AEC_INFO);
    if (p_aec_info) {
      rc = img_set_meta(p_meta, IMG_META_AEC_INFO, p_aec_info);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
        goto error;
      }
    }
    p_awb_info = (img_awb_info_t *)img_get_meta(p_frame_meta,
      IMG_META_AWB_INFO);
    if (p_awb_info) {
      rc = img_set_meta(p_meta, IMG_META_AWB_INFO, p_awb_info);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
        goto error;
      }
    }
  }

  /* queue the meta buffer */
  rc = IMG_COMP_Q_META_BUF(p_comp, p_meta);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    goto error;
  }
  p_client->num_meta_queued++;

  return IMG_SUCCESS;

error:
  if (p_meta) {
    free(p_meta);
  }
  return rc;
}

/**
 * Function: module_imgbase_client_handle_overlap_batch_input
 *
 * Description: This function is used to handle the overlap
 *   batch input buffers. Batch processing refers to queueuing
 *   the current frame and queueing n previous frames. Retreive
 *   the previous n frames from the list of frames previously
 *   received as a buf divert into imglib and queue it to the
 *   component
 *
 * Arguments:
 *   @p_client: IMGLIB_BASE client
 *   @p_buf_divert: buffer divert structure
 *   @p_frame: current frame ptr
 *
 * Return values:
 *   imaging error values
 *
 * Notes: none
 **/
static int module_imgbase_client_handle_overlap_batch_input(
  imgbase_client_t *p_client, isp_buf_divert_t *p_buf_divert,
  img_frame_t *p_frame)
{
  int32_t rc;
  int i;
  uint32_t frame_id;
  int batch_curr_idx;
  int batch_start_idx;
  int batch_next_idx;
  imgbase_buf_t *p_buf = NULL;
  imgbase_buf_t *p_temp_buf = NULL;
  img_frame_t *p_temp_frame = NULL;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  img_component_ops_t *p_comp = &p_client->comp;

  p_buf = p_frame->private_data;
  frame_id = p_buf_divert->buffer.sequence;
  IDBG_HIGH("%s:%d] curr frame %d", __func__, __LINE__, frame_id);

  batch_curr_idx = p_client->buf_list_idx;
  batch_start_idx = (batch_curr_idx - p_mod->caps.num_overlap) < 0 ?
    (batch_curr_idx - p_mod->caps.num_overlap + MAX_IMGLIB_BATCH_SIZE) :
    (batch_curr_idx - p_mod->caps.num_overlap);

  pthread_mutex_lock(&p_client->buf_mutex);

  /* store current frame details within buf list */
  p_temp_buf = p_client->buf_list[batch_curr_idx];
  if (p_temp_buf) {
    if (p_temp_buf->divert_done == FALSE || p_temp_buf->ack.frame_id > 0) {
      IDBG_ERROR("%s:%d] Error, frame %d not yet forwarded, but attempting "
        "to overwrite %d %d", __func__, __LINE__, p_temp_buf->frame.frame_id,
        p_temp_buf->divert_done, p_temp_buf->ack.frame_id);
      pthread_mutex_unlock(&p_client->buf_mutex);
      return IMG_ERR_INVALID_OPERATION;
    }
    free(p_temp_buf);
    p_temp_buf = NULL;
  }
  p_client->buf_list[batch_curr_idx] = p_buf;
  p_client->buf_list_idx = (p_client->buf_list_idx + 1) % MAX_IMGLIB_BATCH_SIZE;

  /* queue batch overlap frames by finding previous frames within buf list.
    If previous frame is not found, queue dummy frame */
  for (i = 0; i < p_mod->caps.num_overlap; i++) {
    p_temp_frame = NULL;
    batch_next_idx = (batch_start_idx + i) % MAX_IMGLIB_BATCH_SIZE;
    p_temp_buf = p_client->buf_list[batch_next_idx];

    if (p_temp_buf) {
      IDBG_HIGH("%s:%d] curr frame %d, found previous frame %d",
        __func__, __LINE__, frame_id, p_temp_buf->frame.frame_id);
      p_temp_buf->queued_cnt++;
      p_temp_frame = &p_temp_buf->frame;
    } else {
      IDBG_HIGH("%s:%d] prev frame not found, use dummy", __func__, __LINE__);
      p_temp_frame = &p_client->dummy_frame;
    }

    /* queue frame */
    rc = IMG_COMP_Q_BUF(p_comp, p_temp_frame, IMG_IN);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Error, failed to qbuf %d", __func__, __LINE__, rc);
      pthread_mutex_unlock(&p_client->buf_mutex);
      return rc;
    }
    p_client->cur_buf_cnt++;
  }
  PRINT_BUF_LIST(p_client);

  pthread_mutex_unlock(&p_client->buf_mutex);
  return IMG_SUCCESS;
}

/**
 * Function: imgbase_client_handle_input_buf
 *
 * Description: This function is used to handle the input buffer
 *   from the upstream module
 *
 * Arguments:
 *   @p_client: IMGLIB_BASE client
 *   @p_buf_divert: Buffer divert structure
 *
 * Return values:
 *   imaging error values
 *
 * Notes: none
 **/
static int module_imgbase_client_handle_input_buf(imgbase_client_t *p_client,
  isp_buf_divert_t *p_buf_divert)
{
  int32_t rc;
  uint32_t frame_id;
  img_frame_t *p_frame;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  img_component_ops_t *p_comp = &p_client->comp;
  imgbase_buf_t *p_buf = (imgbase_buf_t *)calloc(1, sizeof(imgbase_buf_t));

  if (!p_buf) {
    IDBG_ERROR("%s:%d] Error: Cannot allocate memory", __func__, __LINE__);
    return IMG_ERR_NO_MEMORY;
  }

  p_frame = &p_buf->frame;
  p_buf->buf_divert = *p_buf_divert;
  p_frame->private_data = p_buf;

  /* Get input buffer */
  rc = module_imgbase_client_getbuf(p_client, p_buf_divert, p_frame,
    p_buf_divert->native_buf);
  if (rc != IMG_SUCCESS) {
    IDBG_ERROR("%s_%s:%d] Error: Cannot get frame", __func__, p_mod->name,
      __LINE__);
    rc = IMG_ERR_GENERAL;
    goto error;
  }

  frame_id = p_buf_divert->buffer.sequence;
  IDBG_HIGH("%s:%d] dim %dx%d id %d", __func__, __LINE__,
    p_frame->info.width, p_frame->info.height, frame_id);

  /* Store input frame frameId */
  rc = module_imgbase_client_store_frame_id(p_client, frame_id,
    p_client->cur_buf_cnt);
  if (rc != IMG_SUCCESS) {
    IDBG_ERROR("%s_%s:%d] Error storing frameIds", __func__, p_mod->name,
      __LINE__);
    goto error;
  }

  /* Queue input buffer */
  p_buf->queued_cnt++;
  rc = IMG_COMP_Q_BUF(p_comp, p_frame, IMG_IN);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] Error, failed to qbuf %d", __func__, __LINE__, rc);
    goto error;
  }
  p_client->cur_buf_cnt++;

  /* Queue overlap batch buffers */
  if (p_mod->caps.num_overlap) {
    rc = module_imgbase_client_handle_overlap_batch_input(p_client,
      p_buf_divert, p_frame);
    if (rc != IMG_SUCCESS) {
      IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
      goto error;
    }
  }

  IDBG_LOW("%s_%s:%d] bufs queued %d, bufs needed %d, bufs overlap %d",
    __func__, p_mod->name, __LINE__, p_client->cur_buf_cnt,
    p_mod->caps.num_input, p_mod->caps.num_overlap);

  return IMG_SUCCESS;

error:
  if (p_buf) {
    free(p_buf);
  }
  IDBG_ERROR("%s_%s:%d] Error rc %d", __func__, p_mod->name, __LINE__, rc);
  return rc;
}

/**
 * Function: module_imgbase_client_handle_output_buf
 *
 * Description: This function is used to queue the output
 *   buffer to the component. For the output buffer, either
 *   fetch the output buffer OR use the preallocated internal
 *   buffers
 *
 * Arguments:
 *   @p_client: IMGLIB_BASE client
 *   @p_buf_divert: Buffer divert structure
 *
 * Return values:
 *   imaging error values
 *
 * Notes: none
 **/
static int module_imgbase_client_handle_output_buf(imgbase_client_t *p_client,
  isp_buf_divert_t *p_buf_divert)
{
  int32_t rc = IMG_SUCCESS;
  int i;
  int idx = -1;
  uint32_t out_stride;
  img_frame_t *p_out_frame;
  imgbase_buf_t *p_buf = NULL;
  uint8_t *poutframeinfo = NULL;
  img_internal_bufnode_t *p_internal_buf_node = NULL;
  img_component_ops_t *p_comp = &p_client->comp;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;

  if (p_mod->caps.num_output == 0) {
    IDBG_HIGH("%s:%d] num output buf is 0", __func__, __LINE__);
    return rc;
  }

  p_buf = (imgbase_buf_t *)calloc(1, sizeof(imgbase_buf_t));
  if (!p_buf) {
    IDBG_ERROR("%s:%d] Error, Cannot allocate memory", __func__, __LINE__);
    return IMG_ERR_NO_MEMORY;
  }

  /* Fetch output buffer */
  if (!p_mod->caps.use_internal_bufs) {
    poutframeinfo = (uint8_t *)calloc(1, sizeof(img_frame_t) + sizeof(int));
    if (!poutframeinfo) {
      IDBG_ERROR("%s:%d] Error: Cannot allocate memory", __func__, __LINE__);
      rc = IMG_ERR_NO_MEMORY;
      goto error;
    }

    p_out_frame = (img_frame_t *)poutframeinfo;
    p_buf->buf_divert = *p_buf_divert;
    p_out_frame->private_data = p_buf;

    rc = module_imgbase_client_get_outputbuf(p_client, p_out_frame);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Error, rc %d", __func__, __LINE__, rc);
      goto error;
    }

    /* queue the buffer */
    rc = IMG_COMP_Q_BUF(p_comp, p_out_frame, IMG_OUT);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Error, failed to qbuf %d", __func__, __LINE__, rc);
      goto error;
    }
  } else {
    /* Use the preallocated internal buffers for output */
    pthread_mutex_lock(&p_client->buf_mutex);

    /* Find unused internal buffer */
    for (i = 0; i < MAX_IMGLIB_INTERNAL_BUFS; i++) {
      if (p_client->internal_buf.buf[i].ref_cnt == 0 &&
        p_client->internal_buf.buf[i].is_init == TRUE) {
        idx = i;
        break;
      }
    }
    if (idx < 0) {
      IDBG_ERROR("%s:%d] Error, internal buf unavailable", __func__, __LINE__);
      pthread_mutex_unlock(&p_client->buf_mutex);
      rc = IMG_ERR_NO_MEMORY;
      goto error;
    }

    p_internal_buf_node = &p_client->internal_buf.buf[idx];

    /* Set output frame details */
    p_out_frame = &p_internal_buf_node->frame_info;
    p_out_frame->frame_id = p_buf_divert->buffer.sequence;
    p_out_frame->idx = p_buf_divert->buffer.index;
    p_out_frame->frame_cnt = 1;
    out_stride = QIMG_CEILINGN(p_out_frame->info.width, 16);

    p_buf->buf_divert = *p_buf_divert;
    p_out_frame->private_data = p_buf;

    for (i = 0; i < p_out_frame->info.num_planes; i++) {
      p_out_frame->frame[0].plane_cnt = p_out_frame->info.num_planes;
      p_out_frame->frame[0].plane[i].fd = p_internal_buf_node->mem_handle.fd;
      p_out_frame->frame[0].plane[i].offset = 0;
      QIMG_PL_TYPE(p_out_frame, i) = PLANE_Y;
      if (i == 0) { // Y plane
        p_out_frame->frame[0].plane[i].addr = p_internal_buf_node->mem_handle.vaddr;
        p_out_frame->frame[0].plane[i].width = p_out_frame->info.width;
        p_out_frame->frame[0].plane[i].height = p_out_frame->info.height;
        p_out_frame->frame[0].plane[i].stride = (uint32_t)out_stride;
        p_out_frame->frame[0].plane[i].scanline = (uint32_t)p_out_frame->info.height;
        p_out_frame->frame[0].plane[i].length = out_stride * p_out_frame->info.height;
      } else { // Chroma plane
        p_out_frame->frame[0].plane[i].addr = QIMG_ADDR(p_out_frame, 0) + QIMG_LEN(p_out_frame, 0);
        p_out_frame->frame[0].plane[i].width = p_out_frame->info.width;
        p_out_frame->frame[0].plane[i].height = p_out_frame->info.height/2;
        p_out_frame->frame[0].plane[i].stride = (uint32_t)out_stride;
        p_out_frame->frame[0].plane[i].scanline = (uint32_t)p_out_frame->info.height/2;
        p_out_frame->frame[0].plane[i].length = (out_stride * p_out_frame->info.height)/2;
      }
    }

    IDBG_HIGH("%s:%d] queue outbuf %p, %dx%d %dx%d, frameid %d, divert %p",
      __func__, __LINE__, QIMG_ADDR(p_out_frame, 0),
      QIMG_WIDTH(p_out_frame, 0), QIMG_HEIGHT(p_out_frame, 0),
      QIMG_STRIDE(p_out_frame, 0), QIMG_SCANLINE(p_out_frame, 0),
      p_out_frame->frame_id, &p_buf->buf_divert);

    /* queue output buffer */
    rc = IMG_COMP_Q_BUF(p_comp, p_out_frame, IMG_OUT);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Error, failed to qbuf %d", __func__, __LINE__, rc);
      pthread_mutex_unlock(&p_client->buf_mutex);
      goto error;
    }

    /* indicate buffer is in use */
    p_internal_buf_node->ref_cnt++;
    p_internal_buf_node->input_ack_held = TRUE;

    PRINT_INTERNAL_BUF_LIST(p_client);
    pthread_mutex_unlock(&p_client->buf_mutex);
  }

  return rc;

error:
  if (poutframeinfo) {
    free(poutframeinfo);
  }
  if (p_buf) {
    free(p_buf);
  }
  return rc;
}
/**
 * Function: module_imgbase_client_handle_buffer
 *
 * Description: This function is used to handle the divert
 *            buffer event
 *
 * Arguments:
 *   @p_client: IMGLIB_BASE client
 *   @p_buf_divert: Buffer divert structure
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_imgbase_client_handle_buffer(imgbase_client_t *p_client,
  isp_buf_divert_t *p_buf_divert)
{
  int rc = IMG_SUCCESS;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  imgbase_stream_t *p_stream = NULL;
  int stream_idx;
  uint32_t frame_id = p_buf_divert->buffer.sequence;

  stream_idx = module_imgbase_find_stream_by_identity(p_client,
    p_client->divert_identity);
  if (stream_idx < 0) {
    IDBG_ERROR("%s_%s %d] Cannot find stream mapped to idx %x", __func__,
      p_mod->name, __LINE__, p_client->divert_identity);
    return IMG_ERR_GENERAL;
  }

  p_stream = &p_client->stream[stream_idx];
  if (NULL == p_stream) {
    IDBG_ERROR("%s_%s:%d] Cannot find stream mapped to client", __func__,
      p_mod->name, __LINE__);
    return IMG_ERR_GENERAL;
  }

  IDBG_LOW("%s_%s:%d] E", __func__, p_mod->name, __LINE__);

  pthread_mutex_lock(&p_client->mutex);
  if (!p_client->stream_on) {
    IDBG_LOW("%s_%s:%d] Not streaming, discard buffers", __func__,
      p_mod->name, __LINE__);
    pthread_mutex_unlock(&p_client->mutex);
    return IMG_ERR_INVALID_OPERATION;
  }
  pthread_mutex_unlock(&p_client->mutex);

  p_client->p_current_buf_div = p_buf_divert;

  /* Queue input bufs */
  rc = module_imgbase_client_handle_input_buf(p_client,
    p_buf_divert);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s_%s:%d] Error: cannot handle input buf",
      __func__, p_mod->name, __LINE__);
    goto error;
  }

  /* Queue each meta along with the input buf if there are
    an equal number of meta bufs and input bufs */
  if (p_mod->caps.num_meta && p_mod->caps.num_meta == p_mod->caps.num_input) {
    rc = module_imgbase_client_handle_meta(p_client, p_stream, frame_id);
    if (rc != IMG_SUCCESS) {
      IDBG_ERROR("%s_%s:%d] Error rc %d", __func__, p_mod->name, __LINE__, rc);
      goto error;
    }
    IDBG_LOW("%s_%s:%d] queue meta with frame %d",
      __func__, p_mod->name, __LINE__, frame_id);
  }


  if (p_client->cur_buf_cnt >= p_mod->caps.num_input) {
    p_client->cur_buf_cnt = 0;

    /* Queue the output buffer */
    rc = module_imgbase_client_handle_output_buf(p_client, p_buf_divert);
    if (rc != IMG_SUCCESS) {
      IDBG_ERROR("%s_%s:%d] Error queuing the output buffer %d", __func__,
        p_mod->name, __LINE__, rc);
      goto error;
    }

    /* Add imglib debug data to hal meta */
    if (CAM_STREAM_TYPE_OFFLINE_PROC == p_stream->stream_info->stream_type) {
      rc = module_imglib_common_add_imglib_debug_meta(
        p_client->hal_meta_data_list, p_client->meta_data_list);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s_%s:%d] Cannot add imglib debug\n",
          __func__, p_mod->name, __LINE__);
      }
    }

    /* If num meta != num input, it was not queued with the input bufs.
      Queue it now with the output buf */
    if (p_mod->caps.num_meta &&
      (p_mod->caps.num_meta != p_mod->caps.num_input)) {
      rc = module_imgbase_client_handle_meta(p_client, p_stream, frame_id);
        if (rc != IMG_SUCCESS) {
          IDBG_ERROR("%s_%s:%d] Error rc %d", __func__, p_mod->name,
            __LINE__, rc);
          goto error;
       }
      IDBG_LOW("%s_%s:%d] queue meta with last frame %d",
        __func__, p_mod->name, __LINE__, frame_id);
    }
  }

  IDBG_MED("%s_%s:%d] X", __func__, p_mod->name, __LINE__);
  return 0;

error:
  return rc;
}

/**
 * Function: module_imgbase_client_get_frame
 *
 * Description: This function is to fetching the output buffer
 *           based in buffer info
 * Arguments:
 *   @p_appdata: imgbase client
 *   @pframe: frame pointer
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_imgbase_client_get_frame(void *p_appdata,
  img_frame_t **pp_frame)
{
  int rc = IMG_SUCCESS;

  assert(p_appdata != NULL);
  assert(pp_frame != NULL);
  *pp_frame = NULL;

  imgbase_client_t *p_client = (imgbase_client_t *)p_appdata;
  img_frame_t *p_out_frame;
  uint8_t *l_frameid;

  IDBG_HIGH("%s:%d] E", __func__, __LINE__);
  uint8_t *poutframeinfo = (uint8_t *)calloc(1,
    sizeof(img_frame_t) + sizeof(int));
  if (!poutframeinfo) {
    IDBG_ERROR("%s:%d] Error: Cannot allocate memory", __func__, __LINE__);
    rc = IMG_ERR_NO_MEMORY;
    goto error;
  }

  p_out_frame = (img_frame_t *)poutframeinfo;
  l_frameid = p_out_frame->private_data =
    (uint8_t *)poutframeinfo + sizeof(img_frame_t);
  /* Queue output buffer */
  rc = module_imgbase_client_get_outputbuf(p_client, p_out_frame);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    goto error;
  }
  *l_frameid = (uint8_t)p_client->frame_id;
  *pp_frame = p_out_frame;
  return rc;

error:
  if (poutframeinfo) {
    free(poutframeinfo);
  }
  return rc;
}

/**
 * Function: module_imgbase_client_release_frame
 *
 * Description: This function is to release the output buffer
 *           based in buffer info
 * Arguments:
 *   @p_appdata: imgbase client
 *   @pframe: frame pointer
 *   @is_dirty: flag to indicate whether to do buf done
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_imgbase_client_release_frame(void *p_appdata,
  img_frame_t *p_frame,
  int is_dirty)
{
  imgbase_client_t *p_client = (void *)p_appdata;

  if (!p_frame) {
    IDBG_ERROR("%s:%d] Error invalid release", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  IDBG_HIGH("%s:%d] p_client %p is_dirty %d", __func__, __LINE__, p_frame,
    is_dirty);

  if (p_client->p_current_meta) {
    cam_stream_img_prop_t prop;
    memset(&prop, 0x0, sizeof(cam_stream_img_prop_t));
    cam_rect_t *p_rect = &prop.crop;
    img_rect_t *output_crop = (img_rect_t *)img_get_meta(
      p_client->p_current_meta, IMG_META_OUTPUT_ROI);
    if (output_crop) {
      p_rect->left = (int32_t)output_crop->pos.x;
      p_rect->top = (int32_t)output_crop->pos.y;
      p_rect->width = (int32_t)output_crop->size.width;
      p_rect->height = (int32_t)output_crop->size.height;
    } else {
      IDBG_ERROR("%s:%d failed: Output crop not set\n", __func__, __LINE__);
    }
    prop.input.width = (int32_t)p_frame->info.width;
    prop.input.height = (int32_t)p_frame->info.height;
    module_imgbase_client_enqueue_streambuf(p_client,
      p_client->p_current_meta, &prop);
  }

  module_imgbase_client_handle_outbuf_done(p_client, p_frame, (int8_t)is_dirty);

  return IMG_SUCCESS;
}

/**
 * Function: module_imgbase_client_start
 *
 * Description: This function is used to start the IMGLIB_BASE
 *              client
 *
 * Arguments:
 *   @p_client: IMGLIB_BASE client
 *   @identity: identity of the stream
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_imgbase_client_start(imgbase_client_t *p_client, uint32_t identity)
{
  int rc = IMG_SUCCESS;
  img_component_ops_t *p_comp = &p_client->comp;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  int32_t stream_idx, stride, scanline, length, num_planes;
  imgbase_stream_t *p_stream = NULL;
  int i;

  IDBG_LOW("%s:%d] E", __func__, __LINE__);
  pthread_mutex_lock(&p_client->mutex);

  if (!p_client->stream_on) {
    p_client->first_frame = TRUE;
    if (p_client->caps.num_input > 0) {
      img_caps_t caps;
      memset(&caps, 0x0, sizeof(img_caps_t));
      caps.num_input = p_client->caps.num_input;
      caps.num_output = p_client->caps.num_output;
      rc = IMG_COMP_START(p_comp, &caps);
    } else {
      rc = IMG_COMP_START(p_comp, NULL);
    }

    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] start failed %d", __func__, __LINE__, rc);
      goto error;
    }
    p_client->state = IMGLIB_STATE_STARTED;
  }

  /* Allocate internal buffers if required */
  if (p_mod->caps.use_internal_bufs) {
    stream_idx = module_imgbase_find_stream_by_identity(p_client,
      identity);
    if (stream_idx < 0) {
      IDBG_ERROR("%s:%d] Cannot find stream mapped to idx %x", __func__,
        __LINE__, identity);
      goto error;
    }

    p_stream = &p_client->stream[stream_idx];
    if (NULL == p_stream) {
      IDBG_ERROR("%s:%d] Cannot find stream mapped to client", __func__,
       __LINE__);
      goto error;
    }

    stride = p_stream->stream_info->buf_planes.plane_info.mp[0].stride;
    scanline = p_stream->stream_info->buf_planes.plane_info.mp[0].scanline;
    num_planes = p_stream->stream_info->buf_planes.plane_info.num_planes;

    /* Internal buffer size is based on stream info size */
    length = stride * scanline;
    if (length <= 0) {
      IDBG_ERROR("%s:%d] Error, invalid length for buffer", __func__, __LINE__);
      goto error;
    }

    for (i = 0; i < MAX_IMGLIB_INTERNAL_BUFS; i++) {
      rc = img_buffer_get(p_client->internal_buf.type, -1, TRUE, length,
        &p_client->internal_buf.buf[i].mem_handle);
      if (rc != IMG_SUCCESS) {
        IDBG_ERROR("%s:%d] Error, img buffer get failed, %d, %dx%d, %d %d",
          __func__, __LINE__, p_client->internal_buf.type,
          p_stream->stream_info->dim.width, p_stream->stream_info->dim.height,
          stride, scanline);
        goto error;
      }
      p_client->internal_buf.buf[i].frame_info.info.width = stride;
      p_client->internal_buf.buf[i].frame_info.info.height = scanline;
      p_client->internal_buf.buf[i].frame_info.info.num_planes = num_planes;
      p_client->internal_buf.buf[i].is_init = TRUE;
    }
    IDBG_MED("%s:%d] Allocate Internal Buf successful type %d, %dx%d, %d %d",
      __func__, __LINE__, p_client->internal_buf.type,
      p_stream->stream_info->dim.width, p_stream->stream_info->dim.height,
      stride, scanline);
    PRINT_INTERNAL_BUF_LIST(p_client);
  }

  p_client->stream_on++;
  p_client->dummy_frame.private_data = NULL;
  p_client->dummy_frame.frame[0].plane[0].addr = NULL;
  p_client->dummy_frame.frame[0].plane[1].addr = NULL;
  pthread_mutex_unlock(&p_client->mutex);

  p_mod = (module_imgbase_t *)p_client->p_mod;
  if (p_mod->modparams.imgbase_client_streamon) {
    p_mod->modparams.imgbase_client_streamon(p_client);
  }

  IDBG_HIGH("%s:%d] X", __func__, __LINE__);
  return rc;

error:
  pthread_mutex_lock(&p_client->buf_mutex);
  for (i = 0; i < MAX_IMGLIB_INTERNAL_BUFS; i++) {
    if (p_client->internal_buf.buf[i].mem_handle.handle) {
      img_buffer_release(&p_client->internal_buf.buf[i].mem_handle);
      p_client->internal_buf.buf[i].is_init = FALSE;
    }
  }
  pthread_mutex_unlock(&p_client->buf_mutex);
  pthread_mutex_unlock(&p_client->mutex);

  IDBG_ERROR("%s:%d] Error, X", __func__, __LINE__);
  return rc;
}

/**
 * Function: module_imgbase_client_stop
 *
 * Description: This function is used to stop the IMGLIB_BASE
 *              client
 *
 * Arguments:
 *   @p_client: IMGLIB_BASE client
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_imgbase_client_stop(imgbase_client_t *p_client)
{
  int rc = IMG_SUCCESS, i;
  img_component_ops_t *p_comp = &p_client->comp;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  img_core_ops_t *p_core_ops = NULL;

  pthread_mutex_lock(&p_client->mutex);

  p_client->stream_on--;

  if (!p_client->stream_on) {
    IDBG_MED("%s: Calling abort in component", __func__);
    rc = IMG_COMP_ABORT(p_comp, NULL);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] abort failed %d", __func__, __LINE__, rc);
      pthread_mutex_unlock(&p_client->mutex);
      return rc;
    }

    /* Unbind dual camera */
    if (p_client->is_binded && p_client->p_peer_comp) {
      p_core_ops = &p_mod->core_ops;
      if (p_core_ops->unbind) {
        IDBG_MED("%s_%s:%d] unbind p_comp %p peer_comp %p", __func__,
          p_mod->name, __LINE__,
          p_comp, p_client->p_peer_comp);
        rc = IMG_COMP_UNBIND(p_core_ops, p_comp, p_client->p_peer_comp);
        p_client->is_binded = FALSE;
        if (IMG_ERROR(rc)) {
          IDBG_ERROR("%s:%d] unbind failed %d", __func__, __LINE__, rc);
        }
      }
    }

    /* flush any outstanding acks */
    if (p_mod->caps.num_overlap) {
      rc = module_imgbase_client_handle_buffer_ack(p_client,
        IMG_EVT_ACK_FLUSH, NULL);
      if (IMG_ERROR(rc)) {
        IDBG_ERROR("%s:%d] Error in flushing acks", __func__, __LINE__);
      }
    }

    /* Release internal buffers */
    pthread_mutex_lock(&p_client->buf_mutex);
    if (p_mod->caps.use_internal_bufs) {
      for (i = 0; i < MAX_IMGLIB_INTERNAL_BUFS; i++) {
        if (p_client->internal_buf.buf[i].mem_handle.handle) {
          while (p_client->internal_buf.buf[i].ref_cnt > 0) {
            IDBG_HIGH("%s:%d:] waiting for ack frame %d refcnt %d", __func__,
              __LINE__, p_client->internal_buf.buf[i].frame_info.frame_id,
              p_client->internal_buf.buf[i].ref_cnt);
          }

          rc = img_buffer_release(&p_client->internal_buf.buf[i].mem_handle);
          if (IMG_ERROR(rc)) {
            IDBG_ERROR("%s:%d:] Error, buf release fail", __func__, __LINE__);
            pthread_mutex_unlock(&p_client->buf_mutex);
            pthread_mutex_unlock(&p_client->mutex);
            return rc;
          }
          p_client->internal_buf.buf[i].is_init = FALSE;
        }
      }
    }
    pthread_mutex_unlock(&p_client->buf_mutex);

    mct_list_free_all(p_client->meta_data_list, module_imglib_free_func);
    p_client->meta_data_list = NULL;

    mct_list_free_list(p_client->hal_meta_data_list);
    p_client->hal_meta_data_list = NULL;

    p_client->state = IMGLIB_STATE_INIT;
    img_q_flush_and_destroy(&p_mod->msg_thread.msg_q);
    img_q_flush_and_destroy(&p_client->stream_parm_q);

    p_client->is_update_valid = FALSE;
    p_client->stream_crop_valid = FALSE;
  }

  if (p_mod->modparams.imgbase_client_streamoff) {
    p_mod->modparams.imgbase_client_streamoff(p_client);
  }

  pthread_mutex_unlock(&p_client->mutex);
  IDBG_MED("%s:%d] X", __func__, __LINE__);
  return rc;
}

/**
 * Function: module_imgbase_client_destroy
 *
 * Description: This function is used to destroy the imgbase client
 *
 * Arguments:
 *   @p_client: imgbase client
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
void module_imgbase_client_destroy(imgbase_client_t *p_client)
{
  int rc = IMG_SUCCESS;
  img_component_ops_t *p_comp = NULL;
  module_imgbase_t *p_mod;
  int i;

  if (NULL == p_client) {
    return;
  }

  p_mod = (module_imgbase_t *)p_client->p_mod;
  p_comp = &p_client->comp;
  IDBG_MED("%s_%s:%d] state %d", __func__, p_mod->name, __LINE__,
    p_client->state);

  if (IMGLIB_STATE_STARTED == p_client->state) {
    module_imgbase_client_stop(p_client);
  }

  if (IMGLIB_STATE_INIT == p_client->state) {
    rc = IMG_COMP_DEINIT(p_comp);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] deinit failed %d", __func__, __LINE__, rc);
    }
    p_client->state = IMGLIB_STATE_IDLE;
  }

  if (p_mod->modparams.imgbase_client_destroy) {
    p_mod->modparams.imgbase_client_destroy(p_client);
  }

  if (IMGLIB_STATE_IDLE == p_client->state) {
    img_q_deinit(&p_client->stream_parm_q);
    pthread_mutex_destroy(&p_client->mutex);
    pthread_mutex_destroy(&p_client->buf_mutex);
    pthread_cond_destroy(&p_client->cond);
    if (p_client->thread_ops.client_id) {
      img_thread_mgr_unreserve_threads(p_client->thread_ops.client_id);
      p_client->thread_ops.client_id = 0;
    }
    for (i = 0; i < MAX_IMGLIB_BATCH_SIZE; i++) {
      imgbase_buf_t *p_temp_buf = p_client->buf_list[i];
      if (p_temp_buf) {
        free(p_temp_buf);
        p_temp_buf = NULL;
      }
    }

    free(p_client);
    p_client = NULL;
  }
  IDBG_MED("%s:%d] X", __func__, __LINE__);

}

/**
 * Function: module_imgbase_client_init
 *
 * Description: This function is used to initialize the imgbase
 * client
 *
 * Arguments:
 *   @p_appdata: imgbase client
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int32_t module_imgbase_client_init(void *p_appdata)
{
  int32_t rc = IMG_SUCCESS;
  imgbase_client_t *p_client = (imgbase_client_t *)p_appdata;
  img_component_ops_t *p_comp = &p_client->comp;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  uint32_t deinit_rc;

  img_frame_ops_t l_frameops = {
    .get_frame = module_imgbase_client_get_frame,
    .release_frame = module_imgbase_client_release_frame,
    .dump_frame = img_dump_frame,
    .get_meta = img_get_meta,
    .set_meta = img_set_meta,
    .image_copy = img_image_copy,
    .image_scale = img_sw_downscale_2by2,
  };

  IDBG_MED("%s:%d] rc %d E", __func__, __LINE__, rc);
  rc = IMG_COMP_INIT(p_comp, p_client, &p_client->comp_init_params);
  if (IMG_ERROR(rc)) {
    IDBG_ERROR("%s:%d] init failed %d", __func__, __LINE__, rc);
    goto init_error;
  }

  rc = IMG_COMP_SET_CB(p_comp, module_imgbase_client_event_handler);
  if (rc != IMG_SUCCESS) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    goto error;
  }

  rc = IMG_COMP_SET_PARAM(p_comp, QIMG_PARAM_CAPS, &p_mod->caps);
  if (rc != IMG_SUCCESS) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    goto error;
  }

  l_frameops.p_appdata = p_client;
  rc = IMG_COMP_SET_PARAM(p_comp, QIMG_PARAM_FRAME_OPS, &l_frameops);
  if (rc != IMG_SUCCESS) {
    IDBG_ERROR("%s:%d] rc %d", __func__, __LINE__, rc);
    goto error;
  }

  QIMG_LOCK(&p_client->mutex);
  p_client->state = IMGLIB_STATE_INIT;
  QIMG_UNLOCK(&p_client->mutex);

  IDBG_HIGH("%s:%d] p_client->state %d X", __func__, __LINE__, p_client->state);
  return rc;

error:
  deinit_rc = IMG_COMP_DEINIT(p_comp);
  if (IMG_ERROR(deinit_rc)) {
    IDBG_ERROR("%s:%d] Deinit failed rc %d", __func__, __LINE__, deinit_rc);
  }
init_error:
  IDBG_ERROR("%s:%d] Transition to INIT failed", __func__, __LINE__);
  return rc;
}

/** Function: module_imgbase_client_create
 *
 * Description: This function is used to create the IMGLIB_BASE client
 *
 * Arguments:
 *   @p_mct_mod: mct module pointer
 *   @p_port: mct port pointer
 *   @identity: identity of the stream
 *   @session_id: session id
 *   @p_stream_info: pointer to the stream info
 *
 * Return values:
 *     imaging error values
 *
 * Notes: none
 **/
int module_imgbase_client_create(mct_module_t *p_mct_mod,
  mct_port_t *p_port,
  uint32_t session_id,
  mct_stream_info_t *p_stream_info)
{
  int rc = IMG_SUCCESS;
  imgbase_client_t *p_client = NULL;
  img_component_ops_t *p_comp = NULL;
  img_core_ops_t *p_core_ops = NULL;
  module_imgbase_t *p_mod =
    (module_imgbase_t *)p_mct_mod->module_private;
  mct_list_t *p_temp_list = NULL;
  img_init_params_t init_params;
  img_core_type_t thread_affinity[1] = {IMG_CORE_ARM};

  IDBG_MED("%s:%d]", __func__, __LINE__);
  p_client = (imgbase_client_t *)malloc(sizeof(imgbase_client_t));
  if (NULL == p_client) {
    IDBG_ERROR("%s:%d] IMGLIB_BASE client alloc failed", __func__, __LINE__);
    return IMG_ERR_NO_MEMORY;
  }

  /* initialize the variables */
  memset(p_client, 0x0, sizeof(imgbase_client_t));
  memset(&init_params, 0x0, sizeof(img_init_params_t));

  pthread_mutex_init(&p_client->mutex, NULL);
  pthread_cond_init(&p_client->cond, NULL);
  pthread_mutex_init(&p_client->buf_mutex, NULL);
  p_client->state = IMGLIB_STATE_IDLE;
  img_q_init(&p_client->stream_parm_q, "stream_parm_q");

  p_core_ops = &p_mod->core_ops;
  p_comp = &p_client->comp;

  rc = IMG_COMP_CREATE(p_core_ops, p_comp);
  if (IMG_ERROR(rc)) {
   IDBG_ERROR("%s:%d] create failed %d", __func__, __LINE__, rc);
    goto error;
  }

  /* add the client to the list */
  p_temp_list = mct_list_append(p_mod->imgbase_client, p_client,
    NULL, NULL);
  if (NULL == p_temp_list) {
    IDBG_ERROR("%s:%d] list append failed", __func__, __LINE__);
    rc = IMG_ERR_NO_MEMORY;
    goto error;
  }

  p_mod->imgbase_client = p_temp_list;
  p_client->parent_mod = p_mod->parent_mod ? p_mod->parent_mod : p_mct_mod;
  p_client->p_mod = p_mod;
  p_port->port_private = p_client;
  p_client->num_ack = 1;
  p_client->internal_buf.type = IMG_BUFFER_ION_IOMMU;
  p_client->process_all_frames = FALSE;
  p_client->session_id = session_id;
  p_client->ion_fd = -1;
  p_client->session_client =
   (p_stream_info->stream_type == CAM_STREAM_TYPE_PARM);
  p_client->thread_job.client_id = p_mod->th_client_id;
  /* set default stream to process params */
  p_client->streams_to_process = p_mod->modparams.streams_to_process;
  p_client->before_cpp = ((p_mod->modparams.streams_to_process &
    IMG_2_MASK(CAM_STREAM_TYPE_OFFLINE_PROC)) != 0);

  /* Thread operations */
  p_client->thread_ops.schedule_job      = img_thread_mgr_schedule_job;
  p_client->thread_ops.get_time_of_job   = img_thread_mgr_get_time_of_job;
  p_client->thread_ops.get_time_of_joblist = img_thread_mgr_get_time_of_joblist;
  p_client->thread_ops.wait_for_completion_by_jobid =
    img_thread_mgr_wait_for_completion_by_jobid;
  p_client->thread_ops.wait_for_completion_by_joblist =
    img_thread_mgr_wait_for_completion_by_joblist;
  p_client->thread_ops.wait_for_completion_by_clientid =
    img_thread_mgr_wait_for_completion_by_clientid;

  /* set init params */
  p_client->comp_init_params.client_id = p_client->thread_job.client_id;
  if (p_mod->modparams.imgbase_client_init_params) {
    p_mod->modparams.imgbase_client_init_params(&init_params);
  }
  p_client->comp_init_params = init_params;

  if (p_mod->modparams.imgbase_client_created) {
    p_mod->modparams.imgbase_client_created(p_client);
  }

  if (!p_client->async_init) {
    rc = module_imgbase_client_init(p_client);
  } else {
    rc = mod_imgbase_schedule_ind_job(p_client,
      &p_client->thread_job,
      p_client->last_job_id,
      IMG_CORE_ARM,
      thread_affinity,
      TRUE,
      module_imgbase_client_init,
      p_client);
  }
  if (IMG_ERROR(rc)) {
   IDBG_ERROR("%s:%d] init %d failed %d", __func__, __LINE__,
     p_client->async_init, rc);
    goto error;
  }

  IDBG_HIGH("%s:%d] port %p client %p X", __func__, __LINE__, p_port, p_client);
  return rc;

error:
  if (p_client) {
    module_imgbase_client_destroy(p_client);
    p_client = NULL;
  }
  return rc;
}

/**
 * Function: module_imgbase_client_handle_buffer_ack
 *
 * Description: This function is used to handle the buffer ack
 *
 * Arguments:
 *   @p_client: IMGLIB_BASE client
 *   @ack_event: ack event type
 *   @data: payload pertaining to ack_event
 *
 * Return values:
 *   imaging error values
 *
 * Notes: none
 **/
int module_imgbase_client_handle_buffer_ack(imgbase_client_t *p_client,
  img_ack_event_t ack_event, void* data)
{
  int rc = IMG_SUCCESS;
  uint32_t i, j;
  imgbase_buf_t *p_buf = NULL;
  module_imgbase_t *p_mod = (module_imgbase_t *)p_client->p_mod;
  int32_t idx = -1;


  switch (ack_event) {
  case IMG_EVT_ACK_HOLD: {
    isp_buf_divert_ack_t *p_ack = (isp_buf_divert_ack_t *)data;
    IDBG_MED("%s:%d] IMG_EVT_ACK_HOLD, frame %d", __func__, __LINE__,
      p_ack->frame_id);

    /* Do not hold buffers which are not consumed */
    rc = IMG_ERR_NOT_FOUND;
    pthread_mutex_lock(&p_client->buf_mutex);
    for (i = 0; i < MAX_IMGLIB_BATCH_SIZE; i++) {
      p_buf = p_client->buf_list[i];
      if (!p_buf || p_buf->frame.frame_id != p_ack->frame_id) {
        continue;
      }

      /* ACK is late, do not hold it */
      if ((p_buf->queued_cnt == p_mod->caps.num_input) &&
        (p_buf->dequeued_cnt == p_mod->caps.num_input)) {
        IDBG_HIGH("%s:%d] Forward ACK %d since it is no longer needed",
          __func__, __LINE__, p_ack->frame_id);
        pthread_mutex_unlock(&p_client->buf_mutex);
        goto error;
      }

      /* Ack was previously received */
      if (p_buf->ack.ack_received_cnt > 0) {
        p_buf->ack.ack_received_cnt++;
        rc = IMG_SUCCESS;
        break;
      }

      /* New ACK, hold it */
      for (j = 0; j < p_client->stream_cnt; j++) {
        if (p_ack->identity == p_client->stream[j].identity) {
          IDBG_MED("%s:%d] Hold ACK %d", __func__, __LINE__, p_ack->frame_id);
          p_buf->ack.frame_id = p_ack->frame_id;
          p_buf->ack.identity = p_ack->identity;
          p_buf->ack.divert_ack = *p_ack;
          p_buf->ack.ack_received_cnt++;
          rc = IMG_SUCCESS;
          break;
        }
      }
    }
    pthread_mutex_unlock(&p_client->buf_mutex);
    PRINT_BUF_LIST(p_client);
    break;
  }
  case IMG_EVT_ACK_TRY_RELEASE: {
    imgbase_buf_t *p_buf = (imgbase_buf_t *)data;
    IDBG_MED("%s:%d] IMG_EVT_ACK_TRY_RELEASE, frame %d, %d %d", __func__,
      __LINE__, p_buf->frame.frame_id, p_buf->queued_cnt, p_buf->dequeued_cnt);

    isp_buf_divert_ack_t *p_ack = &p_buf->ack.divert_ack;
    rc = module_imgbase_client_forward_ack(p_client, p_ack);
    if (IMG_SUCCEEDED(rc)) {
      IDBG_HIGH("%s:%d] ACK release successful for frame %d",
        __func__, __LINE__, p_ack->frame_id);
      p_buf->ack.frame_id = 0;
      p_buf->ack.ack_received_cnt = 0;
      break;
    }

    PRINT_BUF_LIST(p_client);
    break;
  }
  case IMG_EVT_ACK_FLUSH: {
    IDBG_MED("%s:%d] IMG_EVT_ACK_FLUSH", __func__, __LINE__);

    pthread_mutex_lock(&p_client->buf_mutex);
    for (i = 0; i < MAX_IMGLIB_BATCH_SIZE; i++) {
      p_buf = p_client->buf_list[i];
      if (p_buf && p_buf->ack.frame_id > 0) {
        isp_buf_divert_ack_t *p_ack = &p_buf->ack.divert_ack;
        rc = module_imgbase_client_forward_ack(p_client, p_ack);
        if (IMG_SUCCEEDED(rc)) {
          IDBG_HIGH("%s:%d] Flush outstanding ack for frame %d", __func__,
            __LINE__, p_buf->ack.frame_id);
          p_buf->ack.frame_id = 0;
          p_buf->ack.ack_received_cnt = 0;
        }
      }\
    }
    pthread_mutex_unlock(&p_client->buf_mutex);
    PRINT_BUF_LIST(p_client);
    break;
  }
  case IMG_EVT_ACK_FORCE_RELEASE: {
    isp_buf_divert_t *p_ack = (isp_buf_divert_t *)data;
    IDBG_MED("%s:%d] IMG_EVT_ACK_FORCE_RELEASE, frameid %d",
      __func__, __LINE__,  p_ack->buffer.sequence);

    isp_buf_divert_ack_t buff_divert_ack;
    memset(&buff_divert_ack, 0, sizeof(buff_divert_ack));
    buff_divert_ack.buf_idx = p_ack->buffer.index;
    buff_divert_ack.is_buf_dirty = 1;
    buff_divert_ack.identity = p_client->divert_identity;
    buff_divert_ack.frame_id = p_ack->buffer.sequence;
    buff_divert_ack.channel_id = p_ack->channel_id;
    buff_divert_ack.meta_data = p_ack->meta_data;

    /* If overlap buffers are enabled, hold the ACK */
    if (p_mod->caps.num_overlap) {
      rc = module_imgbase_client_handle_buffer_ack(p_client,
        IMG_EVT_ACK_HOLD, &buff_divert_ack);
      if (IMG_SUCCEEDED(rc)) {
        break;
      }
    }

    /* Else forward the ack */
    rc = module_imgbase_client_forward_ack(p_client, &buff_divert_ack);
    if (IMG_SUCCEEDED(rc)) {
      IDBG_HIGH("%s:%d] Send ACK successful frame %d", __func__,
        __LINE__, p_ack->buffer.sequence);
    }

    /* If internal buffers are used, indicate ACK was forwarded */
    if (p_mod->caps.use_internal_bufs) {
      idx = -1;
      for (i = 0; i < MAX_IMGLIB_INTERNAL_BUFS; i++) {
        if (p_ack->buffer.sequence ==
          p_client->internal_buf.buf[i].frame_info.frame_id) {
          idx = i;
          break;
        }
      }
      if (idx < 0) {
        IDBG_ERROR("%s:%d] Error, internal buf not found for frame %d",
          __func__, __LINE__, p_ack->buffer.sequence);
        goto error;
      }
      p_client->internal_buf.buf[idx].input_ack_held = FALSE;
      PRINT_INTERNAL_BUF_LIST(p_client);
    }

    break;
  }
  case IMG_EVT_ACK_FREE_INTERNAL_BUF: {
    uint32_t* p_data = (uint32_t *)data;
    uint32_t ack_frame_id = *p_data;
    IDBG_MED("%s:%d] IMG_EVT_ACK_FREE_INTERNAL_BUF, frame %d",
      __func__, __LINE__, ack_frame_id);

    if (!p_mod->caps.use_internal_bufs) {
      IDBG_ERROR("%s:%d] Error, internal bufs not used, nothing to free",
        __func__, __LINE__);
      goto error;
    }

    idx = -1;
    for (i = 0; i < MAX_IMGLIB_INTERNAL_BUFS; i++) {
      if (ack_frame_id == p_client->internal_buf.buf[i].frame_info.frame_id) {
        idx = i;
        break;
      }
    }
    if (idx < 0) {
      IDBG_ERROR("%s:%d] Error, internal buf not found for frame %d",
        __func__, __LINE__, ack_frame_id);
      goto error;
    }

    p_buf =
      (imgbase_buf_t *)p_client->internal_buf.buf[idx].frame_info.private_data;
    if (p_buf) {
      free(p_buf);
      p_buf = NULL;
    }

    /* cache invalidate */
    rc = img_buffer_cacheops(&p_client->internal_buf.buf[idx].mem_handle,
      IMG_CACHE_CLEAN_INV, IMG_INTERNAL);
    if (IMG_ERROR(rc)) {
      IDBG_ERROR("%s:%d] Error, cache invalidate fail", __func__, __LINE__);
    }

    p_client->internal_buf.buf[idx].ref_cnt--;
    PRINT_INTERNAL_BUF_LIST(p_client);

    break;
  }
  default:
    break;
  }


  return rc;

error:
  return IMG_ERR_GENERAL;
}
