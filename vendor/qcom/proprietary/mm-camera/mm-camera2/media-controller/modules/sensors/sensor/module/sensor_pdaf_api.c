/* sensor_pdaf_api.c
 *
 * Copyright (c) 2015 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
*/
#include "modules.h"
#include "sensor.h"
#include "sensor_pdaf_api.h"
#include "pdaf_lib.h"
#include "pdaf_api.h"
#include "pdaf_camif_api.h"

#define INTERPOLATION_RATIO(p0, p1, p_tg) \
  ((p0 == p1) ? 0.5 : ((float)abs(p_tg-p0)/abs(p1-p0)))

static void pdaf_rm_pd_offset(void *data){
  pdaf_sensor_native_info_t       *native_pattern
   = (pdaf_sensor_native_info_t *) data;
  unsigned int i = 0;
  for ( i = 0; i< native_pattern->block_pattern.pix_count; i++)
  {
    native_pattern->block_pattern.pix_coords[i].x -=
     native_pattern->block_pattern.pd_offset_horizontal;
    native_pattern->block_pattern.pix_coords[i].y -=
     native_pattern->block_pattern.pd_offset_vertical;
  }
}

boolean pdaf_get_native_pattern(void *sctrl, void *data){
  int32_t                         rc    = SENSOR_SUCCESS;
  sensor_ctrl_t                   *ctrl = (sensor_ctrl_t *)sctrl;
  pdaf_lib_t                      *params;
  sensor_imaging_pixel_array_size *array_size_info;
  sensor_lib_t                    *sensor_lib;
  pdaf_sensor_native_info_t       *native_pattern
   = (pdaf_sensor_native_info_t *) data;
  struct sensor_lib_out_info_t    *out_info;

  RETURN_ERROR_ON_NULL(ctrl);
  RETURN_ERROR_ON_NULL(native_pattern);

  sensor_lib = ctrl->lib_params->sensor_lib_ptr;
  params     = &sensor_lib->pdaf_config;
  array_size_info = &sensor_lib->pixel_array_size_info;
  out_info       = &sensor_lib->out_info_array.out_info[ctrl->s_data->cur_res];
  /* pack native pattern */
  memcpy( &native_pattern->block_pattern,
          &params->sensor_native_pattern_info[ctrl->s_data->cur_res].block_pattern,
          sizeof(native_pattern->block_pattern));
  native_pattern->orientation = params->orientation;

  /* MIPI readout size for pd gain calibration
     assume pd gain calibration is done on the active pixel region
  */
  native_pattern->ori_image_height = array_size_info->active_array_size.height;
  native_pattern->ori_image_width = array_size_info->active_array_size.width;
  /* no isp crop involved
     stats streams only depend on hw register setting
     crop region from effective pixels
  */
  native_pattern->crop_region.x = out_info->offset_x;
  native_pattern->crop_region.y = out_info->offset_y;
  native_pattern->crop_region.w = out_info->x_output;
  native_pattern->crop_region.h = out_info->y_output;

  native_pattern->cur_image_height = native_pattern->crop_region.h;
  native_pattern->cur_image_width = native_pattern->crop_region.w;

  native_pattern->block_count_horizontal =
   params->sensor_native_pattern_info[ctrl->s_data->cur_res].block_count_horizontal;
  native_pattern->block_count_vertical =
   params->sensor_native_pattern_info[ctrl->s_data->cur_res].block_count_vertical;

  native_pattern->buffer_data_type =
   params->buffer_block_pattern_info[ctrl->s_data->cur_res].buffer_data_type;

  return rc;
}

boolean pdaf_deinit(void *sctrl){
  int32_t            rc        = SENSOR_SUCCESS;
  sensor_ctrl_t      *ctrl     = (sensor_ctrl_t *)sctrl;
  pdaf_lib_t          *params;
  sensor_custom_API_t *sensor_custom_API = NULL;
  struct sensor_lib_out_info_t  *out_info;
  sensor_lib_t                  *sensor_lib;
  RETURN_ERROR_ON_NULL(sctrl);
  sensor_lib         = ctrl->lib_params->sensor_lib_ptr;
  params             = &sensor_lib->pdaf_config;
  RETURN_ERROR_ON_NULL(params);
  sensor_custom_API = &ctrl->lib_params->sensor_custom_API;
  out_info = &sensor_lib->out_info_array.out_info[ctrl->s_data->cur_res];

  if(!out_info->is_pdaf_supported)
   return rc;

  /* for thrid party OEM */
  if (sensor_custom_API->pdlib_deinit)
    rc = sensor_custom_API->pdlib_deinit();
  else if(params->vendor_id == QTI)
  {
    /* avoid multiple free */
    if (ctrl->pd_camif_handle && ctrl->pd_handle)
    {
    rc = PDAF_CAMIF_deinit(ctrl->pd_camif_handle);
    if ( rc != PDAF_LIB_OK)
    {
      SERR("PDAF CAMIF deinit failed %d", -rc);
    }

    rc = PDAF_PhaseDetection_deinit(ctrl->pd_handle);
    if ( rc != PDAF_LIB_OK)
    {
      SERR("PDAF PhaseDetection deinit failed %d", -rc);
    }
     }
  }
    /* reset */
    ctrl->pd_camif_handle = ctrl->pd_handle = 0;
  return -rc;
}

boolean pdaf_set_window_update(void *sctrl, void *data){
  sensor_ctrl_t                *ctrl = (sensor_ctrl_t *)sctrl;
  pdaf_lib_t                   *params;
  pdaf_window_configure_t      *window = (pdaf_window_configure_t *)data;
  struct sensor_lib_out_info_t *out_info;
  sensor_lib_t                 *sensor_lib;
  int32_t                      rc = SENSOR_SUCCESS;
  RETURN_ERROR_ON_NULL(sctrl);
  RETURN_ERROR_ON_NULL(data);
  sensor_lib         = ctrl->lib_params->sensor_lib_ptr;
  params             = &sensor_lib->pdaf_config;
  window             = &params->window_configure;
  out_info = &sensor_lib->out_info_array.out_info[ctrl->s_data->cur_res];

  if(!out_info->is_pdaf_supported)
   return rc;

  switch (window->pdaf_sw_window_mode)
  {
   case  FLOAT_WINDOW:
    /* calling event is responsible for matching data size */
    memcpy(&window->float_window_configure, data,
      sizeof(window->float_window_configure));
    break;
   case FIXED_GRID_WINDOW:
    memcpy(&window->fixed_grid_window_configure, data,
      sizeof(window->fixed_grid_window_configure));
    break;
   default:
    SERR("unsopported window type");
    return SENSOR_FAILURE;
   }
  return rc;
}

boolean pdaf_set_buf_data_type(void *sctrl, void *data){
  int32_t                         rc = SENSOR_SUCCESS;
  sensor_ctrl_t                   *ctrl;
  sensor_lib_t                    *sensor_lib;
  pdaf_lib_t                      *params;
  int16_t                         cur_res;
  RETURN_ERROR_ON_NULL(sctrl);

  ctrl       = (sensor_ctrl_t *)sctrl;
  sensor_lib = ctrl->lib_params->sensor_lib_ptr;
  params     = &sensor_lib->pdaf_config;
  cur_res    = ctrl->s_data->cur_res;

  params->buffer_block_pattern_info[cur_res].buffer_data_type =
    *(pdaf_buffer_data_type_t *)data;

  return rc;
}

boolean pdaf_init(void *sctrl, void *data){
  int32_t                         rc = SENSOR_SUCCESS;
  pdaf_camif_init_param_t         input_camif;
  pdaf_init_param_t               input;
  pdaf_init_info_t                *s_pdaf = NULL;
  sensor_ctrl_t                   *ctrl;
  pdaf_lib_t                      *params;
  sensor_lib_t                    *sensor_lib;
  unsigned int                    i;
  int16_t                         cur_res;
  struct sensor_lib_out_info_t    *out_info;
  sensor_custom_API_t             *sensor_custom_API;
  RETURN_ERROR_ON_NULL(sctrl);
  RETURN_ERROR_ON_NULL(data);

  ctrl       = (sensor_ctrl_t *)sctrl;
  s_pdaf     = (pdaf_init_info_t *)data;
  sensor_lib = ctrl->lib_params->sensor_lib_ptr;
  params     = &sensor_lib->pdaf_config;
  sensor_custom_API = &ctrl->lib_params->sensor_custom_API;
  cur_res           = ctrl->s_data->cur_res;
  out_info        = &sensor_lib->out_info_array.out_info[ctrl->s_data->cur_res];

  if(!out_info->is_pdaf_supported)
   return rc;

  /* for thrid party OEM */
  if (sensor_custom_API->pdlib_init)
    rc = sensor_custom_API->pdlib_init(params, s_pdaf);
  else if(params->vendor_id == QTI)
  /* for qti pdaf solution */
  {
    /* qti solution based on valid pd calibration data */
    RETURN_ERROR_ON_NULL(s_pdaf->pdaf_gain_ptr);
    /* avoid multiple init */
    if ( ctrl->pd_camif_handle && ctrl->pd_handle)
      return rc;
    /* clean up memory */
    memset(&input_camif, 0, sizeof(pdaf_camif_init_param_t));
    memset(&input, 0, sizeof(pdaf_init_param_t));

    pdaf_get_native_pattern(sctrl, &input_camif.sensor_native_info);

    pdaf_rm_pd_offset(&input_camif.sensor_native_info);

    /* pack buffer pattern */
    /* T3 buffer pattern is depend by isp configuration */
    switch(params->buffer_block_pattern_info[cur_res].buffer_type)
    {
     case PDAF_BUFFER_FLAG_SPARSE:{
      if (!s_pdaf->isp_config)
      {
       SERR("invalid T3 buffer pattern");
       return SENSOR_FAILURE;
      }
      /* T3 overwrite the buffer info for camif configuration */
      memcpy(&input_camif.buffer_data_info,
             s_pdaf->isp_config,
             sizeof(input_camif.buffer_data_info));
      input_camif.buffer_data_info.buffer_data_type =
        params->buffer_block_pattern_info[cur_res].buffer_data_type;
      input_camif.buffer_data_info.buffer_type      =
        params->buffer_block_pattern_info[cur_res].buffer_type;
     }
     break;
     case PDAF_BUFFER_FLAG_SEQUENTIAL_LINE:
      /* T2 buffer pattern is self-contained in header description */
      input_camif.buffer_data_info.buffer_block_pattern_left =
       params->buffer_block_pattern_info[cur_res].block_pattern;
      input_camif.buffer_data_info.camif_left_buffer_stride  =
       params->buffer_block_pattern_info[cur_res].stride;
      input_camif.buffer_data_info.buffer_data_type          =
        params->buffer_block_pattern_info[cur_res].buffer_data_type;
      input_camif.buffer_data_info.buffer_type               =
        params->buffer_block_pattern_info[cur_res].buffer_type;
      input_camif.buffer_data_info.camif_left_buffer_width   =
        input_camif.sensor_native_info.block_count_horizontal *
        input_camif.buffer_data_info.buffer_block_pattern_left.block_dim.width;
      input_camif.buffer_data_info.camif_buffer_height       =
        input_camif.sensor_native_info.block_count_vertical *
        input_camif.buffer_data_info.buffer_block_pattern_left.block_dim.height;
      break;
     default:
      SHIGH("unsupported buffer buffer type");
    }

    ctrl->pd_camif_handle = PDAF_CAMIF_init(&input_camif);

    /* fill in QC pd input */
    pdaf_get_native_pattern(sctrl, &input.native_pattern_info);

    pdaf_rm_pd_offset(&input.native_pattern_info);
    input.p_calibration_para   = s_pdaf->pdaf_gain_ptr;
    /* not the exactly data in chromatix
       estimation of the module black level
    */
    input.black_level          = params->black_level;
    input.cali_version         = params->cali_version;
    ctrl->pd_handle = PDAF_PhaseDetection_init(&input);

    if (!ctrl->pd_handle ||!ctrl->pd_camif_handle)
    {
     SERR("PDAF_init failed");
     return SENSOR_FAILURE;
    }
  }
  return rc;
}

boolean pdaf_calc_defocus(void *sctrl, void *data){
  int32_t                      rc = SENSOR_SUCCESS;
  pdaf_lib_t                   *params = NULL;
  sensor_ctrl_t                *ctrl = (sensor_ctrl_t *)sctrl;
  pdaf_camif_param_t           input_camif;
  pdaf_param_t                 input;
  pdaf_camif_output_data_t     output_camif;
  pdaf_output_data_t           output;
  pdaf_params_t                *s_pdaf = NULL;
  sensor_custom_API_t          *sensor_custom_API = NULL;
  struct sensor_lib_out_info_t  *out_info;
  sensor_lib_t                  *sensor_lib;

  RETURN_ERROR_ON_NULL(ctrl);
  RETURN_ERROR_ON_NULL(data);
  s_pdaf = (pdaf_params_t *)data;
  sensor_lib = ctrl->lib_params->sensor_lib_ptr;
  params = &sensor_lib->pdaf_config;
  sensor_custom_API = &ctrl->lib_params->sensor_custom_API;
  out_info = &sensor_lib->out_info_array.out_info[ctrl->s_data->cur_res];
  if(!out_info->is_pdaf_supported)
   return rc;

  /* for thrid party OEM */
  if (sensor_custom_API->pdlib_get_defocus)
    rc = sensor_custom_API->pdlib_get_defocus(s_pdaf, &output);
  else if (params->vendor_id == QTI)
  /* for qti pdaf solution */
  {
    /* abort if any of pd handle is null */
    if ( !ctrl->pd_camif_handle || ! ctrl->pd_handle)
      return SENSOR_FAILURE;
    memset(&input, 0, sizeof(input));
    memset(&input_camif, 0, sizeof(input_camif));
    memset(&output_camif, 0, sizeof(output_camif));
    memset(&output, 0, sizeof(output));
    /* isp always just sent one buffer */
    input_camif.p_left = s_pdaf->pd_stats;
    input_camif.p_right = NULL;
    memcpy(&input_camif.window_configure, &params->window_configure,
         sizeof(pdaf_window_configure_t));
    rc = PDAF_CAMIF_getPDAF(ctrl->pd_camif_handle, &input_camif, &output_camif);
    if (rc != PDAF_LIB_OK)
    {
     SERR("PDAF_CAMIF_getPDAF failed");
     return -rc;
    }
    input.camif_out = output_camif;
    /* do not have any info for ROI */
    /* current gain is applied on next frame
       prev_gain takes effect on current frame
    */
    input.image_analog_gain = ctrl->s_data->prev_gain;
    input.defocus_confidence_th = params->defocus_confidence_th;
    memcpy(&input.window_configure, &params->window_configure,
         sizeof(pdaf_window_configure_t));
    rc = PDAF_PhaseDetection(ctrl->pd_handle, &input, &output);
    if ( rc != PDAF_LIB_OK)
    {
     SERR("PDAF_PhaseDetection failed");
     return -rc;
    }
  }

  /*same way: populate defocus value back */
  memcpy(s_pdaf->defocus, output.defocus, sizeof(s_pdaf->defocus));
  switch(params->window_configure.pdaf_sw_window_mode){
   case FLOAT_WINDOW:
    /*
      do not set feedback status to be ture
      af hal will not connect to hybrid-framework-pdaf-algo
    */
    SHIGH("unsupported window configuration");
    break;
   case FIXED_GRID_WINDOW:
     s_pdaf->x_win_num =
      params->window_configure.fixed_grid_window_configure.window_number_hori;
     s_pdaf->y_win_num =
      params->window_configure.fixed_grid_window_configure.window_number_ver;
     s_pdaf->x_offset =
      params->window_configure.fixed_grid_window_configure.af_fix_window.pdaf_address_start_hori;
     s_pdaf->y_offset =
      params->window_configure.fixed_grid_window_configure.af_fix_window.pdaf_address_start_ver;
     s_pdaf->status = TRUE;
     break;
   default:
    SERR("wrong such pd window configuration");
    break;
  }

  return rc;
}

boolean pdaf_get_type(void *sctrl, void *data){
  int32_t                      rc = SENSOR_SUCCESS;
  pdaf_lib_t                   *params = NULL;
  sensor_ctrl_t                *ctrl = (sensor_ctrl_t *)sctrl;
  int32_t                      *buffer_type = (int32_t *)data;
  sensor_custom_API_t          *sensor_custom_API = NULL;
  struct sensor_lib_out_info_array *out_info_array_ptr = NULL;
  RETURN_ON_NULL(sctrl);
  RETURN_ON_NULL(data);
  sensor_custom_API = &ctrl->lib_params->sensor_custom_API;
  params = &ctrl->lib_params->sensor_lib_ptr->pdaf_config;
  out_info_array_ptr = &ctrl->lib_params->sensor_lib_ptr-> out_info_array;

  /* default status as invalid */
  *buffer_type = PDAF_BUFFER_FLAG_INVALID;
  if (out_info_array_ptr->out_info[ctrl->s_data->cur_res].is_pdaf_supported)
   *buffer_type =
     params->buffer_block_pattern_info[ctrl->s_data->cur_res].buffer_type;
  SHIGH("buffer_type %d", *buffer_type);
  return rc;
}

/** pdaf_parse_pd:
 *    @arg1: custom
 *    @arg2: pdaf_params_t
 *
 *  Return: 0 on success
 *    -1 on failure
 *
 *  This function parses phase difference data from sensor HW,
 *  containing flexible window flag, fixed window mode,
 *  confidence level and phase difference
 **/

int32_t pdaf_parse_pd(sensor_stats_format_t format,
 pdaf_params_t *s_pdaf)
{
  uint8_t    *buf;
  uint32_t     i = 0;
  uint8_t    flex_win_flag = 0;
  uint8_t    win_mode = 0;
  uint32_t     win_num;
  int32_t    sign_ext;
  int32_t    phase_diff_raw;

  RETURN_ERROR_ON_NULL(s_pdaf);
  RETURN_ERROR_ON_NULL(s_pdaf->pd_stats);

  buf = (uint8_t *)s_pdaf->pd_stats;

  if(format == SENSOR_STATS_RAW10_8B_CONF_10B_PD) {
    flex_win_flag = buf[0];
    win_mode = buf[1] >> 6; /*window mode 0: 16x12, 1: 8x6, 2: flexible */

    SLOW("PDAF parsing PD stats: win_flag: %d, area_mode: %d",
    flex_win_flag, win_mode);

    if(win_mode == 0)
      win_num = 192;
    else if(win_mode == 1)
      win_num= 48;
    else
      win_num = 8; /* max flexible window */

    buf += 5;

    for(i = 0;i < win_num;i++) {
    s_pdaf->defocus[i].df_conf_level = buf[0];
    phase_diff_raw = buf[1] << 2 | buf[2] >> 6;
    sign_ext = (buf[1] & 0x80) ? 0xFFFFFC00 : 0;/* s5.4: 1st bit is sign bit */
    phase_diff_raw |= sign_ext;
    s_pdaf->defocus[i].phase_diff = (float)phase_diff_raw / 16.0f;
    SHIGH("PDAF: window %d, conf. level:%d, phase diff: %f", i,
    s_pdaf->defocus[i].df_conf_level,
    s_pdaf->defocus[i].phase_diff);

    if(win_num == 48 && (i % 8) == 7) {
    /* HW limitation: need to skip alternate rows (8 windows) */
    buf += 45;
    }
    else
    buf += 5;
    }
  } else if(format == SENSOR_STATS_RAW10_11B_CONF_11B_PD) {
    flex_win_flag = buf[0];
    win_mode = buf[1] >> 6; /*window mode 0: 16x12, 1: 8x6, 2: flexible */

    SLOW("PDAF parsing PD stats: win_flag: %d, area_mode: %d",
    flex_win_flag, win_mode);

    if(win_mode == 0)
      win_num = 192;
    else if(win_mode == 1)
      win_num= 48;
    else
      win_num = 8; /* max flexible window */

    buf += 10;

    for(i = 0;i < win_num;i++) {
      s_pdaf->defocus[i].df_conf_level = buf[0] << 3 | buf[1] >> 5;
      phase_diff_raw = (buf[1] & 0x1F) << 6 | buf[2] >> 2;
      /* s6.4 sign extension: 11 bit to 32 bit */
      phase_diff_raw = (phase_diff_raw << 21) >> 21;

      s_pdaf->defocus[i].phase_diff = (float)phase_diff_raw / 16.0f;
      SHIGH("PDAF: window %d, conf. level:%d, phase diff: %f", i,
      s_pdaf->defocus[i].df_conf_level,
      s_pdaf->defocus[i].phase_diff);

      if(win_num == 48 && (i % 8) == 7) {
        /* HW limitation: need to skip alternate rows (8 windows) */
        buf += 45;
      } else
        buf += 5;
    }
  } else {
    SHIGH("unsupported PD data type");
  }

  return 0;
}

/** pdaf_get_defocus_with_pd_data:
 *    @arg1: PdLibInputData_t
 *    @arg2: PdLibOutputData_t
 *
 *  Return: 0 on success
 *    -1 on failure
 *
 *  This function gets defocus with PD data from sensor HW
 *
 **/
signed long pdaf_get_defocus_with_pd_data(void *arg1, void *arg2)
{
  PdLibInputData_t *input = (PdLibInputData_t *)arg1;
  PdLibOutputData_t *output = (PdLibOutputData_t *)arg2;
  PdLibPoint_t p0 = {0, 0}, p1 = {0, 0}, p2 = {0, 0}, p3 = {0, 0}, p_tg = {0, 0};
  unsigned int i = 0, x_sta_idx = 0, x_end_idx = 0, y_sta_idx = 0, y_end_idx = 0;
  int d0 = 0, d1 = 0, d2 = 0, d3 = 0, d_tg = 0;
  int temp0 = 0, temp1 = 0;
  int pd_area_x = 0, pd_area_y = 0, pd_offset_x = 0, pd_offset_y = 0;
  unsigned long threshold = 0;

  RETURN_ERR_ON_NULL(input, -1);
  RETURN_ERR_ON_NULL(output, -1);
  RETURN_ERR_ON_NULL(input->p_SlopeData, -1);
  RETURN_ERR_ON_NULL(input->p_OffsetData, -1);
  RETURN_ERR_ON_NULL(input->p_XAddressKnotSlopeOffset, -1);
  RETURN_ERR_ON_NULL(input->p_YAddressKnotSlopeOffset, -1);

  /* Find 4 adjacent data points;
   * data points is at the center of each grid.
   * Define p0 as the nearest point to p_tg
   *    p0 --------- p1
   *    |             |
   *    |    p_tg     |
   *    |             |
   *    p2 --------- p3
   */
  p_tg.x = (input->XAddressOfWindowStart + input->XAddressOfWindowEnd) / 2;
  p_tg.y = (input->YAddressOfWindowStart + input->YAddressOfWindowEnd) / 2;

  pd_area_x = input->p_XAddressKnotSlopeOffset[1]
    - input->p_XAddressKnotSlopeOffset[0];
  pd_area_y = input->p_YAddressKnotSlopeOffset[1]
    - input->p_YAddressKnotSlopeOffset[0];
  pd_offset_x = input->p_XAddressKnotSlopeOffset[0];
  pd_offset_y = input->p_YAddressKnotSlopeOffset[0];


  x_sta_idx = (p_tg.x - pd_offset_x) / pd_area_x;
  y_sta_idx = (p_tg.y - pd_offset_y) / pd_area_y;

  p0.x = input->p_XAddressKnotSlopeOffset[x_sta_idx] + pd_area_x / 2;
  p0.y = input->p_YAddressKnotSlopeOffset[y_sta_idx] + pd_area_y / 2;
  d0 = input->p_SlopeData[x_sta_idx + input->XKnotNumSlopeOffset * y_sta_idx];

  if(abs(p_tg.x - p0.x) < pd_area_x*0.01 &&
    abs(p_tg.y - p0.y) < pd_area_y*0.01) {
    /* target point at avaible data point, allow 1% diff */
    d_tg = d0;
  } else {
    if(p_tg.x < p0.x && x_sta_idx > 0)
      x_end_idx = x_sta_idx - 1;
    else if(p_tg.x > p0.x &&
      x_sta_idx < (unsigned)(input->XKnotNumSlopeOffset - 1))
      x_end_idx = x_sta_idx + 1;
    else
      x_end_idx = x_sta_idx;

    if(p_tg.y < p0.y && y_sta_idx > 0)
      y_end_idx = y_sta_idx - 1;
    else if(p_tg.y > p0.y &&
      y_sta_idx < (unsigned)(input->YKnotNumSlopeOffset - 1))
      y_end_idx = y_sta_idx + 1;
    else
      y_end_idx = y_sta_idx;

    if(x_sta_idx >= input->XKnotNumSlopeOffset ||
      x_end_idx >= input->XKnotNumSlopeOffset ||
      y_sta_idx >= input->YKnotNumSlopeOffset ||
      y_end_idx >= input->YKnotNumSlopeOffset) {
      SERR("failed: invalid PD window");
      return -1;
    }

    p2.x =
      input->p_XAddressKnotSlopeOffset[x_sta_idx] + pd_area_x / 2;
    p1.x = p3.x =
      input->p_XAddressKnotSlopeOffset[x_end_idx] + pd_area_x / 2;
    p1.y =
      input->p_YAddressKnotSlopeOffset[y_sta_idx] + pd_area_y / 2;
    p2.y = p3.y =
      input->p_YAddressKnotSlopeOffset[y_end_idx] + pd_area_y / 2;

    d0 = input->p_SlopeData[x_sta_idx + input->XKnotNumSlopeOffset*y_sta_idx];
    d1 = input->p_SlopeData[x_end_idx + input->XKnotNumSlopeOffset*y_sta_idx];
    d2 = input->p_SlopeData[x_sta_idx + input->XKnotNumSlopeOffset*y_end_idx];
    d3 = input->p_SlopeData[x_end_idx + input->XKnotNumSlopeOffset*y_end_idx];

    /* Bilinear interpolation */
    temp0 = INTERPOLATION_RATIO(p0.x, p1.x, p_tg.x) * d1 +
      INTERPOLATION_RATIO(p1.x, p0.x, p_tg.x) * d0;

    temp1 = INTERPOLATION_RATIO(p0.x, p1.x, p_tg.x) * d3 +
      INTERPOLATION_RATIO(p1.x, p0.x, p_tg.x) * d2;

    d_tg = INTERPOLATION_RATIO(p0.y, p2.y, p_tg.y) * temp1 +
      INTERPOLATION_RATIO(p2.y, p0.y, p_tg.y) * temp0;
  }
  /* Give defocus and confidence level */
  if(input->p_DefocusOKNGThrLine)
    threshold = input->p_DefocusOKNGThrLine[0].p_Confidence[0];

  if(threshold == 0)
    threshold = 255;

  output->Defocus = input->PhaseDifference * d_tg;
  output->DefocusConfidenceLevel = 1024 * input->ConfidenceLevel / threshold;
  output->DefocusConfidence = (input->ConfidenceLevel < threshold) ? 0 : 1;
  output->PhaseDifference = input->PhaseDifference;

  SLOW("x idx: %d %d, y idx: %d %d",x_sta_idx, x_end_idx, y_sta_idx, y_end_idx);
  SLOW("p0: %d %d, p1: %d %d, p2: %d %d, p3: %d %d, p_tg: %d %d",
    p0.x, p0.y,p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, p_tg.x, p_tg.y);
  SLOW("4 data %f %f %f %f, DCC: %f",
    d0/1024.0, d1/1024.0,d2/1024.0,d3/1024.0, d_tg/1024.0);

  return 0;
}

