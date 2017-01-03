/* aec_biz.c
*
* Copyright (c) 2014-2016 Qualcomm Technologies, Inc. All Rights Reserved.
* Qualcomm Technologies Proprietary and Confidential.
*/


#include "aec_biz.h"

static char *aec_biz_get_param_strings[AEC_GET_PARAM_MAX+1] = {
  AEC_GET_PARAM_ENUM_LIST(AEC_PARAM_GENERATE_STRING)
};
static char *aec_biz_set_param_strings[AEC_SET_PARAM_MAX+1] = {
  AEC_SET_PARAM_ENUM_LIST(AEC_PARAM_GENERATE_STRING)
};

/** aec_biz_get_param_string
 *    @eventId: aec get param
 *
 *  Return char *
 **/
inline static char * aec_biz_get_param_string(int eventId)
{
  return aec_biz_get_param_strings[eventId < AEC_GET_PARAM_MAX ?
    eventId : AEC_GET_PARAM_MAX];
}

/** aec_biz_set_param_string
 *    @eventId: aec set param
 *
 *  Return char *
 **/
inline static char * aec_biz_set_param_string(int eventId)
{
  return aec_biz_set_param_strings[eventId < AEC_SET_PARAM_MAX ?
    eventId : AEC_SET_PARAM_MAX];
}

static boolean aec_add_core_entry(int entry_type, uint32 entry_size,
  const void* entry_value, aec_core_input_info_type* input_info)
{
  if (entry_type >= AEC_INPUT_PARAM_MAX ||
    entry_size > sizeof(aec_input_payload_type)) {
    AEC_ERR("invalid input: entry_type: %d, entry_size: %d",
      entry_type, entry_size);
    return FALSE;
  }

  PARAM_POSITION_LINK_RESET(input_info, entry_type);
  memcpy(POINTER_OF(entry_type, input_info), entry_value, entry_size);

  return TRUE;
}

boolean aec_biz_translate_coord_fov2camif(aec_biz_t *aec,
  uint16_t *x, uint16_t *y)
{
  int  roi_x, roi_y;
  if(aec->stream_crop_info.vfe_out_width == 0 ||
    aec->stream_crop_info.vfe_out_height == 0)
    return FALSE;
  if(aec->preview_width == 0 || aec->preview_height == 0)
  {
    aec->preview_width = aec->stream_crop_info.pp_crop_out_x + 1;
    aec->preview_height = aec->stream_crop_info.pp_crop_out_y + 1;
  }
  AEC_LOW("aec->preview_width=%d, aec->preview_height=%d,\
    aec->stream_crop_info.vfe_out_width=%d,aec->stream_crop_info.vfe_out_height=%d",
    aec->preview_width,aec->preview_height,
    aec->stream_crop_info.vfe_out_width, aec->stream_crop_info.vfe_out_height);
  if (! aec->stream_crop_info.pp_crop_out_x ||
    !aec->stream_crop_info.pp_crop_out_y) {
   aec->stream_crop_info.pp_x = 0;
   aec->stream_crop_info.pp_y = 0;
   aec->stream_crop_info.pp_crop_out_x = aec->preview_width;
   aec->stream_crop_info.pp_crop_out_y = aec->preview_height;
  }

  /* Reverse calculation to cpp output */
  roi_x = *x  * aec->stream_crop_info.pp_crop_out_x /aec->preview_width ;
  roi_y = *y *  aec->stream_crop_info.pp_crop_out_y/ aec->preview_height;
  roi_x += aec->stream_crop_info.pp_x;
  roi_y += aec->stream_crop_info.pp_y;
  /* Reverse calculation for vfe output */
  roi_x = roi_x * aec->stream_crop_info.vfe_map_width /  aec->stream_crop_info.vfe_out_width ;
  roi_y = roi_y *  aec->stream_crop_info.vfe_map_height /  aec->stream_crop_info.vfe_out_height;
  roi_x += aec->stream_crop_info.vfe_map_x;
  roi_y += aec->stream_crop_info.vfe_map_y;

  AEC_LOW("coor orig (%d,%d), vfe (%d, %d,%d %d ) pp(%d %d %d %d) after (%d, %d) prev(%d %d)",
    *x, *y, aec->stream_crop_info.vfe_map_x, aec->stream_crop_info.vfe_map_y,
    aec->stream_crop_info.vfe_map_width,aec->stream_crop_info.vfe_map_height,
    aec->stream_crop_info.pp_x, aec->stream_crop_info.pp_y,
    aec->stream_crop_info.pp_crop_out_x, aec->stream_crop_info.pp_crop_out_y,
    roi_x, roi_y, aec->preview_width, aec->preview_height);

  *x = roi_x;
  *y = roi_y;
  return TRUE;
}

boolean aec_biz_translate_coord_camif2fov(aec_biz_t *aec,
  uint16_t *roi_x, uint16_t *roi_y)
{
  int  x, y;

  x = *roi_x;
  y = *roi_y;

  if (aec->stream_crop_info.vfe_out_width == 0 ||
    aec->stream_crop_info.vfe_out_height == 0)
    return FALSE;

  if (aec->preview_width == 0 || aec->preview_height == 0) {
    aec->preview_width = aec->stream_crop_info.pp_crop_out_x + 1;
    aec->preview_height = aec->stream_crop_info.pp_crop_out_y + 1;
    AEC_ERR("preview size invalid. Set it to crop info. preview x,y: (%d, %d)",
      aec->preview_width, aec->preview_height);
  }
  AEC_LOW("aec->preview_width=%d, aec->preview_height=%d,\
    aec->stream_crop_info.vfe_out_width=%d,aec->stream_crop_info.vfe_out_height=%d",
    aec->preview_width,aec->preview_height,
    aec->stream_crop_info.vfe_out_width, aec->stream_crop_info.vfe_out_height);
  if (!aec->stream_crop_info.pp_crop_out_x ||
    !aec->stream_crop_info.pp_crop_out_y) {
    aec->stream_crop_info.pp_x = 0;
    aec->stream_crop_info.pp_y = 0;
    aec->stream_crop_info.pp_crop_out_x = aec->preview_width;
    aec->stream_crop_info.pp_crop_out_y = aec->preview_height;
    AEC_ERR("crop info invalid. Set it to preview size. crop_out x,y: (%d, %d)",
      aec->stream_crop_info.pp_crop_out_x, aec->stream_crop_info.pp_crop_out_y);
  }

  x -= aec->stream_crop_info.vfe_map_x;
  y -= aec->stream_crop_info.vfe_map_y;

  if (x < 0) {
    AEC_ERR("ROI x invalid %d", x);
    x = 0;
  }
  if (y < 0) {
    AEC_ERR("ROI y invalid %d", y);
    y = 0;
  }

  x = x * aec->stream_crop_info.vfe_out_width / aec->stream_crop_info.vfe_map_width;
  y = y * aec->stream_crop_info.vfe_out_height / aec->stream_crop_info.vfe_map_height;

  x -= aec->stream_crop_info.pp_x;
  y -= aec->stream_crop_info.pp_y;

  if (x < 0) {
    AEC_ERR("ROI x invalid %d", x);
    x = 0;
  }
  if (y < 0) {
    AEC_ERR("ROI y invalid %d", y);
    y = 0;
  }
  x = x * aec->preview_width / aec->stream_crop_info.pp_crop_out_x;
  y = y * aec->preview_height / aec->stream_crop_info.pp_crop_out_y;

  *roi_x = x;
  *roi_y = y;
  return TRUE;
}

/** aec_process_translate_dim_fov2camif
 *
 **/
boolean aec_biz_translate_dim_fov2camif(aec_biz_t *aec,
  uint16_t *dx, uint16_t *dy)
{
  int  roi_dx, roi_dy;
  if(aec->stream_crop_info.vfe_out_width == 0 ||
    aec->stream_crop_info.vfe_out_height == 0)
    return FALSE;
  if (! aec->stream_crop_info.pp_crop_out_x ||
   !aec->stream_crop_info.pp_crop_out_y) {
   aec->stream_crop_info.pp_x = 0;
   aec->stream_crop_info.pp_y = 0;
   aec->stream_crop_info.pp_crop_out_x = aec->preview_width;
   aec->stream_crop_info.pp_crop_out_y = aec->preview_height;
  }

  /* Reverse calculation to cpp output */
  roi_dx = *dx  * aec->stream_crop_info.pp_crop_out_x /aec->preview_width ;
  roi_dy = *dy *  aec->stream_crop_info.pp_crop_out_y/ aec->preview_height;
  /* Reverse calculation for vfe output */
  roi_dx = roi_dx * aec->stream_crop_info.vfe_map_width /  aec->stream_crop_info.vfe_out_width ;
  roi_dy = roi_dy *  aec->stream_crop_info.vfe_map_height /  aec->stream_crop_info.vfe_out_height;
  AEC_LOW("orig (%d,%d), vfe (%d, %d,%d %d ) vfe out(%d %d) pp(%d %d %d %d) after (%d, %d)",
    *dx, *dy, aec->stream_crop_info.vfe_map_x, aec->stream_crop_info.vfe_map_y,
    aec->stream_crop_info.vfe_map_width,aec->stream_crop_info.vfe_map_height,
    aec->stream_crop_info.vfe_out_width, aec->stream_crop_info.vfe_out_height,
    aec->stream_crop_info.pp_x, aec->stream_crop_info.pp_y,
    aec->stream_crop_info.pp_crop_out_x, aec->stream_crop_info.pp_crop_out_y,
    roi_dx, roi_dy);
  *dx = roi_dx;
  *dy = roi_dy;
  return TRUE;
}

/** aec_process_translate_dim_camif2fov
 *
 **/
boolean aec_biz_translate_dim_camif2fov(aec_biz_t *aec,
  uint16_t *dx, uint16_t *dy)
{
  int  roi_dx, roi_dy;
  if (aec->stream_crop_info.vfe_out_width == 0 || aec->stream_crop_info.vfe_out_height == 0)
    return FALSE;

  if (! aec->stream_crop_info.pp_crop_out_x ||
   !aec->stream_crop_info.pp_crop_out_y) {
   aec->stream_crop_info.pp_x = 0;
   aec->stream_crop_info.pp_y = 0;
   aec->stream_crop_info.pp_crop_out_x = aec->preview_width;
   aec->stream_crop_info.pp_crop_out_y = aec->preview_height;
  }

  /* VFE to CPP */
  roi_dx = *dx * aec->stream_crop_info.vfe_out_width / aec->stream_crop_info.vfe_map_width;
  roi_dy = *dy * aec->stream_crop_info.vfe_out_height / aec->stream_crop_info.vfe_map_height;

  /* CPP to Preview */
  roi_dx = roi_dx * aec->preview_width / aec->stream_crop_info.pp_crop_out_x;
  roi_dy = roi_dy * aec->preview_height / aec->stream_crop_info.pp_crop_out_y;

  AEC_LOW("ddl orig (%d,%d), vfe (%d, %d,%d %d ) vfe out(%d %d) pp(%d %d %d %d) after (%d, %d)",
    *dx, *dy, aec->stream_crop_info.vfe_map_x, aec->stream_crop_info.vfe_map_y,
    aec->stream_crop_info.vfe_map_width,aec->stream_crop_info.vfe_map_height,
    aec->stream_crop_info.vfe_out_width, aec->stream_crop_info.vfe_out_height,
    aec->stream_crop_info.pp_x, aec->stream_crop_info.pp_y,
    aec->stream_crop_info.pp_crop_out_x, aec->stream_crop_info.pp_crop_out_y,
    roi_dx, roi_dy);

  *dx = roi_dx;
  *dy = roi_dy;
  return TRUE;
}

/** aec_process_pack_stats_config(output, aec);:
 *
 **/
void aec_biz_pack_stats_config(aec_biz_t *aec,
  aec_output_data_t *output, aec_core_output_type *core_output)
{
  uint16_t top, left, width = 0, height = 0, config_width, config_height;
  uint32_t r_Max, gr_Max, gb_Max, b_Max;
  aec_config_t *config = NULL;
  aec_bg_config_t* bg_config = NULL;
  config = &aec->stats_config;
  top = left = 0;
  if (!aec_biz_translate_coord_fov2camif(aec, &left, &top)){
    AEC_LOW("invalid inputs to translate fov 2 camif ");
  }
  /* add check to to make widht as vfe width */
  if(!aec->stream_crop_info.pp_x && !aec->stream_crop_info.pp_y &&
    !aec->stream_crop_info.pp_crop_out_x && !aec->stream_crop_info.pp_crop_out_y){
    width = aec->stream_crop_info.vfe_out_width;
    height = aec->stream_crop_info.vfe_out_height;
  } else {
    width = aec->preview_width;
    height = aec->preview_height;
  }
  if(!aec_biz_translate_dim_fov2camif(aec,&width, &height)){
    AEC_HIGH("invalid inputs to translate dimensions for  fov 2 camif ");
    width = aec->sensor_info.sensor_res_width;
    height =  aec->sensor_info.sensor_res_height;
  }

  int32 bit_shift = 0;
  if (aec->stats_depth > AEC_BG_STATS_CONSUMP_BIT_WIDTH) {
    bit_shift = aec->stats_depth - AEC_BG_STATS_CONSUMP_BIT_WIDTH;
  }

  /* Select BG stats to be configure */
  if (aec->aec_enable_stats_mask & (1 << MSM_ISP_STATS_BG)) {
    bg_config = &config->bg_config;
  } else if (aec->aec_enable_stats_mask & (1 << MSM_ISP_STATS_AEC_BG)) {
    bg_config = &config->aec_bg_config;
  } else if (aec->aec_enable_stats_mask & (1 << MSM_ISP_STATS_HDR_BE) ||
             aec->aec_enable_stats_mask & (1 << MSM_ISP_STATS_BE)) {
    /* these two are of the same underlying type for now */
    bg_config = &config->be_config;
  } else {
    AEC_ERR("Error: invalid config mask: 0x%x", aec->aec_enable_stats_mask);
    return;
  }

  /* Configure selected BG stats */
  bg_config->is_valid = TRUE;
  float bg_thres_divider = MAX(1.0f, core_output->bg_thres_divider);
  if (aec->aec_enable_stats_mask & (1 << MSM_ISP_STATS_BG)     ||
      aec->aec_enable_stats_mask & (1 << MSM_ISP_STATS_HDR_BE) ||
      aec->aec_enable_stats_mask & (1 << MSM_ISP_STATS_BE)) {
    b_Max  = (uint32_t)(((255 - 16) << bit_shift) / bg_thres_divider);
    gb_Max = (uint32_t)(((255 - 16) << bit_shift) / bg_thres_divider);
    gr_Max = (uint32_t)(((255 - 16) << bit_shift) / bg_thres_divider);
    r_Max  = (uint32_t)(((255 - 16) << bit_shift) / bg_thres_divider);
  } else { /* For AEC_BG */
    b_Max  = (uint32_t)(((1 << aec->stats_depth) - 1) / bg_thres_divider);
    gb_Max = (uint32_t)(((1 << aec->stats_depth) - 1) / bg_thres_divider);
    gr_Max = (uint32_t)(((1 << aec->stats_depth) - 1) / bg_thres_divider);
    r_Max  = (uint32_t)(((1 << aec->stats_depth) - 1) / bg_thres_divider);
  }
  bg_config->grid_info.h_num = 64;
  bg_config->grid_info.v_num = 48;

  config_width =
    (left + width > aec->sensor_info.sensor_res_width) ?
    (aec->sensor_info.sensor_res_width - left) : width;
  config_height =
    (top + height > aec->sensor_info.sensor_res_height) ?
    (aec->sensor_info.sensor_res_height - top) : height;

  if (bg_config->roi.left != left ||
      bg_config->roi.top != top ||
      bg_config->roi.width != config_width ||
      bg_config->roi.height != config_height ||
      bg_config->r_Max != r_Max ||
      bg_config->gr_Max != gr_Max ||
      bg_config->gb_Max != gb_Max ||
      bg_config->b_Max != b_Max) {

    /* Config BG */
    bg_config->roi.left   = left;
    bg_config->roi.top    = top;
    bg_config->roi.width  = config_width;
    bg_config->roi.height = config_height;
    bg_config->r_Max      = r_Max;
    bg_config->gr_Max     = gr_Max;
    bg_config->gb_Max     = gb_Max;
    bg_config->b_Max      = b_Max;

    /* Config Bhist*/
    config->bhist_config.roi.left   = left;
    config->bhist_config.roi.top    = top;
    config->bhist_config.roi.width  = config_width;
    config->bhist_config.roi.height = config_height;

    output->need_config = 1;
    memcpy(&output->config, config, sizeof(aec_config_t));
  } else {
    output->need_config = 0;
  }
}

/** aec_biz_map_stats_type_to_algo
 *    @dest_algo_stats: The stats mapped to algo are written here (output)
 *    @src_biz_stats: Stats mask from ISP (input)
 *
 * Map ISP stats to Core stats
 *
 * Return: TRUE in success
 **/
static boolean aec_biz_map_stats_type_to_algo(uint32 *dest_algo_stats,
  uint32 *src_biz_stats)
{
  uint32 algo_dest = 0;
  uint32 isp_stats = *src_biz_stats;

  /* Mapping only stats that AEC may require */
  if (isp_stats & (1 << MSM_ISP_STATS_AEC)) {
    algo_dest |= STATS_AEC; /* Map the stats corresponding to the bit */
  }
  if (isp_stats & (1 << MSM_ISP_STATS_IHIST)) {
    algo_dest |= STATS_IHISTO;
  }
  if (isp_stats & (1 << MSM_ISP_STATS_BG)) {
    algo_dest |= STATS_BG;
  }
  if (isp_stats & (1 << MSM_ISP_STATS_AEC_BG)) {
    algo_dest |= STATS_BG_AEC;
  }
  if (isp_stats & (1 << MSM_ISP_STATS_BE)) {
    algo_dest |= STATS_BE;
  }
  if (isp_stats & (1 << MSM_ISP_STATS_HDR_BHIST)) {
    algo_dest |= STATS_HBHISTO;
  }
  if (isp_stats & (1 << MSM_ISP_STATS_BHIST)) {
    algo_dest |= STATS_BHISTO;
  }
  if (isp_stats & (1 << MSM_ISP_STATS_HDR_BE)) {
    algo_dest |= STATS_HDR_BE;
  }

  *dest_algo_stats = algo_dest;

  AEC_LOW("map stats from 0x%x to 0x%x", *src_biz_stats, *dest_algo_stats);
  return TRUE;
}

/** aec_biz_map_stats_type_to_biz
 *    @dest_biz_stats: The stats mapped to biz logic ISP (output)
 *    @src_algo_stats: Stats mask from algo (input)
 *
 * Map Core to ISP stats
 *
 * Return: TRUE in success
 **/
static boolean aec_biz_map_stats_type_to_biz(uint32 *dest_biz_stats,
  uint32 *src_algo_stats)
{
  uint32 dest_stats = 0;
  uint32 algo_stats = *src_algo_stats;

  /* STATS_HDR_VID: This is provided by sensor no ISP, there is no need to map
   * Mapping only stats that AEC may require */
  if (algo_stats & STATS_BE) {
    dest_stats |= (1 << MSM_ISP_STATS_BE);
  }
  if (algo_stats & STATS_BG) {
    dest_stats |= (1 << MSM_ISP_STATS_BG);
  }
  if (algo_stats & STATS_BG_AEC) {
    dest_stats |= (1 << MSM_ISP_STATS_AEC_BG);
  }
  if (algo_stats & STATS_HBHISTO) {
    dest_stats |= (1 << MSM_ISP_STATS_HDR_BHIST);
  }
  if (algo_stats & STATS_BHISTO) {
    dest_stats |= (1 << MSM_ISP_STATS_BHIST);
  }
  if (algo_stats & STATS_HDR_BE) {
    dest_stats |= (1 << MSM_ISP_STATS_HDR_BE);
  }
  if (algo_stats & STATS_AEC) {
    dest_stats |= (1 << MSM_ISP_STATS_AEC);
  }
  if (algo_stats & STATS_IHISTO) {
    dest_stats |= (1 << MSM_ISP_STATS_IHIST);
  }

  *dest_biz_stats = dest_stats;

  AEC_LOW("map stats from 0x%x to 0x%x", *src_algo_stats, *dest_biz_stats);
  return TRUE;
}

/** aec_get_parameters:
 *
 **/
static boolean aec_biz_get_param(aec_get_parameter_t *param, void *aec_obj)
{
  q3a_core_result_type rc = Q3A_CORE_RESULT_SUCCESS;
  aec_biz_t *aec = (aec_biz_t *)aec_obj;
  aec_core_get_info_type get_param_info;
  aec_core_get_output_type get_param_output;

  if (!param || !aec) {
   AEC_ERR("Invalid input: %p,%p",param, aec);
    return FALSE;
  }

  if (NULL == aec->aec_algo_ops.get_parameters) {
    AEC_ERR("ops.get_parameters is NULL, can't get: %p,%p", param, aec);
    return FALSE;
  }

  AEC_LOW("AEC_EVENT: %s", aec_biz_get_param_string(param->type));

  switch (param->type) {
  case AEC_GET_PARAM_EXPOSURE_PARAMS: {
    get_param_info.param_type = AEC_GET_EXPOSURE_PARAMS;
    q3a_custom_data_t custom_data =
      param->u.exp_params.custom_param;
    memset(&get_param_output.u.exp_params.custom_param, 0,
      sizeof(q3a_custom_data_t));
    if (custom_data.data != NULL &&
      custom_data.size > 0) {
      get_param_output.u.exp_params.custom_param =
        custom_data;
    }
    rc = aec->aec_algo_ops.get_parameters(
      aec->handle, &get_param_info, &get_param_output);
    if (rc != Q3A_CORE_RESULT_SUCCESS) {
      AEC_ERR("aec get parameter failed");
    return FALSE;
    }
    memcpy(&param->u.exp_params,
      &get_param_output.u.exp_params, sizeof(aec_exp_parms_t));
  } /* case AEC_GET_PARAM_EXPOSURE_PARAMS */
    break;

  case AEC_GET_PARAM_REQUIRED_STATS: {
    get_param_info.param_type = AEC_GET_STATS_REQUIRED;
    rc = aec_biz_map_stats_type_to_algo(&get_param_info.u.supported_stats_mask,
      &param->u.request_stats.supported_stats_mask);
    if (!rc) {
      AEC_ERR("Error to map stats to algo type");
      return FALSE;
    }
    rc = aec->aec_algo_ops.get_parameters(
      aec->handle, &get_param_info, &get_param_output);
    if (rc != Q3A_CORE_RESULT_SUCCESS) {
      AEC_ERR("aec get parameter failed");
      return FALSE;
    }

    rc = aec_biz_map_stats_type_to_biz(&param->u.request_stats.enable_stats_mask,
      &get_param_output.u.enable_stats_mask);
    if (!rc) {
      AEC_ERR("Error to map stats to biz type");
      return FALSE;
    }
    aec->aec_enable_stats_mask = param->u.request_stats.enable_stats_mask;

    AEC_LOW("AEC_GET_PARAM_REQUIRED_STATS: 0x%x",
      param->u.request_stats.enable_stats_mask);

    get_param_info.param_type = AEC_GET_RGN_SKIP_PATTERN;
    rc = aec_biz_map_stats_type_to_algo(&get_param_info.u.supported_rgn_skip_mask,
      &param->u.request_stats.supported_rgn_skip_mask);
    if (!rc) {
      AEC_ERR("Error to map stats to algo type");
      return FALSE;
    }
    rc = aec->aec_algo_ops.get_parameters(
      aec->handle, &get_param_info, &get_param_output);
    if (rc != Q3A_CORE_RESULT_SUCCESS) {
      AEC_ERR("aec get parameter failed");
      return FALSE;
    }
    param->u.request_stats.enable_rgn_skip_pattern =
      get_param_output.u.enable_rgn_skip_pattern;

  }
  break;

  case AEC_GET_PARAM_UNIFIED_FLASH: {
    get_param_info.param_type = AEC_GET_UNIFIED_FLASH;
    get_param_output.u.frame_info.num_batch = param->u.frame_info.num_batch;
    get_param_output.u.frame_info.frame_batch = param->u.frame_info.frame_batch;
    rc = aec->aec_algo_ops.get_parameters(
      aec->handle, &get_param_info, &get_param_output);
    if (rc != Q3A_CORE_RESULT_SUCCESS) {
      AEC_ERR("aec get parameter failed");
      return FALSE;
    }
    param->u.frame_info.luma_target = get_param_output.u.frame_info.luma_target;
    param->u.frame_info.current_luma = get_param_output.u.frame_info.current_luma;
    param->u.frame_info.led_off_real_gain = get_param_output.u.frame_info.led_off_real_gain;
    param->u.frame_info.led_off_sensor_gain = get_param_output.u.frame_info.led_off_sensor_gain;
    param->u.frame_info.led_off_linecount = get_param_output.u.frame_info.led_off_linecount;
    param->u.frame_info.led_off_s_gain = get_param_output.u.frame_info.led_off_s_gain;
    param->u.frame_info.led_off_s_linecount = get_param_output.u.frame_info.led_off_s_linecount;
    param->u.frame_info.led_off_l_gain = get_param_output.u.frame_info.led_off_l_gain;
    param->u.frame_info.led_off_l_linecount = get_param_output.u.frame_info.led_off_l_linecount;
    param->u.frame_info.valid_exp_entries = get_param_output.u.frame_info.valid_exp_entries;
    param->u.frame_info.use_led_estimation = get_param_output.u.frame_info.use_led_estimation;
    param->u.frame_info.metering_type = get_param_output.u.frame_info.metering_type;
    param->u.frame_info.led_off_drc_gains= get_param_output.u.frame_info.led_off_drc_gains;

  } /* case AEC_GET_PARAM_EXPOSURE_PARAMS */
  break;

  default:
    AEC_ERR("invalid param type: %d",param->type);
    return FALSE;
  }

  return TRUE;
}

static boolean aec_biz_map_set_iso(aec_biz_t *aec, int32 iso)
{
  boolean rc = TRUE;
  aec_iso_mode_type iso_mode;
  uint32 iso_value;

  if (iso < AEC_ISO_AUTO)
    iso = AEC_ISO_AUTO;

  if (iso == AEC_ISO_AUTO || iso == AEC_ISO_DEBLUR) {
    iso_mode = (aec_iso_mode_type)iso;
    iso_value = 0;
  } else if (iso >= AEC_ISO_MAX){
    /* This is true for the manual long exposure mode which applies only to the non-ZSL
       snapshot and not to the preview. Manual long exposure mode is completely handled
       in aec_port_proc_get_aec_data. These parameters should not be sent to AEC algorithm
       as there can be few side effects like exposure time getting clamped to honor min_fps.
       In long exposure use-cases min_fps for snapshot is not expected to be honored as
       fps range is not directly set by the user. FPS range gets honored for NON long exposure
       ISO settings.
       Also, AEC algorithm doesn't have the max limit of sensor long exposure time and will
       caps it with the non long expsure max sensor exposure time. */
    iso_mode = AEC_ISO_AUTO;
    iso_value = 0;
  } else {
    iso_mode = AEC_ISO_MODE_MANUAL;
    int32 temp_iso_multiplier = iso - AEC_ISO_100;
    temp_iso_multiplier = (1 << temp_iso_multiplier);
    iso_value = temp_iso_multiplier * 100;
  }

  aec->bestshot_off_data.iso_mode = iso_mode;
  aec->bestshot_off_data.iso_value = iso_value;
  rc = aec_add_core_entry(AEC_INPUT_PARAM_ISO_MODE,
    sizeof(aec_iso_mode_type), &iso_mode, &aec->core_input.input_info);
  rc = aec_add_core_entry(AEC_INPUT_PARAM_ISO_VALUE,
    sizeof(uint32), &iso_value, &aec->core_input.input_info);

  return rc;
}

static boolean aec_biz_map_set_roi(aec_biz_t *aec,
  const aec_interested_region_t *roi, boolean sensor_roi)
{
  uint32 i = 0;
  boolean rc = TRUE;
  q3a_core_roi_configuration_type  aec_roi;

  if (!aec || !roi) {
    AEC_ERR("Invalid input: %p,%p",aec, roi);
    return FALSE;
  }

  uint16 x = roi->r[i].x;
  uint16 y = roi->r[i].y;
  uint16 dx = roi->r[i].dx;
  uint16 dy = roi->r[i].dy;

  if (!roi->enable) {
    aec_roi.roi_count = 0;
  } else {
    aec_roi.roi_count = roi->num_regions;
    for (i = 0; i < roi->num_regions && i < Q3A_CORE_MAX_ROI_COUNT; i++) {
     if (sensor_roi) {
       aec_biz_translate_coord_camif2fov(aec, &x, &y);
       aec_biz_translate_dim_camif2fov(aec, &dx, &dy);
     }
     aec_roi.roi[i].x = (float)x;
     aec_roi.roi[i].y = (float)y;
     aec_roi.roi[i].dx = (float)dx;
     aec_roi.roi[i].dy = (float)dy;
     aec_roi.roi[i].weight = roi->weight;
    }
  }
  rc = aec_add_core_entry(AEC_INPUT_PARAM_ROI,
    sizeof(q3a_core_roi_configuration_type), &aec_roi, &aec->core_input.input_info);

  return rc;
}

static boolean aec_biz_map_set_fd_roi(aec_biz_t *aec,const aec_proc_roi_info_t* roi)
{
  uint32 i = 0;
  boolean rc = TRUE;
  aec_proc_fd_roi_info_type fd_roi;

  if (!aec || !roi) {
    AEC_ERR("Invalid input: %p,%p",aec, roi);
    return FALSE;
  }

  fd_roi.roi_count = roi->num_roi;
  fd_roi.frm_height = roi->frm_height;
  fd_roi.frm_width = roi->frm_height;
  for (i = 0; i < roi->num_roi; i++) {
    fd_roi.roi[i].x = roi->roi[i].x;
    fd_roi.roi[i].y = roi->roi[i].y;
    fd_roi.roi[i].dx = roi->roi[i].dx;
    fd_roi.roi[i].dy = roi->roi[i].dy;
  }

  rc = aec_add_core_entry(AEC_INPUT_PARAM_FD_ROI,
    sizeof(aec_proc_fd_roi_info_type), &fd_roi, &aec->core_input.input_info);

  return rc;
}

static boolean aec_biz_map_set_mtr_area(aec_biz_t *aec, const aec_proc_mtr_area_t* mtr_area)
{
  int i = 0;
  boolean rc = TRUE;
  q3a_core_roi_configuration_type aec_mtr_area;

  if (!aec || !mtr_area) {
    AEC_ERR("Invalid input: %p,%p",aec, mtr_area);
    return FALSE;
  }

  aec_mtr_area.roi_count = mtr_area->num_area;
  for (i = 0; i <mtr_area->num_area; i++) {
    aec_mtr_area.roi[i].x = mtr_area->mtr_area[i].x;
    aec_mtr_area.roi[i].y = mtr_area->mtr_area[i].y;
    aec_mtr_area.roi[i].dx = mtr_area->mtr_area[i].dx;
    aec_mtr_area.roi[i].dy = mtr_area->mtr_area[i].dy;
    aec_mtr_area.roi[i].weight = (float)mtr_area->weight[i];
  }

  rc = aec_add_core_entry(AEC_INPUT_PARAM_MTR_AREA,
    sizeof(q3a_core_roi_configuration_type), &aec_mtr_area, &aec->core_input.input_info);

  return rc;
}

static boolean aec_biz_set_bestshot_mode(aec_biz_t *aec,
  aec_bestshot_mode_type_t new_mode)
{
  int rc = 0;
  int32 exp_comp_val = 0;
  aec_iso_mode_type iso_mode = AEC_ISO_MODE_AUTO;
  int32 iso_value = 0;
  aec_auto_exposure_mode_t metering_type = AEC_METERING_FRAME_AVERAGE;


  AEC_LOW("new mode: %d", new_mode);
  if (new_mode >= AEC_BESTSHOT_MAX) {
    new_mode = AEC_BESTSHOT_OFF;
    AEC_ERR("%s: Invalid bestshot mode, setting it to default", __func__);
  }

  if (new_mode != AEC_BESTSHOT_OFF) {
    /* CONFIG AEC for BESTHOT mode */
    switch (new_mode) {
    case AEC_BESTSHOT_SPORTS:
    case AEC_BESTSHOT_ANTISHAKE:
    case AEC_BESTSHOT_ACTION:
      iso_mode = AEC_ISO_MODE_MANUAL;
      iso_value = 400;
      rc = aec_add_core_entry(AEC_INPUT_PARAM_ISO_MODE,
        sizeof(aec_iso_mode_t), &iso_mode, &aec->core_input.input_info);
      rc = aec_add_core_entry(AEC_INPUT_PARAM_ISO_VALUE,
        sizeof(int32), &iso_value, &aec->core_input.input_info);
      break;

    case AEC_BESTSHOT_OFF:
    case AEC_BESTSHOT_LANDSCAPE:
    case AEC_BESTSHOT_SNOW:
    case AEC_BESTSHOT_BEACH:
    case AEC_BESTSHOT_SUNSET:
    case AEC_BESTSHOT_NIGHT:
    case AEC_BESTSHOT_PORTRAIT:
    case AEC_BESTSHOT_BACKLIGHT:
    case AEC_BESTSHOT_FLOWERS:
    case AEC_BESTSHOT_CANDLELIGHT:
    case AEC_BESTSHOT_FIREWORKS:
    case AEC_BESTSHOT_PARTY:
    case AEC_BESTSHOT_NIGHT_PORTRAIT:
    case AEC_BESTSHOT_THEATRE:
    case AEC_BESTSHOT_AR:
    default:
      iso_mode = AEC_ISO_MODE_AUTO;
      rc = aec_add_core_entry(AEC_INPUT_PARAM_ISO_MODE,
        sizeof(aec_iso_mode_t), &iso_mode, &aec->core_input.input_info);
      break;
    }

    switch (new_mode) {
    case AEC_BESTSHOT_LANDSCAPE:
    case AEC_BESTSHOT_SNOW:
    case AEC_BESTSHOT_BEACH:
    case AEC_BESTSHOT_SUNSET:
      metering_type = AEC_METERING_FRAME_AVERAGE;
      rc = aec_add_core_entry(AEC_INPUT_PARAM_METERING_MODE,
        sizeof(aec_auto_exposure_mode_t), &metering_type, &aec->core_input.input_info);
      break;

    case AEC_BESTSHOT_OFF:
    case AEC_BESTSHOT_NIGHT:
    case AEC_BESTSHOT_PORTRAIT:
    case AEC_BESTSHOT_BACKLIGHT:
    case AEC_BESTSHOT_SPORTS:
    case AEC_BESTSHOT_ANTISHAKE:
    case AEC_BESTSHOT_FLOWERS:
    case AEC_BESTSHOT_CANDLELIGHT:
    case AEC_BESTSHOT_FIREWORKS:
    case AEC_BESTSHOT_PARTY:
    case AEC_BESTSHOT_NIGHT_PORTRAIT:
    case AEC_BESTSHOT_THEATRE:
    case AEC_BESTSHOT_ACTION:
    case AEC_BESTSHOT_AR:
    default:
      metering_type = AEC_METERING_CENTER_WEIGHTED;
      rc = aec_add_core_entry(AEC_INPUT_PARAM_METERING_MODE,
        sizeof(aec_auto_exposure_mode_t), &metering_type, &aec->core_input.input_info);
    }

    switch (new_mode) {
    case AEC_BESTSHOT_SNOW:
    case AEC_BESTSHOT_BEACH:
      /* set EV of 6 */
      exp_comp_val = 6;
      rc = aec_add_core_entry(AEC_INPUT_PARAM_EXP_COMPENSATION,
        sizeof(int32), &exp_comp_val, &aec->core_input.input_info);
      break;

    case AEC_BESTSHOT_SUNSET:
    case AEC_BESTSHOT_CANDLELIGHT:
      /* set EV of -6 */
      exp_comp_val = -6;
      rc = aec_add_core_entry(AEC_INPUT_PARAM_EXP_COMPENSATION,
        sizeof(int32), &exp_comp_val, &aec->core_input.input_info);
      break;

    case AEC_BESTSHOT_FACE_PRIORITY:
      /* for face priority mode, we don't touch EV, otherwise it will
       * nullify the EV setting from app */
      break;
    default:
      exp_comp_val = 0;
      rc = aec_add_core_entry(AEC_INPUT_PARAM_EXP_COMPENSATION,
        sizeof(int32), &exp_comp_val, &aec->core_input.input_info);
      break;
    }
  } else {
    /* Restore AEC vals */
    rc = aec_add_core_entry(AEC_INPUT_PARAM_ISO_MODE, sizeof(aec_iso_mode_type),
      &aec->bestshot_off_data.iso_mode, &aec->core_input.input_info);
    rc = aec_add_core_entry(AEC_INPUT_PARAM_ISO_VALUE, sizeof(int32),
      &aec->bestshot_off_data.iso_value, &aec->core_input.input_info);
    rc = aec_add_core_entry(AEC_INPUT_PARAM_METERING_MODE,
      sizeof(aec_auto_exposure_mode_t), &aec->bestshot_off_data.metering_type,
      &aec->core_input.input_info);
    rc = aec_add_core_entry(AEC_INPUT_PARAM_EXP_COMPENSATION, sizeof(int32),
      &aec->bestshot_off_data.exp_comp_val, &aec->core_input.input_info);
  }

  return rc;
}

static void aec_biz_map_exp_bracket(char *str, aec_bracket_t* aec_bracket)
{
  int val_i, table_size;
  char *val, *prev_value;

  if (str == NULL || aec_bracket == NULL) {
    AEC_ERR("Invalid input, %p, %p",str, aec_bracket);
    return;
  }

  memset(aec_bracket, 0, sizeof(aec_bracket_t));
  val = strtok_r(str,",", &prev_value);
  while (val != NULL) {
    val_i = atoi(val);
    aec_bracket->ev_val[aec_bracket->valid_entries] = val_i;
    aec_bracket->valid_entries++;
    val = strtok_r(NULL, ",", &prev_value);
  }
}

static void aec_biz_pack_output(aec_biz_t* aec, aec_output_data_t *output)
{
  int size = sizeof(aec_output_data_t);
  aec_core_output_type *core_output = &aec->core_output;

  if (!aec || !output) {
    AEC_ERR("invalid input: %p,%p",aec,output);
    return;
  }

  if (!aec->exif_dbg_enable)
  {
    size -= AEC_DEBUG_DATA_SIZE * sizeof(char);
  }

  memset(output, 0, size);
  size = 0;
  output->type = AEC_UPDATE;

  output->aec_af.cur_af_luma = core_output->cur_af_luma;

  output->stats_update.aec_update.cur_luma = core_output->cur_luma;
  output->stats_update.aec_update.luma_delta = core_output->luma_delta;

  output->stats_update.aec_update.luma_settled_cnt = core_output->luma_settled_cnt;
  /* AWB related update */
  output->aec_awb.prev_exp_index  = core_output->prev_exp_index;
  output->stats_update.aec_update.exp_time = core_output->exp_time;
  output->stats_update.aec_update.sof_id = aec->sof_id;

  /* === Normal Updates === */
  output->stats_update.aec_update.exp_index     = core_output->exp_index;
  output->stats_update.aec_update.exp_index_for_awb = core_output->exp_index_for_awb;
  output->stats_update.aec_update.lux_idx       = core_output->lux_idx;
  output->pixelsPerRegion  = core_output->pixelsPerRegion;
  output->numRegions = core_output->numRegions;
  output->stats_update.aec_update.frame_id = core_output->frame_id;
  memcpy(output->SY, core_output->SY, sizeof(uint32) * 256);
  output->stats_update.aec_update.SY_data.SY = &core_output->SY[0];
  output->stats_update.aec_update.SY_data.is_valid = TRUE;
  output->preview_fps = core_output->preview_fps;
  output->afr_enable = core_output->afr_enable;
  output->aec_locked = core_output->aec_locked;
  output->metering_type = core_output->metering_type;
  if (core_output->iso_mode == AEC_ISO_MODE_AUTO ||
    core_output->iso_mode == AEC_ISO_MODE_DEBLUR){
    output->iso = core_output->iso_mode;
  } else {
    output->iso = core_output->iso_value / 100 + 1;
  }
  output->stats_update.aec_update.settled = core_output->settled;
  output->stats_update.aec_update.pixelsPerRegion = core_output->pixelsPerRegion;
  output->stats_update.aec_update.numRegions = core_output->numRegions;
  output->stats_update.aec_update.low_light_shutter_flag =
    core_output->low_light_shutter_flag;
  output->stats_update.aec_update.touch_ev_status = core_output->touch_ev_status;
  /* Luma */
  output->stats_update.aec_update.target_luma = core_output->target_luma;
  output->stats_update.aec_update.cur_luma  = core_output->cur_luma;
  output->prev_sensitivity = core_output->prev_sensitivity;
  output->stats_update.aec_update.exp_tbl_val  = core_output->exp_tbl_val;
  output->stats_update.aec_update.max_line_cnt = core_output->max_line_cnt;
  output->stats_update.aec_update.comp_luma = core_output->comp_luma;
  output->stats_update.aec_update.preview_fps = (core_output->preview_fps);
  output->stats_update.aec_update.preview_linesPerFrame = core_output->preview_linesPerFrame;

  output->stats_update.aec_update.real_gain = core_output->real_gain;
  output->stats_update.aec_update.sensor_gain = core_output->sensor_gain;
  output->stats_update.aec_update.total_drc_gain = core_output->drc_gains.total_drc_gain;
  output->stats_update.aec_update.color_drc_gain = core_output->drc_gains.color_drc_gain;
  output->stats_update.aec_update.gtm_ratio = core_output->drc_gains.gtm_ratio;
  output->stats_update.aec_update.ltm_ratio = core_output->drc_gains.ltm_ratio;
  output->stats_update.aec_update.la_ratio = core_output->drc_gains.la_ratio;
  output->stats_update.aec_update.gamma_ratio = core_output->drc_gains.gamma_ratio;
  output->stats_update.aec_update.linecount = core_output->linecount;
  output->stats_update.aec_update.s_real_gain = core_output->s_real_gain;
  output->stats_update.aec_update.s_linecount = core_output->s_linecount;
  output->stats_update.aec_update.l_real_gain = core_output->l_real_gain;
  output->stats_update.aec_update.l_linecount = core_output->l_linecount;
  output->stats_update.aec_update.hdr_sensitivity_ratio = core_output->hdr_sensitivity_ratio;
  output->stats_update.aec_update.hdr_exp_time_ratio = core_output->hdr_exp_time_ratio;
  output->stats_update.aec_update.is_hdr_drc_enabled =
    core_output->is_hdr_drc_enabled;

  output->stats_update.aec_update.led_off_params.sensor_gain = core_output->led_off_gain;
  output->stats_update.aec_update.led_off_params.linecnt = core_output->led_off_linecnt;
  output->stats_update.aec_update.led_off_params.s_gain = core_output->led_off_s_gain;
  output->stats_update.aec_update.led_off_params.s_linecnt = core_output->led_off_s_linecnt;
  output->stats_update.aec_update.led_off_params.l_gain = core_output->led_off_l_gain;
  output->stats_update.aec_update.led_off_params.l_linecnt = core_output->led_off_l_linecnt;
  output->force_prep_snap_done = core_output->force_prep_snap_done;
  output->stats_update.aec_update.prep_snap_no_led = core_output->prep_snap_no_led;
  output->stats_update.aec_update.led_state = core_output->led_state;
  output->stats_update.aec_update.use_led_estimation = core_output->use_led_estimation;
  output->stats_update.aec_update.led_needed = core_output->led_needed;
  output->stats_update.aec_update.flash_sensitivity.high = core_output->flash_sensitivity.high;
  output->stats_update.aec_update.flash_sensitivity.low = core_output->flash_sensitivity.low;
  output->stats_update.aec_update.flash_sensitivity.off = core_output->flash_sensitivity.off;

  if (core_output->eztune.running) {
    memcpy(&output->eztune, &core_output->eztune, sizeof(aec_ez_tune_t));
  }
  /* AFD */
  output->stats_update.aec_update.band_50hz_gap = core_output->band_50hz_gap;
  output->stats_update.aec_update.cur_atb = core_output->cur_atb;

  /* HJR */
  output->hjr_snap_frame_cnt    = core_output->hjr_snap_frame_cnt;
  output->stats_update.aec_update.asd_extreme_green_cnt = core_output->asd_extreme_green_cnt;
  output->stats_update.aec_update.asd_extreme_blue_cnt  = core_output->asd_extreme_blue_cnt;

  /* total bayer stat regions 64x48  used to get ratio of extreme stats.*/
  output->stats_update.aec_update.asd_extreme_tot_regions = core_output->asd_extreme_tot_regions;
  output->hjr_dig_gain = core_output->hjr_dig_gain;
  output->snap = core_output->snap;
  /* EZtune related update */
  output->stats_update.aec_update.flash_needed = core_output->flash_needed;
  output->stats_update.aec_update.hdr_indoor_detected = core_output->hdr_indoor_detected;

  output->stats_update.aec_update.Bv = core_output->Bv;
  output->stats_update.aec_update.Tv = core_output->Tv;
  output->stats_update.aec_update.Sv = core_output->Sv;
  output->stats_update.aec_update.Av = core_output->Av;
  output->stats_update.aec_update.exif_iso = core_output->iso_Exif;

  /* Stats configuration */
  aec_biz_pack_stats_config(aec, output, core_output);

  if (aec->exif_dbg_enable) {
    q3a_core_result_type rc = Q3A_CORE_RESULT_SUCCESS;
    aec_core_get_info_type get_param_info;
    aec_core_get_output_type get_param_output;
    get_param_info.param_type = AEC_GET_META_INFO;
    get_param_info.u.metadata_size = AEC_DEBUG_DATA_SIZE;
    get_param_output.u.meta_info.metadata = (void*)output->aec_debug_data_array;
    if (aec->aec_algo_ops.get_parameters) {
      rc = aec->aec_algo_ops.get_parameters(aec->handle, &get_param_info, &get_param_output);
    } else {
      rc = Q3A_CORE_RESULT_FAILED;
    }
    if (rc != Q3A_CORE_RESULT_SUCCESS) {
      AEC_ERR("aec get parameter failed");
    } else {
      output->aec_debug_data_size = get_param_output.u.meta_info.metadata_size;
      AEC_LOW("send AEC debug data of size: %d", output->aec_debug_data_size);
    }
  } else {
    output->aec_debug_data_size = 0;
    AEC_LOW("debug data not enabled");
  }

  AEC_LOW("SOF ID = %d target_luma=%d cur_luma=%d stored_digital_gain=%f exp_index=%d, "
     "real_gain=%f, linecnt=%d, aec_settled=%d, iso %d, is_flash_snap=%d, "
     "snap_lux_idx=%f snap_gain=%f snap_lc=%d",
     output->stats_update.aec_update.sof_id, output->stats_update.aec_update.target_luma,
     output->stats_update.aec_update.cur_luma, output->stats_update.aec_update.stored_digital_gain,
     output->stats_update.aec_update.exp_index, output->stats_update.aec_update.real_gain,
     output->stats_update.aec_update.linecount, output->stats_update.aec_update.settled,
     output->stats_update.aec_update.exif_iso, output->snap.is_flash_snapshot, output->snap.lux_index,
     output->snap.real_gain, output->snap.line_count);
}

static boolean aec_biz_map_input_info(aec_biz_t *aec, aec_set_parameter_t *param)
{
  boolean rc = TRUE;

  if (!aec || !param) {
    AEC_ERR("invalid input: %p, %p",aec, param)
    return FALSE;
  }
  AEC_LOW("param_type: %d",param->type);
  switch (param->type) {
  case AEC_SET_PARAM_METERING_MODE:
    aec->bestshot_off_data.metering_type = param->u.aec_metering;
    rc = aec_add_core_entry(AEC_INPUT_PARAM_METERING_MODE,
      sizeof(aec_auto_exposure_mode_t),&param->u.aec_metering, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_ISO_MODE:
    rc = aec_biz_map_set_iso(aec, (int32)param->u.iso.value);
    break;

  case AEC_SET_PARAM_ANTIBANDING:
    rc = aec_add_core_entry(AEC_INPUT_PARAM_ANTIBANDING,
      sizeof(aec_antibanding_type_t), &param->u.antibanding, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_BRIGHTNESS_LVL:
    rc = aec_add_core_entry(AEC_INPUT_PARAM_BRIGHTNESS_LVL,
      sizeof(int),&param->u.brightness,&aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_EXP_COMPENSATION:
    aec->bestshot_off_data.exp_comp_val = param->u.exp_comp;
    rc = aec_add_core_entry(AEC_INPUT_PARAM_EXP_COMPENSATION,
      sizeof(int32), &param->u.exp_comp, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_HJR_AF:
    rc = aec_add_core_entry(AEC_INPUT_PARAM_HJR_AF,
      sizeof(uint32),&param->u.aec_af_hjr, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_ROI:
    rc = aec_biz_map_set_roi(aec, &param->u.aec_roi, FALSE);
    break;

  case AEC_SET_PARAM_CROP_INFO:
    aec->stream_crop_info = param->u.stream_crop;
    rc = aec_add_core_entry(AEC_INPUT_PARAM_CROP_INFO,
      sizeof(q3a_stream_crop_t), &param->u.stream_crop, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_FD_ROI:
    rc = aec_biz_map_set_fd_roi(aec, &param->u.fd_roi);
    break;

  case AEC_SET_PARAM_MTR_AREA:
    rc = aec_biz_map_set_mtr_area(aec, &param->u.mtr_area);
    break;

  case AEC_SET_PARAM_ASD_PARM: {
    rc = aec_add_core_entry(AEC_INPUT_PARAM_ASD_PARM,
      sizeof(aec_set_asd_param_t),&param->u.asd_param, &aec->core_input.input_info);
  }
    break;

  case AEC_SET_PARAM_AFD_PARM:
    rc = aec_add_core_entry(AEC_INPUT_PARAM_AFD_PARM,
      sizeof(aec_set_afd_parm_t),&param->u.afd_param, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_AWB_PARM:
    rc = aec_add_core_entry(AEC_INPUT_PARAM_AWB_PARM,
      sizeof(aec_set_awb_parm_t), &param->u.awb_param, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_GYRO_INFO:
    rc = aec_add_core_entry(AEC_INPUT_PARAM_GYRO_INFO,
      sizeof(aec_algo_gyro_info_t), &param->u.gyro_info, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_LED_MODE:
    rc = aec_add_core_entry(AEC_INPUT_PARAM_LED_MODE,
      sizeof(q3a_led_flash_mode_t), &param->u.led_mode, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_UI_FRAME_DIM: {
    aec_set_parameter_init_t *init_param = &(param->u.init_param);
    aec->preview_width =  init_param->frame_dim.width;
    aec->preview_height = init_param->frame_dim.height;
    rc = aec_add_core_entry(AEC_INPUT_PARAM_UI_FRAME_DIM,
      sizeof(aec_set_parameter_init_t), init_param, &aec->core_input.input_info);
  }
    break;

  case AEC_SET_PARAM_ZSL_OP:
    rc = aec_add_core_entry(AEC_INPUT_PARAM_ZSL_OP,
      sizeof(int32), &param->u.zsl_op, &aec->core_input.input_info);
    break;

  case AEC_SET_PARAM_CTRL_MODE: {
    q3a_core_roi_configuration_type aec_roi;
    aec_roi.roi_count = 0;
    rc = aec_add_core_entry(AEC_INPUT_PARAM_ROI,
      sizeof(q3a_core_roi_configuration_type), &aec_roi, &aec->core_input.input_info);
  }
    break;

  case AEC_SET_PARAM_SENSOR_ROI:
    rc = aec_biz_map_set_roi(aec, &param->u.aec_roi, TRUE);
    break;

  case AEC_SET_MANUAL_AUTO_SKIP:{
    uint32 manual_to_auto_skip_cnt = MANUAL_TO_AUTO_SKIP_CNT;
    rc = aec_add_core_entry(AEC_INPUT_MANUAL_SKIP,
      sizeof(uint32), &manual_to_auto_skip_cnt, &aec->core_input.input_info);
  }
    break;
  case AEC_SET_PARAM_STATS_DEPTH: {
    aec->stats_depth = param->u.stats_depth;
    AEC_LOW("set stats bit depth: %d", aec->stats_depth);
  }
  break;
  default:
    AEC_ERR("invalid param type: %d",param->type);
    rc = FALSE;
    break;
  }

  return rc;
}

static void aec_biz_initialize_custom_tuning_param(
  aec_set_parameter_init_t *param)
{
  if (param) {
   /*
   * Here we initialize future chromatix variables if chromatix header is fixed.
   * These variables will go in to next chromatix headers
   */
  }
}

static boolean aec_biz_map_init_chromatix_sensor(aec_biz_t *aec, aec_set_parameter_t *param)
{
  q3a_core_result_type rc = Q3A_CORE_RESULT_SUCCESS;
  aec_core_set_param_type set_param;

  if (!aec || !param) {
    AEC_ERR("invalid input: %p, %p",aec, param)
    return FALSE;
  }

  set_param.type = AEC_SET_CLAMP_SNAP_LC_FOR_MIN_FPS;
  /* if ISO is manual mode and LC calculated is greater than limit to
   * maintain min fps, then clamp the linecount. Captured images can
   * be dark in this case */
  set_param.u.clamp_snap_lc_for_min_fps = TRUE;
  if (aec->aec_algo_ops.set_parameters) {
    rc = aec->aec_algo_ops.set_parameters(aec->handle, &set_param);
  } else {
    rc = Q3A_CORE_RESULT_FAILED;
  }
  if (rc != Q3A_CORE_RESULT_SUCCESS) {
    AEC_ERR("aec set param failed");
    return FALSE;
  }

  set_param.type = AEC_SET_INIT_CHROMATIX_SENSOR;
  memcpy(&set_param.u.init_param, &param->u.init_param,
    sizeof(aec_set_parameter_init_t));

  /* Set the longshot parameter */
  set_param.u.init_param.longshot_flash_aec_lock = LONGSHOT_FLASH_AEC_LOCK;

  /* Set Flash ON/OFF parameter for AEC lock state and Flash ON*/
  set_param.u.init_param.enable_flash_for_aec_lock =
    ENABLE_FLASH_FOR_AEC_LOCKED_STATE;
  aec_biz_initialize_custom_tuning_param(&set_param.u.init_param);

  if (aec->aec_algo_ops.set_parameters) {
    rc = aec->aec_algo_ops.set_parameters(aec->handle, &set_param);
  } else {
    rc = Q3A_CORE_RESULT_FAILED;
  }
  if (rc != Q3A_CORE_RESULT_SUCCESS) {
    AEC_ERR("aec set param failed");
    return FALSE;
  }

  /* Do not reload parameter for snapshot mode */
  if (param->u.init_param.op_mode != AEC_OPERATION_MODE_SNAPSHOT)
  {
    aec_core_get_info_type get_param_info;
    get_param_info.param_type = AEC_GET_RELOAD_EXPOSURE_PARAMS;
    aec_core_get_output_type get_param_output;
    get_param_output.u.core_output = &aec->core_output;
    if (aec->aec_algo_ops.get_parameters) {
      rc = aec->aec_algo_ops.get_parameters(aec->handle, &get_param_info, &get_param_output);
    } else {
      rc = Q3A_CORE_RESULT_FAILED;
    }
    if(rc != Q3A_CORE_RESULT_SUCCESS){
      AEC_ERR("aec get param failed");
      return FALSE;
    }
  }

  return TRUE;
}

static boolean aec_biz_map_set_param(aec_biz_t *aec, aec_set_parameter_t *param)
{
  q3a_core_result_type rc = Q3A_CORE_RESULT_SUCCESS;
  aec_core_set_param_type set_param;

  if (!aec || !param) {
    AEC_ERR("invalid input: %p, %p",aec, param)
    return FALSE;
  }

  AEC_LOW("param_type: %d",param->type);
  switch (param->type) {
  case AEC_SET_PARAM_LED_RESET:
    set_param.type = AEC_SET_LED_RESET;
    break;

  case AEC_SET_PARAM_RESET_LED_EST:
    set_param.type = AEC_SET_RESET_LED_EST;
    break;

  case AEC_SET_PARAM_PREPARE_FOR_SNAPSHOT:
    set_param.type = AEC_SET_PREPARE_FOR_SNAPSHOT;
    set_param.u.aec_trigger = param->u.aec_trigger;
    break;

  case AEC_SET_PARAM_PREP_FOR_SNAPSHOT_NOTIFY:
    set_param.type = AEC_SET_PREP_FOR_SNAPSHOT_NOTIFY;
    set_param.u.aec_trigger = param->u.aec_trigger;
    break;

  case AEC_SET_PARAM_DO_LED_EST_FOR_AF:
    set_param.type = AEC_SET_DO_LED_EST_FOR_AF;
    set_param.u.est_for_af = param->u.est_for_af;
    break;

  case AEC_SET_PARAM_EZ_DISABLE:
    set_param.type = AEC_SET_EZ_DISABLE;
    set_param.u.ez_disable = param->u.ez_disable;
    break;

  case AEC_SET_PARAM_EZ_LOCK_OUTPUT:
    set_param.type = AEC_SET_EZ_LOCK_OUTPUT;
    set_param.u.ez_lock_output = param->u.ez_lock_output;
    break;

  case AEC_SET_PARAM_EZ_FORCE_EXP:
    set_param.type = AEC_SET_EZ_FORCE_EXP;
    set_param.u.ez_force_exp = param->u.ez_force_exp;
    break;

  case AEC_SET_PARAM_EZ_FORCE_LINECOUNT:
    set_param.type = AEC_SET_EZ_FORCE_LINECOUNT;
    set_param.u.ez_force_linecount = param->u.ez_force_linecount;
    break;

  case AEC_SET_PARAM_EZ_FORCE_GAIN:
    set_param.type = AEC_SET_EZ_FORCE_GAIN;
    set_param.u.ez_force_gain = param->u.ez_force_gain;
    break;

  case AEC_SET_PARAM_EZ_TEST_ENABLE:
    set_param.type = AEC_SET_EZ_TEST_ENABLE;
    set_param.u.ez_test_enable = param->u.ez_test_enable;
    break;

  case AEC_SET_PARAM_EZ_TEST_ROI:
    set_param.type = AEC_SET_EZ_TEST_ROI;
    set_param.u.ez_test_roi = param->u.ez_test_roi;
    break;

  case AEC_SET_PARAM_EZ_TEST_MOTION:
    set_param.type = AEC_SET_EZ_TEST_MOTION;
    set_param.u.ez_test_motion = param->u.ez_test_motion;
    break;

  case AEC_SET_PARAM_EZ_FORCE_SNAP_EXP:
    set_param.type = AEC_SET_EZ_FORCE_SNAP_EXP;
    set_param.u.ez_force_snap_exp = param->u.ez_force_snap_exp;
    break;

  case AEC_SET_PARAM_EZ_FORCE_SNAP_LINECOUNT:
    set_param.type = AEC_SET_EZ_FORCE_SNAP_LINECOUNT;
    set_param.u.ez_force_snap_linecount = param->u.ez_force_snap_linecount;
    break;

  case AEC_SET_PARAM_EZ_FORCE_SNAP_GAIN:
    set_param.type = AEC_SET_EZ_FORCE_SNAP_GAIN;
    set_param.u.ez_force_snap_gain = param->u.ez_force_snap_gain;
    break;

  case AEC_SET_PARAM_EZ_TUNE_RUNNING:
    set_param.type = AEC_SET_EZ_TUNE_RUNNING;
    set_param.u.ez_running = param->u.ez_running;
    break;

  case AEC_SET_PARAM_LOCK:
    set_param.type = AEC_SET_LOCK;
    set_param.u.aec_lock = param->u.aec_lock;
    break;

  case AEC_SET_PARAM_ADRC_ENABLE:
    set_param.type = AEC_SET_ADRC_ENABLE;
    set_param.u.adrc_enable = param->u.adrc_enable;
    break;

  case AEC_SET_PARAM_RESET_STREAM_INFO:
    set_param.type = AEC_SET_RESET_STREAM_INFO;
    break;

  case AEC_SET_PARAM_INIT_SENSOR_INFO:
    set_param.type = AEC_SET_INIT_SENSOR_INFO;
    memcpy(&aec->sensor_info, &param->u.init_param.sensor_info,
      sizeof(aec_sensor_info_t));
    memcpy(&set_param.u.init_param, &param->u.init_param,
      sizeof(aec_set_parameter_init_t));
    memset(&aec->stats_config, 0, sizeof(aec_config_t));
    break;

  case AEC_SET_PARAM_VIDEO_HDR:
    set_param.type = AEC_SET_VIDEO_HDR;
    set_param.u.video_hdr = param->u.video_hdr;
    break;

  case AEC_SET_PARAM_SNAPSHOT_HDR:
    set_param.type = AEC_SET_SNAPSHOT_HDR;
    set_param.u.snapshot_hdr = param->u.snapshot_hdr;
    break;

  case AEC_SET_PARAM_BRACKET:
    set_param.type = AEC_SET_BRACKET;
    aec_biz_map_exp_bracket(param->u.aec_bracket, &set_param.u.aec_bracket);
    break;

  case AEC_SET_PARAM_ENABLE:
    set_param.type = AEC_SET_ENABLE;
    set_param.u.aec_enable = param->u.aec_enable;
    break;

  case AEC_SET_PARM_FAST_AEC_DATA:
    if (aec->fast_aec_enable == param->u.fast_aec_data.enable)
      return TRUE;

    set_param.type = AEC_SET_INSTANT_AEC_TYPE;
    set_param.u.instant_aec_type = (param->u.fast_aec_data.enable) ? (AEC_CONVERGENCE_FAST) :
                                      AEC_CONVERGENCE_NORMAL;
    AEC_LOW("Instant AEC type: %d",set_param.u.instant_aec_type);
    aec->fast_aec_enable = param->u.fast_aec_data.enable;
    break;

  case AEC_SET_PARAM_FPS:
    set_param.type = AEC_SET_FPS_RANGE;
    set_param.u.fps_range = param->u.fps;
    break;

  case AEC_SET_PARAM_INIT_EXPOSURE_INDEX:
    set_param.type = AEC_SET_INIT_EXPOSURE_INDEX;
    set_param.u.exp_index = param->u.init_exposure_index;
    break;

  case AEC_SET_PARAM_LONGSHOT_MODE:
    set_param.type = AEC_SET_LONGSHOT_ENABLE;
    set_param.u.longshot_enable = param->u.longshot_mode;
    break;

  case AEC_SET_PARM_INSTANT_AEC_DATA:
    set_param.type = AEC_SET_INSTANT_AEC_TYPE;
    set_param.u.instant_aec_type = param->u.instant_aec_type;
    break;

  case AEC_SET_PARM_DUAL_LED_CALIB_MODE:
    set_param.type = AEC_SET_DUAL_LED_CALIB_MODE;
    set_param.u.dual_led_calib_data.enabled = param->u.dual_led_calib_mode;
    set_param.u.dual_led_calib_data.prefered_exp_index =
      DUAL_LED_CALIB_AEC_PREFERED_EXP_INDEX;
    break;

  default:
    AEC_ERR("invalid param type: %d",param->type);
    return FALSE;
  }

  if (aec->aec_algo_ops.set_parameters) {
    rc = aec->aec_algo_ops.set_parameters(aec->handle, &set_param);
  } else {
    rc = Q3A_CORE_RESULT_FAILED;
  }
  if (rc != Q3A_CORE_RESULT_SUCCESS) {
    AEC_ERR("aec set param failed for type=%d", param->type);
    return FALSE;
  }

  return TRUE;
}

/** aec_set_parameters:
 *
 **/
boolean aec_biz_set_param(aec_set_parameter_t *param, aec_output_data_t *output, void *aec_obj)
{
  boolean rc            = TRUE;
  aec_biz_t *aec = (aec_biz_t *)aec_obj;

  if (!param || !aec) {
   rc = FALSE;
   goto done;
  }

  AEC_LOW("AEC_EVENT: %s", aec_biz_set_param_string(param->type));

  switch (param->type) {
  case AEC_SET_PARAM_METERING_MODE:
  case AEC_SET_PARAM_ISO_MODE:
  case AEC_SET_PARAM_ANTIBANDING:
  case AEC_SET_PARAM_BRIGHTNESS_LVL:
  case AEC_SET_PARAM_EXP_COMPENSATION:
  case AEC_SET_PARAM_HJR_AF:
  case AEC_SET_PARAM_ROI:
  case AEC_SET_PARAM_CROP_INFO:
  case AEC_SET_PARAM_FD_ROI:
  case AEC_SET_PARAM_MTR_AREA:
  case AEC_SET_PARAM_ASD_PARM:
  case AEC_SET_PARAM_AFD_PARM:
  case AEC_SET_PARAM_AWB_PARM:
  case AEC_SET_PARAM_GYRO_INFO:
  case AEC_SET_PARAM_LED_MODE:
  case AEC_SET_PARAM_UI_FRAME_DIM:
  case AEC_SET_PARAM_ZSL_OP:
  case AEC_SET_PARAM_CTRL_MODE:
  case AEC_SET_PARAM_SENSOR_ROI:
  case AEC_SET_MANUAL_AUTO_SKIP:
  case AEC_SET_PARAM_STATS_DEPTH:
    rc = aec_biz_map_input_info(aec,param);
    break;

  case AEC_SET_PARAM_FPS:
  case AEC_SET_PARAM_LED_RESET:
  case AEC_SET_PARAM_RESET_LED_EST:
  case AEC_SET_PARAM_PREPARE_FOR_SNAPSHOT:
  case AEC_SET_PARAM_PREP_FOR_SNAPSHOT_NOTIFY:
  case AEC_SET_PARAM_DO_LED_EST_FOR_AF:
  case AEC_SET_PARAM_EZ_DISABLE:
  case AEC_SET_PARAM_EZ_LOCK_OUTPUT:
  case AEC_SET_PARAM_EZ_FORCE_EXP:
  case AEC_SET_PARAM_EZ_FORCE_LINECOUNT:
  case AEC_SET_PARAM_EZ_FORCE_GAIN:
  case AEC_SET_PARAM_EZ_TEST_ENABLE:
  case AEC_SET_PARAM_EZ_TEST_ROI:
  case AEC_SET_PARAM_EZ_TEST_MOTION:
  case AEC_SET_PARAM_EZ_FORCE_SNAP_EXP:
  case AEC_SET_PARAM_EZ_FORCE_SNAP_LINECOUNT:
  case AEC_SET_PARAM_EZ_FORCE_SNAP_GAIN:
  case AEC_SET_PARAM_EZ_TUNE_RUNNING:
  case AEC_SET_PARAM_LOCK:
  case AEC_SET_PARAM_RESET_STREAM_INFO:
  case AEC_SET_PARAM_INIT_SENSOR_INFO:
  case AEC_SET_PARAM_VIDEO_HDR:
  case AEC_SET_PARAM_SNAPSHOT_HDR:
  case AEC_SET_PARAM_BRACKET:
  case AEC_SET_PARAM_ENABLE:
  case AEC_SET_PARAM_LED_EST:
  case AEC_SET_PARM_FAST_AEC_DATA:
  case AEC_SET_PARAM_ADRC_ENABLE:
  case AEC_SET_PARAM_INIT_EXPOSURE_INDEX:
  case AEC_SET_PARAM_LONGSHOT_MODE:
  case AEC_SET_PARM_INSTANT_AEC_DATA:
  case AEC_SET_PARM_DUAL_LED_CALIB_MODE:
    rc = aec_biz_map_set_param(aec,param);
    break;

  case AEC_SET_PARAM_BESTSHOT:
    rc = aec_biz_set_bestshot_mode(aec, param->u.bestshot_mode);
    break;

  case AEC_SET_PARAM_PACK_OUTPUT:
    aec->sof_id = param->u.current_sof_id;
    aec_biz_pack_output(aec, output);
    break;

  case AEC_SET_PARAM_INIT_CHROMATIX_SENSOR:
    rc = aec_biz_map_init_chromatix_sensor(aec,param);
    break;

  default:
    rc = FALSE;
    break;
  }

done:
  return rc;
}

static void aec_biz_set_debug_data_enable(aec_biz_t *aec)
{
  boolean exif_dbg_enable = FALSE;
  q3a_core_result_type rc = Q3A_CORE_RESULT_SUCCESS;
  aec_debug_data_level_type debug_level = AEC_DEBUG_DATA_LEVEL_VERBOSE;

  /* Check whether debug data levels are set. */
  exif_dbg_enable = stats_exif_debug_mask & EXIF_DEBUG_MASK_AEC ? TRUE : FALSE;

  if ((stats_debug_data_log_level & Q3A_DEBUG_DATA_LEVEL_CONCISE) > 0) {
    debug_level = AEC_DEBUG_DATA_LEVEL_CONCISE;
  } else if ((stats_debug_data_log_level & Q3A_DEBUG_DATA_LEVEL_VERBOSE) > 0) {
    debug_level = AEC_DEBUG_DATA_LEVEL_VERBOSE;
  }

  if (aec->exif_dbg_enable != exif_dbg_enable) {
    aec_core_set_param_type set_info;
    set_info.type = AEC_SET_ENABLE_DEBUG_DATA;
    set_info.u.debug_data_enable = exif_dbg_enable;
    if (aec->aec_algo_ops.set_parameters) {
      rc = aec->aec_algo_ops.set_parameters(aec->handle, &set_info);
    } else {
      rc = Q3A_CORE_RESULT_FAILED;
    }
    if (rc != Q3A_CORE_RESULT_SUCCESS) {
      AEC_ERR("aec set parameter failed with result %d", rc);
      return;
    }
    aec->exif_dbg_enable = exif_dbg_enable;
  }

  /* Set the EXIF debug level if it has changed and EXIF debugging is enabled. */
  if (aec->exif_dbg_level != debug_level && TRUE == aec->exif_dbg_enable) {
    aec_core_set_param_type set_param_info;

    /* Use AEC_SET_DEBUG_DATA_LEVEL set param to set debug data level. */
    set_param_info.type = AEC_SET_DEBUG_DATA_LEVEL;
    set_param_info.u.debug_data_level = debug_level;

    if (aec->aec_algo_ops.set_parameters) {
      rc = aec->aec_algo_ops.set_parameters(aec->handle, &set_param_info);
    } else {
      rc = Q3A_CORE_RESULT_FAILED;
    }

    if (rc != Q3A_CORE_RESULT_SUCCESS) {
      AEC_ERR("aec set param AEC_SET_PARAM_DEBUG_DATA_LEVEL failed with result %d", rc);
      return;
    }

    aec->exif_dbg_level = debug_level;
  }

}

static boolean aec_biz_stats_map(aec_biz_t *aec, const stats_t* stats)
{
  if (!aec || !stats) {
    AEC_ERR("Invalid input: %p,%p",aec, stats);
    return FALSE;
  }

  if (stats->stats_type_mask & STATS_BG ||
      stats->stats_type_mask & STATS_BG_AEC){
    aec->core_input.stats.stats_type_mask = Q3A_CORE_STATS_BAYER;
    aec->core_input.stats.frame_id = stats->frame_id;

    const q3a_bg_stats_t* q3a_bg_stats = stats->bayer_stats.p_q3a_bg_stats;
    const q3a_bhist_stats_t* q3a_bhist_stats = stats->bayer_stats.p_q3a_bhist_stats;

    if (q3a_bg_stats == NULL) {
      AEC_ERR("stats buffer error!");
      return FALSE;
    }

    /** map bg stats. */
    q3a_core_bg_stats_type* core_bg_stats = &aec->core_input.stats.bg_stats;
    core_bg_stats->array_length = MAX_BG_STATS_NUM;
    core_bg_stats->num_horizontal_regions = q3a_bg_stats->bg_region_h_num;
    core_bg_stats->num_vertical_regions = q3a_bg_stats->bg_region_v_num;
    core_bg_stats->region_height = q3a_bg_stats->bg_region_height;
    core_bg_stats->region_width = q3a_bg_stats->bg_region_width;
    core_bg_stats->region_pixel_cnt = q3a_bg_stats->region_pixel_cnt;
    core_bg_stats->r_max = q3a_bg_stats->rMax;
    core_bg_stats->gr_max = q3a_bg_stats->grMax;
    core_bg_stats->gb_max = q3a_bg_stats->gbMax;
    core_bg_stats->b_max = q3a_bg_stats->bMax;
    core_bg_stats->bit_depth = aec->stats_depth;
    core_bg_stats->r_info.channel_sums = q3a_bg_stats->bg_r_sum;
    core_bg_stats->r_info.channel_counts = q3a_bg_stats->bg_r_num;
    core_bg_stats->r_info.channel_sat_sums = q3a_bg_stats->bg_r_sat_sum;
    core_bg_stats->r_info.channel_sat_counts = q3a_bg_stats->bg_r_sat_num;
    core_bg_stats->gr_info.channel_sums = q3a_bg_stats->bg_gr_sum;
    core_bg_stats->gr_info.channel_counts = q3a_bg_stats->bg_gr_num;
    core_bg_stats->gr_info.channel_sat_sums = q3a_bg_stats->bg_gr_sat_sum;
    core_bg_stats->gr_info.channel_sat_counts = q3a_bg_stats->bg_gr_sat_num;
    core_bg_stats->b_info.channel_sums = q3a_bg_stats->bg_b_sum;
    core_bg_stats->b_info.channel_counts = q3a_bg_stats->bg_b_num;
    core_bg_stats->b_info.channel_sat_sums = q3a_bg_stats->bg_b_sat_sum;
    core_bg_stats->b_info.channel_sat_counts = q3a_bg_stats->bg_b_sat_num;
    core_bg_stats->gb_info.channel_sums = q3a_bg_stats->bg_gb_sum;
    core_bg_stats->gb_info.channel_counts = q3a_bg_stats->bg_gb_num;
    core_bg_stats->gb_info.channel_sat_sums = q3a_bg_stats->bg_gb_sat_sum;
    core_bg_stats->gb_info.channel_sat_counts = q3a_bg_stats->bg_gb_sat_num;
    /** map bhist stats.  */
    if (NULL != q3a_bhist_stats) {
      q3a_core_bhist_stats_type* core_bhist_stats = &aec->core_input.stats.bhist;
      core_bhist_stats->array_size = MAX_BHIST_STATS_NUM;
      core_bhist_stats->bayer_b_hist = q3a_bhist_stats->bayer_b_hist;
      core_bhist_stats->bayer_gb_hist = q3a_bhist_stats->bayer_gb_hist;
      core_bhist_stats->bayer_gr_hist = q3a_bhist_stats->bayer_gr_hist;
      core_bhist_stats->bayer_r_hist = q3a_bhist_stats->bayer_r_hist;
      core_bhist_stats->hdr_mode = (q3a_core_stats_hdr_type)q3a_bhist_stats->hdr_mode;
      core_bhist_stats->num_bins = q3a_bhist_stats->num_bins;
    } else {
      AEC_ERR("bhist stats buffer error!");
      q3a_core_bhist_stats_type* core_bhist_stats = &aec->core_input.stats.bhist;
      core_bhist_stats->array_size = 0;
      core_bhist_stats->bayer_b_hist = NULL;
      core_bhist_stats->bayer_gb_hist = NULL;
      core_bhist_stats->bayer_gr_hist = NULL;
      core_bhist_stats->bayer_r_hist = NULL;
      core_bhist_stats->hdr_mode = 0;
      core_bhist_stats->num_bins = 0;
    }

  } else if(stats->stats_type_mask & STATS_HDR_VID){
    aec->core_input.stats.stats_type_mask = Q3A_CORE_STATS_HDR_VID;
    aec->core_input.stats.frame_id = stats->frame_id;
    /** map yuv stats.  */
    q3a_core_yuv_stats_type* core_yuv_stats = &aec->core_input.stats.yuv_stats;
    const q3a_aec_stats_t* q3a_aec_stats = stats->yuv_stats.p_q3a_aec_stats;
    if(q3a_aec_stats == NULL){
      AEC_ERR("stats buffer error: q3a_aec_stats: %p",q3a_aec_stats);
      return FALSE;
    }
    core_yuv_stats->SY = q3a_aec_stats->SY;
    core_yuv_stats->ae_region_h_num = q3a_aec_stats->ae_region_h_num;
    core_yuv_stats->ae_region_v_num = q3a_aec_stats->ae_region_v_num;
    core_yuv_stats->array_size = MAX_YUV_STATS_NUM;

  } else if (stats->stats_type_mask & STATS_BE) {
    aec->core_input.stats.stats_type_mask = Q3A_CORE_STATS_BE;
    aec->core_input.stats.frame_id = stats->frame_id;

    const q3a_be_stats_t* q3a_be_stats = stats->bayer_stats.p_q3a_be_stats;
    if(NULL == q3a_be_stats){
      AEC_ERR("stats buffer error: q3a_be_stats: %p", q3a_be_stats);
      return FALSE;
    }
    /** map be stats. */
    q3a_core_be_stats_type* core_be_stats = &aec->core_input.stats.be_stats;
    core_be_stats->array_length = MAX_BE_STATS_NUM;
    core_be_stats->num_horizontal_regions   = (uint32)q3a_be_stats->nx;
    core_be_stats->num_vertical_regions     = (uint32)q3a_be_stats->ny;
    core_be_stats->region_width             = q3a_be_stats->sx;
    core_be_stats->region_height            = q3a_be_stats->sy;
    core_be_stats->bit_depth                = aec->stats_depth;
    core_be_stats->r_max = core_be_stats->gr_max = core_be_stats->gb_max =
      core_be_stats->b_max = (uint32)q3a_be_stats->saturation_thresh;
    core_be_stats->r_info.channel_sums      = q3a_be_stats->r_sum;
    core_be_stats->r_info.channel_counts    = q3a_be_stats->r_count;
    core_be_stats->gr_info.channel_sums     = q3a_be_stats->gr_sum;
    core_be_stats->gr_info.channel_counts   = q3a_be_stats->gr_count;
    core_be_stats->gb_info.channel_sums     = q3a_be_stats->gb_sum;
    core_be_stats->gb_info.channel_counts   = q3a_be_stats->gb_count;
    core_be_stats->b_info.channel_sums      = q3a_be_stats->b_sum;
    core_be_stats->b_info.channel_counts    = q3a_be_stats->b_count;
  } else if (stats->stats_type_mask & STATS_HDR_BE) {
    aec->core_input.stats.stats_type_mask = Q3A_CORE_STATS_BE;
    aec->core_input.stats.frame_id = stats->frame_id;

    const q3a_hdr_be_stats_t* q3a_hdr_be_stats = stats->bayer_stats.p_q3a_hdr_be_stats;
    if(NULL == q3a_hdr_be_stats){
      AEC_ERR("stats buffer error: q3a_hdr_be_stats: %p", q3a_hdr_be_stats);
      return FALSE;
    }
    /** map HDR be stats. */
    q3a_core_be_stats_type* core_be_stats = &aec->core_input.stats.be_stats;
    core_be_stats->array_length = MAX_BE_STATS_NUM;
    core_be_stats->num_horizontal_regions   = (uint32)q3a_hdr_be_stats->be_region_h_num;
    core_be_stats->num_vertical_regions     = (uint32)q3a_hdr_be_stats->be_region_v_num;
    core_be_stats->region_width             = q3a_hdr_be_stats->rgnWidth;
    core_be_stats->region_height            = q3a_hdr_be_stats->rgnHeight;
    core_be_stats->bit_depth                = aec->stats_depth;
    core_be_stats->r_max = core_be_stats->gr_max = core_be_stats->gb_max =
      core_be_stats->b_max = (uint32)q3a_hdr_be_stats->saturation_thresh;
    core_be_stats->r_info.channel_sums      = (int*)q3a_hdr_be_stats->be_r_sum;
    core_be_stats->r_info.channel_counts    = (int*)q3a_hdr_be_stats->be_r_num;
    core_be_stats->gr_info.channel_sums     = (int*)q3a_hdr_be_stats->be_gr_sum;
    core_be_stats->gr_info.channel_counts   = (int*)q3a_hdr_be_stats->be_gr_num;
    core_be_stats->gb_info.channel_sums     = (int*)q3a_hdr_be_stats->be_gb_sum;
    core_be_stats->gb_info.channel_counts   = (int*)q3a_hdr_be_stats->be_gb_num;
    core_be_stats->b_info.channel_sums      = (int*)q3a_hdr_be_stats->be_b_sum;
    core_be_stats->b_info.channel_counts    = (int*)q3a_hdr_be_stats->be_b_num;
  } else {
    AEC_ERR("invalid stats type mask: 0x%x", stats->stats_type_mask);
    return FALSE;
  }

  return TRUE;
}

boolean aec_biz_process(stats_t *stats, void *aec_obj, aec_output_data_t *output)
{
  q3a_core_result_type rc = Q3A_CORE_RESULT_SUCCESS;
  aec_biz_t* aec = (aec_biz_t*)aec_obj;
  boolean ret = TRUE;

  if (!aec || !stats || !output) {
    AEC_ERR("Invalid input: %p,%p,%p",aec, stats,output);
    return FALSE;
  }

  aec_biz_set_debug_data_enable(aec);
  ret = aec_biz_stats_map(aec, stats);
  if(ret != TRUE){
    AEC_ERR("aec stats mapping failed");
    return FALSE;
  }
  if (aec->aec_algo_ops.process) {
    rc = aec->aec_algo_ops.process(aec->handle, &aec->core_input, &aec->core_output);
    aec_biz_pack_output(aec, output);
  } else {
    rc = Q3A_CORE_RESULT_FAILED;
  }
  if (rc != Q3A_CORE_RESULT_SUCCESS) {
    AEC_ERR("aec core process failed");
    return FALSE;
  }
  aec->core_input.input_info.first_flagged_entry = AEC_INPUT_PARAM_MAX;

  return TRUE;
}

float aec_biz_map_iso_to_real_gain(void *aec_obj, uint32_t iso)
{
  q3a_core_result_type rc = Q3A_CORE_RESULT_SUCCESS;
  uint32 iso_value = 0;
  aec_biz_t* aec = (aec_biz_t*)aec_obj;
  if(!aec){
    AEC_ERR("Invalid input: %p",aec);
    return 0;
  }

  if(iso < AEC_ISO_100)
    iso = AEC_ISO_100;

  if (iso >= AEC_ISO_MAX){
    iso_value = (uint32)iso;
  } else {
    uint32 temp_iso_multiplier = iso - AEC_ISO_100;
    temp_iso_multiplier = (1 << temp_iso_multiplier);
    iso_value = temp_iso_multiplier * 100;
  }

  aec_core_get_info_type get_param_info;
  get_param_info.param_type = AEC_GET_REAL_GAIN_FROM_ISO;
  get_param_info.u.iso_value = iso_value;
  aec_core_get_output_type get_param_output;
  if (aec->aec_algo_ops.get_parameters) {
    rc = aec->aec_algo_ops.get_parameters(aec->handle, &get_param_info, &get_param_output);
  } else {
    rc = Q3A_CORE_RESULT_FAILED;
  }
  if(rc != Q3A_CORE_RESULT_SUCCESS){
    AEC_ERR("aec get param failed");
    return 0;
  }

  return get_param_output.u.real_gain;
}

/**
 * aec_biz_clear_iface_ops
 *
 * @aec_object: structure with function pointers to be assign
 *
 * Clear interface by setting all pointers to NULL
 *
 * Return: void
 **/
void aec_biz_clear_iface_ops(aec_object_t *aec_object)
{
  aec_object->set_parameters = NULL;
  aec_object->get_parameters = NULL;
  aec_object->process = NULL;
  aec_object->init = NULL;
  aec_object->deinit = NULL;
  aec_object->iso_to_real_gain = NULL;
  aec_object->get_version = NULL;
  return;
}

/**
 * aec_biz_clear_algo_ops
 *
 * @aec_algo_ops: structure with function pointers to algo lib
 *
 * Clear interface by setting all pointers to NULL
 *
 * Return: void
 **/
void aec_biz_clear_algo_ops(aec_biz_algo_ops_t *aec_algo_ops)
{
  aec_algo_ops->set_parameters = NULL;
  aec_algo_ops->get_parameters = NULL;
  aec_algo_ops->process = NULL;
  aec_algo_ops->init = NULL;
  aec_algo_ops->deinit = NULL;
  aec_algo_ops->get_version = NULL;
  aec_algo_ops->print_change_id = NULL;
  aec_algo_ops->set_log = NULL;
  return;
}

/** aec_biz_dlsym
 *
 *    @lib_handler: Handler to library
 *    @fn_ptr: Function to initialize
 *    @fn_name: Name of symbol to find in library
 *
 * Return: TRUE on success
 **/
static boolean aec_biz_dlsym(void *lib_handler, void *fn_ptr,
  const char *fn_name)
{
  char *error = NULL;

  if (NULL == lib_handler || NULL == fn_ptr) {
    AEC_ERR("Error Loading %s", fn_name);
    return FALSE;
  }

  *(void **)(fn_ptr) = dlsym(lib_handler, fn_name);
  if (!fn_ptr) {
    error = (char *)dlerror();
    AEC_ERR("Error: %s", error);
    return FALSE;
  }

  AEC_LOW("Loaded %s %p", fn_name, fn_ptr);
  return TRUE;
}

static void aec_biz_init_data(aec_biz_t *aec)
{
  aec->bestshot_off_data.curr_mode = AEC_BESTSHOT_OFF;
  aec->bestshot_off_data.metering_type = AEC_METERING_CENTER_WEIGHTED;
  aec->bestshot_off_data.exp_comp_val = 0;
  aec->bestshot_off_data.iso_mode = AEC_ISO_AUTO;
  aec->bestshot_off_data.iso_value = 0;
}

void *aec_biz_init(void *libptr)
{
  q3a_core_result_type rc = Q3A_CORE_RESULT_SUCCESS;
  aec_biz_t  *aec_biz = NULL;

  do {
    aec_biz = (aec_biz_t *)calloc(1, sizeof(aec_biz_t));
    if (NULL == aec_biz) {
      AEC_ERR("malloc failed");
      break;
    }
    aec_biz_init_data(aec_biz);

    aec_biz->core_input.input_info.first_flagged_entry = AEC_INPUT_PARAM_MAX;

    dlerror(); /* Clear previous errors */
    if (!aec_biz_dlsym(libptr, &aec_biz->aec_algo_ops.set_parameters,
      "aec_set_param")) {
      break;
    }
    if (!aec_biz_dlsym(libptr, &aec_biz->aec_algo_ops.get_parameters,
      "aec_get_param")) {
      break;
    }
    if (!aec_biz_dlsym(libptr, &aec_biz->aec_algo_ops.process,
      "aec_process")) {
      break;
    }
    if (!aec_biz_dlsym(libptr, &aec_biz->aec_algo_ops.init,
      "aec_init")) {
      break;
    }
    if (!aec_biz_dlsym(libptr, &aec_biz->aec_algo_ops.deinit,
      "aec_deinit")) {
      break;
    }
    if (!aec_biz_dlsym(libptr, &aec_biz->aec_algo_ops.get_version,
      "get_3A_version")) {
      break;
    }
    if (!aec_biz_dlsym(libptr, &aec_biz->aec_algo_ops.print_change_id,
      "q3a_core_print_change_id")) {
      break;
    }

    if (!aec_biz_dlsym(libptr, &aec_biz->aec_algo_ops.set_log,
      "q3a_core_set_log_level")) {
      break;
    }
    /* set logging for core */
    aec_biz->aec_algo_ops.set_log();

   if (aec_biz->aec_algo_ops.print_change_id)
     aec_biz->aec_algo_ops.print_change_id();

    rc = aec_biz->aec_algo_ops.init(&aec_biz->handle);
    if (rc != Q3A_CORE_RESULT_SUCCESS) {
      AEC_ERR("aec core init failed");
      break;
    }

    return aec_biz;
  } while (0);

  /* Handling error */
  aec_biz_clear_algo_ops(&aec_biz->aec_algo_ops);
  if (aec_biz) {
    free(aec_biz);
    aec_biz = NULL;
  }
  return NULL;
}

void aec_biz_destroy(void *aec_obj)
{
  aec_biz_t *aec_biz = (aec_biz_t*)aec_obj;

  if (aec_biz) {
    if (aec_biz->handle && aec_biz->aec_algo_ops.deinit) {
      aec_biz->aec_algo_ops.deinit(aec_biz->handle);
    }
    aec_biz_clear_algo_ops(&aec_biz->aec_algo_ops);
    free(aec_biz);
  }
}

/**
 * aec_biz_get_version
 *
 * @version: Output structure were the version is written
 *
 * Query the AEC library for the version and write data in @version
 *
 * Return: void
 **/
boolean aec_biz_get_version(void *aec_obj, Q3a_version_t *version)
{
  aec_biz_t *aec_biz = (aec_biz_t*)aec_obj;
  if (!(aec_biz && aec_biz->aec_algo_ops.get_version)) {
    AEC_ERR("error: get version function not available");
    return FALSE;
  }
  /* Get 3A version number */
  aec_biz->aec_algo_ops.get_version((void *)version);

  return TRUE;
}

/**
 * aec_biz_load_function
 *
 * @aec_object: structure with function pointers to be assign
 *
 * Return: Handler to AEC interface library
 **/
void * aec_biz_load_function(aec_object_t *aec_object)
{
  void *q3a_handler = NULL;

  if (!aec_object) {
    return NULL;
  }

  q3a_handler = dlopen("libmmcamera2_q3a_core.so", RTLD_NOW);
  if (!q3a_handler) {
    AEC_ERR("dlerror: %s", dlerror());
    return NULL;
  }

  aec_object->set_parameters = aec_biz_set_param;
  aec_object->get_parameters = aec_biz_get_param;
  aec_object->process = aec_biz_process;
  aec_object->init = aec_biz_init;
  aec_object->deinit = aec_biz_destroy;
  aec_object->iso_to_real_gain = aec_biz_map_iso_to_real_gain;
  aec_object->get_version = aec_biz_get_version;

  return q3a_handler;
}

/**
 * aec_biz_unload_function
 *
 * @aec_object: structure with function pointers to be assign
 * @lib_handler: Handler to the algo library
 *
 * Return: void
 **/
void aec_biz_unload_function(aec_object_t *aec_object, void *lib_handler)
{
  if (lib_handler) {
    dlclose(lib_handler);
  }
  aec_biz_clear_iface_ops(aec_object);

  return;
}
