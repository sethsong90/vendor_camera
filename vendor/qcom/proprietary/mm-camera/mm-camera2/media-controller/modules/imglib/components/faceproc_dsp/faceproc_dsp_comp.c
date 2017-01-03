/***************************************************************************
Copyright (c) 2015-2016 Qualcomm Technologies, Inc.
All Rights Reserved.
Confidential and Proprietary - Qualcomm Technologies, Inc.
***************************************************************************/

#include "faceproc_dsp_comp.h"
#include "facial_parts_wrapper.h"
#include <math.h>
#include <dlfcn.h>
#ifdef _ANDROID_
#include <cutils/properties.h>
#endif

#include <cutils/trace.h>
#include "AEEStdErr.h"

#include "img_thread.h"
#include "img_dsp_dl_mgr.h"

#ifdef FD_WITH_DSP_SW_FALLBACK_TEST
static int init_return_count = 0;
#endif
/**
 * CONSTANTS and MACROS
 **/
/*#define FD_FPS*/
// max wait for scheduled job on DSP dedicated thread
#define MAX_FDDSP_API_WAIT 2000
#define MAX_FDDSP_TEST_API_WAIT 200
/* sometimes it might take longer time during load because so many others
 * threads also trying to allocate/initialize  */
#define MAX_FDDSP_LOAD_API_WAIT 5000

#define PI 3.14159265
/*internal intermediate fddsp Q size */
#define MAX_INTER_Q_SIZE MAX_NUM_FD_FRAMES

#ifdef FD_FPS
static int start_fd_ms = 0;
static int end_fd_ms = 0;
static int total_elapsed_ms;
static struct timeval fd_time;
#endif

#ifdef FD_PROFILE
#define FACEPROC_START_MEASURE \
  struct timeval start_time, mid_time, end_time;\
  gettimeofday(&start_time, NULL); \
  mid_time = start_time \

#define FACEPROC_MIDDLE_TIME \
do { \
  gettimeofday(&end_time, NULL); \
  IDBG_INFO("%s]%d Middle mtime  %lu ms",  __func__, __LINE__, \
  ((end_time.tv_sec * 1000) + (end_time.tv_usec / 1000)) - \
  ((mid_time.tv_sec * 1000) + (mid_time.tv_usec / 1000))); \
  mid_time = end_time; \
} while (0)\

#define FACEPROC_END_MEASURE \
do { \
  gettimeofday(&end_time, NULL); \
  IDBG_HIGH("End of measure Total %lu ms", \
  ((end_time.tv_sec * 1000) + (end_time.tv_usec / 1000)) - \
  ((start_time.tv_sec * 1000) + (start_time.tv_usec / 1000))); \
} while (0) \

#else
#define FACEPROC_START_MEASURE \
  do{}while(0) \

#define FACEPROC_MIDDLE_TIME \
  do{}while(0) \

#define FACEPROC_END_MEASURE \
  do{}while(0) \

#endif

#define FD_MAX_DUMP_CNT 10
static unsigned frame_count = 0;
// static const unsigned skip_count = 10;
// static const unsigned initial_skip_count = 5;
static unsigned dump_count = 0;



/** FD_SET_PROCESS_BUF_CNT_LOCKED
 *   @p: pointer to the image component
 *   @b: buffer cnt need to be set
 *   @m: pointer to the mutex
 *
 *   Set buffer count value with component locked.
 **/
#define FD_SET_PROCESS_BUF_CNT_LOCKED(p, b, m) ({ \
  pthread_mutex_lock(m); \
  (p)->processing_buff_cnt = (b); \
  pthread_mutex_unlock(m); \
})

#undef FACEPROC_NORMAL
#define FACEPROC_NORMAL AEE_SUCCESS

static faceproc_dsp_lib_t g_faceproc_dsp_lib;

static int faceproc_dsp_comp_abort(void *handle, void *p_data);
static int faceproc_dsp_comp_test_dsp_connection_common();

/**
 * Function: faceproc_dsp_error_to_img_error
 *
 * Description: Converts DSP error to Img error
 *
 * Input parameters:
 *   dsp_error - Error returned from DSP function
 *
 * Return values:
 *     IMG_xx error corresponds to DSP error
 *
 * Notes: none
 **/
int faceproc_dsp_error_to_img_error(int dsp_error)
{
  int img_error = IMG_SUCCESS;

  switch (dsp_error) {
    case AEE_SUCCESS :
      img_error = IMG_SUCCESS;
      break;

    case AEE_EFAILED :
      img_error = IMG_ERR_GENERAL;
      break;

    case AEE_ENOMEMORY :
      img_error = IMG_ERR_NO_MEMORY;
      break;

    case AEE_ECLASSNOTSUPPORT :
    case AEE_EVERSIONNOTSUPPORT :
    case AEE_ESCHEMENOTSUPPORTED :
      img_error = IMG_ERR_NOT_SUPPORTED;
      break;

    case AEE_EBADPARM :
      img_error = IMG_ERR_INVALID_INPUT;
      break;

    case AEE_ENOTALLOWED :
      img_error = IMG_ERR_INVALID_OPERATION;
      break;

    case AEE_ERESOURCENOTFOUND :
      img_error = IMG_ERR_NOT_FOUND;
      break;

    case AEE_EITEMBUSY :
      img_error = IMG_ERR_BUSY;
      break;

    case ECONNRESET :
      IDBG_ERROR("%s:%d] Connection reset error : %d ",
        __func__, __LINE__, dsp_error);
      img_error = IMG_ERR_CONNECTION_FAILED;
      break;

    default:
      img_error = IMG_ERR_GENERAL;
      break;
  }

  return img_error;
}


/**
 * Function: faceproc_dsp_comp_eng_handle_error
 *
 * Description: Handle dsp context error cases
 *
 * Input parameters:
 *   p_comp - The pointer to comp structure
 *   img_error - error
 *
 * Return values:
 *     None
 *
 * Notes: noneg
 **/
void faceproc_dsp_comp_eng_handle_error(faceproc_dsp_comp_t *p_comp,
  int32_t img_error)
{
  switch(img_error) {
    case IMG_ERR_CONNECTION_FAILED : {
      g_faceproc_dsp_lib.restore_needed_flag = TRUE;
      img_dsp_dl_mgr_set_reload_needed(TRUE);

      // If the error is Connection Lost, send an event to Module.
      if (p_comp != NULL) {
        IMG_SEND_EVENT(&(p_comp->b), QIMG_EVT_COMP_CONNECTION_FAILED);
      }
    }

    default :
      // Todo : Do we need to send ERROR event for all other errors.
      break;
  }
}


/**
 * Function: faceproc_dsp_comp_init
 *
 * Description: Initializes the Qualcomm Technologies, Inc. faceproc component
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
int faceproc_dsp_comp_init(void *handle, void* p_userdata, void *p_data)
{
  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)handle;
  int status = IMG_SUCCESS;
  int32_t i = 0;

  IDBG_MED("%s:%d] ", __func__, __LINE__);
  status = p_comp->b.ops.init(&p_comp->b, p_userdata, p_data);
  if (IMG_ERROR(status))
    return status;

  p_comp->mode = FACE_DETECT;

  img_q_init(&p_comp->intermediate_in_use_Q, "intermediate_in_use_Q");
  img_q_init(&p_comp->intermediate_free_Q, "intermediate_free_Q");

  for (i = 0; i < MAX_INTER_Q_SIZE; i++) {
    faceproc_internal_queue_struct * p_node =
      (faceproc_internal_queue_struct *)malloc(
        sizeof(faceproc_internal_queue_struct));
    if (!p_node) {
      IDBG_ERROR("%s:%d] error initializing inter Q", __func__, __LINE__);
      return IMG_ERR_NO_MEMORY;
    }
    status = img_q_enqueue(&p_comp->intermediate_free_Q, (void*)p_node);
    if (IMG_ERROR(status)) {
      IDBG_ERROR("%s:%d] can't add to free Q", __func__, __LINE__);
      free(p_node);
      return IMG_ERR_NO_MEMORY;
    }
  }

  IDBG_MED("%s:%d] ", __func__, __LINE__);
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_common_destroy
 *
 * Description: schedules the destroy job  to the DSP thread
 *
 * Input parameters:
 *   p_comp - The pointer to the component handle.
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *
 * Notes: none
 **/
int faceproc_dsp_comp_common_destroy(faceproc_dsp_comp_t * p_comp)
{
  int status = IMG_SUCCESS;
  IDBG_MED("%s:%d] Enter ", __func__, __LINE__);

  //Start:add img_thread API to schedule job.
  faceproc_dsp_comp_struct job_args;
  job_args.p_comp = p_comp;
  uint32_t current_job_id = 0;
  img_thread_job_params_t fddspc_eng_destroy_job;
  fddspc_eng_destroy_job.args = (void*)&job_args;
  fddspc_eng_destroy_job.client_id = g_faceproc_dsp_lib.client_id;
  fddspc_eng_destroy_job.core_affinity = IMG_CORE_DSP;
  fddspc_eng_destroy_job.dep_job_ids = 0;
  fddspc_eng_destroy_job.dep_job_count = 0;
  fddspc_eng_destroy_job.delete_on_completion = TRUE;
  fddspc_eng_destroy_job.execute = faceproc_dsp_comp_eng_destroy_task_exec;
  current_job_id = img_thread_mgr_schedule_job(&fddspc_eng_destroy_job);
  //now wait for job to complete or let it run.
  if (0 < current_job_id) {
    status = img_thread_mgr_wait_for_completion_by_jobid(current_job_id,
      MAX_FDDSP_API_WAIT);
    status = job_args.return_value;
  }
  //End:add img_thread API to schedule job.

  IDBG_HIGH("%s:%d] Exit returned %d ", __func__, __LINE__ , status);

  return status;

}
/**
 * Function: faceproc_dsp_comp_deinit
 *
 * Description: Un-initializes the face processing component
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
int faceproc_dsp_comp_deinit(void *handle)
{
  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)handle;
  int status = IMG_SUCCESS;
  int32_t i = 0;

  IDBG_HIGH("%s:%d] ", __func__, __LINE__);
  status = faceproc_dsp_comp_abort(handle, NULL);
  if (IMG_ERROR(status))
    return status;

  status = p_comp->b.ops.deinit(&p_comp->b);
  if (IMG_ERROR(status))
    return status;

  if (p_comp->p_lib->facial_parts_hndl) {
    facial_parts_wrap_destroy(p_comp->p_lib->facial_parts_hndl);
  }

  status = faceproc_dsp_comp_common_destroy(p_comp);
  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] failed status =%d ", __func__, __LINE__ , status);
    return status;
  }

  //free memory allocated for intermediate results
  faceproc_internal_queue_struct *p_node;
  for (i = 0; i < MAX_INTER_Q_SIZE; i++) {
    p_node = img_q_dequeue(&p_comp->intermediate_free_Q);
    if (NULL == p_node) {
      IDBG_ERROR("%s %d] Fail to dequeue free buffer", __func__, __LINE__);
      break;
    }
    free(p_node);
  }

  img_q_deinit(&p_comp->intermediate_in_use_Q);
  img_q_deinit(&p_comp->intermediate_free_Q);

  pthread_mutex_destroy(&p_comp->result_mutex);

  free(p_comp);
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_flush_buffers
 *
 * Description: Flush buffers from component queue.
 *
 * Arguments:
 *   @p_comp: Pointer to Faceproc component instance.
 *
 * Return values:
 *   none.
 **/
void faceproc_dsp_comp_flush_buffers(faceproc_dsp_comp_t *p_comp)
{
  int i;
  int count;
  int status;
  img_frame_t *p_frame;

  count = img_q_count(&p_comp->b.inputQ);
  for (i = 0; i < count; i++) {
    p_frame = img_q_dequeue(&p_comp->b.inputQ);
    if (NULL == p_frame) {
      IDBG_ERROR("%s %d]Fail to dequeue input buffer", __func__, __LINE__);
      continue;
    }
    status = img_q_enqueue(&p_comp->b.outputQ, p_frame);
    if (IMG_ERROR(status)) {
      IDBG_ERROR("%s %d]Fail to enqueue input buffer", __func__, __LINE__);
      continue;
    }
    IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_BUF_DONE);
  }

  //free from intermediate Queue as well.
  faceproc_internal_queue_struct *p_node;
  count = img_q_count(&p_comp->intermediate_in_use_Q);
  for (i = 0; i < count; i++) {
    p_node = img_q_dequeue(&p_comp->intermediate_in_use_Q);
    if (NULL == p_node || NULL == p_node->p_frame) {
      IDBG_ERROR("Fail to dequeue input buffer");
      continue;
    }
    status = img_q_enqueue(&p_comp->b.outputQ, p_node->p_frame);
    if (IMG_ERROR(status)) {
      IDBG_ERROR("Fail to enqueue input buffer");
      continue;
    }
    IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_BUF_DONE);
  }
}

/**
 * Function: faceproc_dsp_comp_set_param
 *
 * Description: Set faceproc parameters
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
int faceproc_dsp_comp_set_param(void *handle, img_param_type param,
  void *p_data)
{
  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)handle;
  img_component_t *p_base = &p_comp->b;
  int status = IMG_SUCCESS;

  status = p_comp->b.ops.set_parm(&p_comp->b, param, p_data);
  if (IMG_ERROR(status))
    return status;

  IDBG_MED("%s:%d] param 0x%x", __func__, __LINE__, param);
  switch (param) {
  case QWD_FACEPROC_CFG: {
    faceproc_config_t *p_config = (faceproc_config_t *)p_data;

    if (NULL == p_config) {
      IDBG_ERROR("%s:%d] invalid faceproc config", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    p_comp->config = *p_config;
    p_comp->config_set = TRUE;
  }
    break;
  case QWD_FACEPROC_TRY_SIZE: {
    faceproc_frame_cfg_t *p_config = (faceproc_frame_cfg_t *)p_data;

    if (NULL == p_config) {
      IDBG_ERROR("%s:%d] invalid faceproc config", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
  }
    break;
  case QWD_FACEPROC_MODE: {
    faceproc_mode_t *p_mode = (faceproc_mode_t *)p_data;

    if (NULL == p_mode) {
      IDBG_ERROR("%s:%d] invalid faceproc mode", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    p_comp->mode = *p_mode;
    IDBG_MED("%s:%d] mode %d", __func__, __LINE__, p_comp->mode);
  }
    break;
  case QWD_FACEPROC_CHROMATIX: {
    fd_chromatix_t *p_chromatix = (fd_chromatix_t *)p_data;

    if (NULL == p_chromatix) {
      IDBG_ERROR("%s:%d] invalid faceproc chromatix", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    p_comp->fd_chromatix = *p_chromatix;
    p_comp->is_chromatix_changed = TRUE;
    IDBG_HIGH("%s:%d] Set chromatix in state %d", __func__, __LINE__,
      p_base->state);
  }
    break;
  case QWD_FACEPROC_DUMP_DATA: {
    faceproc_dump_mode_t *p_dump_mode = (faceproc_dump_mode_t *)p_data;
    if (NULL == p_dump_mode) {
      IDBG_ERROR("%s:%d] invalid faceproc dump mode", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    p_comp->dump_mode = *p_dump_mode;
    IDBG_HIGH("%s:%d] Set dump mode %d", __func__, __LINE__,
      p_comp->dump_mode);
  }
    break;
  default:
    IDBG_ERROR("%s:%d] Error, param=%d", __func__, __LINE__, param);
    return IMG_ERR_INVALID_INPUT;
  }
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_get_param
 *
 * Description: Gets faceproc parameters
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
int faceproc_dsp_comp_get_param(void *handle, img_param_type param,
  void *p_data)
{
  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)handle;
  int status = IMG_SUCCESS;

  status = p_comp->b.ops.get_parm(&p_comp->b, param, p_data);
  if (IMG_ERROR(status))
    return status;

  switch (param) {
  case QWD_FACEPROC_RESULT: {
    faceproc_result_t *p_result = (faceproc_result_t *)p_data;

    if (NULL == p_result) {
      IDBG_ERROR("%s:%d] invalid faceproc result", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    if (!p_comp->width || !p_comp->height) {
      IDBG_ERROR("%s:%d] Invalid param w=%d, h=%d",
        __func__, __LINE__, p_comp->width, p_comp->height);
      return IMG_ERR_INVALID_INPUT;
    }
    pthread_mutex_lock(&p_comp->result_mutex);
    *p_result = p_comp->inter_result;
    pthread_mutex_unlock(&p_comp->result_mutex);

    p_result->client_id = p_comp->client_id;
    break;
  }
  case QWD_FACEPROC_USE_INT_BUFF: {
    uint32_t *p_use_int_buff = (uint32_t *)p_data;

    if (NULL == p_use_int_buff) {
      IDBG_ERROR("%s:%d] invalid input", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    *p_use_int_buff = FALSE;
    break;
  }
  case QWD_FACEPROC_BUFF_TYPE: {
    img_buf_type_t *p_type = (img_buf_type_t *)p_data;

    if (NULL == p_type) {
      IDBG_ERROR("%s:%d] invalid input", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    *p_type = IMG_BUFFER_HEAP;

   char value[PROPERTY_VALUE_MAX];
   property_get("persist.camera.imglib.fddsp", value, "1");
   if (atoi(value)){
#ifdef USE_SMMU_BUFFERS_FOR_FDDSP
     IDBG_MED("%s:%d] FD in DSP mode, use ION_IOMMU",
       __func__, __LINE__);
     *p_type = IMG_BUFFER_ION_IOMMU;
#else
     IDBG_MED("%s:%d] FD in DSP mode, use ION_ADSP",
       __func__, __LINE__);
     *p_type = IMG_BUFFER_ION_ADSP;
#endif
   } else {
     IDBG_MED("%s:%d] FD NOT in DSP mode, use BUFFER_HEAP",__func__, __LINE__);
   }

    break;
  }
  default:
    IDBG_ERROR("%s:%d] Error, param=%d", __func__, __LINE__, param);
    return IMG_ERR_INVALID_INPUT;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_cfg_debug
 *
 * Description: Debug params for faceproc configuration
 *
 * Input parameters:
 * @p_config: pointer to facepeoc config
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
static void faceproc_dsp_comp_cfg_debug(faceproc_config_t *p_config)
{
  IDBG_MED("%s:%d] FaceProc cfg hist enable %d", __func__, __LINE__,
    p_config->histogram_enable);
  IDBG_MED("%s:%d] FaceProc cfg max_height %d",
    __func__, __LINE__,
    p_config->frame_cfg.max_height);
  IDBG_MED("%s:%d] FaceProc cfg max_width %d",
    __func__, __LINE__,
    p_config->frame_cfg.max_width);
  IDBG_MED("%s:%d] FaceProc cfg face_orientation_hint %d",
    __func__, __LINE__,
    p_config->face_cfg.face_orientation_hint);
  IDBG_HIGH("%s:%d] FaceProc cfg max_face_size %d",
    __func__, __LINE__,
    p_config->face_cfg.max_face_size);
  IDBG_HIGH("%s:%d] FaceProc cfg max_num_face_to_detect %d",
    __func__, __LINE__,
    p_config->face_cfg.max_num_face_to_detect);
  IDBG_HIGH("%s:%d] FaceProc cfg min_face_size %d",
    __func__, __LINE__,
    p_config->face_cfg.min_face_size);
  IDBG_MED("%s:%d] FaceProc cfg rotation_range %d",
    __func__, __LINE__,
    p_config->face_cfg.rotation_range);
}

/**
 * Function: faceproc_dsp_comp_chromatix_debug
 *
 * Description: Debug params for faceproc chromatix
 *
 * Input parameters:
 *   p_chromatix - FD chromatix pointer
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
static void faceproc_dsp_comp_chromatix_debug(fd_chromatix_t *p_chromatix)
{

  IDBG_MED("%s:%d] FaceProc angle_front %d", __func__, __LINE__,
    p_chromatix->angle_front);
  IDBG_MED("%s:%d] FaceProc angle_full_profile %d", __func__, __LINE__,
    p_chromatix->angle_full_profile);
  IDBG_MED("%s:%d] FaceProc angle_half_profile %d", __func__, __LINE__,
    p_chromatix->angle_half_profile);
  IDBG_MED("%s:%d] FaceProc detection_mode %d", __func__, __LINE__,
    p_chromatix->detection_mode);
  IDBG_MED("%s:%d] FaceProc enable_blink_detection %d", __func__, __LINE__,
    p_chromatix->enable_blink_detection);
  IDBG_MED("%s:%d] FaceProc enable_gaze_detection %d", __func__, __LINE__,
    p_chromatix->enable_gaze_detection);
  IDBG_MED("%s:%d] FaceProc enable_smile_detection %d", __func__, __LINE__,
    p_chromatix->enable_smile_detection);
  IDBG_MED("%s:%d] FaceProc frame_skip %d", __func__, __LINE__,
    p_chromatix->frame_skip);
  IDBG_MED("%s:%d] FaceProc max_face_size %d", __func__, __LINE__,
    p_chromatix->max_face_size);
  IDBG_MED("%s:%d] FaceProc max_num_face_to_detect %d", __func__, __LINE__,
    p_chromatix->max_num_face_to_detect);
  IDBG_MED("%s:%d] FaceProc min_face_adj_type %d", __func__, __LINE__,
    p_chromatix->min_face_adj_type);
  IDBG_MED("%s:%d] FaceProc min_face_size %d", __func__, __LINE__,
    p_chromatix->min_face_size);
  IDBG_MED("%s:%d] FaceProc min_face_size_ratio %f", __func__, __LINE__,
    p_chromatix->min_face_size_ratio);
}

/**
 * Function: face_proc_dsp_can_wait
 *
 * Description: Queue function to check if abort is issued
 *
 * Input parameters:
 *   p_userdata - The pointer to faceproc component
 *
 * Return values:
 *     true/false
 *
 * Notes: none
 **/
int face_proc_dsp_can_wait(void *p_userdata)
{
  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)p_userdata;
  img_component_t *p_base = &p_comp->b;
  return !((p_base->state == IMG_STATE_STOP_REQUESTED)
    || (p_base->state == IMG_STATE_STOPPED));
}

/**
 * Function: face_proc_dsp_can_wait
 *
 * Description: Queue function to check if abort is issued
 *
 * Input parameters:
 *   p_userdata - The pointer to faceproc component
 *   data - pointer to frame data
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_INVALID_OPERATION
 *   IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
static int face_proc_release_frame(void *data, void *p_userdata)
{
  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)p_userdata;
  img_component_t *p_base = &p_comp->b;
  int status = IMG_SUCCESS;
  img_frame_t *p_frame = (img_frame_t *)data;

  status = img_q_enqueue(&p_base->outputQ, p_frame);
  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] enqueue error %d", __func__, __LINE__, status);
  } else {
    IMG_SEND_EVENT(p_base, QIMG_EVT_BUF_DONE);
  }
  p_comp->facedrop_cnt++;
  return status;
}

/**
 * Function: faceproc_dsp_comp_get_facialparts_config
 *
 * Description: To set facial parts config from chromatix
 *
 * Input parameters:
 *   p_chromatix - The pointer to fd chromatix
 *   p_facial_parts_config - config struct for fd facial parts
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
int faceproc_dsp_comp_get_facialparts_config(fd_chromatix_t *p_chromatix,
  facial_parts_wrap_config_t *p_facial_parts_config)
{
  if (NULL == p_chromatix || NULL == p_facial_parts_config) {
    IDBG_ERROR("%s:%d] error p_chromatix=%p, p_facial_parts_config=%p",
      __func__, __LINE__, p_chromatix, p_facial_parts_config);
    return IMG_ERR_INVALID_INPUT;
  }
  /* Fill facial parts detection tuning */
  p_facial_parts_config->enable_blink =
    p_chromatix->enable_blink_detection;
  p_facial_parts_config->enable_smile =
    p_chromatix->enable_smile_detection;
  p_facial_parts_config->enable_gaze =
    p_chromatix->enable_gaze_detection;
  p_facial_parts_config->detection_threshold =
    p_chromatix->facial_parts_threshold;

  p_facial_parts_config->discard_threshold =
    p_chromatix->assist_facial_discard_threshold;
  p_facial_parts_config->weight_eyes =
    p_chromatix->assist_facial_weight_eyes;
  p_facial_parts_config->weight_mouth =
    p_chromatix->assist_facial_weight_mouth;
  p_facial_parts_config->weight_nose =
    p_chromatix->assist_facial_weight_nose;
  p_facial_parts_config->weight_face =
    p_chromatix->assist_facial_weight_face;
  p_facial_parts_config->discard_face_below =
    p_chromatix->assist_below_threshold;
  p_facial_parts_config->weight_eyes =
    p_chromatix->assist_facial_weight_eyes;
  p_facial_parts_config->eyes_use_max_filter =
    (p_chromatix->assist_facial_eyes_filter_type == FD_FILTER_TYPE_MAX);
  p_facial_parts_config->nose_use_max_filter =
    (p_chromatix->assist_facial_nose_filter_type == FD_FILTER_TYPE_MAX);
  p_facial_parts_config->sw_face_threshold =
    p_chromatix->assist_sw_detect_threshold;
  p_facial_parts_config->sw_face_box_border_per =
    p_chromatix->assist_sw_detect_box_border_perc;
  p_facial_parts_config->sw_face_search_dens =
    p_chromatix->assist_sw_detect_search_dens;
  p_facial_parts_config->sw_face_discard_border =
    p_chromatix->assist_sw_discard_frame_border;
  p_facial_parts_config->sw_face_discard_out =
    p_chromatix->assist_sw_discard_out_of_border;
  p_facial_parts_config->min_threshold =
    p_chromatix->assist_facial_min_face_threshold;
  p_facial_parts_config->enable_contour =
    p_chromatix->enable_contour_detection;
  p_facial_parts_config->contour_detection_mode =
    p_chromatix->ct_detection_mode;
  p_facial_parts_config->enable_fp_false_pos_filtering =
    p_chromatix->enable_facial_parts_assisted_face_filtering;
  p_facial_parts_config->enable_sw_false_pos_filtering =
    p_chromatix->enable_sw_assisted_face_filtering;
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_start
 *
 * Description: Start the execution of faceproc
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *   p_data - The pointer to the command structure. The structure
 *            for each command type will be defined in denoise.h
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_start(void *handle, void *p_data)
{
  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)handle;
  img_component_t *p_base = &p_comp->b;
  int status = IMG_SUCCESS;
  facial_parts_wrap_config_t fp_config;

  p_comp->is_chromatix_changed = FALSE;
  pthread_mutex_lock(&p_base->mutex);
  if (p_base->state != IMG_STATE_INIT) {
    IDBG_ERROR("%s:%d] Error state %d", __func__, __LINE__,
      p_base->state);
    pthread_mutex_unlock(&p_base->mutex);
    return IMG_ERR_NOT_SUPPORTED;
  }
  if (NULL == p_base->thread_loop) {
    IDBG_MED("%s:%d] comp loop warning %d", __func__, __LINE__,
      p_base->state);
  }

  if (!p_comp->config_set) {
    IDBG_ERROR("%s:%d] error config not set", __func__, __LINE__);
    pthread_mutex_unlock(&p_base->mutex);
    return status;
  }
  faceproc_dsp_comp_cfg_debug(&p_comp->config);

  faceproc_dsp_comp_chromatix_debug(&p_comp->fd_chromatix);

  memset(&fp_config, 0x0, sizeof(fp_config));
  faceproc_dsp_comp_get_facialparts_config(&p_comp->fd_chromatix,
    &fp_config);

  if (p_comp->p_lib->facial_parts_hndl) {
    status = facial_parts_wrap_config(p_comp->p_lib->facial_parts_hndl,
      &fp_config);
    if (IMG_ERROR(status)) {
      IDBG_ERROR("%s %d]Can not config face parts, status=%d",
        __func__, __LINE__, status);
      pthread_mutex_unlock(&p_base->mutex);
      return IMG_ERR_INVALID_INPUT;
    }
  }


  IDBG_LOW("%s:%d] Enter ", __func__, __LINE__);
  //Start:add img_thread API to schedule job.
  faceproc_dsp_comp_struct struct_faceproc_comp ;
  struct_faceproc_comp.p_comp = p_comp;
  uint32_t current_job_id = 0;
  img_thread_job_params_t cfddspc_eng_config_job;
  cfddspc_eng_config_job.args = &struct_faceproc_comp;
  cfddspc_eng_config_job.client_id =
    g_faceproc_dsp_lib.client_id;
  cfddspc_eng_config_job.core_affinity = IMG_CORE_DSP;
  cfddspc_eng_config_job.dep_job_ids = 0;
  cfddspc_eng_config_job.dep_job_count = 0;
  cfddspc_eng_config_job.delete_on_completion = TRUE;
  cfddspc_eng_config_job.execute =
    faceproc_dsp_comp_eng_config_task_exec;
  current_job_id = img_thread_mgr_schedule_job(
     &cfddspc_eng_config_job);
  IDBG_LOW("%s:%d] Current Job Id =%d ", __func__, __LINE__ , current_job_id);

  if (0 < current_job_id) {
    status = img_thread_mgr_wait_for_completion_by_jobid(current_job_id,
      MAX_FDDSP_API_WAIT );
    status = struct_faceproc_comp.return_value;
  }
  //End:add img_thread API to schedule job.
    IDBG_LOW("%s:%d] Exit ", __func__, __LINE__);

  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] failed, status=%d", __func__, __LINE__, status);
    pthread_mutex_unlock(&p_base->mutex);
    /*In DSP case if eng config fails it will block the preview */
    return status;
  }

  IMG_CHK_ABORT_UNLK_RET(p_base, &p_base->mutex);

  /* flush the queues */
  img_q_flush(&p_base->inputQ);
  img_q_flush(&p_base->outputQ);

  pthread_mutex_unlock(&p_base->mutex);

  status = p_comp->b.ops.start(&p_comp->b, p_data);
  if (status < 0)
    return status;

  return 0;
}

/**
 * Function: faceproc_dsp_comp_abort
 *
 * Description: Aborts the execution of faceproc
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *   p_data - The pointer to the command structure. The structure
 *            for each command type is defined in denoise.h
 *
 * Return values:
 *     IMG_SUCCESS
 *
 * Notes: none
 **/
int faceproc_dsp_comp_abort(void *handle, void *p_data)
{
  IMG_UNUSED(p_data);

  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)handle;
  img_component_t *p_base = (img_component_t *)handle;
  int status = IMG_SUCCESS;

  IDBG_HIGH("%s:%d] state %d", __func__, __LINE__, p_base->state);
  pthread_mutex_lock(&p_base->mutex);
  if (IMG_STATE_STARTED != p_base->state) {
    pthread_mutex_unlock(&p_base->mutex);
    return IMG_SUCCESS;
  }
  p_base->state = IMG_STATE_STOP_REQUESTED;
  pthread_mutex_unlock(&p_base->mutex);
  /*signal the thread*/
  img_q_signal(&p_base->inputQ);

  IDBG_MED("%s:%d] waiting for all jobs to finish ", __func__, __LINE__);
  status = img_thread_mgr_wait_for_completion_by_clientid(
    p_comp->p_lib->client_id  , MAX_FDDSP_LOAD_API_WAIT );
  if (status == IMG_ERR_TIMEOUT) {
    IDBG_ERROR("%s:%d] wait for threadmgr TIMED OUT ", __func__, __LINE__);
  } else if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] wait for threadmgr FAILED status=%d",
      __func__, __LINE__, status);
  }
  IDBG_MED("%s:%d] done wait for jobs  ", __func__, __LINE__);

  /* flush rest of the buffers */
  faceproc_dsp_comp_flush_buffers(p_comp);

  /* destroy the handle */
  status = faceproc_dsp_comp_common_destroy(p_comp);
  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] failed, status=%d", __func__, __LINE__, status);
    return status;
  }

  pthread_mutex_lock(&p_base->mutex);
  p_base->state = IMG_STATE_INIT;
  pthread_mutex_unlock(&p_base->mutex);
  IDBG_HIGH("%s:%d] X", __func__, __LINE__);
  return status;
}

/**
 * Function: faceproc_dsp_comp_process
 *
 * Description: This function is used to send any specific commands for the
 *              faceproc component
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *   cmd - The command type which needs to be processed
 *   p_data - The pointer to the command payload
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *
 * Notes: none
 **/
int faceproc_dsp_comp_process (void *handle, img_cmd_type cmd, void *p_data)
{
  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)handle;
  int status;

  status = p_comp->b.ops.process(&p_comp->b, cmd, p_data);
  if (IMG_ERROR(status))
    return status;

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_queue_buffer
 *
 * Description: This function is used to send buffers to the component
 *
 * Input parameters:
 *   handle - The pointer to the component handle.
 *   p_frame - The frame buffer which needs to be processed by the imaging
 *             library
 *   @type: image type (main image or thumbnail image)
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *
 * Notes: none
 **/
int faceproc_dsp_comp_queue_buffer(void *handle, img_frame_t *p_frame,
  img_type_t type)
{
  if (!handle || !p_frame || type != IMG_IN) {
    IDBG_ERROR("invalid input : handle=%p, p_frame=%p, type=%d",
      handle, p_frame, type);
    return IMG_ERR_INVALID_INPUT;
  }

  faceproc_dsp_comp_t *p_comp = (faceproc_dsp_comp_t *)handle;
  img_component_t *p_base = &p_comp->b;
  int status = IMG_SUCCESS;
  img_queue_t *queue = &p_base->inputQ;
  unsigned int count = img_q_count(queue);

  pthread_mutex_lock(&p_base->mutex);
  if ((p_base->state != IMG_STATE_INIT)
    && (p_base->state != IMG_STATE_STARTED)) {
    IDBG_ERROR("%s:%d] Error %d", __func__, __LINE__, p_base->state);
    pthread_mutex_unlock(&p_base->mutex);
    return IMG_ERR_INVALID_OPERATION;
  }

  /* drop the frame if DSP is currently processing the frame */
  if ((count > 0) || p_comp->processing) {
    IDBG_MED("%s:%d] Drop the frame %d processing %d",
      __func__, __LINE__, count, p_comp->processing);
    pthread_mutex_unlock(&p_base->mutex);
    return IMG_ERR_BUSY;
  }

  status = img_q_enqueue(queue, p_frame);
  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] Error enqueue, status=%d", __func__, __LINE__, status);
    pthread_mutex_unlock(&p_base->mutex);
    return status;
  }
  IDBG_MED("%s:%d] q_count %d", __func__, __LINE__, img_q_count(queue));
  IDBG_MED("%s:%d] p_frame before q_remove p_comp %d p_base %d "
    "  &p_base->inputQ %d",__func__, __LINE__ ,
    (int) p_comp, (int) p_base, (int) queue);

  //// start FROM THREAD_LOOP
  //Start:add img_thread API to schedule job.
  IDBG_MED("%s:%d] Enter scheduled job creation ", __func__, __LINE__);
  faceproc_dsp_comp_exec_struct *p_job_args = (faceproc_dsp_comp_exec_struct*)
    malloc(sizeof(faceproc_dsp_comp_exec_struct));
  if (NULL == p_job_args ) {
    IDBG_ERROR("%s:%d] No memory, continue", __func__, __LINE__);
    pthread_mutex_unlock(&p_base->mutex);
    return IMG_ERR_NO_MEMORY;
  }
  p_job_args->p_comp = p_comp;
  uint32_t current_job_id = 0;
  img_thread_job_params_t fddspc_eng_exec_job;
  fddspc_eng_exec_job.args = p_job_args;
  fddspc_eng_exec_job.client_id = g_faceproc_dsp_lib.client_id;
  fddspc_eng_exec_job.core_affinity = IMG_CORE_DSP;
  fddspc_eng_exec_job.dep_job_ids = 0;
  fddspc_eng_exec_job.dep_job_count = 0;
  fddspc_eng_exec_job.delete_on_completion = TRUE;
  fddspc_eng_exec_job.execute = faceproc_dsp_comp_eng_exec_task_exec;
  current_job_id = img_thread_mgr_schedule_job(&fddspc_eng_exec_job);
  //now wait for job to complete or let it run.
  if (0 < current_job_id) {
    IDBG_MED("%s:%d] not Waiting for scheduled faceproc_dsp"
      "_comp_eng_exec_job completion ", __func__, __LINE__);
    status = p_job_args->return_value;
  }
  //End:add img_thread API to schedule job.
  IDBG_MED("%s:%d] Exit ", __func__, __LINE__);
  //// end FROM THREAD_LOOP

  pthread_mutex_unlock(&p_base->mutex);
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_create
 *
 * Description: This function is used to create Qualcomm Technologies, Inc.
 * faceproc component
 *
 * Input parameters:
 *   @handle: library handle
 *   @p_ops - The pointer to img_component_t object. This object
 *            contains the handle and the function pointers for
 *            communicating with the imaging component.
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_NO_MEMORY
 *     IMG_ERR_INVALID_INPUT
 *     IMG_ERR_INVALID_OPERATION
 *
 * Notes: none
 **/
int faceproc_dsp_comp_create(void* handle, img_component_ops_t *p_ops)
{
  IMG_UNUSED(handle);

  faceproc_dsp_comp_t *p_comp = NULL;
  int status;

  if (g_faceproc_dsp_lib.restore_needed_flag == TRUE) {
    IDBG_MED("%s:%d] restore needed", __func__, __LINE__ );
    img_dsp_dl_requestall_to_close_and_reopen();
  }

  if (NULL == g_faceproc_dsp_lib.ptr_stub) {
    IDBG_WARN("fddsp stub library not loaded , add");
    return IMG_ERR_INVALID_OPERATION;
  }

  if(IMG_SUCCESS != faceproc_dsp_comp_test_dsp_connection_common()){
    IDBG_ERROR("%s:%d] test DSP connection failed ", __func__, __LINE__ );
    img_dsp_dl_mgr_set_reload_needed(TRUE);
    g_faceproc_dsp_lib.restore_needed_flag = TRUE;
    return IMG_ERR_GENERAL;
  }
  g_faceproc_dsp_lib.restore_needed_flag = FALSE;

  p_comp = (faceproc_dsp_comp_t *)malloc(sizeof(faceproc_dsp_comp_t));
  if (NULL == p_comp) {
    IDBG_ERROR("%s:%d] failed", __func__, __LINE__);
    return IMG_ERR_NO_MEMORY;
  }

  if (NULL == p_ops) {
    IDBG_ERROR("%s:%d] failed", __func__, __LINE__);
    status = IMG_ERR_INVALID_INPUT;
    goto error;
  }

  memset(p_comp, 0x0, sizeof(faceproc_dsp_comp_t));
  status = img_comp_create(&p_comp->b);
  if (IMG_ERROR(status)) {
    goto error;
  }

  p_comp->clip_face_data = 1;
  p_comp->p_lib = &g_faceproc_dsp_lib;
  //NOTE : FD-DSP comp does not need thread loop
  p_comp->b.thread_loop = NULL;

  p_comp->b.p_core = p_comp;

  /* copy the ops table from the base component */
  *p_ops = p_comp->b.ops;
  p_ops->init            = faceproc_dsp_comp_init;
  p_ops->deinit          = faceproc_dsp_comp_deinit;
  p_ops->set_parm        = faceproc_dsp_comp_set_param;
  p_ops->get_parm        = faceproc_dsp_comp_get_param;
  p_ops->start           = faceproc_dsp_comp_start;
  p_ops->abort           = faceproc_dsp_comp_abort;
  p_ops->process         = faceproc_dsp_comp_process;
  p_ops->queue_buffer    = faceproc_dsp_comp_queue_buffer;

  p_ops->handle = (void *)p_comp;

  pthread_mutex_init(&p_comp->result_mutex, NULL);

  p_comp->p_lib->facial_parts_hndl = facial_parts_wrap_create();
  if (!p_comp->p_lib->facial_parts_hndl) {
    IDBG_WARN("Facial create failed. Working without it");
  }

  return IMG_SUCCESS;

error:
  IDBG_ERROR("%s:%d] failed %d", __func__, __LINE__, status);
  if (p_comp) {
    free(p_comp);
    p_comp = NULL;
  }
  return status;
}

/**
 * Function: faceproc_dsp_comp_load
 *
 * Description: This function is used to load the faceproc library
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
int faceproc_dsp_comp_load(const char* name, void** handle)
{
  IDBG_HIGH("%s:%d] E \n", __func__, __LINE__);
  IMG_UNUSED(name);
  IMG_UNUSED(handle);
  int ret = IMG_SUCCESS;

  if (g_faceproc_dsp_lib.ptr_stub) {
    IDBG_ERROR("%s:%d] library already loaded", __func__, __LINE__);
    return IMG_ERR_NOT_FOUND;
  }

#ifdef FD_WITH_DSP_NEW
  uint32_t client_id = 0;
  char value[PROPERTY_VALUE_MAX];
  #define MODULE_FDDSP_CONTROL_FLAG     ("persist.camera.imglib.fddsp")
  #define MODULE_FDDSP_CONTROL_ENABLED  ("1")
  #define MODULE_FDDSP_CONTROL_DISABLED ("0")
  #define MODULE_FDDSP_CONTROL_DEFAULT  (MODULE_FDDSP_CONTROL_DISABLED)

  property_get(MODULE_FDDSP_CONTROL_FLAG, value, MODULE_FDDSP_CONTROL_DEFAULT);
  if (!strncmp(MODULE_FDDSP_CONTROL_ENABLED,
    value, PROPERTY_VALUE_MAX - 1)) {
    g_faceproc_dsp_lib.load_dsp_lib = 1;

    IDBG_HIGH("%s:%d] Enter ", __func__, __LINE__);
    img_core_type_t fddsp_img_core_type[2] = { IMG_CORE_DSP,
      IMG_CORE_ARM
    };

    if (IMG_SUCCESS != img_thread_mgr_create_pool(2)) {
      IDBG_ERROR("%s:%d] FDDSP img_thread_mgr_create_pool failed ",
                 __func__, __LINE__);
      return IMG_ERR_NOT_FOUND;
    }
    client_id = img_thread_mgr_reserve_threads(2,
      (img_core_type_t *)(&fddsp_img_core_type));
    if (client_id > 0x7FFFFFFF) {
      IDBG_ERROR("%s:%d] FDDSP reserve threads failed client_id %d \n",
        __func__, __LINE__,  client_id);
    }
    if (client_id == 0) {
      IDBG_ERROR("%s:%d] FDDSP reserve threads failed client_id %d \n",
                 __func__, __LINE__, client_id);
      return IMG_ERR_GENERAL;
    }
    g_faceproc_dsp_lib.client_id = client_id;
    IDBG_HIGH("%s:%d] FDDSP reserve threads client id %d \n",
      __func__, __LINE__, g_faceproc_dsp_lib.client_id);

    //Start:add img_thread API to schedule job.
    faceproc_dsp_comp_eng_load_struct job_args;
    job_args.p_lib = &g_faceproc_dsp_lib;
    uint32_t current_job_id = 0;
    img_thread_job_params_t fddspc_eng_load_job;
    fddspc_eng_load_job.args = &job_args;
    fddspc_eng_load_job.client_id = g_faceproc_dsp_lib.client_id;
    fddspc_eng_load_job.core_affinity = IMG_CORE_DSP;
    fddspc_eng_load_job.dep_job_ids = 0;
    fddspc_eng_load_job.dep_job_count = 0;
    fddspc_eng_load_job.delete_on_completion = TRUE;
    fddspc_eng_load_job.execute = faceproc_dsp_comp_eng_load_task_exec;
    current_job_id = img_thread_mgr_schedule_job(&fddspc_eng_load_job);
    //now wait for job to complete or let it run.
    if (0 < current_job_id) {
      ret = img_thread_mgr_wait_for_completion_by_jobid(current_job_id,
        MAX_FDDSP_LOAD_API_WAIT);
      if (IMG_ERR_TIMEOUT == ret) {
        IDBG_ERROR("%s:%d] TIME OUT - something went wrong during loading",
          __func__, __LINE__);
        ret = img_thread_mgr_unreserve_threads(g_faceproc_dsp_lib.client_id);
        if(IMG_ERROR(ret)){
          IDBG_MED("%s:%d] error unreserving threads",__func__, __LINE__);
        }
        g_faceproc_dsp_lib.client_id = 0;
        img_thread_mgr_destroy_pool();
        ret = IMG_ERR_GENERAL;
      } else {
        ret = job_args.return_value;
      }
    }
    //End:add img_thread API to schedule job.
    IDBG_HIGH("%s:%d] Exit ", __func__, __LINE__);
    if (IMG_SUCCEEDED(ret)) {
      IDBG_HIGH("face detection running in DSP mode");
    } else {
      IDBG_ERROR("face detection DSP Mode Failed");
      ret = IMG_ERR_GENERAL;
    }
  } else {
    g_faceproc_dsp_lib.load_dsp_lib = 0;
    IDBG_INFO("face detection dsp disabled , enable by set property");
    ret = IMG_ERR_GENERAL;
  }
#else
  g_faceproc_dsp_lib.load_dsp_lib = 0;
  IDBG_HIGH("face detection running in ARM mode,"
    "DSP is disabled in make file");
  ret = IMG_ERR_GENERAL;
#endif

  return ret;
}

/**
 * Function: faceproc_dsp_comp_unload
 *
 * Description: This function is used to unload the faceproc library
 *
 * Input parameters:
 *   @handle: library handle
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_NOT_FOUND
 *
 * Notes: none
 **/
void faceproc_dsp_comp_unload(void* handle)
{
  int32 ret = IMG_ERR_GENERAL;
  IDBG_HIGH("%s:%d] Enter ", __func__, __LINE__);
  IMG_UNUSED(handle);

  //Start:add img_thread API to schedule job.
  faceproc_dsp_comp_eng_load_struct job_args ;
  job_args.p_lib = &g_faceproc_dsp_lib;
  uint32_t current_job_id = 0;
  img_thread_job_params_t fddspc_eng_unload_job;
  fddspc_eng_unload_job.args = &job_args;
  fddspc_eng_unload_job.client_id = g_faceproc_dsp_lib.client_id;
  fddspc_eng_unload_job.core_affinity = IMG_CORE_DSP;
  fddspc_eng_unload_job.dep_job_ids = 0;
  fddspc_eng_unload_job.dep_job_count = 0;
  fddspc_eng_unload_job.delete_on_completion = TRUE;
  fddspc_eng_unload_job.execute =
    faceproc_dsp_comp_eng_unload_task_exec;
  current_job_id = img_thread_mgr_schedule_job(
    &fddspc_eng_unload_job);
  if (0 < current_job_id) {
    ret = img_thread_mgr_wait_for_completion_by_jobid(current_job_id,
      MAX_FDDSP_API_WAIT );
    ret = job_args.return_value;
  }
  //End:add img_thread API to schedule job.
  IDBG_MED("%s:%d] Exit ret %d ", __func__, __LINE__ ,(int) ret);

  ret = img_thread_mgr_unreserve_threads(g_faceproc_dsp_lib.client_id);
  if(IMG_ERROR(ret)){
    IDBG_MED("%s:%d] error unreserving threads",__func__, __LINE__);
  }
  g_faceproc_dsp_lib.client_id = 0;
  img_thread_mgr_destroy_pool();
  IDBG_HIGH("%s:%d] Exit after Thread Destroy ", __func__, __LINE__);

}

/**
 * Function: get_faceproc_dsp_lib
 *
 * Description: returns pointer to golbal faceproc dsp lib
 * struct
 *
 * Input parameters:
 *   none
 *
 * Return values:
 *     faceproc_dsp_lib_t* pointer to lib
 *
 * Notes: none
 **/
static faceproc_dsp_lib_t * get_faceproc_dsp_lib()
{
  return &g_faceproc_dsp_lib;
}

/**
 * Function: faceproc_dsp_comp_test_dsp_connection_common
 *
 * Description: schedules the destroy job  to the DSP thread
 *
 * Input parameters:
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_OPERATION
 *
 * Notes: none
 **/
int faceproc_dsp_comp_test_dsp_connection_common() {
  IDBG_HIGH("%s:%d] E \n", __func__, __LINE__);
  int ret = IMG_SUCCESS;

  //Start:add img_thread API to schedule job.
  faceproc_dsp_comp_eng_load_struct job_args ;
  job_args.p_lib = &g_faceproc_dsp_lib;
  uint32_t current_job_id = 0;
  img_thread_job_params_t fddspc_eng_test_job;
  fddspc_eng_test_job.args = &job_args;
  fddspc_eng_test_job.client_id = g_faceproc_dsp_lib.client_id;
  fddspc_eng_test_job.core_affinity = IMG_CORE_DSP;
  fddspc_eng_test_job.dep_job_ids = 0;
  fddspc_eng_test_job.dep_job_count = 0;
  fddspc_eng_test_job.delete_on_completion = TRUE;
  fddspc_eng_test_job.execute =
    faceproc_dsp_comp_eng_test_dsp_connection_task_exec;
  current_job_id = img_thread_mgr_schedule_job(
    &fddspc_eng_test_job);
  //now wait for job to complete or let it run.
  if (0 < current_job_id) {
    ret = img_thread_mgr_wait_for_completion_by_jobid(current_job_id,
      MAX_FDDSP_TEST_API_WAIT);
    if (IMG_ERR_TIMEOUT == ret) {
      IDBG_ERROR("%s:%d] TIME OUT - something went wrong during fddsptest API",
        __func__, __LINE__);
    } else {
      ret = job_args.return_value;
      if (ret != IMG_SUCCESS) {
        IDBG_ERROR("%s:%d] pstruct->return_value %d", __func__, __LINE__ ,
          job_args.return_value);
      }
    }
  }

  IDBG_HIGH("%s:%d] Exit returned %d ", __func__, __LINE__, ret);

  return ret;

}

/**
 * Function: faceproc_dsp_comp_eng_reset_fn_ptrs
 *
 * Description: resets the dynamically loaded function pointers
 *
 * Input parameters:
 *   @p_lib: pointer to faceproc lib
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
static int faceproc_dsp_comp_eng_reset_fn_ptrs(faceproc_dsp_lib_t *p_lib)
{
  if (!p_lib) {
    IDBG_ERROR("%s:%d] invalid param",
      __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  IDBG_LOW("%s:%d] in",
    __func__, __LINE__);

  memset(&(p_lib->fns), 0x0, sizeof(p_lib->fns));

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_eng_load_fn_ptrs
 *
 * Description: Loads the dynamically loaded function pointers
 *
 * Input parameters:
 *   @p_lib: pointer to faceproc lib
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
static int faceproc_dsp_comp_eng_load_fn_ptrs(faceproc_dsp_lib_t *p_lib)
{
  int lib_status_rc;
  uint8 fd_minor_version, fd_major_version;
  if (!p_lib || !p_lib->ptr_stub) {
    IDBG_ERROR("%s:%d] libmmcamera_imglib_"
      "faceproc_adspstub.so lib is not loaded",
      __func__, __LINE__);
    return IMG_ERR_GENERAL;
  }

  /* Link all the fd dsp functions in adsp stub lib */
  *(void **)&(p_lib->fns.FACEPROC_Dt_VersionDSP) =
  dlsym(p_lib->ptr_stub, "adsp_fd_getVersion");
  if (p_lib->fns.FACEPROC_Dt_VersionDSP == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_Dt_VersionDSP error", __func__);
    return IMG_ERR_GENERAL;
  }

  IDBG_HIGH("%s:%d]  ADSP STUB FACEPROC_Dt_VersionDSP loaded fine %d ",
    __func__, __LINE__, (int)p_lib->fns.FACEPROC_Dt_VersionDSP);

  /* Check if FD DSP stub library is requested and valid */
  lib_status_rc = p_lib->fns.FACEPROC_Dt_VersionDSP
    (&fd_minor_version, &fd_major_version);

  IDBG_HIGH("%s], ADSP STUB Checking if DSP stub is valid - rc: %d",
    __func__, lib_status_rc);

  if (lib_status_rc != FACEPROC_NORMAL) {  /*Is DSP stub lib invalid */
    IDBG_WARN("%s],ADSP STUB FD DSP lib error = %d "
      , __func__, lib_status_rc);
  }

  IDBG_HIGH("%s:%d]  ADSP STUB exe FACEPROC_Dt_VersionDSP rc %d  , "
    "minv %d load %d , status %d",
    __func__, __LINE__,  lib_status_rc, fd_minor_version,
    p_lib->load_dsp_lib,p_lib->status_dsp_lib);

  *(void **)&(p_lib->fns.FACEPROC_DeleteDtResult) =
    dlsym(p_lib->ptr_stub, "adsp_fd_DeleteDtResult");
  if (p_lib->fns.FACEPROC_DeleteDtResult == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_DeleteDtResult error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_DeleteDetection) =
    dlsym(p_lib->ptr_stub, "adsp_fd_DeleteDetection");
  if (p_lib->fns.FACEPROC_DeleteDetection == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_DeleteDetection error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_GetDtFaceCount) =
    dlsym(p_lib->ptr_stub, "adsp_fd_GetDtFaceCount");
  if (p_lib->fns.FACEPROC_GetDtFaceCount == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_GetDtFaceCount error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_GetDtFaceInfo) =
    dlsym(p_lib->ptr_stub, "adsp_fd_GetDtFaceInfo");
  if (p_lib->fns.FACEPROC_GetDtFaceInfo == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_GetDtFaceInfo error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_CreateDetection) =
    dlsym(p_lib->ptr_stub, "adsp_fd_CreateDetection");
  if (p_lib->fns.FACEPROC_CreateDetection == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_CreateDetection error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_SetDtMode) =
    dlsym(p_lib->ptr_stub, "adsp_fd_SetDtMode");
  if (p_lib->fns.FACEPROC_SetDtMode == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_SetDtMode error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_SetDtStep) =
    dlsym(p_lib->ptr_stub, "adsp_fd_SetDtStep");
  if (p_lib->fns.FACEPROC_SetDtStep == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_SetDtStep error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_SetDtAngle) =
    dlsym(p_lib->ptr_stub, "adsp_fd_SetDtAngle");
  if (p_lib->fns.FACEPROC_SetDtAngle == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_SetDtAngle error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_SetDtDirectionMask) =
    dlsym(p_lib->ptr_stub, "adsp_fd_SetDtDirectionMask");
  if (p_lib->fns.FACEPROC_SetDtDirectionMask == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_SetDtDirectionMask error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_SetDtFaceSizeRange) =
    dlsym(p_lib->ptr_stub, "adsp_fd_SetDtFaceSizeRange");
  if (p_lib->fns.FACEPROC_SetDtFaceSizeRange == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_SetDtFaceSizeRange error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_SetDtThreshold) =
    dlsym(p_lib->ptr_stub, "adsp_fd_SetDtThreshold");
  if (p_lib->fns.FACEPROC_SetDtThreshold == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_SetDtThreshold error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_CreateDtResult) =
    dlsym(p_lib->ptr_stub, "adsp_fd_CreateDtResult");
  if (p_lib->fns.FACEPROC_CreateDtResult == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_CreateDtResult error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_DetectionDSP) =
    dlsym(p_lib->ptr_stub, "adsp_fd_Detection");
  if (p_lib->fns.FACEPROC_DetectionDSP == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_DetectionDSP error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_SetDtRefreshCount) =
    dlsym(p_lib->ptr_stub, "adsp_fd_SetDtRefreshCount");
  if (p_lib->fns.FACEPROC_SetDtRefreshCount == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_SetDtRefreshCount error", __func__);
    return IMG_ERR_GENERAL;
  }

  *(void **)&(p_lib->fns.FACEPROC_SetDSPPowerPref) =
    dlsym(p_lib->ptr_stub, "adsp_fd_SetDSPPowerPref");
  if (p_lib->fns.FACEPROC_SetDSPPowerPref == NULL) {
    IDBG_ERROR("%s Loading FACEPROC_SetDSPPowerPref error", __func__);
    return IMG_ERR_GENERAL;
  }

  FD_DLSYM_ERROR_RET(p_lib, FACEPROC_SetDtLostParam, "adsp_fd_SetDtLostParam");
  FD_DLSYM_ERROR_RET(p_lib, FACEPROC_DtLockID, "adsp_fd_DtLockID");

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_eng_unload_lib
 *
 * Description: Callback to be called from dsp dl mgr to unload adspstub.so
 *
 * Input parameters:
 *   @handle: handle to faceproc lib
 *   @p_userdata: pointer to userdata
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
static int faceproc_dsp_comp_eng_unload_lib(void *handle, void *p_userdata)
{
  IMG_UNUSED(p_userdata);
  if (g_faceproc_dsp_lib.ptr_stub) {
    dlclose(g_faceproc_dsp_lib.ptr_stub);
    g_faceproc_dsp_lib.ptr_stub = NULL;
  }
  faceproc_dsp_comp_eng_reset_fn_ptrs(&g_faceproc_dsp_lib);

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_eng_reload_lib
 *
 * Description: Callback to be called from dsp dl mgr to reload adspstub.so
 *
 * Input parameters:
 *   @handle: handle to faceproc lib
 *   @name: library name
 *   @p_userdata: pointer to userdata
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
static int faceproc_dsp_comp_eng_reload_lib(void *handle, const char *name,
  void *p_userdata)
{
  IMG_UNUSED(name);
  IMG_UNUSED(handle);
  IMG_UNUSED(p_userdata);
  faceproc_dsp_lib_t *p_lib = &g_faceproc_dsp_lib;

  IDBG_HIGH("%s:%d] Will Try Loading ADSP STUB NOW libmmcamera_"
    "imglib_faceproc_adspstub.so ", __func__, __LINE__);
  /* Load adsp stub lib */
  if (p_lib->ptr_stub == NULL) {
    p_lib->ptr_stub =
      dlopen("libmmcamera_imglib_faceproc_adspstub.so", RTLD_NOW);
    IDBG_HIGH("%s] ptr_stub: %p", __func__, p_lib->ptr_stub);
    if (!p_lib->ptr_stub) {
      IDBG_ERROR("%s:%d] Error loading libmmcamera_imglib_"
        "faceproc_adspstub.so lib",
        __func__, __LINE__);
      return IMG_ERR_GENERAL;
    }
  }
  return faceproc_dsp_comp_eng_load_fn_ptrs(p_lib);

}

/**
 * Function: faceproc_dsp_comp_eng_load_dt_dsp
 *
 * Description: Loads the faceproc library for DSP
 *
 * Input parameters:
 *   p_lib - The pointer to the faceproc lib object
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
static int faceproc_dsp_comp_eng_load_dt_dsp(faceproc_dsp_lib_t *p_lib)
{
  IDBG_HIGH("%s:%d] Will Try Loading ADSP STUB NOW libmmcamera_"
    "imglib_faceproc_adspstub.so ", __func__, __LINE__);
  /* Load adsp stub lib */
  faceproc_dsp_comp_eng_reload_lib(p_lib,
    "libmmcamera_imglib_faceproc_adspstub.so", NULL);
  img_dsp_dlopen("libmmcamera_imglib_faceproc_adspstub.so",p_lib,
    faceproc_dsp_comp_eng_unload_lib,
    faceproc_dsp_comp_eng_reload_lib );

  p_lib->status_dsp_lib = 1;  /* FD DSP lib loaded */
  return IMG_SUCCESS;
}


/**
 * Function: faceproc_dsp_comp_eng_unload
 *
 * Description: Unload the faceproc library
 *
 * Input parameters:
 *   p_lib - The pointer to the faceproc lib object
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
void faceproc_dsp_comp_eng_unload(faceproc_dsp_lib_t *p_lib)
{
  IDBG_HIGH("%s:%d] E", __func__, __LINE__);
  if (p_lib->ptr_stub) {
    dlclose(p_lib->ptr_stub);
    p_lib->ptr_stub = NULL;
  }
  memset(p_lib, 0, sizeof(faceproc_dsp_lib_t));
}

/**
 * Function: faceproc_dsp_comp_eng_load
 *
 * Description: Loads the faceproc library
 *
 * Input parameters:
 *   p_lib - The pointer to the faceproc lib object
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_load(faceproc_dsp_lib_t *p_lib)
{
  int rc = 0;
  IDBG_HIGH("%s:%d] E, p_lib->load_dsp_lib=%d",
    __func__, __LINE__, p_lib->load_dsp_lib);

  if (p_lib->load_dsp_lib) {
    rc = faceproc_dsp_comp_eng_load_dt_dsp(p_lib);
  }

  if (rc < 0) {
    IDBG_ERROR("%s:%d] comp_eng_load_dt_dsp failed rc=%d",
      __func__, __LINE__, rc);
    faceproc_dsp_comp_eng_unload(p_lib);
  }
  return rc;
}

/**
 * Function: faceproc_fd_output
 *
 * Description: Gets the frameproc output
 *
 * Input parameters:
 *   p_comp - The pointer to the faceproc engine object
 *   fd_data - Faceproc result data
 *   num_faces - Number of faces
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
static int faceproc_fd_output(faceproc_dsp_comp_t *p_comp,
  faceproc_result_t *fd_data,
  INT32 *num_faces)
{
  int rc;
  uint32_t i;
  faceproc_info_t *p_output = NULL;

  /* FD START */
  /* Get the number of faces */
  rc = p_comp->p_lib->fns.FACEPROC_GetDtFaceCount(p_comp->hresult, num_faces);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s:%d]FACEPROC_GetDtFaceCount returned error: %d",
      __func__, __LINE__, (uint32_t)rc);
    *num_faces = 0;
    return faceproc_dsp_error_to_img_error(rc);
  }
  IDBG_MED("%s:%d] num_faces %d   fd_dsp", __func__, __LINE__, *num_faces);

  /* Parse and store the faces */
  fd_data->num_faces_detected = (uint32_t)*num_faces;
  fd_data->frame_id = p_comp->frame_id;

  if (fd_data->num_faces_detected > MAX_FACE_ROI)
    fd_data->num_faces_detected = MAX_FACE_ROI;

  if (!fd_data->num_faces_detected) {
    for (i = 0; i < MAX_FACE_ROI; i++) {
      p_output = &fd_data->roi[i];
      p_output->blink_detected = 0;
      p_output->left_blink = 0;
      p_output->right_blink = 0;
      p_output->left_right_gaze = 0;
      p_output->top_bottom_gaze = 0;
    }
  }
  for (i = 0; i < fd_data->num_faces_detected; i++) {
    FACEINFO face_info;
    uint32_t left, top, right, bottom;
    rc = p_comp->p_lib->fns.FACEPROC_GetDtFaceInfo(p_comp->hresult, (int32_t)i,
      &face_info);
    if (rc != FACEPROC_NORMAL) {
      IDBG_ERROR("%s FACEPROC_GetDtFaceInfo returned error: %d",
        __func__, (uint32_t)rc);
      fd_data->num_faces_detected--;
      return faceproc_dsp_error_to_img_error(rc);
    }
    IDBG_MED("%s:%d] FACE INFO conf %d, nid %d, nPose %d, LT x %d, LT y %d, "
      "LB x %d, LB y %d lock %d",__func__, __LINE__,
      face_info.nConfidence,
      face_info.nID,
      face_info.nPose,
      face_info.ptLeftTop.x,
      face_info.ptLeftTop.y,
      face_info.ptLeftBottom.x,
      face_info.ptLeftBottom.y,
      p_comp->fd_chromatix.lock_faces);

    if (p_comp->fd_chromatix.lock_faces && (face_info.nID > 0)) {
      rc = p_comp->p_lib->fns.FACEPROC_DtLockID(p_comp->hresult, face_info.nID);
      if (FACEPROC_NORMAL != rc) {
        IDBG_ERROR("%s:%d] Error FACEPROC_DtLockID %d ID %u",
          __func__, __LINE__, rc, (uint32_t)face_info.nID);
      }
    }

    /* Translate the data */
    /* Clip each detected face coordinates to be within the frame boundary */
    CLIP(face_info.ptLeftTop.x, 0, (int32_t)p_comp->width);
    CLIP(face_info.ptRightTop.x, 0, (int32_t)p_comp->width);
    CLIP(face_info.ptLeftBottom.x, 0, (int32_t)p_comp->width);
    CLIP(face_info.ptRightBottom.x, 0,
      (int32_t)p_comp->width);
    CLIP(face_info.ptLeftTop.y, 0, (int32_t)p_comp->height);
    CLIP(face_info.ptRightTop.y, 0, (int32_t)p_comp->height);
    CLIP(face_info.ptLeftBottom.y, 0,
      (int32_t)p_comp->height);
    CLIP(face_info.ptRightBottom.y, 0,
      (int32_t)p_comp->height);

    /* Find the bounding box */
    left = (uint32_t)MIN4(face_info.ptLeftTop.x, face_info.ptRightTop.x,
      face_info.ptLeftBottom.x, face_info.ptRightBottom.x);
    top = (uint32_t)MIN4(face_info.ptLeftTop.y, face_info.ptRightTop.y,
      face_info.ptLeftBottom.y, face_info.ptRightBottom.y);
    right = (uint32_t)MAX4(face_info.ptLeftTop.x, face_info.ptRightTop.x,
      face_info.ptLeftBottom.x, face_info.ptRightBottom.x);
    bottom = (uint32_t)MAX4(face_info.ptLeftTop.y, face_info.ptRightTop.y,
      face_info.ptLeftBottom.y, face_info.ptRightBottom.y);

    p_output = &fd_data->roi[i];

    if (p_comp->clip_face_data) {
      POINT center;
      POINT *p_left, *p_right;
      uint32_t face_len;
      p_left = &face_info.ptLeftTop;
      p_right = &face_info.ptRightBottom;

      int32_t x_delta = p_right->x - p_left->x;
      int32_t y_delta = p_right->y - p_left->y;
      face_len = (uint32_t)sqrt((uint32_t)
        ((pow(y_delta, 2) + pow(x_delta, 2))) >> 1);
      center.x = ((p_right->x + p_left->x) + 1) >> 1;
      center.y = ((p_right->y + p_left->y) + 1) >> 1;

      IDBG_MED("%s:%d] face_len %d center (%d %d) old (%d %d)",
        __func__, __LINE__,
        face_len, center.x, center.y,
        left + ((right - left) >> 1),
        top + ((bottom - top) >> 1));
      left = (uint32_t)center.x - (face_len >> 1);
      top = (uint32_t)center.y - (face_len >> 1);
      p_output->face_boundary.dx = face_len;
      p_output->face_boundary.dy = face_len;
    } else {
      p_output->face_boundary.dx = right - left;
      p_output->face_boundary.dy = bottom - top;
    }

    p_output->gaze_angle = face_info.nPose;
    p_output->face_boundary.x = left;
    p_output->face_boundary.y = top;
    p_output->unique_id = abs(face_info.nID);
    p_output->fd_confidence = face_info.nConfidence;

    /* some logic to caculate correct roll angle */
    float angle = atan2( face_info.ptLeftTop.x - face_info.ptLeftBottom.x,
      face_info.ptLeftTop.y - face_info.ptLeftBottom.y);
    int angle_deg = angle * 180 / PI;
    p_output->face_angle_roll = angle_deg;

    p_output->face_angle_roll = 180 - p_output->face_angle_roll;
    IDBG_LOW("%s:%d] face_angle_roll %d  angle= %f )",
      __func__, __LINE__, angle_deg, angle);

  }  /* end of forloop */
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_frame_dump
 *
 * Description: Dump frames based on dump configuration
 *
 * Arguments:
 *   @p_comp: Pointer to faceproc component struct
 *   @p_frame: Pointer to frame which need to be dumped.
 *   @num_faces: Number of detected faces.
 *
 * Return values:
 *   None
 **/
static void faceproc_frame_dump(faceproc_dsp_comp_t *p_comp,
  img_frame_t *p_frame, unsigned int num_faces)
{
  unsigned int i;
  unsigned int tracked;
  int rc;

  if (!p_comp || !p_frame) {
    IDBG_ERROR("Invalid frame dump input p_comp=%p, p_frame=%p",
      p_comp, p_frame);
    return;
  }

  if (p_comp->dump_mode != FACE_FRAME_DUMP_OFF) {
    FACEINFO face_info;

    tracked = 0;
    for (i = 0; i < num_faces; i++) {
      rc = p_comp->p_lib->fns.FACEPROC_GetDtFaceInfo(p_comp->hresult, (int32_t)i,
        &face_info);
      if (rc != FACEPROC_NORMAL) {
        IDBG_ERROR("%s FACEPROC_GetDtFaceInfo returned error: %d",
          __func__, (uint32_t)rc);
        return;
      }
      if (face_info.nID < 0) {
        tracked = 1;
      }
    }

    switch (p_comp->dump_mode) {
    case FACE_FRAME_DUMP_NON_TRACKED:
      if (num_faces && !tracked) {
        img_dump_frame(p_frame, FACE_DEBUG_PATH, 0, NULL);
      }
      break;
    case FACE_FRAME_DUMP_TRACKED:
      if (tracked) {
        img_dump_frame(p_frame, FACE_DEBUG_PATH, 0, NULL);
      }
      break;
    case FACE_FRAME_DUMP_NOT_DETECTED:
      if (num_faces == 0) {
        img_dump_frame(p_frame, FACE_DEBUG_PATH, 0, NULL);
      }
      break;
    case FACE_FRAME_DUMP_ALL:
      img_dump_frame(p_frame, FACE_DEBUG_PATH, 0, NULL);
      break;
    default:
      return;
    }
  }
}

/**
 * Function: faceproc_fd_execute
 *
 * Description: Executes the face detecttion algorithm
 *
 * Input parameters:
 *   p_comp - The pointer to the faceproc engine object
 *   p_frame - pointer to input frame
 *   num_faces - number of faces
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
static int faceproc_fd_execute(faceproc_dsp_comp_t *p_comp,
  img_frame_t *p_frame, INT32 * num_faces)
{
  ATRACE_BEGIN("Camera:Faceproc");
  struct timeval start_time, end_time;
  IDBG_MED("%s:%d] E %dx%d", __func__, __LINE__,
    IMG_FD_WIDTH(p_frame), IMG_HEIGHT(p_frame));

  IMG_TIMER_START(start_time);

  int rc = FACEPROC_NORMAL;
  p_comp->frame_id = p_frame->frame_id;
  if (p_comp->p_lib->status_dsp_lib) {
    IDBG_MED("%s:%d] before FACEPROC_DetectionDSP E", __func__, __LINE__);
    IDBG_MED("before FACEPROC_DetectionDSP, hDT: %p, pImage: %p, nWidth: %d,"
      "nHeight: %d, nAccuracy: %d, hResult: %p",
      p_comp->hdt,
      (RAWIMAGE *)IMG_ADDR(p_frame),
      IMG_FD_WIDTH(p_frame),
      IMG_HEIGHT(p_frame),
      ACCURACY_HIGH_TR,
      p_comp->hresult);

    rc = p_comp->p_lib->fns.FACEPROC_DetectionDSP(p_comp->hdt,
      (RAWIMAGE *)IMG_ADDR(p_frame),
      IMG_FD_WIDTH(p_frame)*IMG_HEIGHT(p_frame),
      IMG_FD_WIDTH(p_frame),
      IMG_HEIGHT(p_frame),
      ACCURACY_HIGH_TR,
      p_comp->hresult);

    IDBG_MED("%s:%d] after FACEPROC_DetectionDSP", __func__, __LINE__);

    if (rc != FACEPROC_NORMAL) {
      IDBG_ERROR("%s FACEPROC_DetectionDSP returned error: %d",
        __func__, (uint32_t)rc);
      *num_faces = 0;
      goto fd_ex_end;
    }
  } else {
    IDBG_MED("%s:%d] FACEPROC_DetectionDSP disabled ", __func__, __LINE__);
  }

  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_Detection returned error: %d",
      __func__, (uint32_t)rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  if (frame_count < 10)
    IMG_TIMER_END(start_time, end_time, "FD_DSP", IMG_TIMER_MODE_MS);

  ATRACE_END();

  frame_count++;

#ifdef FD_FPS
  /*Log FD fps every 16 fd_frames */
  if (frame_count % 16 == 0) {
    gettimeofday (&fd_time, NULL);
    end_fd_ms = (fd_time.tv_sec * 1000 + fd_time.tv_usec / 1000);
    total_elapsed_ms = end_fd_ms - start_fd_ms;
    IDBG_HIGH("FD frame rate: %2.1f",
      (16000.0 / ((double)end_fd_ms - (double)start_fd_ms)));
    start_fd_ms = end_fd_ms;
  }
#endif

  /*Set Position for PT */
  rc = p_comp->p_lib->fns.FACEPROC_GetDtFaceCount(p_comp->hresult, num_faces);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s:%d]FACEPROC_GetDtFaceCount returned error: %d",
      __func__, __LINE__, (uint32_t)rc);
    *num_faces = 0;
    return faceproc_dsp_error_to_img_error(rc);
  }

  IDBG_MED("%s:%d]FACEPROC_GetDtFaceCount executed successfully "
           "returned error: %d", __func__, __LINE__, (uint32_t)rc);

  faceproc_frame_dump(p_comp, p_frame, *num_faces);

  IDBG_MED("%s:%d] num faces %d, dsp %d    fd_dsp", __func__, __LINE__,
    *num_faces, p_comp->p_lib->status_dsp_lib);

  if (*num_faces <= 0) {
    IDBG_MED("%s:%d] no faces detected X", __func__, __LINE__);
    return IMG_SUCCESS;
  }

fd_ex_end:
  IDBG_MED("%s:%d] X", __func__, __LINE__);

  return faceproc_dsp_error_to_img_error(rc);
}


/**
 * Function: faceproc_dsp_comp_eng_get_angle
 *
 * Description: Get the faceproc angle
 *
 * Input parameters:
 *   angle: face detection angle macro
 *
 * Return values:
 *     bitmask for engine angle
 *
 * Notes: none
 **/
uint32_t faceproc_dsp_comp_eng_get_angle(fd_chromatix_angle_t angle)
{
  switch (angle) {
    case FD_ANGLE_ALL:
      return ANGLE_ALL;
    case FD_ANGLE_15_ALL:
      return(ANGLE_0 | ANGLE_3 | ANGLE_6 | ANGLE_9);
    case FD_ANGLE_45_ALL:
      return(ANGLE_0 | ANGLE_1 | ANGLE_2 |
        ANGLE_3 | ANGLE_4 | ANGLE_8 |
        ANGLE_9 |ANGLE_10 | ANGLE_11);
    case FD_ANGLE_MANUAL:
      /* todo */
      return ANGLE_NONE;
    case FD_ANGLE_NONE:
    default:
      return ANGLE_NONE;
  }
  return ANGLE_NONE;
}

/**
 * Function: faceproc_dsp_comp_eng_face_size
 *
 * Description: Get the face size
 *
 * Input parameters:
 *   face_adj_type: type of face dimension calculation
 *   face_size: face size for fixed adjustment
 *   ratio: facesize ratio for floating adjustment
 *   min_size: minimum face size supported by library
 *   dimension: min(height/width) of the image
 *
 * Return values:
 *     face size based on input parameters
 *
 * Notes: none
 **/
uint32_t faceproc_dsp_comp_eng_face_size(fd_face_adj_type_t face_adj_type,
  uint32_t face_size,
  float ratio,
  uint32_t min_size,
  uint32_t dimension)
{
  switch (face_adj_type) {
    case FD_FACE_ADJ_FLOATING: {
      uint32_t size = (uint32_t)((float)dimension * ratio);;
      size = (size/10) * 10;
      return(size < min_size) ? min_size : size;
    }
    default:
    case FD_FACE_ADJ_FIXED:
      return face_size;
  }
  return face_size;
}

/**
 * Function: faceproc_dsp_comp_eng_set_facesize
 *
 * Description: Configure the faceproc engine min face size
 *
 * Input parameters:
 *   p_comp - The pointer to the faceproc engine object
 *   max_width - width of the image
 *   max_height - height of the image
 *
 * Return values:
 *     none
 *
 * Notes: none
 **/
void faceproc_dsp_comp_eng_set_facesize(faceproc_dsp_comp_t *p_comp,
  uint32_t max_width, uint32_t max_height)
{
  uint32_t min_face_size = faceproc_dsp_comp_eng_face_size(
    p_comp->fd_chromatix.min_face_adj_type,
    p_comp->fd_chromatix.min_face_size,
    p_comp->fd_chromatix.min_face_size_ratio,
    50, /* keeping min facesize as 50 */
    MIN(max_width, max_height));
  IDBG_MED("%s:%d] new ###min_face_size %d", __func__, __LINE__,
    min_face_size);

  /* Set the max and min face size for detection */
  int rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtFaceSizeRange(
    p_comp->hdt, (int32_t)min_face_size,
    (int32_t)p_comp->fd_chromatix.max_face_size);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s:%d] FACEPROC_SetDtFaceSizeRange failed %d",
      __func__, __LINE__, rc);
  }
}

/**
 * Function: faceproc_dsp_comp_eng_config
 *
 * Description: Configure the faceproc engine
 *
 * Input parameters:
 *   p_comp - The pointer to the faceproc engine object
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_config(faceproc_dsp_comp_t *p_comp)
{
  IDBG_LOW("%s] Enter: %p", __func__, (void *)p_comp);

  UINT32 an_still_angle[POSE_TYPE_COUNT];
  int rc = IMG_SUCCESS;
  faceproc_config_t *p_cfg ;
  uint32_t rotation_range ;
  uint32_t max_num_face_to_detect;
  uint32_t min_face_size;

  if (!p_comp ) {
    IDBG_ERROR("%s:%d] NULL component", __func__, __LINE__);
    return IMG_ERR_GENERAL;
  }
  p_cfg = &p_comp->config;
  rotation_range = p_cfg->face_cfg.rotation_range;

  min_face_size = faceproc_dsp_comp_eng_face_size(
    p_comp->fd_chromatix.min_face_adj_type,
    p_comp->fd_chromatix.min_face_size,
    p_comp->fd_chromatix.min_face_size_ratio,
    50, // keeping min facesize as 50
    MIN(p_cfg->frame_cfg.max_width, p_cfg->frame_cfg.max_height));

  IDBG_MED("%s:%d] ###min_face_size %d", __func__, __LINE__,
    min_face_size);

  max_num_face_to_detect = p_comp->fd_chromatix.max_num_face_to_detect;

  IDBG_MED("%s], Enter: p_comp: [%p, %p]load_dsp: %d, max_faces: %d",
    __func__,    p_comp, (void *)&p_comp,
    p_comp->p_lib->status_dsp_lib, max_num_face_to_detect);

  if (FD_ANGLE_ENABLE(p_comp)) {
    an_still_angle[POSE_FRONT] = faceproc_dsp_comp_eng_get_angle(
      p_comp->fd_chromatix.angle_front);
    an_still_angle[POSE_HALF_PROFILE] = faceproc_dsp_comp_eng_get_angle(
      p_comp->fd_chromatix.angle_half_profile);
    an_still_angle[POSE_PROFILE] = faceproc_dsp_comp_eng_get_angle(
      p_comp->fd_chromatix.angle_full_profile);
  } else {
    IDBG_MED("%s:%d] ###Disable Angle", __func__, __LINE__);
    an_still_angle[POSE_FRONT] = ANGLE_NONE;
    an_still_angle[POSE_HALF_PROFILE] = ANGLE_NONE;
    an_still_angle[POSE_PROFILE] = ANGLE_NONE;
  }

  IDBG_MED("%s], Before calling FACEPROC_CreateDetection: p_comp: %p",
    __func__, p_comp);
  p_comp->hdt = p_comp->p_lib->fns.FACEPROC_CreateDetection();
  if (!p_comp->hdt) {
    IDBG_ERROR("%s FACEPROC_CreateDetection failed",  __func__);
    return IMG_ERR_GENERAL;
  } else {
    IDBG_MED("%s %d] calling FACEPROC_SetDSPPowerPref:p_comp:%p, %d %d %d %d",
    __func__, __LINE__, p_comp, 522, 240, 1000, 1);
    p_comp->p_lib->fns.FACEPROC_SetDSPPowerPref(522,240,1000,1);
  }
  IDBG_MED("After calling FACEPROC_CreateDetection");


  /* Set best Faceproc detection mode for video */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtMode(
    p_comp->hdt, (int32_t)p_comp->fd_chromatix.detection_mode);


  IDBG_MED("After calling FACEPROC_SetDtMode");
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtMode failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* Set search density */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtStep(
    p_comp->hdt,
    (int32_t)p_comp->fd_chromatix.search_density_nontracking,
    (int32_t)p_comp->fd_chromatix.search_density_tracking);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtStep failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* Set Detection Angles */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtAngle(
    p_comp->hdt, an_still_angle,
    ANGLE_ROTATION_EXT0 | ANGLE_POSE_EXT0);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtAngle failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* Set refresh count */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtRefreshCount(
    p_comp->hdt, (int32_t)p_comp->fd_chromatix.detection_mode,
    (int32_t)p_comp->fd_chromatix.refresh_count);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtRefreshCount failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtDirectionMask(
    p_comp->hdt, (int32_t)p_comp->fd_chromatix.direction);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtDirectionMask failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* Minimum face size to be detected should be at most half the
    height of the input frame */
  if (min_face_size > (p_cfg->frame_cfg.max_height/2)) {
    IDBG_ERROR("%s:%d] Error, min face size to detect is greater than "
      "half the height of the input frame", __func__, __LINE__);
    return IMG_SUCCESS;
  }

  /* Set the max and min face size for detection */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtFaceSizeRange(
    p_comp->hdt, (int32_t)min_face_size,
    (int32_t)p_comp->fd_chromatix.max_face_size);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtFaceSizeRange failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }
  /* Set Detection Threshold */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtThreshold(
    p_comp->hdt, (int32_t)p_comp->fd_chromatix.threshold,
    (int32_t)p_comp->fd_chromatix.threshold);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtFaceSizeRange failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* FD Configuration logging */
  //IDBG_HIGH("fddsp_config: Parts: (%d, %d), Contour: %d, BGS: %d, Recog: %d",
  //  FD_FACEPT_ENABLE(p_comp), FACE_PART_DETECT, FACE_CONTOUR_DETECT,
  //  FACE_BGS_DETECT, FACE_RECOGNITION);
  IDBG_HIGH("fddsp_config: MAX # of faces: %d", max_num_face_to_detect);
  IDBG_HIGH("fddsp_config: MIN, MAX face size: %d, %d",
    min_face_size, p_comp->fd_chromatix.max_face_size);
  IDBG_HIGH("fddsp_config: DT_mode: %d, Refresh_count: %d",
    p_comp->fd_chromatix.detection_mode, p_comp->fd_chromatix.refresh_count);
  IDBG_HIGH("fddsp_config: Search Density: %d",
    p_comp->fd_chromatix.search_density_tracking);
  IDBG_HIGH("fddsp_config: Detection Threshold: %d",
    p_comp->fd_chromatix.threshold);
  IDBG_HIGH("fddsp_config: Angles: %d, %d, %d, Track: %d",
    an_still_angle[POSE_FRONT], an_still_angle[POSE_HALF_PROFILE],
    an_still_angle[POSE_PROFILE], (ANGLE_ROTATION_EXT0 | ANGLE_POSE_EXT0));

  /* Create Faceproc result handle */
  p_comp->hresult = p_comp->p_lib->fns.FACEPROC_CreateDtResult(
    (int32_t)max_num_face_to_detect ,
    (int32_t)(max_num_face_to_detect/2));
  if (!(p_comp->hresult)) {
    IDBG_ERROR("%s FACEPROC_CreateDtResult failed",  __func__);
    return IMG_ERR_GENERAL;
  }
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_eng_reconfig_core
 *
 * Description: Re-Configure the faceproc engine
 *
 * Input parameters:
 *   p_comp - The pointer to the faceproc engine object
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_reconfig_core(faceproc_dsp_comp_t *p_comp)
{
  IDBG_MED("%s] Enter: %p", __func__, (void *)p_comp);

  UINT32 an_still_angle[POSE_TYPE_COUNT];
  int rc = IMG_SUCCESS;
  faceproc_config_t *p_cfg ;
  uint32_t rotation_range ;
  uint32_t max_num_face_to_detect;
  uint32_t min_face_size;

  if (!p_comp) {
    IDBG_ERROR("%s:%d] NULL component", __func__, __LINE__);
    return IMG_ERR_GENERAL;
  }
  p_cfg = &p_comp->config;
  rotation_range = p_cfg->face_cfg.rotation_range;

  min_face_size = faceproc_dsp_comp_eng_face_size(
    p_comp->fd_chromatix.min_face_adj_type,
    p_comp->fd_chromatix.min_face_size,
    p_comp->fd_chromatix.min_face_size_ratio,
    50, // keeping min facesize as 50
    MIN(p_cfg->frame_cfg.max_width, p_cfg->frame_cfg.max_height));

  IDBG_MED("%s:%d] ###min_face_size %d", __func__, __LINE__,
    min_face_size);

  max_num_face_to_detect = p_comp->fd_chromatix.max_num_face_to_detect;

  IDBG_MED("%s], Enter: p_comp: [%p, %p]load_dsp: %d, max_faces: %d",
    __func__, p_comp, (void *)&p_comp, p_comp->p_lib->status_dsp_lib,
    max_num_face_to_detect);

  if (FD_ANGLE_ENABLE(p_comp)) {
    an_still_angle[POSE_FRONT] = faceproc_dsp_comp_eng_get_angle(
      p_comp->fd_chromatix.angle_front);
    an_still_angle[POSE_HALF_PROFILE] = faceproc_dsp_comp_eng_get_angle(
      p_comp->fd_chromatix.angle_half_profile);
    an_still_angle[POSE_PROFILE] = faceproc_dsp_comp_eng_get_angle(
      p_comp->fd_chromatix.angle_full_profile);
  } else {
    IDBG_MED("%s:%d] ###Disable Angle", __func__, __LINE__);
    an_still_angle[POSE_FRONT] = ANGLE_NONE;
    an_still_angle[POSE_HALF_PROFILE] = ANGLE_NONE;
    an_still_angle[POSE_PROFILE] = ANGLE_NONE;
  }

  /* Set best Faceproc detection mode for video */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtMode(
    p_comp->hdt, (int32_t)p_comp->fd_chromatix.detection_mode);


  IDBG_MED("After calling FACEPROC_SetDtMode");
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtMode failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* Set search density */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtStep(
    p_comp->hdt,
    (int32_t)p_comp->fd_chromatix.search_density_nontracking,
    (int32_t)p_comp->fd_chromatix.search_density_tracking);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtStep failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* Set Detection Angles */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtAngle(
    p_comp->hdt, an_still_angle,
    ANGLE_ROTATION_EXT0 | ANGLE_POSE_EXT0);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtAngle failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* Set refresh count */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtRefreshCount(
    p_comp->hdt, (int32_t)p_comp->fd_chromatix.detection_mode,
    (int32_t)p_comp->fd_chromatix.refresh_count);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtRefreshCount failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtDirectionMask(
    p_comp->hdt, (BOOL)p_comp->fd_chromatix.direction);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtDirectionMask failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* Minimum face size to be detected should be at most half the
    height of the input frame */
  if (min_face_size > (p_cfg->frame_cfg.max_height/2)) {
    IDBG_ERROR("%s:%d] ", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }

  /* Set the max and min face size for detection */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtFaceSizeRange(
    p_comp->hdt, (int32_t)min_face_size,
    (int32_t)p_comp->fd_chromatix.max_face_size);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtFaceSizeRange failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }
  /* Set Detection Threshold */
  rc = (int) p_comp->p_lib->fns.FACEPROC_SetDtThreshold(
    p_comp->hdt, (int32_t)p_comp->fd_chromatix.threshold,
    (int32_t)p_comp->fd_chromatix.threshold);
  if (rc != FACEPROC_NORMAL) {
    IDBG_ERROR("%s FACEPROC_SetDtFaceSizeRange failed %d",  __func__, rc);
    return faceproc_dsp_error_to_img_error(rc);
  }

  /* FD Configuration logging */
  IDBG_HIGH("fddsp_reconfig: MAX # of faces: %d", max_num_face_to_detect);
  IDBG_HIGH("fddsp_reconfig: MIN, MAX face size: %d, %d",
    min_face_size, p_comp->fd_chromatix.max_face_size);
  IDBG_HIGH("fddsp_reconfig: DT_mode: %d, Refresh_count: %d",
    p_comp->fd_chromatix.detection_mode, p_comp->fd_chromatix.refresh_count);
  IDBG_HIGH("fddsp_reconfig: Search Density: %d",
    p_comp->fd_chromatix.search_density_tracking);
  IDBG_HIGH("fddsp_reconfig: Detection Threshold: %d",
    p_comp->fd_chromatix.threshold);
  IDBG_HIGH("fddsp_reconfig: Angles: %d, %d, %d, Track: %d",
    an_still_angle[POSE_FRONT], an_still_angle[POSE_HALF_PROFILE],
    an_still_angle[POSE_PROFILE], (ANGLE_ROTATION_EXT0 | ANGLE_POSE_EXT0));

  /* Delete Old Result handle */
  if (p_comp->hresult) {
    rc = p_comp->p_lib->fns.FACEPROC_DeleteDtResult(p_comp->hresult);
    if (rc != FACEPROC_NORMAL) {
      IDBG_ERROR("%s FACEPROC_DeleteDtResult failed",  __func__);
      return faceproc_dsp_error_to_img_error(rc);
    }
    p_comp->hresult = NULL;
  }
  /* Create Faceproc result handle */
  p_comp->hresult = p_comp->p_lib->fns.FACEPROC_CreateDtResult(
    (int32_t)max_num_face_to_detect ,
    (int32_t)(max_num_face_to_detect/2));
  if (!(p_comp->hresult)) {
    IDBG_ERROR("%s FACEPROC_CreateDtResult failed",  __func__);
    return IMG_ERR_GENERAL;
  }
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_eng_exec
 *
 * Description: main algorithm execution function for face processing
 *
 * Input parameters:
 *   p_comp - The pointer to the faceproc engine object
 *   p_frame - Input frame
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
extern void faceproc_dsp_comp_flush_buffers(faceproc_dsp_comp_t *p_comp);
extern faceproc_dsp_lib_t * get_faceproc_dsp_lib();

int faceproc_dsp_comp_eng_exec(faceproc_dsp_comp_t *p_comp,
  img_frame_t *p_frame)
{
  INT32 num_faces;
  img_component_t *p_base;
  facial_parts_wrap_config_t fp_config;
  faceproc_result_t fd_result;
  int status = IMG_SUCCESS;

  if (NULL == p_comp) {
    IDBG_ERROR("%s:%d] null p_comp ", __func__, __LINE__ );
    return IMG_SUCCESS;
  }

  p_base = &p_comp->b;

  if (!face_proc_dsp_can_wait(p_comp)) {
    IDBG_HIGH("%s:%d] Exit the thread", __func__, __LINE__);
    return IMG_SUCCESS;
  }

  IDBG_MED("%s:%d] p_frame before q_remove p_comp %d p_base %d &p_base->inputQ"
    " %d",__func__, __LINE__ , (int)p_comp, (int)p_base, (int)&p_base->inputQ);
  p_frame = img_q_dequeue(&p_base->inputQ);
  IDBG_MED("%s:%d] p_frame AFTER q_remove %d ", __func__, __LINE__ ,
    (int) p_frame);

  if (NULL == p_frame) {
    IDBG_ERROR("%s:%d] NO Frame in in input Queue ", __func__, __LINE__ );
    return IMG_ERR_INVALID_INPUT;
  }
  if ((p_comp->width != IMG_FD_WIDTH(p_frame)) ||
    (p_comp->height != IMG_HEIGHT(p_frame))) {
    IDBG_HIGH("%s:%d] [FD_HAL3] Update dimensions to %dx%d", __func__,
      __LINE__, IMG_FD_WIDTH(p_frame), IMG_HEIGHT(p_frame));
    p_comp->width = IMG_FD_WIDTH(p_frame);
    p_comp->height = IMG_HEIGHT(p_frame);
    faceproc_dsp_comp_eng_set_facesize(p_comp, p_comp->width, p_comp->height);
  }
  IDBG_MED("%s:%d] Enter ", __func__, __LINE__);
  //////   END FROM THREAD_LOOP

  switch (p_comp->mode) {
    case FACE_DETECT_BSGC:
    case FACE_DETECT:
    case FACE_DETECT_LITE:
      if (p_comp->is_chromatix_changed == TRUE) {
        p_comp->is_chromatix_changed = FALSE;

        memset(&fp_config,0,sizeof(fp_config));
        faceproc_dsp_comp_get_facialparts_config(&p_comp->fd_chromatix,
          &fp_config);
        if (p_comp->p_lib->facial_parts_hndl) {
          status = facial_parts_wrap_config(p_comp->p_lib->facial_parts_hndl,
            &fp_config);
          if (IMG_ERROR(status)) {
            IDBG_ERROR("Can not config face parts");
            return status;
          }
        }

        status = faceproc_dsp_comp_eng_reconfig_core(p_comp);
      }

      status = faceproc_fd_execute(p_comp, p_frame, &num_faces);
      break;
    default :
      IDBG_ERROR("%s MODE not selected/recognized", __func__);
  }

  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] error %d", __func__, __LINE__, status);
    goto exec_error;
  }

  IDBG_MED("%s:%d] state %d abort %d", __func__, __LINE__,
    p_base->state, p_comp->abort_flag);
  if (IMG_CHK_ABORT_LOCKED(p_base, &p_base->mutex)) {
    IDBG_ERROR("%s:%d] Abort requested %d", __func__, __LINE__, status);
    status = face_proc_release_frame(p_frame, p_comp);
    return IMG_SUCCESS;
  }

  memset(&fd_result,0,sizeof(fd_result));
  status = faceproc_dsp_comp_eng_get_output(p_comp, &fd_result);
  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] invalid faceproc result", __func__, __LINE__);
    goto exec_error;
  }

  faceproc_internal_queue_struct *p_node;
  p_node = img_q_dequeue(&p_comp->intermediate_free_Q);
  if (!p_node) {
    IDBG_ERROR("%s:%d] dequeue error %d", __func__, __LINE__, status);
    goto exec_error;
  }

  p_node->inter_result = fd_result;
  p_node->p_frame = p_frame;
  status = img_q_enqueue(&p_comp->intermediate_in_use_Q, p_node);
  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] enqueue error %d", __func__, __LINE__, status);
    goto exec_error;
  }

  //S:add img_thread API to schedule job.
  faceproc_dsp_comp_eng_get_output_struct *p_job_args =
    (faceproc_dsp_comp_eng_get_output_struct*)malloc(
       sizeof(faceproc_dsp_comp_eng_get_output_struct));
  if (NULL == p_job_args ) {
    IDBG_ERROR("%s:%d] No memory", __func__, __LINE__);
    status = IMG_ERR_NO_MEMORY;
    goto exec_error;
  }

  p_job_args->p_comp = p_comp;
  uint32_t current_job_id = 0;
  img_thread_job_params_t fddspc_eng_get_output_job;
  fddspc_eng_get_output_job.args = p_job_args;
  fddspc_eng_get_output_job.client_id =
    get_faceproc_dsp_lib()->client_id;
  fddspc_eng_get_output_job.core_affinity = IMG_CORE_ARM;
  fddspc_eng_get_output_job.dep_job_ids = 0;
  fddspc_eng_get_output_job.dep_job_count = 0;
  fddspc_eng_get_output_job.delete_on_completion = TRUE;
  fddspc_eng_get_output_job.execute =
    faceproc_dsp_comp_eng_get_output_task_exec;
  current_job_id = img_thread_mgr_schedule_job(
     &fddspc_eng_get_output_job);
  if (0 < current_job_id) {
    IDBG_MED("%s:%d] not waiting for scheduled faceproc_dsp_comp"
      "_eng_get_output_task_exec completion ", __func__, __LINE__);
    status = p_job_args->return_value;
  }
  //End:add img_thread API to schedule job.
  IDBG_MED("%s:%d] Exit QWD_FACEPROC_RESULT", __func__, __LINE__);

  return IMG_SUCCESS;

exec_error:
  face_proc_release_frame(p_frame, p_comp);
  return status;
}

/**
 * Function: faceproc_dsp_comp_eng_get_output
 *
 * Description: Get the output from the frameproc engine
 *
 * Input parameters:
 *   p_comp - The pointer to the faceproc engine object
 *   fd_data - Input frame
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_get_output(faceproc_dsp_comp_t *p_comp,
  faceproc_result_t *fd_data)
{
  INT32 num_faces;
  int status = IMG_SUCCESS;
  IDBG_LOW("%s %d, p_comp:%p, p_res:%p",  __func__, __LINE__, p_comp, fd_data);
  switch (p_comp->mode) {
  case FACE_DETECT_BSGC:
  case FACE_DETECT:
  case FACE_DETECT_LITE:
    status = faceproc_fd_output(p_comp, fd_data, &num_faces);
    break;

  default:
    IDBG_ERROR("%s %d: Unsupported mode selected", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  IDBG_LOW("%s, After rc: %d, p_comp:%p, p_res:%p",  __func__,
    status, p_comp, fd_data);
  return status;
}

/**
 * Function: faceproc_dsp_comp_eng_destroy
 *
 * Description: Destroy the faceproc engine
 *
 * Input parameters:
 *   p_comp - The pointer to the faceproc engine object
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_destroy(faceproc_dsp_comp_t *p_comp)
{
  int rc;

  if (!p_comp)
    return IMG_ERR_GENERAL;

  IDBG_MED("%s:%d] faceproc engine clean", __func__, __LINE__);
  rc = faceproc_dsp_comp_eng_reset(p_comp);
  if (rc != IMG_SUCCESS) {
    IDBG_ERROR("%s: faceproc_dsp_comp_eng_reset failed %d", __func__, rc);
  }
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_eng_reset
 *
 * Description: Reset the faceproc engine
 *
 * Input parameters:
 *   p_comp - The pointer to the faceproc engine object
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_reset(faceproc_dsp_comp_t *p_comp)
{
  IDBG_MED("%s:%d]", __func__, __LINE__);
  int ret;

  if (!p_comp || !p_comp->p_lib)
    return IMG_ERR_GENERAL;

  /* Delete Result handle */
  if (p_comp->hresult) {
    ret = p_comp->p_lib->fns.FACEPROC_DeleteDtResult(p_comp->hresult);
    if (ret != FACEPROC_NORMAL)
      return faceproc_dsp_error_to_img_error(ret);
    p_comp->hresult = NULL;
  }
  /* Delete Handle */
  if (p_comp->hdt) {
    ret = p_comp->p_lib->fns.FACEPROC_DeleteDetection(p_comp->hdt);
    if (ret != FACEPROC_NORMAL)
      return faceproc_dsp_error_to_img_error(ret);
    p_comp->hdt = NULL;
  }

  frame_count = 0;
  dump_count = 0;
  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_eng_test_dsp_connection
 *
 * Description: to call test function to find if DSP working
 * well
 *
 * Input parameters:
 *   p_lib - The pointer to the faceproc lib object
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_test_dsp_connection(faceproc_dsp_lib_t *p_lib)
{
  uint8 fd_minor_version, fd_major_version;
  int lib_status_rc;

  IDBG_MED("%s:%d] E", __func__, __LINE__);

  if (!p_lib->ptr_stub) {
    IDBG_ERROR("%s:%d] dsp lib NOT loaded ", __func__, __LINE__);
    return IMG_ERR_GENERAL;
  }

  if (!p_lib->fns.FACEPROC_Dt_VersionDSP) {
    IDBG_ERROR("%s:%d] dsp lib NOT loaded fn ", __func__, __LINE__);
    return IMG_ERR_GENERAL;
  }

  /* Check if FD DSP stub library is requested and valid */
  lib_status_rc = p_lib->fns.FACEPROC_Dt_VersionDSP(
       &fd_minor_version, &fd_major_version);

  if (lib_status_rc != IMG_SUCCESS) {  /*Is DSP stub lib invalid */
    IDBG_ERROR("%s %d],ADSP STUB FD DSP lib error, lib_status_rc=%d ",
      __func__, __LINE__, lib_status_rc);
    return faceproc_dsp_error_to_img_error(lib_status_rc);
  }
  else{
    IDBG_MED("%s:%d]  ADSP STUB exe FACEPROC_Dt_VersionDSP rc %d  ,"
      " minv %d load %d , status %d",__func__, __LINE__,
      lib_status_rc, fd_minor_version, p_lib->load_dsp_lib,
      p_lib->status_dsp_lib);
    return IMG_SUCCESS;
  }
}

//// Below are the Job Execute functions.    //////

/**
 * Function: faceproc_dsp_comp_eng_config_task_exec
 *
 * Description: executes the config function as a job in
 * loadbalancer/threadmgr. eng_config has actual implementation.
 *
 * Input parameters:
 *   param - The pointer to function/usecase specific params
 *   structure
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_config_task_exec(void *param)
{
  if (NULL == param) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  faceproc_dsp_comp_struct * pstruct = (faceproc_dsp_comp_struct *)param;
  IDBG_LOW("%s %d]", __func__, __LINE__);
  pstruct->return_value = faceproc_dsp_comp_eng_config(pstruct->p_comp);
  return IMG_SUCCESS;

}

/**
 * Function: faceproc_dsp_comp_eng_exec_task_exec
 *
 * Description: executes the eng_execute function as a job in
 * loadbalancer/threadmgr. eng_execute has actual
 * implementation.
 *
 * Input parameters:
 *   param - The pointer to function/usecase specific params
 *   structure
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_exec_task_exec(void *param)
{
  if (NULL == param) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  faceproc_dsp_comp_exec_struct * pstruct =
    (faceproc_dsp_comp_exec_struct *)param;
  IDBG_LOW("%s %d]", __func__, __LINE__);

  pstruct->p_comp->processing = TRUE;

  pstruct->return_value = faceproc_dsp_comp_eng_exec(
    pstruct->p_comp , pstruct->p_frame);
  pstruct->p_comp->processing = FALSE;

  // If the error is Connection Lost, send an event to Module.
  if (pstruct->return_value == IMG_ERR_CONNECTION_FAILED) {
    g_faceproc_dsp_lib.restore_needed_flag = TRUE;
    img_dsp_dl_mgr_set_reload_needed(TRUE);
    IMG_SEND_EVENT(&(pstruct->p_comp->b), QIMG_EVT_COMP_CONNECTION_FAILED);
  }

  free(pstruct);

  return IMG_SUCCESS;

}

/**
 * Function: faceproc_dsp_comp_get_result_and_send
 *
 * Description: executes the facial parts detection,
 * sending fd_result to faceproc module
 * sending of BUF_DONE event upstream
 * as a job in loadbalancer/threadmgr on ARM thread so that DSP
 * thread is not blocked.
 *
 * Input parameters:
 *   p_comp - The pointer to fddsp component structure
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_get_result_and_send(faceproc_dsp_comp_t *p_comp)
{
  int32_t status = IMG_SUCCESS;
  uint32_t i = 0;
  faceproc_internal_queue_struct *p_node;
  img_frame_t *p_frame;
  faceproc_result_t *p_fd_result;
  img_component_t *p_base;

  if (NULL == p_comp) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  p_base = &(p_comp->b);

  p_node = img_q_dequeue(&p_comp->intermediate_in_use_Q);
  if (!p_node) {
    IDBG_ERROR("%s:%d] dequeue error %d", __func__, __LINE__, status);
    return IMG_ERR_GENERAL;
  }

  p_fd_result = &(p_node->inter_result);
  p_frame = p_node->p_frame;

  for (i = 0; i < p_fd_result->num_faces_detected; i++) {
    /* Detect facial parts */
    if (p_comp->p_lib->facial_parts_hndl) {
      status = facial_parts_wrap_process_result(
        p_comp->p_lib->facial_parts_hndl,
        p_frame,
        &(p_fd_result->roi[i]),
        0,
        0,
        NULL
        );
      if (status == IMG_ERR_NOT_FOUND) {
        IDBG_ERROR("%s %d][FD_FALSE_POS_DBG] Filter face %d",
          __func__, __LINE__, i);
        continue;
      }
    }
  }

  pthread_mutex_lock(&p_comp->result_mutex);
  p_comp->inter_result = *p_fd_result;
  pthread_mutex_unlock(&p_comp->result_mutex);

  memset(p_node, 0x0, sizeof(*p_node));
  status = img_q_enqueue(&p_comp->intermediate_free_Q, p_node);
  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] enqueue error %d", __func__, __LINE__, status);
    face_proc_release_frame(p_frame, p_comp);
    return IMG_ERR_GENERAL;
  }

  status = img_q_enqueue(&p_base->outputQ, p_frame);
  if (IMG_ERROR(status)) {
    IDBG_ERROR("%s:%d] enqueue error %d", __func__, __LINE__, status);
    IMG_SEND_EVENT(p_base, QIMG_EVT_ERROR);
    return IMG_ERR_GENERAL;
  }

  if ((IMG_SUCCEEDED(status)) &&
    !(IMG_CHK_ABORT_LOCKED(p_base, &p_base->mutex))) {
    p_comp->client_id = p_frame->info.client_id;
    IDBG_MED("%s:%d] Sending QIMG_EVT_FACE_PROC ", __func__, __LINE__);
    IMG_SEND_EVENT(p_base, QIMG_EVT_FACE_PROC);
    IDBG_MED("%s:%d] after  QIMG_EVT_FACE_PROC ", __func__, __LINE__);
  }

  IMG_SEND_EVENT(p_base, QIMG_EVT_BUF_DONE);

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_dsp_comp_eng_get_output_task_exec
 *
 * Description: executes the sending of BUF_DONE event upstream
 * as a job in loadbalancer/threadmgr on ARM thread so that DSP
 * thread is not blocked.
 *
 * Input parameters:
 *   param - The pointer to function/usecase specific params
 *   structure
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_get_output_task_exec(void *param)
{
  if (NULL == param) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  faceproc_dsp_comp_eng_get_output_struct * pstruct =
    (faceproc_dsp_comp_eng_get_output_struct *)param;
  IDBG_LOW("%s %d]", __func__, __LINE__);

  faceproc_dsp_comp_get_result_and_send(pstruct->p_comp);
  pstruct->return_value = IMG_SUCCESS;
  free(pstruct);
  return IMG_SUCCESS;

}

/**
 * Function: faceproc_dsp_comp_eng_destroy_task_exec
 *
 * Description: executes the eng_destroy function as a job in
 * loadbalancer/threadmgr. eng_destroy has actual
 * implementation.
 *
 * Input parameters:
 *   param - The pointer to function/usecase specific params
 *   structure
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_destroy_task_exec(void *param)
{
  if (NULL == param) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  faceproc_dsp_comp_struct * pstruct = (faceproc_dsp_comp_struct *)param;
  IDBG_LOW("%s %d]", __func__, __LINE__);
  pstruct->return_value = faceproc_dsp_comp_eng_destroy(pstruct->p_comp);
  return IMG_SUCCESS;

}

/**
 * Function: faceproc_dsp_comp_eng_load_task_exec
 *
 * Description: executes the eng_load function as a job in
 * loadbalancer/threadmgr. eng_load has actual
 * implementation.
 *
 * Input parameters:
 *   param - The pointer to function/usecase specific params
 *   structure
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_load_task_exec(void *param)
{
  if (NULL == param) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  faceproc_dsp_comp_eng_load_struct * pstruct =
    (faceproc_dsp_comp_eng_load_struct *)param;
  IDBG_LOW("%s %d]", __func__, __LINE__);
  pstruct->return_value = faceproc_dsp_comp_eng_load(pstruct->p_lib);
  return IMG_SUCCESS;

}

/**
 * Function: faceproc_dsp_comp_eng_unload_task_exec
 *
 * Description: executes the eng_unload function as a job in
 * loadbalancer/threadmgr. eng_unload has actual implementation.
 *
 * Input parameters:
 *   param - The pointer to function/usecase specific params
 *   structure
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int faceproc_dsp_comp_eng_unload_task_exec(void *param)
{
  if (NULL == param) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  faceproc_dsp_comp_eng_load_struct * pstruct =
    (faceproc_dsp_comp_eng_load_struct *)param;
  IDBG_LOW("%s %d]",  __func__, __LINE__);
  pstruct->return_value = IMG_SUCCESS;
  faceproc_dsp_comp_eng_unload(pstruct->p_lib);
  return IMG_SUCCESS;

}

/**
 * Function: faceproc_dsp_comp_eng_load_task_exec
 *
 * Description: executes the eng_test_dsp_connection function as
 * a job in loadbalancer/threadmgr. eng_test_dsp_connection has
 * actual implementation for calling DSP rpc call.
 *
 * Input parameters:
 *   param - The pointer to function/usecase specific params
 *   structure
 *
 * Return values:
 *     IMG_SUCCESS
 *     IMG_ERR_GENERAL
 *
 * Notes: noneg
 **/
int faceproc_dsp_comp_eng_test_dsp_connection_task_exec(void *param)
{
  if (NULL == param) {
    IDBG_ERROR("%s:%d] Invalid input", __func__, __LINE__);
    return IMG_ERR_INVALID_INPUT;
  }
  faceproc_dsp_comp_eng_load_struct * pstruct =
    (faceproc_dsp_comp_eng_load_struct *)param;
  IDBG_LOW("%s %d]",  __func__, __LINE__);
  pstruct->return_value = faceproc_dsp_comp_eng_test_dsp_connection(
     pstruct->p_lib);
  IDBG_MED("%s:%d] input pstruct->return_value %d", __func__, __LINE__ ,
    pstruct->return_value);
  return IMG_SUCCESS;

}

