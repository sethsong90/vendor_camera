/**********************************************************************
*  Copyright (c) 2015-2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/

#include "lib2d.h"
#include "lib2d_comp.h"
#include "lib2d_util_interface.h"

/**
 * Function: lib2d_comp_process_frame
 *
 * Description: Process the given frame
 *
 * Input parameters:
 *   p_comp - The pointer to the component handle.
 *   p_in_frame - Input Frame which needs to be processed
 *   p_out_frame - Output Frame which needs to be written
 *
 * Return values:
 *     IMG_SUCCESS
 *
 * Notes: none
 **/
int lib2d_comp_process_frame(lib2d_comp_t *p_comp, img_frame_t *p_in_frame,
  img_frame_t *p_out_frame)
{
  int rc = IMG_SUCCESS;
  img_component_t *p_base = &p_comp->b;

  IDBG_HIGH("%s:%d] Start Lib2d conversion", __func__, __LINE__);
  rc = c2d_util_process_frame(p_comp->lib2dutil_handle,
    p_in_frame, p_out_frame);

  pthread_mutex_lock(&p_base->mutex);
  p_comp->b.state = IMG_STATE_IDLE;
  pthread_mutex_unlock(&p_base->mutex);

  if (rc) {
    IDBG_ERROR("%s:%d] Lib2d component process failed",
      __func__, __LINE__);
  } else {
    IDBG_HIGH("%s:%d] Lib2d component process Successful",
      __func__, __LINE__);
  }

  return rc;
}

/**
 * Function: lib2d_thread_loop
 *
 * Description: Main algorithm thread loop
 *
 * Input parameters:
 *   data - The pointer to the component object
 *
 * Return values:
 *     NULL
 *
 * Notes: none
 **/
void *lib2d_thread_loop(void *handle)
{
  lib2d_comp_t *p_comp = (lib2d_comp_t *)handle;
  img_component_t *p_base = &p_comp->b;
  int status = IMG_SUCCESS;
  img_frame_t *p_in_frame;
  img_frame_t *p_out_frame;
  int i = 0, count;
  IDBG_MED("%s:%d] ", __func__, __LINE__);

  count = img_q_count(&p_base->inputQ);
  IDBG_MED("%s:%d] num buffers %d", __func__, __LINE__, count);

  for (i = 0; i < count; i++) {
    p_in_frame = img_q_dequeue(&p_base->inputQ);
    if (NULL == p_in_frame) {
      IDBG_ERROR("%s:%d] invalid buffer", __func__, __LINE__);
      goto error;
    }
    p_out_frame = img_q_dequeue(&p_base->outBufQ);
    if (NULL == p_out_frame) {
      IDBG_ERROR("%s:%d] invalid buffer", __func__, __LINE__);
      goto error;
    }
    /*process the frame*/
    status = lib2d_comp_process_frame(p_comp, p_in_frame, p_out_frame);
    if (status < 0) {
      IDBG_ERROR("%s:%d] process error %d", __func__, __LINE__, status);
      goto error;
    }

    IMG_SEND_EVENT(p_base, QIMG_EVT_BUF_DONE);

    // Call the callback function to inform conversion has been completed.
    if (p_comp->lib2d_cb != NULL) {
      p_comp->lib2d_cb(p_comp->userdata, p_in_frame, p_out_frame);
    }
  }

  pthread_mutex_lock(&p_base->mutex);
  p_base->state = IMG_STATE_STOPPED;
  pthread_mutex_unlock(&p_base->mutex);
  IMG_SEND_EVENT(p_base, QIMG_EVT_DONE);
  return IMG_SUCCESS;

error:
  /* flush rest of the buffers */
  count = img_q_count(&p_base->inputQ);
  IDBG_MED("%s:%d] Error buf count %d", __func__, __LINE__, count);

  for (i = 0; i < count; i++) {
    p_in_frame = img_q_dequeue(&p_base->inputQ);
    if (NULL == p_in_frame) {
      IDBG_ERROR("%s:%d] invalid buffer", __func__, __LINE__);
      continue;
    }
    p_out_frame = img_q_dequeue(&p_base->outBufQ);
    if (NULL == p_out_frame) {
      IDBG_ERROR("%s:%d] invalid buffer", __func__, __LINE__);
      goto error;
    }
    IMG_SEND_EVENT(p_base, QIMG_EVT_BUF_DONE);
  }
  pthread_mutex_lock(&p_base->mutex);
  p_base->state = IMG_STATE_STOPPED;
  pthread_mutex_unlock(&p_base->mutex);
  IMG_SEND_EVENT_PYL(p_base, QIMG_EVT_ERROR, status, status);
  return NULL;

}

/**
 * Function: lib2d_comp_abort
 *
 * Description: Aborts the execution of lib2d
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *   p_data - The pointer to the command structure. The structure
 *            for each command type is defined in lib2d.h
 *
 * Return values:
 *     IMG_SUCCESS
 *
 * Notes: none
 **/
int lib2d_comp_abort(void *handle, void *p_data)
{
  lib2d_comp_t *p_comp = (lib2d_comp_t *)handle;
  img_component_t *p_base = &p_comp->b;
  int status;

  if (p_base->mode == IMG_ASYNC_MODE) {
    status = p_comp->b.ops.abort(&p_comp->b, p_data);
    if (status < 0) {
      return status;
    }
  }
  pthread_mutex_lock(&p_base->mutex);
  p_base->state = IMG_STATE_INIT;
  pthread_mutex_unlock(&p_base->mutex);

  return 0;
}

/**
 * Function: lib2d_comp_start
 *
 * Description: Start the execution of lib2d
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *   p_data - The pointer to the command structure. The structure
 *            for each command type will be defined in lib2d.h
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int lib2d_comp_start(void *handle, void *p_data)
{
  lib2d_comp_t *p_comp = (lib2d_comp_t *)handle;
  img_component_t *p_base = &p_comp->b;
  int status = IMG_SUCCESS;
  img_frame_t *p_in_frame;
  img_frame_t *p_out_frame;

  pthread_mutex_lock(&p_base->mutex);
  if ((p_base->state != IMG_STATE_INIT) ||
    (NULL == p_base->thread_loop)) {
    IDBG_ERROR("%s:%d] Error state %d", __func__, __LINE__,
      p_base->state);
    pthread_mutex_unlock(&p_base->mutex);
    return IMG_ERR_NOT_SUPPORTED;
  }

  p_base->state = IMG_STATE_STARTED;
  pthread_mutex_unlock(&p_base->mutex);

  if (p_base->mode == IMG_SYNC_MODE) {
    // Deque input buffer.
    p_in_frame = img_q_dequeue(&p_base->inputQ);
    if (NULL == p_in_frame) {
      IDBG_ERROR("%s:%d] invalid buffer", __func__, __LINE__);
      status = IMG_ERR_INVALID_INPUT;
      goto error;
    }

    // Deque output buffer.
    p_out_frame = img_q_dequeue(&p_base->outBufQ);
    if (NULL == p_out_frame) {
      IDBG_ERROR("%s:%d] invalid buffer", __func__, __LINE__);
      status = IMG_ERR_INVALID_INPUT;
      goto error;
    }

    // Start processing
    status = lib2d_comp_process_frame(p_comp, p_in_frame, p_out_frame);

    // Send BUF_DONE event
    IMG_SEND_EVENT(p_base, QIMG_EVT_BUF_DONE);

    // Call the callback function to inform conversion has been completed.
    if (p_comp->lib2d_cb != NULL) {
      p_comp->lib2d_cb(p_comp->userdata, p_in_frame, p_out_frame);
    }
    IMG_SEND_EVENT(p_base, QIMG_EVT_DONE);
  } else {
    status = p_comp->b.ops.start(&p_comp->b, NULL);
  }

error:
  if (status != IMG_SUCCESS) {
    pthread_mutex_lock(&p_base->mutex);
    p_base->state = IMG_STATE_INIT;
    pthread_mutex_unlock(&p_base->mutex);
  }

  return status;
}

/**
 * Function: lib2d_comp_get_param
 *
 * Description: Gets lib2d parameters
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *   param - The type of the parameter
 *   p_data - The pointer to the paramter structure. The structure
 *            for each paramter type will be defined in denoise.h
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *     IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
int lib2d_comp_get_param(void *handle, img_param_type param, void *p_data)
{
  lib2d_comp_t *p_comp = (lib2d_comp_t *)handle;
  int status = IMG_SUCCESS;

  status = p_comp->b.ops.get_parm(&p_comp->b, param, p_data);
  if (status < 0) {
    return status;
  }

  switch (param) {
    case QIMG_PARAM_MODE: {
      img_comp_mode_t *l_mode = (img_comp_mode_t *) p_data;
      if (l_mode == NULL) {
        return IMG_ERR_INVALID_INPUT;
      }
      *l_mode = p_comp->mode;
    }
    break;
    case QLIB2D_SOURCE_FORMAT : {
      cam_format_t *src_format = (cam_format_t *) p_data;
      if (NULL == src_format) {
        IDBG_ERROR("%s:%d] invalid param", __func__, __LINE__);
        return IMG_ERR_INVALID_INPUT;
      }
      *src_format = p_comp->src_format;
    }
    break;
    case QLIB2D_DESTINATION_FORMAT : {
      cam_format_t *dst_format = (cam_format_t *) p_data;
      if (NULL == dst_format) {
        IDBG_ERROR("%s:%d] invalid param", __func__, __LINE__);
        return IMG_ERR_INVALID_INPUT;
      }
      *dst_format = p_comp->src_format;
    }
    break;
    default: {
      IDBG_ERROR("%s:%d] invalid parameter %d", __func__, __LINE__, param);
      return IMG_ERR_INVALID_INPUT;
    }
  }

  return IMG_SUCCESS;
}

/**
 * Function: lib2d_comp_set_param
 *
 * Description: Set lib2d parameters
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *   param - The type of the parameter
 *   p_data - The pointer to the paramter structure. The structure
 *            for each paramter type will be defined in lib2d.h
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *     IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
int lib2d_comp_set_param(void *handle, img_param_type param, void *p_data)
{
  lib2d_comp_t *p_comp = (lib2d_comp_t *)handle;
  int status = IMG_SUCCESS;

  status = p_comp->b.ops.set_parm(&p_comp->b, param, p_data);
  if (status < 0) {
    return status;
  }

  switch (param) {
    case QIMG_PARAM_MODE : {
      img_comp_mode_t *l_mode = (img_comp_mode_t *) p_data;
      if (NULL == l_mode) {
        IDBG_ERROR("%s:%d] invalid mode", __func__, __LINE__);
        // pthread_mutex_unlock(&p_comp->mutex);
        return IMG_ERR_INVALID_INPUT;
      }
      p_comp->mode = *l_mode;
    }
    break;
    case QLIB2D_SOURCE_FORMAT : {
      cam_format_t *src_format = (cam_format_t *) p_data;
      if (NULL == src_format) {
        IDBG_ERROR("%s:%d] invalid src format", __func__, __LINE__);
        return IMG_ERR_INVALID_INPUT;
      }

      // Check if the current source format is same as new.
      // If not, we need to inform util layers to update the
      // source surface.
      if (p_comp->src_format != *src_format) {
        status = c2d_util_update_default_surface(p_comp->lib2dutil_handle,
          TRUE, *src_format);
        if (status != IMG_SUCCESS) {
          IDBG_ERROR("%s:%d] invalid src format", __func__, __LINE__);
          return status;
        }
        p_comp->src_format = *src_format;
      }
    }
    break;
    case QLIB2D_DESTINATION_FORMAT : {
      cam_format_t *dst_format = (cam_format_t *) p_data;
      if (NULL == dst_format) {
        IDBG_ERROR("%s:%d] invalid dst format", __func__, __LINE__);
        return IMG_ERR_INVALID_INPUT;
      }

      // Check if the current source format is same as new.
      // If not, we need to inform util layers to update the
      // source surface.
      if (p_comp->dst_format != *dst_format) {
        status = c2d_util_update_default_surface(p_comp->lib2dutil_handle,
          FALSE, *dst_format);
        if (status != IMG_SUCCESS) {
          IDBG_ERROR("%s:%d] Error updating default surface, status=%d",
            __func__, __LINE__, status);
          return status;
        }
        p_comp->src_format = *dst_format;
      }
    }
    break;
    default: {
      IDBG_ERROR("%s:%d] invalid parameter %d", __func__, __LINE__, param);
      return IMG_ERR_INVALID_INPUT;
    }
  }
  return status;
}


/**
 * Function: lib2d_comp_init
 *
 * Description: Initializes the lib2d component
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *   p_userdata - the handle which is passed by the client
 *   p_data - The pointer to the parameter which is required during the
 *            init phase
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *
 * Notes: none
 **/
int lib2d_comp_init(void *handle, void* p_userdata, void *p_data)
{
  lib2d_comp_t *p_comp = (lib2d_comp_t *)handle;
  int status = IMG_SUCCESS;

  IDBG_MED("%s:%d] %p ", __func__, __LINE__, p_userdata);

  status = p_comp->b.ops.init(&p_comp->b, p_userdata, p_data);
  if (status < 0) {
    IDBG_ERROR("%s:%d] p_comp->b.ops.init returned %d",
      __func__, __LINE__, status);
    return status;
  }
  p_comp->userdata = p_userdata;
  p_comp->lib2d_cb = p_data;
  return status;
}

/**
 * Function: lib2d_comp_deinit
 *
 * Description: Deinitializes the lib2d component
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *
 * Notes: none
 **/
int lib2d_comp_deinit(void *handle)
{
  lib2d_comp_t *p_comp = (lib2d_comp_t *)handle;
  int status = IMG_SUCCESS;

  IDBG_MED("%s:%d] \n", __func__, __LINE__);
  status = lib2d_comp_abort(handle, NULL);
  if (status < 0) {
    return status;
  }

  status = p_comp->b.ops.deinit(&p_comp->b);
  if (status < 0) {
    return status;
  }

  free(p_comp);
  return IMG_SUCCESS;
}

/**
 * Function: lib2d_comp_create
 *
 * Description: This function is used to create lib2d component
 *
 * Input parameters:
 *   @handle: library handle
 *   @p_ops - The pointer to img_component_t object. This object
 *            contains the handle and the function pointers for
 *            communicating with the imaging component.
 *
 * Return values:
 *     IMG_SUCCESS
 *
 * Notes: none
 **/
int lib2d_comp_create(void* handle, img_component_ops_t *p_ops)
{
  IMG_UNUSED(handle);

  lib2d_comp_t *p_comp;
  int status;

  if (NULL == handle) {
    IDBG_ERROR("%s:%d] C2d library not loaded", __func__, __LINE__);
    return IMG_ERR_INVALID_OPERATION;
  }

  if (NULL == p_ops) {
    IDBG_ERROR("%s:%d] failed", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  p_comp = (lib2d_comp_t *)malloc(sizeof(lib2d_comp_t));
  if (NULL == p_comp) {
    IDBG_ERROR("%s:%d] failed", __func__, __LINE__);
    return IMG_ERR_NO_MEMORY;
  }

  memset(p_comp, 0x0, sizeof(lib2d_comp_t));
  status = img_comp_create(&p_comp->b);
  if (status < 0) {
    free(p_comp);
    return status;
  }

  p_comp->lib2dutil_handle = handle;
  p_comp->mode             = IMG_SYNC_MODE;

  /*set the main thread*/
  p_comp->b.thread_loop = lib2d_thread_loop;
  p_comp->b.p_core = p_comp;

  /* copy the ops table from the base component */
  *p_ops = p_comp->b.ops;
  p_ops->init            = lib2d_comp_init;
  p_ops->deinit          = lib2d_comp_deinit;
  p_ops->set_parm        = lib2d_comp_set_param;
  p_ops->get_parm        = lib2d_comp_get_param;
  p_ops->start           = lib2d_comp_start;
  p_ops->abort           = lib2d_comp_abort;

  p_ops->handle = (void *)p_comp;
  return IMG_SUCCESS;
}

/**
 * Function: lib2d_comp_load
 *
 * Description: This function is used to load lib2d library
 *
 * Input parameters:
 *   @name: library name
 *   @handle: library handle
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_NOT_FOUND
 *
 * Notes: none
 **/
int lib2d_comp_load(const char* name, void** handle)
{
  IMG_UNUSED(name);
  int rc = IMG_SUCCESS;

  if (handle == NULL) {
    IDBG_ERROR("%s:%d] invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  *handle = NULL;
  rc = c2d_util_open(handle);
  if ((rc != IMG_SUCCESS) || (*handle == NULL)) {
    IDBG_ERROR("%s:%d] Error opening C2D handle", __func__, __LINE__);
    return IMG_ERR_NOT_FOUND;
  }

  IDBG_HIGH("%s:%d] Lib2D library loaded successfully", __func__, __LINE__);

  return rc;
}

/**
 * Function: lib2d_comp_unload
 *
 * Description: This function is used to unload lib2d library
 *
 * Input parameters:
 *   @handle: library handle
 *
 * Return values:
 *     none
 *
 * Notes: none
 **/
void lib2d_comp_unload(void* handle)
{
  IMG_UNUSED(handle);
  IDBG_HIGH("%s:%d]", __func__, __LINE__);

  c2d_util_close(handle);
}
