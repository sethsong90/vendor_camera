/**********************************************************************
*  Copyright (c) 2014-2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/
#include "module_imgbase.h"

/**
 *  Static functions
 **/
static int32_t module_llvd_client_created(imgbase_client_t *p_client);
static int32_t module_llvd_client_destroy(imgbase_client_t *p_client);
static int32_t module_llvd_client_process_done(imgbase_client_t *p_client,
  img_frame_t *p_frame);
static int32_t module_llvd_client_streamon(imgbase_client_t * p_client);
static int32_t module_llvd_client_streamoff(imgbase_client_t * p_client);
static int32_t module_llvd_client_handle_ssr(imgbase_client_t *p_client);
static int32_t module_llvd_session_start(void *p_imgbasemod,
  uint32_t sessionid);
static int32_t module_llvd_update_meta(imgbase_client_t *p_client,
  img_meta_t *p_meta);

/** g_cfg:
 *
 *  Set the tuning parameters for seemore (LLVD) module.
*/
static img_seemore_cfg_t g_cfg = {
  .br_intensity = 1.0f,
  .br_color = 0.6f,
  .enable_LTM = 1,
  .enable_TNR = 1,
};

/** g_caps:
 *
 *  Set the capabilities for SeeMore (LLVD) module
*/
static img_caps_t g_caps = {
  .num_input = 2,
  .num_output = 0,
  .num_meta = 1,
  .inplace_algo = 1,
  .num_release_buf = 0,
  .num_overlap = 1,
};

/** g_params:
 *
 *  imgbase parameters
 **/
static module_imgbase_params_t g_params = {
  .imgbase_client_created = module_llvd_client_created,
  .imgbase_client_destroy = module_llvd_client_destroy,
  .imgbase_client_streamon = module_llvd_client_streamon,
  .imgbase_client_streamoff = module_llvd_client_streamoff,
  .imgbase_client_process_done = module_llvd_client_process_done,
  .imgbase_client_handle_ssr = module_llvd_client_handle_ssr,
  .imgbase_session_start = module_llvd_session_start,
  .imgbase_client_update_meta = module_llvd_update_meta,
};


/** img_llvd_session_data_t:
*
*   @disable_preview: disable llvd for preview
*
*   Session based parameters for llvd module
*/
typedef struct {
  bool disable_preview;
} img_llvd_session_data_t;


/** img_llvd_client_t:
*
*   @p_session_data: pointer to the session based data
*   @p_client: imgbase client
*
*   llvd client private structure
*/
typedef struct {
  img_llvd_session_data_t *p_session_data;
  imgbase_client_t *p_client;
} img_llvd_client_t;


/** img_llvdmod_priv_t:
*
*  @session_data: llvdmod session data
*
*  llvdmodule private structure
*/
typedef struct {
  img_llvd_session_data_t session_data[MAX_IMGLIB_SESSIONS];
} img_llvdmod_priv_t;

/**
 * Function: module_llvd_client_created
 *
 * Description: function called after client creation
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int32_t module_llvd_client_created(imgbase_client_t *p_client)
{
  IDBG_MED("%s:%d: E", __func__, __LINE__);
  bool disable_preview = 0;
  img_llvd_client_t *p_llvd_client;
  module_imgbase_t *p_mod =
    (module_imgbase_t *)p_client->p_mod;
  img_llvdmod_priv_t *p_llvdmod_priv =
    (img_llvdmod_priv_t *)p_mod->mod_private;

  p_client->rate_control = TRUE;
  p_client->exp_frame_delay = 0LL;
  p_client->ion_fd = open("/dev/ion", O_RDONLY);
  p_client->before_cpp = TRUE;
  p_client->feature_mask = CAM_QCOM_FEATURE_LLVD;

  /* alloc llvd client private data */
  p_llvd_client = calloc(1, sizeof(img_llvd_client_t));
  if (!p_llvd_client) {
    IDBG_ERROR("%s:%d] llvd client data alloc failed", __func__, __LINE__);
    return IMG_ERR_NO_MEMORY;
  }

  /* update llvd client priv data */
  p_llvd_client->p_session_data =
    &p_llvdmod_priv->session_data[p_client->session_id - 1];
  p_client->p_private_data = p_llvd_client;
  p_llvd_client->p_client = p_client;

  disable_preview =
    p_llvdmod_priv->session_data[p_client->session_id - 1].disable_preview;

  /* process seemore only when bufs received on port where
    video stream is mapped */
  p_client->streams_to_process = 1 << CAM_STREAM_TYPE_VIDEO;
  if (!disable_preview) {
    p_client->streams_to_process |= 1 << CAM_STREAM_TYPE_PREVIEW;
  }

  IDBG_HIGH("%s:%d: disable_preview %d streams_to_process %x",
    __func__, __LINE__, disable_preview, p_client->streams_to_process);

  /* process seemore on all frames regardless of if HAL3 has a
    request on it */
  p_client->process_all_frames = TRUE;

  /* send preferred stream mapping requesting for preview and
    video on the same port */
  p_client->set_preferred_mapping = TRUE;
  p_client->preferred_mapping_single.stream_num = 1;
  p_client->preferred_mapping_single.streams[0].max_streams_num = 2;
  p_client->preferred_mapping_single.streams[0].stream_mask =
    (1 << CAM_STREAM_TYPE_PREVIEW) | (1 << CAM_STREAM_TYPE_VIDEO);
  p_client->preferred_mapping_multi.stream_num = 1;
  p_client->preferred_mapping_multi.streams[0].max_streams_num = 2;
  p_client->preferred_mapping_multi.streams[0].stream_mask =
    (1 << CAM_STREAM_TYPE_VIDEO) | (1 << CAM_STREAM_TYPE_PREVIEW);

  /* overlap batch input requires extra buffers */
  p_client->isp_extra_native_buf += p_mod->caps.num_overlap;

  return IMG_SUCCESS;
}

/**
 * Function: module_llvd_session_start
 *
 * Description: function called after session start
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
 int32_t module_llvd_session_start(void *p_imgbasemod, uint32_t sessionid)
{
  module_imgbase_t *p_mod = (module_imgbase_t *)p_imgbasemod;
  img_llvdmod_priv_t *p_llvdmod_priv;
  img_llvd_session_data_t *p_session_data;

  IDBG_MED("%s:%d] E", __func__, __LINE__);

  if (!p_mod || sessionid > MAX_IMGLIB_SESSIONS) {
    IDBG_ERROR("%s:%d: Invalid input, %p %d", __func__, __LINE__,
      p_mod, sessionid);
    return IMG_ERR_INVALID_INPUT;
  }
  p_llvdmod_priv = (img_llvdmod_priv_t *)p_mod->mod_private;

  if (!p_llvdmod_priv) {
    IDBG_ERROR("%s:%d: Invalid llvd private data, %p", __func__, __LINE__,
      p_llvdmod_priv);
    return IMG_ERR_INVALID_INPUT;
  }
  p_session_data = &p_llvdmod_priv->session_data[sessionid - 1];
  p_session_data->disable_preview =  module_imglib_common_get_prop(
    "camera.llvd.preview.disable", "0");

  IDBG_HIGH("%s:%d] sessionid %d disable_preview %d", __func__, __LINE__,
    sessionid, p_session_data->disable_preview);

  IDBG_MED("%s:%d] X", __func__, __LINE__);

  return IMG_SUCCESS;
}

/**
 * Function: module_llvd_client_streamon
 *
 * Description: function called after stream on
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
static int32_t module_llvd_client_streamon(imgbase_client_t * p_client)
{
  module_imgbase_t *p_mod =
    (module_imgbase_t *)p_client->p_mod;
  img_llvdmod_priv_t *p_llvdmod_priv =
    (img_llvdmod_priv_t *)p_mod->mod_private;
  bool disable_preview =
    p_llvdmod_priv->session_data[p_client->session_id - 1].disable_preview;

  p_client->streams_to_process = 0;
  if (CAM_HAL_V1 == p_mod->hal_version) {
    p_client->streams_to_process = 1 << CAM_STREAM_TYPE_VIDEO;
    if (!disable_preview) {
      p_client->streams_to_process |= 1 << CAM_STREAM_TYPE_PREVIEW;
    }
  }

  return IMG_SUCCESS;
}

/**
 * Function: module_llvd_client_streamoff
 *
 * Description: function called after streamoff
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
static int32_t module_llvd_client_streamoff(imgbase_client_t * p_client)
{
  /* In the event of a SSR, reload the library */
  img_dsp_dl_requestall_to_close_and_reopen();

  return IMG_SUCCESS;
}

/**
 * Function: module_llvd_client_destroy
 *
 * Description: function called before client is destroyed
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int32_t module_llvd_client_destroy(imgbase_client_t *p_client)
{
  IDBG_MED("%s:%d: E", __func__, __LINE__);

  /* free llvd client data */
  if (p_client->p_private_data) {
    free(p_client->p_private_data);
    p_client->p_private_data = NULL;
  }

  if (p_client->ion_fd >= 0) {
    close(p_client->ion_fd);
    p_client->ion_fd = -1;
  }
  return IMG_SUCCESS;
}

/**
 * Function: module_llvd_client_process_done
 *
 * Description: function called after frame is processed
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *   @p_frame: output frame
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int32_t module_llvd_client_process_done(imgbase_client_t *p_client,
  img_frame_t *p_frame)
{
  void *v_addr = IMG_ADDR(p_frame);
  int32_t fd = IMG_FD(p_frame);
  int32_t buffer_size = IMG_FRAME_LEN(p_frame);
  IDBG_MED("%s:%d] addr %p fd %d size %d ion %d", __func__, __LINE__,
    v_addr, fd, buffer_size, p_client->ion_fd);
  int rc = img_cache_ops_external(v_addr, buffer_size, 0, fd,
    CACHE_CLEAN_INVALIDATE, p_client->ion_fd);
  return rc;
}

/**
 * Function: module_llvd_client_handle_ssr
 *
 * Description: function called after DSP SSR occured
 *
 * Arguments:
 *   @p_client - IMGLIB_BASE client
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int32_t module_llvd_client_handle_ssr(imgbase_client_t *p_client)
{
  IDBG_MED("%s:%d] E", __func__, __LINE__);

  /* In the event of a SSR, SeeMore uses deferred reload. Set
    reload needed to true here and begin reloading during streamoff */
  img_dsp_dl_mgr_set_reload_needed(TRUE);

  return IMG_SUCCESS;
}

/**
 * Function: module_llvd_update_meta
 *
 * Description: This function is used to called when the base
 *                       module updates the metadata
 *
 * Arguments:
 *   @p_client - pointer to imgbase client
 *   @p_meta: pointer to the image meta
 *
 * Return values:
 *     error values
 *
 * Notes: none
 **/
int32_t module_llvd_update_meta(imgbase_client_t *p_client,
  img_meta_t *p_meta)
{
  int rc;
  if (!p_client || !p_meta) {
    IDBG_ERROR("%s:%d] invalid input %p %p", __func__, __LINE__,
      p_client, p_meta);
    rc = IMG_ERR_INVALID_INPUT;
    goto error;
  }

  rc = img_set_meta(p_meta, IMG_META_SEEMORE_CFG, &g_cfg);
  if (rc != IMG_SUCCESS) {
    IDBG_ERROR("%s:%d] Error rc %d", __func__, __LINE__, rc);
    goto error;
  }

  IDBG_HIGH("%s:%d] Success ", __func__, __LINE__);
  return IMG_SUCCESS;

error:
  return rc;
}
/**
 * Function: module_llvd_deinit
 *
 * Description: Function used to deinit SeeMore module
 *
 * Arguments:
 *   p_mct_mod - MCTL module instance pointer
 *
 * Return values:
 *     none
 *
 * Notes: none
 **/
void module_llvd_deinit(mct_module_t *p_mct_mod)
{
  module_imgbase_t *p_mod;
  img_llvdmod_priv_t *p_llvdmod_priv;

  if(!p_mct_mod) {
    IDBG_ERROR("%s:%d] Invalid module %p", __func__, __LINE__, p_mct_mod);
    return;
  }

  p_mod = (module_imgbase_t *)p_mct_mod->module_private;
  if (!p_mod) {
    IDBG_ERROR("%s:%d] Invalid base module %p", __func__, __LINE__, p_mod);
    return;
  }
  p_llvdmod_priv = p_mod->mod_private;
  //free llvd mod priv data
  if (p_llvdmod_priv) {
    free(p_llvdmod_priv);
  }

  module_imgbase_deinit(p_mct_mod);
}

/** module_llvd_init:
 *
 *  Arguments:
 *  @name - name of the module
 *
 * Description: Function used to initialize the SeeMore module
 *
 * Return values:
 *     MCTL module instance pointer
 *
 * Notes: none
 **/
mct_module_t *module_llvd_init(const char *name)
{
  img_llvdmod_priv_t *p_llvdmod_priv = calloc(1, sizeof(img_llvdmod_priv_t));
  if (!p_llvdmod_priv) {
    IDBG_ERROR("%s:%d] llvd private data alloc failed!!!", __func__, __LINE__);
    return NULL;
  }

  return module_imgbase_init(name,
    IMG_COMP_GEN_FRAME_PROC,
    "qcom.gen_frameproc",
    p_llvdmod_priv,
    &g_caps,
    "libmmcamera_llvd.so",
    CAM_QCOM_FEATURE_LLVD,
    &g_params);
}

/** module_llvd_set_parent:
 *
 *  Arguments:
 *  @p_parent - parent module pointer
 *
 * Description: This function is used to set the parent pointer
 *              of the LLVD (SeeMore) module
 *
 * Return values:
 *     none
 *
 * Notes: none
 **/
void module_llvd_set_parent(mct_module_t *p_mct_mod, mct_module_t *p_parent)
{
  return module_imgbase_set_parent(p_mct_mod, p_parent);
}

/**
 * Function: module_sw_tnr_deinit
 *
 * Description: Function used to deinit SeeMore module
 *
 * Arguments:
 *   p_mct_mod - MCTL module instance pointer
 *
 * Return values:
 *     none
 *
 * Notes: none
 **/
void module_sw_tnr_deinit(mct_module_t *p_mct_mod)
{
  module_llvd_deinit(p_mct_mod);
}

/** module_sw_tnr_init:
 *
 *  Arguments:
 *  @name - name of the module
 *
 * Description: Function used to initialize the SW TNR module
 *
 * Return values:
 *     MCTL module instance pointer
 *
 * Notes: none
 **/
mct_module_t *module_sw_tnr_init(const char *name)
{
  img_llvdmod_priv_t *p_swtnrmod_priv = calloc(1, sizeof(img_llvdmod_priv_t));
  if (!p_swtnrmod_priv) {
    IDBG_ERROR("%s:%d] swtnr private data alloc failed!!", __func__, __LINE__);
    return NULL;
  }

  return module_imgbase_init(name,
    IMG_COMP_GEN_FRAME_PROC,
    "qcom.gen_frameproc",
    p_swtnrmod_priv,
    &g_caps,
    "libmmcamera_sw_tnr.so",
    CAM_QTI_FEATURE_SW_TNR,
    &g_params);
}

/** module_sw_tnr_set_parent:
 *
 *  Arguments:
 *  @p_parent - parent module pointer
 *
 * Description: This function is used to set the parent pointer
 *              of the SW TNR module
 *
 * Return values:
 *     none
 *
 * Notes: none
 **/
void module_sw_tnr_set_parent(mct_module_t *p_mct_mod, mct_module_t *p_parent)
{
  return module_llvd_set_parent(p_mct_mod, p_parent);
}
