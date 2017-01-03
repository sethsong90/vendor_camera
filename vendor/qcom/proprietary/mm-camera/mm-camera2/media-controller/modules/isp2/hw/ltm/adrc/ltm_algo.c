/* ltm_algo.c
 *
 * Copyright (c) 2016 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */

/* std headers */
#include <unistd.h>
#include <math.h>

#undef ISP_DBG
#define ISP_DBG(fmt, args...) \
  ISP_DBG_MOD(ISP_LOG_LTM, fmt, ##args)
#undef ISP_HIGH
#define ISP_HIGH(fmt, args...) \
  ISP_HIGH_MOD(ISP_LOG_LTM, fmt, ##args)

/* isp headers */
#include "isp_sub_module_log.h"
#include "isp_sub_module_util.h"
#include "chromatix.h"
#include "isp_pipeline_reg.h"
//#include "ltm_curves.h"
#include "ltm_algo.h"

isp_adrc_knee_lut_type ltm_adrc_tuning_config = {
    #include "isp_adrc_tune_param.h"
};



#define NUM_OF_CURVE_SMOOTH_TAP        3


/** ltm_get_gamma_interpolate_table
 *
 *  @module: mct module handle
   @event_identity: identity of trigger update mct event
 *
 *  Return TRUE on success and FALSE on failure
 **/
static boolean ltm_get_gamma_interpolated_table(mct_module_t *module,
  uint32_t event_identity, void *gamma_table_g)
{
  boolean ret = TRUE;
  mct_event_t                    event;
  isp_private_event_t            private_event;

  memset(&event, 0, sizeof(event));
  memset(&private_event, 0, sizeof(private_event));

  private_event.data = gamma_table_g;
  private_event.type = ISP_PRIVATE_GET_GAMMA_INTERPOLATED_TABLE;
  if (!private_event.data) {
    ISP_ERR("failed: private_event.data %p", private_event.data);
    return FALSE;
  }

  event.direction = MCT_EVENT_UPSTREAM;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.u.module_event.module_event_data = &private_event;
  event.u.module_event.type = MCT_EVENT_MODULE_ISP_PRIVATE_EVENT;
  event.identity = event_identity;


  ret = isp_sub_module_util_forward_event_from_module(module, &event);
  if (ret == FALSE) {
    ISP_ERR(" failed to fetch rolloff table ");
    return ret;
  }
  return ret;
}

/** ltm_adrc_generate_ltm_curve
 *
 *    @gamma_in_tbl: Interpolated Gamma Table
 *    @ltm: LA module handle
 *    @ltm_ratio: ltm_ratio given by AEC
 *
 * Update the LTM curve based on the DRC gain and default gamma
 * values.
 **/
static boolean ltm_adrc_generate_ltm_curve(void *gamma_in_tbl,
  ltm_algo_t *adrc_algo, float ltm_ratio)
{
  int i = 0, x1 = 0, x2 = 0, gamma_idx = 0;
  float x_a = 0.0f, val1 = 0.0f, val2 = 0.0f, cur_y = 0.0f, cur_x = 0.0f;
  float input = 0.0f, x_temp_gain = 0.0f, x_a_eqvlnt = 0.0f, x_b_eqvlnt = 0.0f;
  float x_a_eqvlnt_true = 0.0f, gain_eqvlnt = 0.0f, cur_y_true;
  float cur_gain[CURVE_ENTRY_NUM + 1];
  GAMMA_LUT_TYPE gamma_in[GAMMA_NUM_ENTRIES];
  float max_ms_gain = 1.0f, min_ms_gain = 5000.0f, norm_factor;
  unsigned int unity_reached = 0;
  float ltm_curve_x_step = (float)(1 << GAMMA_HW_PACK_BIT) /
      CURVE_ENTRY_NUM;

  /* Gain lut related */
  unsigned short num_knee = ltm_adrc_tuning_config.knee_points;
  float* knee_gain = adrc_algo->adrc_gain_lut_table.knee_gain;
  unsigned int* knee_index = adrc_algo->adrc_gain_lut_table.knee_index;
  float knee_lut[ISP_ADRC_GAIN_LUT_KNEE_LENGTH];
  unsigned int lut_max_gain_idx = num_knee + 1;
  unsigned int gain_idx = 1;
  int ntap;
  float total_sample;
  float temp_master[CURVE_ENTRY_NUM+1], temp_sum;

  if( num_knee > ISP_ADRC_GAIN_LUT_KNEE_LENGTH ) {
    ISP_ERR(" num_knee is greater than MAX LUT Knee length");
    return TRUE;
  }

  /* Updated gain lut curve (in LUT format) based on gain rate */
  isp_sub_module_util_gain_lut_with_drc_gain_weight(knee_gain, knee_index,
    num_knee, ltm_ratio, knee_lut);

  /* Master scale Generation [128] @ 512 as unity gain.
     will be reduced by half after function returns */
  for (i = 0; i < CURVE_ENTRY_NUM +1 ; i++)
    adrc_algo->adrc_hw_master_scale[i] = MASTER_SCALE_CURVE_UNITY * 2 - 1;

  /* Gamma table copy [64] @ 256 as full BW output */
  for (i = 0; i < GAMMA_NUM_ENTRIES; i++) {
    if (GAMMA_HW_PACK_BIT == 8)
      gamma_in[i] = (((uint8_t *)gamma_in_tbl)[i] & 0xff);
    else
      gamma_in[i] = (((uint16_t *)gamma_in_tbl)[i] & 0x3fff);
  }

  /* Generate each entry of the LTM_Curve */
  for (i = 1; i < CURVE_ENTRY_NUM; i++) {
    /* Get X_b */
    cur_x = ltm_curve_x_step * i;

    /* Get X_a by inversing Gamma*/
    /* Sub sample gamma entries to that of LTM curve 128 values */
    while (gamma_idx + 1 <= (GAMMA_NUM_ENTRIES - 1) &&
           (cur_x > gamma_in[gamma_idx + 1]))
      gamma_idx++;

    input = cur_x;

    x1 = gamma_in[gamma_idx];
    x2 = (gamma_idx == (GAMMA_NUM_ENTRIES - 1) ?
          (1 << GAMMA_HW_PACK_BIT) : gamma_in[gamma_idx + 1]);
    val1 = (float)gamma_idx;
    val2 = (float)(gamma_idx + 1);
    x_a = isp_sub_module_util_linear_interpolate(input, x1, x2, val1, val2);
    x_a = x_a /((float)GAMMA_NUM_ENTRIES / (float)ISP_ADRC_LUT_LENGTH);

    /* Get X_a' */
    /* Get GLUT(X_a) (pre-gamma) */
    input = x_a;
    /* Find appropriate index in gain lut. */
    if (input > (float)knee_index[gain_idx] )
      gain_idx += 1;
    /* Get ADRC gain in linear domain */
    if ( gain_idx < ISP_ADRC_GAIN_LUT_KNEE_LENGTH ) {
      x1    = knee_index[gain_idx - 1];
      x2    = knee_index[gain_idx];
      val1  = knee_lut[gain_idx - 1];
      val2  = knee_lut[gain_idx];
    }
    x_a_eqvlnt  = isp_sub_module_util_linear_interpolate(
                    input, x1, x2, val1, val2);

    /* Get X_b'  = Gamma(X_a')) (post-gamma) */
    input = x_a_eqvlnt * GAMMA_NUM_ENTRIES/ISP_ADRC_LUT_LENGTH;
    x1 =   input >= GAMMA_NUM_ENTRIES ?
      (GAMMA_NUM_ENTRIES - 1) : (unsigned int)input;

    x2 = x1 + 1;
    val1 = (float)gamma_in[x1];
    val2 = (float)(x2 == GAMMA_NUM_ENTRIES ?
                   (1 << GAMMA_HW_PACK_BIT) : gamma_in[x2]);

    x_b_eqvlnt =
      isp_sub_module_util_linear_interpolate(input, x1, x2, val1, val2);
    cur_y = x_b_eqvlnt;

    /* Get Master Curve Entry by subtracting unity */
    adrc_algo->adrc_hw_master_curve[i] = ((float)MASTER_CURVE_UNITY /
      ((float)CURVE_ENTRY_NUM * ltm_curve_x_step)) * (cur_y - cur_x);

    /* Feature to improve Master Scale (instead of using unity Scale) */

    /* X_a' (with actual DRC gain */
    gain_eqvlnt = (float)pow((double)knee_gain[0], (double)ltm_ratio);
    x_a_eqvlnt_true = x_a * gain_eqvlnt;
    x_a_eqvlnt_true = (x_a_eqvlnt_true < GAMMA_NUM_ENTRIES) ?
      x_a_eqvlnt_true : GAMMA_NUM_ENTRIES;

    /* Get X_b' = Gamma(X_a') (post-gamma) */
    input = x_a_eqvlnt_true;
    x1 = (input >= GAMMA_NUM_ENTRIES) ?
      (GAMMA_NUM_ENTRIES - 1) : (unsigned int)input;
    x2 = x1 + 1;
    val1 = (float)gamma_in[x1];
    val2 = (float)(x2 == GAMMA_NUM_ENTRIES ?
                   (1 << GAMMA_HW_PACK_BIT) : gamma_in[x2]);
    cur_y_true =
      isp_sub_module_util_linear_interpolate(input, x1, x2, val1, val2);

    /* Record cur_gain, Max and Min gain */
    cur_gain[i] = cur_y_true / cur_x;
    max_ms_gain = cur_gain[i] > max_ms_gain ? cur_gain[i] : max_ms_gain;
    min_ms_gain = cur_gain[i] < min_ms_gain ? cur_gain[i] : min_ms_gain;
  }

  /*Adjust Master Scale curve by gain betwn Xb' and Xb */
  if (fabs(max_ms_gain - min_ms_gain) > 0.0001) {
    for (i = 1; i < CURVE_ENTRY_NUM; i++) {
      norm_factor = (cur_gain[i] - min_ms_gain) / (max_ms_gain - min_ms_gain);
      if (unity_reached == 0) {
        unity_reached = norm_factor < 0.99999f ? unity_reached : 1;
        norm_factor = norm_factor < 0.99999f ? 1 : norm_factor;
      }
      // Master Scale will be normalized to between unity and zero
      adrc_algo->adrc_hw_master_scale[i] *= norm_factor;
    }
  }
  /* Make the first entry equal to second */
  adrc_algo->adrc_hw_master_curve[0] = 0;
  adrc_algo->adrc_hw_master_curve[CURVE_ENTRY_NUM] = 0;
  adrc_algo->adrc_hw_master_scale[CURVE_ENTRY_NUM] =
    adrc_algo->adrc_hw_master_scale[CURVE_ENTRY_NUM - 1];
  adrc_algo->adrc_hw_master_scale[0] = adrc_algo->adrc_hw_master_scale[1];

  /* smooth master curve : 7 point smoothing to avoid contour */
  ntap = NUM_OF_CURVE_SMOOTH_TAP;
  temp_sum = 0;
  total_sample = 2.0f * ntap + 1;
  for (i = 0; i < total_sample-1; i++)
    temp_sum += adrc_algo->adrc_hw_master_curve[i];
  for (i = ntap; i <= CURVE_ENTRY_NUM - ntap; i++) {
    temp_sum += adrc_algo->adrc_hw_master_curve[i + ntap];
    temp_master[i] = temp_sum / total_sample;
    temp_sum -= adrc_algo->adrc_hw_master_curve[i - ntap];
  }
  for (i = ntap; i <= CURVE_ENTRY_NUM - ntap; i++)
    adrc_algo->adrc_hw_master_curve[i] = temp_master[i];

  for (i = 0; i < CURVE_ENTRY_NUM + 1; i++) {
    if (LUT_BASE_BITS == 14) {
      adrc_algo->adrc_hw_master_curve[i] *= 3.0f;
    } else {
      adrc_algo->adrc_hw_master_curve[i] *= 4.0f;
    }
  }
  return TRUE;
}

/** ltm_update_adrc_ltm_curve:
 *  @module: mct module
   @event: mct event
 *
 * Update the LTM curve based on the DRC gain and default gamma
 * values.
 **/
boolean ltm_update_adrc_ltm_curve(ltm_algo_t *adrc_algo,
  aec_update_t *aec_update, mct_module_t *module, mct_event_t *event)
{
  boolean                  ret = TRUE;
  uint32_t                 lut_idx = 0, gamma_table_size = 0;
  float                    drc_gain = 0.0, ltm_ratio = 0.0;
  uint32_t                 i = 0;

  if (!adrc_algo || !event || !aec_update) {
    ISP_ERR("failed: %p %p %p", adrc_algo, aec_update, event);
    return FALSE;
  }

  /* get the ADRC DRC gain values, updated by 3A*/
  drc_gain = aec_update->total_drc_gain; 
  ltm_ratio = aec_update->ltm_ratio;

  /* get the LUT indec values based the calculation*/
  lut_idx = isp_sub_module_util_get_lut_index(drc_gain);

  /*update LTM HW table entries from ADRC LTM LUTs based on the
    LTM Gain LUT index*/
  for (i = 0; i < ISP_ADRC_GAIN_LUT_KNEE_LENGTH; i++) {
    adrc_algo->adrc_gain_lut_table.knee_gain[i] =
        ltm_adrc_tuning_config.adrc_knee_gain_lut[lut_idx].knee_gain[i];
    adrc_algo->adrc_gain_lut_table.knee_index[i] =
        ltm_adrc_tuning_config.adrc_knee_gain_lut[lut_idx].knee_index[i];
  }

  /* Gamma table size depends on the Gamma HW version.*/
    gamma_table_size = sizeof(GAMMA_LUT_TYPE) * GAMMA_NUM_ENTRIES;

  if (adrc_algo->gamma_table_g == NULL) {
    adrc_algo->gamma_table_g = (GAMMA_LUT_TYPE*)malloc(gamma_table_size);
    if (!adrc_algo->gamma_table_g) {
      ISP_ERR("failed: gamma_table_rgb %p", adrc_algo->gamma_table_g);
      return FALSE;
    }
    memset(adrc_algo->gamma_table_g, 0, gamma_table_size);
  }
  /* get Gamma interpolated table */
  ret = ltm_get_gamma_interpolated_table(module,
          event->identity, adrc_algo->gamma_table_g);
  if (ret == FALSE) {
    ISP_ERR("failed: ltm_get_gamma_interpolated_table");
  }

  for (i = 0; i < CURVE_ENTRY_NUM + 1; i++) {
    adrc_algo->adrc_hw_shift_curve[i] = 0;
    adrc_algo->adrc_hw_shift_scale[i] = 0;
    adrc_algo->adrc_hw_master_curve[i] = 0;
    adrc_algo->adrc_hw_master_scale[i] = 0;
  }

  /* get interpolated LTM Curve based on ADRC Knee gain
     and interpolated Gamma*/
  ret = ltm_adrc_generate_ltm_curve(adrc_algo->gamma_table_g,
                                    adrc_algo, ltm_ratio);
  if (ret == FALSE) {
    ISP_ERR("failed: ltm_adrc_generate_ltm_curve, assign default curve");
    return ret;
  }
  return ret;
}
