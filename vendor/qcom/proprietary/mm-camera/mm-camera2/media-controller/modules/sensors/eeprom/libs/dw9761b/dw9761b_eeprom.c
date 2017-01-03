/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

============================================================================*/
#include "dw9761b_eeprom.h"

/** dw9761b_eeprom_get_calibration_items:
 *    @e_ctrl: point to sensor_eeprom_data_t of the eeprom device
 *
 * Get calibration capabilities and mode items.
 *
 * This function executes in eeprom module context
 *
 * Return: void.
 **/
void dw9761b_eeprom_get_calibration_items(void *e_ctrl)
{
  sensor_eeprom_data_t *ectrl = (sensor_eeprom_data_t *)e_ctrl;
  eeprom_calib_items_t *e_items = &(ectrl->eeprom_data.items);

  e_items->is_wbc = awb_present ? TRUE : FALSE;
  e_items->is_afc = af_present ? TRUE : FALSE;
  e_items->is_lsc = lsc_present ? TRUE : FALSE;
  e_items->is_dpc = FALSE;
  e_items->is_insensor = FALSE;
  e_items->is_ois = FALSE;

  SLOW("is_wbc:%d,is_afc:%d,is_lsc:%d,is_dpc:%d,is_insensor:%d,\
  is_ois:%d",e_items->is_wbc,e_items->is_afc,
    e_items->is_lsc,e_items->is_dpc,e_items->is_insensor,
    e_items->is_ois);

}

/** dw9761b_eeprom_format_wbdata:
 *    @e_ctrl: point to sensor_eeprom_data_t of the eeprom device
 *
 * Format the data structure of white balance calibration
 *
 * This function executes in eeprom module context
 *
 * Return: void.
 **/
static void dw9761b_eeprom_format_wbdata(sensor_eeprom_data_t *e_ctrl)
{
  unsigned char flag;
  module_info_t *module_info;
  awb_data_t    *wb;
  float         r_over_gr, b_over_gb, gr_over_gb;
  int           i;

  SDBG("Enter");
  /* Check validity */
  flag = e_ctrl->eeprom_params.buffer[AWB_FLAG_OFFSET];
  if (flag != VALID_FLAG) {
    awb_present = FALSE;
    SERR("AWB : empty or invalid data");
    return;
  }
  awb_present = TRUE;
  /* Print module info */
  module_info = (module_info_t *)
    (e_ctrl->eeprom_params.buffer + MODULE_INFO_OFFSET);
  SLOW("Module ID : 0x%x", module_info->id);
  SLOW("Y/M/D : %d/%d/%d",
    module_info->year, module_info->month, module_info->day);

  /* Get AWB data */
  wb = (awb_data_t *)(e_ctrl->eeprom_params.buffer + AWB_OFFSET);

  r_over_gr = ((float)((wb->r_over_gr_h << 8) | wb->r_over_gr_l)) / QVALUE;
  b_over_gb = ((float)((wb->b_over_gb_h << 8) | wb->b_over_gb_l)) / QVALUE;
  gr_over_gb = ((float)((wb->gr_over_gb_h << 8) | wb->gr_over_gb_l)) / QVALUE;

  SLOW("AWB : r/gr = %f", r_over_gr);
  SLOW("AWB : b/gb = %f", b_over_gb);
  SLOW("AWB : gr/gb = %f", gr_over_gb);

  for (i = 0; i < AGW_AWB_MAX_LIGHT; i++) {
    e_ctrl->eeprom_data.wbc.r_over_g[i] = r_over_gr;
    e_ctrl->eeprom_data.wbc.b_over_g[i] = b_over_gb;
  }
  e_ctrl->eeprom_data.wbc.gr_over_gb = gr_over_gb;
  SDBG("Exit");
}

/** dw9761b_eeprom_format_lensshading:
 *    @e_ctrl: point to sensor_eeprom_data_t of the eeprom device
 *
 * Format the data structure of lens shading correction calibration
 *
 * This function executes in eeprom module context
 *
 * Return: void.
 **/
void dw9761b_eeprom_format_lensshading(sensor_eeprom_data_t *e_ctrl)
{
  unsigned char  flag;
  unsigned short i, light, grid_size;
  unsigned char  *lsc_r, *lsc_b, *lsc_gr, *lsc_gb;
  float          gain;

  SDBG("Enter");
  /* Check validity */
  flag = e_ctrl->eeprom_params.buffer[LSC_FLAG_OFFSET];
  if (flag != VALID_FLAG) {
   lsc_present = FALSE;
   SERR("LSC : empty or invalid data");
   return;
  }
  lsc_present = TRUE;

  /* Get LSC data */
  grid_size = LSC_GRID_SIZE * 2;

  lsc_r = e_ctrl->eeprom_params.buffer + LSC_OFFSET;
  lsc_b = lsc_r + grid_size;
  lsc_gr = lsc_b + grid_size;
  lsc_gb = lsc_gr + grid_size;

  for (light = 0; light < ROLLOFF_MAX_LIGHT; light++) {
    e_ctrl->eeprom_data.lsc.lsc_calib[light].mesh_rolloff_table_size =
      LSC_GRID_SIZE;
  }

  /* (1) r gain */
  for (i = 0; i < grid_size; i += 2) {
    gain = lsc_r[i + 1] << 8 | lsc_r[i];

    for (light = 0; light < ROLLOFF_MAX_LIGHT; light++) {
      e_ctrl->eeprom_data.lsc.lsc_calib[light].r_gain[i/2] = gain;
    }
  }
  /* (2) b gain */
  for (i = 0; i < grid_size; i += 2) {
    gain = lsc_b[i + 1] << 8 | lsc_b[i];

    for (light = 0; light < ROLLOFF_MAX_LIGHT; light++) {
      e_ctrl->eeprom_data.lsc.lsc_calib[light].b_gain[i/2] = gain;
    }
  }
  /* (3) gr gain */
  for (i = 0; i < grid_size; i += 2) {
    gain = lsc_gr[i + 1] << 8 | lsc_gr[i];

    for (light = 0; light < ROLLOFF_MAX_LIGHT; light++) {
      e_ctrl->eeprom_data.lsc.lsc_calib[light].gr_gain[i/2] = gain;
    }
  }
  /* (4) gb gain */
  for (i = 0; i < grid_size; i += 2) {
    gain = lsc_gb[i + 1] << 8 | lsc_gb[i];

    for (light = 0; light < ROLLOFF_MAX_LIGHT; light++) {
      e_ctrl->eeprom_data.lsc.lsc_calib[light].gb_gain[i/2] = gain;
    }
  }
  SDBG("Exit");
}

/** dw9761b_eeprom_format_afdata:
 *    @e_ctrl: point to sensor_eeprom_data_t of the eeprom device
 *
 * Format the data structure of white balance calibration
 *
 * This function executes in eeprom module context
 *
 * Return: void.
 **/
static void dw9761b_eeprom_format_afdata(sensor_eeprom_data_t *e_ctrl)
{
  unsigned char    flag;
  af_data_t        *af;

  SDBG("Enter");
  /* Check validity */
  flag = e_ctrl->eeprom_params.buffer[AF_FLAG_OFFSET];
  if (flag != VALID_FLAG) {
   /* need to call autofocus caliberation to boost up code_per_step
      invalid AF EEPROM data will not be consumed by af parameters
   */
   SERR("AF : empty or invalid data");
  }
  af_present = TRUE;
  /* Get AF data */
  af = (af_data_t *)(e_ctrl->eeprom_params.buffer + AF_OFFSET);

  e_ctrl->eeprom_data.afc.macro_dac = ((af->macro_h << 8) | af->macro_l);
  e_ctrl->eeprom_data.afc.infinity_dac =
    ((af->infinity_h << 8) | af->infinity_l);
  e_ctrl->eeprom_data.afc.starting_dac = e_ctrl->eeprom_data.afc.infinity_dac;

  SLOW("AF : macro %d infinity %d (no starting DAC set to infinity)",
    e_ctrl->eeprom_data.afc.macro_dac, e_ctrl->eeprom_data.afc.infinity_dac);
  SDBG("Exit");
}


static void dw9761b_eeprom_format_pdaf2Dgain(sensor_eeprom_data_t *e_ctrl)
{
  unsigned char                flag, flag2, flag3;
  unsigned short               i, j, temp = 0;
  /* Check validity */
  flag = e_ctrl->eeprom_params.buffer[PDGAIN];
  flag2 = e_ctrl->eeprom_params.buffer[PD_CC];
  flag3 = e_ctrl->eeprom_params.buffer[PD_EDIAN];
  if ( (flag == VALID_FLAG) && (flag2 == VALID_FLAG)){
   if(flag3 == VALID_FLAG){
    for ( i = 0; i < 900; i = i + 2)
    {
      temp = e_ctrl->eeprom_params.buffer[PDGAIN + 1 + i];
      e_ctrl->eeprom_params.buffer[PDGAIN + 1 + i]
       = e_ctrl->eeprom_params.buffer[PDGAIN + 1 + i + 1];
      e_ctrl->eeprom_params.buffer[PDGAIN + 1 + i + 1]
       = temp;
    }
    e_ctrl->eeprom_params.buffer[PD_EDIAN] = 0xFF;
   }
   e_ctrl->eeprom_data.pdaf_ptr = e_ctrl->eeprom_params.buffer + PDGAIN + 1;
  }
 else
  SHIGH("no manual calibration for 3m2xm 2D");
}

static void dw9761b_eeprom_format_pdafgain(sensor_eeprom_data_t *e_ctrl)
{

  pdaf_calibration_param_t     pdaf3_gain;
  unsigned char                i, j, flag, flag2;
  unsigned char                *data, *data2;

  SDBG("Enter");
  /* Check validity */
  flag = e_ctrl->eeprom_params.buffer[PDGAIN];
  flag2 = e_ctrl->eeprom_params.buffer[PD_CC];

  memset(e_ctrl->eeprom_data.pdafc_1d.gain_tbl_left,
         0, sizeof(int) * MAXLENGTH1D);
  memset(e_ctrl->eeprom_data.pdafc_1d.gain_tbl_right,
         0, sizeof(int) * MAXLENGTH1D);

  if ( (flag == VALID_FLAG) && (flag2 == VALID_FLAG))
  {
    SLOW("PD: prase table inside of eeprom");
    data = e_ctrl->eeprom_params.buffer + PDGAIN + 1;
    data2 = e_ctrl->eeprom_params.buffer + PD_CC + 1;
    e_ctrl->eeprom_data.pdafc_1d.gain_map_DSRatio = (data[0] << 8) | data[1];
    e_ctrl->eeprom_data.pdafc_1d.gain_map_length = (data[2] << 8) | data[3];
    e_ctrl->eeprom_data.pdafc_1d.gain_map_DSLength = (data[4] << 8) | data[5];
    for ( i = 0; i < PDGAIN_MAP_SIZE; i++)
    {
      j = 2 * i;
      e_ctrl->eeprom_data.pdafc_1d.gain_tbl_left[i] =
       (data[6 + j] << 8) | data[j + 7];
      e_ctrl->eeprom_data.pdafc_1d.gain_tbl_right[i] =
       (data[6 + PDGAIN_MAP_SIZE * 2 + j] << 8)
       | data[PDGAIN_MAP_SIZE *2 + j + 7];
    }
    e_ctrl->eeprom_data.pdafc_1d.PD_conversion_coeff[0] =
     (data2[0] << 8)| data2[1];
  } else /* use default */
  {
    SLOW("PD: no valid gain map, use default table");
    int  left_gain[MAXLENGTH1D] = {164,164,165,163,164,162,161,160,159,
    159,159,157,157,155,156,155,153,155,153,153,154,154,152,152,153,152,
    151,150,150,149,150,150,150,150,149,150,148,148,149,150,149,149,149,
    150,149,149,150,149,150,150,150,149,150,150,150,152,150,149,151,148,
    150,150,151,152,151,151,153,153,155,156,154,155,155,158,158,156,160,
    159,160,162,161,163,162,163,163,165,165,165,166,167,168,169,171,170,
    171,174,175,177,176,176,181,178,180,181,179,184,183,184,185,186,184,
    188,187,190,188,189,191,189,191,192,191,191,193,192,195,195,192,199,
    203,208,207};
   int right_gain[MAXLENGTH1D] = {199,201,203,201,201,202,203,203,203,
    203,204,203,203,203,206,207,206,207,207,205,204,205,207,203,204,204,
    203,203,204,201,201,202,201,203,201,199,201,199,198,196,197,197,196,
    196,194,192,192,193,191,190,191,189,186,186,186,187,184,186,184,182,
    181,180,181,182,181,179,179,180,179,178,178,177,176,176,175,176,173,
    173,176,174,174,174,171,171,171,172,170,171,171,170,170,170,171,168,
    170,168,167,168,166,165,167,166,167,167,165,167,166,166,165,164,164,
    160,163,163,164,164,163,163,164,165,164,162,164,165,164,164,164,160,
    159,158,158};
   e_ctrl->eeprom_data.pdafc_1d.gain_map_DSRatio = 2;
   e_ctrl->eeprom_data.pdafc_1d.gain_map_DSLength = 131;
   e_ctrl->eeprom_data.pdafc_1d.gain_map_length = 260;
   e_ctrl->eeprom_data.pdafc_1d.PD_conversion_coeff[0] = 23240;
   memcpy(e_ctrl->eeprom_data.pdafc_1d.gain_tbl_left, &left_gain,
     sizeof(int) * MAXLENGTH1D);
   memcpy(e_ctrl->eeprom_data.pdafc_1d.gain_tbl_right, &right_gain,
     sizeof(int) * MAXLENGTH1D);
  }
  e_ctrl->eeprom_data.pdaf_ptr = (void *)&e_ctrl->eeprom_data.pdafc_1d;
  SLOW("Exit");
}

/** dw9761b_eeprom_format_calibration_data:
 *    @e_ctrl: point to sensor_eeprom_data_t of the eeprom device
 *
 * Format all the data structure of calibration
 *
 * This function executes in eeprom module context and generate
 *   all the calibration registers setting of the sensor.
 *
 * Return: void.
 **/
void dw9761b_eeprom_format_calibration_data(void *e_ctrl)
{
  sensor_eeprom_data_t * ctrl = (sensor_eeprom_data_t *)e_ctrl;

  SDBG("Enter");
  RETURN_VOID_ON_NULL(ctrl);

  SLOW("Total bytes in OTP buffer: %d", ctrl->eeprom_params.num_bytes);

  if (!ctrl->eeprom_params.buffer || !ctrl->eeprom_params.num_bytes) {
    SERR("failed: Buff pointer %p buffer size %d", ctrl->eeprom_params.buffer,
      ctrl->eeprom_params.num_bytes);
    return;
  }

  dw9761b_eeprom_format_wbdata(ctrl);
  dw9761b_eeprom_format_lensshading(ctrl);
  dw9761b_eeprom_format_afdata(ctrl);
#ifndef PDAF2D_GAIN
  dw9761b_eeprom_format_pdafgain(ctrl);
#else
  dw9761b_eeprom_format_pdaf2Dgain(ctrl);
#endif
}

static int dw9761b_autofocus_calibration(void *e_ctrl) {
  sensor_eeprom_data_t    *ectrl = (sensor_eeprom_data_t *) e_ctrl;
  int                     i = 0;
  actuator_tuned_params_t *af_driver_tune = NULL;
  actuator_params_t       *af_params = NULL;
  unsigned int            total_steps = 0;
  unsigned short          macro_dac, infinity_dac;
  unsigned short          new_step_bound, otp_step_bound;
  unsigned int            qvalue = 0;

  SDBG("Enter");
  RETURN_ON_NULL(e_ctrl);
  RETURN_ON_NULL(ectrl->eeprom_afchroma.af_driver_ptr);

  af_driver_tune =
    &(ectrl->eeprom_afchroma.af_driver_ptr->actuator_tuned_params);
  af_params = &(ectrl->eeprom_afchroma.af_driver_ptr->actuator_params);

  /* Get the total steps */
  total_steps = af_driver_tune->region_params[af_driver_tune->region_size - 1].
    step_bound[0] - af_driver_tune->region_params[0].step_bound[1];

  if (!total_steps) {
    SERR("Invalid total_steps count: %d",total_steps);
    return FALSE;
  }

  qvalue = af_driver_tune->region_params[0].qvalue;
  if(qvalue < 1 && qvalue > 4096){
    SERR("Invalid qvalue %d", qvalue);
    return FALSE;
  }
  if ( ectrl->eeprom_data.afc.macro_dac < INVALID_DATA)
  {
   macro_dac = ectrl->eeprom_data.afc.macro_dac;
   infinity_dac = ectrl->eeprom_data.afc.infinity_dac;
   otp_step_bound = macro_dac - infinity_dac;
   /* adjust af_driver_ptr */
   af_driver_tune->initial_code = infinity_dac - otp_step_bound * INFINITY_MARGIN;
   new_step_bound = otp_step_bound * (1 + INFINITY_MARGIN + MACRO_MARGIN);
   af_driver_tune->region_params[0].code_per_step =
    new_step_bound / (float)total_steps * qvalue;
  }
  else{
   /* if AF data is invalid, only boost code_per_step */
     af_driver_tune->region_params[0].code_per_step = qvalue;
  }

  SLOW("initial code %d, adjusted code_per_step: %d, qvalue: %d",
    af_driver_tune->initial_code,
    af_driver_tune->region_params[0].code_per_step,
    qvalue);

  SDBG("Exit");

  return TRUE;
}

/** dw9761b_eeprom_eeprom_open_lib:
 *
 * Get the funtion pointer of this lib.
 *
 * This function executes in eeprom module context.
 *
 * Return: eeprom_lib_func_t point to the function pointer.
 **/
void* dw9761b_eeprom_open_lib(void) {
  return &dw9761b_eeprom_lib_func_ptr;
}
