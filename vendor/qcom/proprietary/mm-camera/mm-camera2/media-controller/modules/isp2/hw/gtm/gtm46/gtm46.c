/* gtm46.c
 *
 * Copyright (c) 2014-2016 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

/* std headers */
#include <unistd.h>
#include <math.h>

/* mctl headers */

#undef ISP_DBG
#define ISP_DBG(fmt, args...) \
  ISP_DBG_MOD(ISP_LOG_GTM, fmt, ##args)
#undef ISP_HIGH
#define ISP_HIGH(fmt, args...) \
  ISP_HIGH_MOD(ISP_LOG_GTM, fmt, ##args)

/* isp headers */
#include "isp_sub_module_log.h"
#include "isp_sub_module_util.h"
#include "gtm46.h"
#include "gtm_curve.h"
#include "isp_pipeline_reg.h"
#include "isp_defs.h"
#include "isp_adrc_tune_def.h"

isp_adrc_knee_lut_type ltm_adrc_tuning_config = {
    #include "isp_adrc_tune_param.h"
};

#ifndef MIN
  #define MIN(a,b)          (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
  #define MAX(a,b)          (((a) > (b)) ? (a) : (b))
#endif

#ifndef CLAMP
  #define CLAMP(x,min,max)  MAX (MIN (x, max), min)
#endif

uint32_t gtm_xin_tbl[] = {
  0,    2,    4,    7,    13,    21,    31,    45,
  62,   83,   107,  137,  169,   201,   233,   265,
  297,  329,  361,  428,  463,   499,   538,   578,
  621,  665,  712,  760,  811,   863,   919,   976,
  1037, 1098, 1164, 1230, 1300,  1371,  1447,  1524,
  1605, 1687, 1774, 1861, 1954,  2047,  2146,  2245,
  2456, 2679, 2915, 3429, 3999,  4630,  5323,  6083,
  6911, 7812, 8787, 9840, 10975, 12193, 13499, 14894,
  16383};

/** gtm46_curve_debug:
 *
 *  @isp_sub_module: isp sub module handle
 *  @gtm_mod: gtm module handle
 *  @hw_update_list: hw update list handle
 *
 *  Create hw update list and append it
 *
 *  Return TRUE on success and FALSE on failure
 **/
static void gtm46_curve_debug(gtm46_t *gtm_mod)
{
  int i = 0;

  ISP_HIGH("==== GTM CURVE ====");
  for (i = 0; i < (GTM_LUT_NUM_BIN-1); i++) {
    ISP_HIGH("gtm tbl[%d] %llx", i, gtm_mod->dmi_tbl[i]);
  }
}

/** gtm46_do_hw_update:
 *
 *  @isp_sub_module: isp sub module handle
 *  @gtm_mod: gtm module handle
 *  @hw_update_list: hw update list handle
 *
 *  Create hw update list and append it
 *
 *  Return TRUE on success and FALSE on failure
 **/
static boolean gtm46_do_hw_update(isp_sub_module_t *isp_sub_module,
  gtm46_t *gtm_mod, isp_sub_module_output_t *output)
{
  boolean                      ret         = TRUE;
  struct msm_vfe_cfg_cmd2     *cfg_cmd     = NULL;
  struct msm_vfe_reg_cfg_cmd  *reg_cfg_cmd = NULL;
  struct msm_vfe_cfg_cmd_list *hw_update   = NULL;
  ISP_GTM_Cfg                  cfg_mask;

  cfg_mask.fields.lutBankSel |= ~0;

  if (GTM_CGC_OVERRIDE == TRUE) {
    ret = isp_sub_module_util_update_cgc_mask(isp_sub_module,
      GTM_CGC_OVERRIDE_REGISTER, GTM_CGC_OVERRIDE_BIT, TRUE);
    if (ret == FALSE) {
      ISP_ERR("failed: enable cgc");
    }
  }

  hw_update = (struct msm_vfe_cfg_cmd_list *)
    malloc(sizeof(struct msm_vfe_cfg_cmd_list));
  if (!hw_update) {
    ISP_ERR("failed allocating for hw_update");
    return FALSE;
  }
  memset(hw_update, 0, sizeof(struct msm_vfe_cfg_cmd_list));
  cfg_cmd = &hw_update->cfg_cmd;

  reg_cfg_cmd =
    (struct msm_vfe_reg_cfg_cmd *)malloc(sizeof(struct msm_vfe_reg_cfg_cmd));
  if (!reg_cfg_cmd) {
    ISP_ERR("failed allocating for reg_cfg_cmd");
    goto ERROR_FREE_HW_UPDATE;
  }
  memset(reg_cfg_cmd, 0, sizeof(struct msm_vfe_reg_cfg_cmd));

  cfg_cmd->cfg_cmd = (void *)reg_cfg_cmd;
  cfg_cmd->num_cfg = 1;

  reg_cfg_cmd[0].cmd_type = VFE_CFG_MASK;
  reg_cfg_cmd[0].u.mask_info.reg_offset = ISP_GTM_OFF;
  reg_cfg_cmd[0].u.mask_info.mask = cfg_mask.bytes.value;
  reg_cfg_cmd[0].u.mask_info.val  = gtm_mod->reg_cfg.bytes.value;

  ISP_LOG_LIST("hw_update %p cfg_cmd %p", hw_update, cfg_cmd->cfg_cmd);
  ret = isp_sub_module_util_store_hw_update(isp_sub_module, hw_update);
  if (ret == FALSE) {
    ISP_ERR("failed: isp_sub_module_util_store_hw_update %d", ret);
    goto ERROR_FREE_REG_CFG_CMD;
  }

  gtm46_curve_debug(gtm_mod);
  ret = isp_sub_module_util_write_dmi(gtm_mod->dmi_tbl,
    sizeof(gtm_mod->dmi_tbl),
    GTM_LUT_RAM_BANK0 + gtm_mod->reg_cfg.fields.lutBankSel,
    VFE_WRITE_DMI_64BIT, ISP_DMI_CFG_OFF, ISP_DMI_ADDR, isp_sub_module);
  if (ret == FALSE) {
    ISP_ERR("failed: isp_sub_module_util_write_dmi");
    goto ERROR_FREE_REG_CFG_CMD;
  }

  ret = isp_sub_module_util_append_hw_update_list(isp_sub_module, output);
  if (ret == FALSE) {
    ISP_ERR("failed: isp_sub_module_util_append_hw_update_list");
    goto ERROR_FREE_REG_CFG_CMD;
  }

  gtm_mod->reg_cfg.fields.lutBankSel ^= 1;

  if (GTM_CGC_OVERRIDE == TRUE) {
    ret = isp_sub_module_util_update_cgc_mask(isp_sub_module,
      GTM_CGC_OVERRIDE_REGISTER, GTM_CGC_OVERRIDE_BIT, FALSE);
    if (ret == FALSE) {
      ISP_ERR("failed: disable cgc");
    }
  }

  isp_sub_module->trigger_update_pending = FALSE;
  return TRUE;

ERROR_FREE_REG_CFG_CMD:
  free(reg_cfg_cmd);
ERROR_FREE_HW_UPDATE:
  free(hw_update);
  return FALSE;
}

/** gtm46_pack_dmi:
 *
 *  Return TRUE on success and FALSE on failure
 **/
static void gtm46_pack_dmi(double *yout_tbl, uint32_t *xin_tbl,
  uint64_t *dmi_tbl, uint32_t *base, int32_t *slope)
{
  uint32_t i;
  uint32_t gains[GTM_LUT_NUM_BIN];

  for (i = 0; i < GTM_LUT_NUM_BIN; i++) {
    /* compute gain base: (Yout / Xin) */
    gains[i] = FLOAT_TO_Q(12, yout_tbl[i] / (xin_tbl[i] ? xin_tbl[i] : 1));
    /* Clamp to 0 and 2^18-1 */
    gains[i] = CLAMP(gains[i], 0, (1 << 18) - 1);
  }
  for (i = 0; i < GTM_LUT_NUM_BIN-1; i++) {
    int32_t slope_loc;
    int32_t x_delta = xin_tbl[i+1] - xin_tbl[i];
    int32_t y_delta = (int32_t)gains[i+1] - (int32_t)gains[i];

    y_delta *= (1 << 8);
    if (y_delta >= 0)
      y_delta += x_delta / 2;
    else
      y_delta -= x_delta / 2;
    slope_loc = y_delta / x_delta;
    /* Clamp to -2^25 and 2^25-1 */
    slope_loc = CLAMP(slope_loc, -(1 << 25), (1 << 25) - 1);
    base[i] = gains[i];
    slope[i] = slope_loc;
    dmi_tbl[i] = (uint64_t)gains[i] & 0x3FFFF;
    dmi_tbl[i] |= ((uint64_t)slope_loc & 0x3FFFFFF) << 32;
  }
}

/** gtm46_pack_dmi_fix_curve:
 *
 *  Return TRUE on success and FALSE on failure
 **/
static void gtm46_pack_dmi_fix_curve(uint64_t *dmi_tbl,
  const uint32_t *base, const int32_t *slope)
{
  uint32_t i;

  for (i = 0; i < GTM_LUT_NUM_BIN-1; i++) {
    dmi_tbl[i] = (uint64_t)base[i] & 0x3FFFF;
    dmi_tbl[i] |= ((uint64_t)slope[i] & 0x3FFFFFF) << 32;
  }

}

/** gtm46_interpolate:
 *
 *  Return TRUE on success and FALSE on failure
 **/
static void gtm46_interpolate(chromatix_GTM *in1,
  chromatix_GTM *in2, chromatix_GTM *out, float ratio)
{
  out->gtm_reserve.GTM_maxval_th =
    LINEAR_INTERPOLATION_INT(in1->gtm_reserve.GTM_maxval_th,
                             in2->gtm_reserve.GTM_maxval_th, ratio);
  out->gtm_reserve.GTM_key_min_th =
    LINEAR_INTERPOLATION_INT(in1->gtm_reserve.GTM_key_min_th,
                             in2->gtm_reserve.GTM_key_min_th, ratio);
  out->gtm_reserve.GTM_key_max_th =
    LINEAR_INTERPOLATION_INT(in1->gtm_reserve.GTM_key_max_th,
                             in2->gtm_reserve.GTM_key_max_th, ratio);
  out->gtm_reserve.GTM_key_hist_bin_weight =
    LINEAR_INTERPOLATION(in1->gtm_reserve.GTM_key_hist_bin_weight,
                         in2->gtm_reserve.GTM_key_hist_bin_weight, ratio);
  out->gtm_reserve.GTM_Yout_maxval =
    LINEAR_INTERPOLATION_INT(in1->gtm_reserve.GTM_Yout_maxval,
                             in2->gtm_reserve.GTM_Yout_maxval, ratio);
  out->gtm_core.GTM_a_middletone =
    LINEAR_INTERPOLATION(in1->gtm_core.GTM_a_middletone,
                         in2->gtm_core.GTM_a_middletone, ratio);
  out->gtm_core.GTM_temporal_w =
    LINEAR_INTERPOLATION(in1->gtm_core.GTM_temporal_w,
                         in2->gtm_core.GTM_temporal_w, ratio);
  out->gtm_core.GTM_middletone_w =
    LINEAR_INTERPOLATION(in1->gtm_core.GTM_middletone_w,
                         in2->gtm_core.GTM_middletone_w, ratio);
  out->gtm_core.GTM_max_percentile=
    LINEAR_INTERPOLATION(in1->gtm_core.GTM_max_percentile,
                         in2->gtm_core.GTM_max_percentile, ratio);
  out->gtm_core.GTM_min_percentile =
    LINEAR_INTERPOLATION(in1->gtm_core.GTM_min_percentile,
                         in2->gtm_core.GTM_min_percentile, ratio);
  out->gtm_reserve.GTM_minval_th =
    LINEAR_INTERPOLATION(in1->gtm_reserve.GTM_minval_th,
                         in2->gtm_reserve.GTM_minval_th, ratio);
}

/** gtm46_ez_isp_update
 *
 *  @mod: gtm module handle
 *  @gicDiag: gic Diag handle
 *
 *  eztune update
 *
 *  Return NONE
 **/
static void gtm46_ez_isp_update(gtm46_t *mod,
  chromatix_GTM *params, gtmdiag_t  *gtmDiag)
{
  ISP_GTM_Cfg *gtmCfg = &(mod->reg_cfg);

  gtmDiag->LUTBankSel = gtmCfg->fields.lutBankSel;
  gtmDiag->KeyHistBinWeight = params->gtm_reserve.GTM_key_hist_bin_weight;
  gtmDiag->KeyMaxThresh = params->gtm_reserve.GTM_key_max_th;
  gtmDiag->KeyMinThresh = params->gtm_reserve.GTM_key_min_th;
  gtmDiag->MaxValThresh = params->gtm_reserve.GTM_maxval_th;
  gtmDiag->YoutMaxVal = params->gtm_reserve.GTM_Yout_maxval;
  gtmDiag->AMiddleTone = params->gtm_core.GTM_a_middletone;
  gtmDiag->MiddleToneW = params->gtm_core.GTM_middletone_w;
  gtmDiag->TemporalW = params->gtm_core.GTM_temporal_w;

  memcpy(&gtmDiag->Xarr[0], &gtm_xin_tbl[0], (GTM_LUT_NUM_BIN-1));
  memcpy(&gtmDiag->YRatioBase[0], &mod->base[0], (GTM_LUT_NUM_BIN-1));
  memcpy(&gtmDiag->YRatioSlope[0], &mod->slope[0], (GTM_LUT_NUM_BIN-1));

}/* gtm46_ez_isp_update */

/** gtm46_fill_vfe_diag_data:
 *
 *  @gic: gic module instance
 *
 *  This function fills vfe diagnostics information
 *
 *  Return: TRUE success
 **/
static boolean gtm46_fill_vfe_diag_data(gtm46_t *mod, chromatix_GTM *params,
  isp_sub_module_t *isp_sub_module, isp_sub_module_output_t *sub_module_output)
{
  boolean  ret = TRUE;
  gtmdiag_t  *gtmDiag = NULL;
  vfe_diagnostics_t  *vfe_diag = NULL;

  if (sub_module_output->frame_meta) {
    sub_module_output->frame_meta->vfe_diag_enable =
      isp_sub_module->vfe_diag_enable;
    vfe_diag = &sub_module_output->frame_meta->vfe_diag;
    gtmDiag = &(vfe_diag->prev_gtmdiag);

    gtm46_ez_isp_update(mod, params, gtmDiag);
  }

  return ret;
}/* gtm46_fill_vfe_diag_data */

/** gtm46_adrc_get_knee_gain:
 *
 *  @gtm_ratio: ADRC GTM gain ratio
 *  Return gain
 **/
static float gtm46_adrc_get_knee_gain(
   float gtm_ratio, unsigned int *knee_index,
  float* knee_gain, int kneePoints, unsigned int input)
{
  int32_t low_idx = 0, i;
  float input_x, gain, temp_gain;

  /* convert input to range */
  input_x = 64 * ((float) input / (float) ISP_ADRC_LUT_LENGTH);
  // look at nearest index
  for (i = 0; i < kneePoints; i++) {
    if (input_x >= (float) knee_index[i])
      low_idx = i;
  }

  if(low_idx == kneePoints - 1)
    low_idx -= 1;

  /* interpolate gain */
  float val1 = knee_gain[low_idx];
  float val2 = knee_gain[low_idx + 1];
  temp_gain = isp_sub_module_util_linear_interpolate(input_x,
                knee_index[low_idx], knee_index[low_idx+1], val1, val2);
    /* apply ADRC percentage */
  gain = (float) pow((double)temp_gain, (double) gtm_ratio);

  return gain;
}


/** gtm46_derive_and_pack_lut_adrc_based:
 *
 *  @gtm: gtm module handle
 *  Return TRUE on success and FALSE on failure
 **/
static void gtm46_derive_and_pack_lut_adrc_based(gtm46_t *gtm)
{
  int32_t i;
  float temp;
  int32_t  x_delta, y_delta;
  int32_t kneePoints;
  uint32_t Yratio_base[ISP_ADRC_LUT_LENGTH+1];
  int32_t  Yratio_slope;
  float    drc_gain = 0.0, gtm_ratio = 0.0;
  float *knee_gain;
  uint32_t *knee_index;
  uint32_t  lut_idx = 0;

  /* get the ADRC DRC gain values, updated by 3A*/
  drc_gain = gtm->aec_update.total_drc_gain;
  gtm_ratio = gtm->aec_update.gtm_ratio;

  /* get the LUT indec values based the calculation*/
  lut_idx = isp_sub_module_util_get_lut_index(drc_gain);

  knee_gain = ltm_adrc_tuning_config.adrc_knee_gain_lut[lut_idx].knee_gain;
  knee_index = ltm_adrc_tuning_config.adrc_knee_gain_lut[lut_idx].knee_index;
  kneePoints = ltm_adrc_tuning_config.knee_points;

  //compute Y_ratio
  for (i = 0; i<GTM_LUT_NUM_BIN; i++) {
    /* Map from 16384(2^14) to 64(adrc lut length */
    temp = (float)(gtm_xin_tbl[i]*(ISP_ADRC_LUT_LENGTH-1))/16383.0f;
    Yratio_base[i] = gtm46_adrc_get_knee_gain(gtm_ratio, knee_index,
                                     knee_gain, kneePoints, temp) * Q12;

    /* Clamp to 0 and 2^18-1 */
    Yratio_base[i] = CLAMP(Yratio_base[i], 0, (1 << 18) - 1);
  }

  //compute Y_ratio_slope
  for (i = 0; i < GTM_LUT_NUM_BIN-1; i++) {
    x_delta = gtm_xin_tbl[i+1] - gtm_xin_tbl[i];
    y_delta = (int32_t)Yratio_base[i+1] - (int32_t)Yratio_base[i];

    y_delta *= (1 << 8);
    if (y_delta >= 0)
      y_delta += x_delta / 2;
    else
      y_delta -= x_delta / 2;
    Yratio_slope = y_delta / x_delta;
    /* Clamp to -2^25 and 2^25-1 */
    Yratio_slope = CLAMP(Yratio_slope, -(1 << 25), (1 << 25) - 1);
    gtm->dmi_tbl[i] = (uint64_t)Yratio_base[i] & 0x3FFFF;
    gtm->dmi_tbl[i] |= ((uint64_t)Yratio_slope & 0x3FFFFFF) << 32;
  }
  return;
}

/** gtm_trigger_update:
 *
 *  @module: mct module handle
 *  @isp_sub_module: ISP sub module handle
 *  @event: mct event handle
 *
 *  Perform trigger update using aec_update
 *
 *  Return TRUE on success and FALSE on failure
 **/
boolean gtm46_trigger_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event)
{
  boolean                  ret               = TRUE;
  gtm46_t                 *gtm               = NULL;
  isp_private_event_t     *private_event     = NULL;
  isp_sub_module_output_t *output            = NULL;
  chromatix_parms_type    *chromatix_ptr     = NULL;
  chromatix_GTM_type      *chromatix_gtm     = NULL;
  isp_gtm_algo_params_t   *algo_param        = NULL;
  isp_meta_entry_t        *gtm_dmi_info      = NULL;
  trigger_point_type      *gtm_trigger       = NULL;
  trigger_point2_type     *gtm_trigger2      = NULL;
  float                    start             = 0.0;
  float                    end               = 0.0;
  float                    ratio             = 0.0;
  uint32_t                 i                 = 0;
  float                    aec_reference     = 0.0;
  uint8_t                  trigger_index     = MAX_SETS_FOR_TONE_NOISE_ADJ + 1;

  RETURN_IF_NULL(module);
  RETURN_IF_NULL(isp_sub_module);
  RETURN_IF_NULL(event);

  gtm = (gtm46_t *)isp_sub_module->private_data;
  RETURN_IF_NULL(gtm);

  private_event =
    (isp_private_event_t *)event->u.module_event.module_event_data;
  RETURN_IF_NULL(private_event);

  output = (isp_sub_module_output_t *)private_event->data;
  RETURN_IF_NULL(output);

  algo_param = &(output->algo_params->gtm);

  chromatix_ptr =
    (chromatix_parms_type *)isp_sub_module->chromatix_ptrs.chromatixPtr;
  RETURN_IF_NULL(chromatix_ptr);

  chromatix_gtm = &chromatix_ptr->chromatix_VFE.chromatix_gtm;

  PTHREAD_MUTEX_LOCK(&isp_sub_module->mutex);

  if ((isp_sub_module->submod_enable == FALSE) ||
      (isp_sub_module->submod_trigger_enable == FALSE)) {
    ISP_DBG("enable = %d, trigger_enable = %d",
      isp_sub_module->submod_enable, isp_sub_module->submod_trigger_enable);
    PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);
    return TRUE;
  }


  if (isp_sub_module->trigger_update_pending == FALSE) {
    ISP_DBG("enable = %d, trigger_enable = %d trigger_update_pending %d",
      isp_sub_module->submod_enable, isp_sub_module->submod_trigger_enable,
      isp_sub_module->trigger_update_pending);
    goto FILL_METADATA;
  }

  if (gtm->enable_adrc) {
    gtm46_derive_and_pack_lut_adrc_based(gtm);
  } else {
    /* prepare algo parameters */
    if (USE_GTM46_FIXED_CURVE == 0 ||
      isp_sub_module->is_zzhdr_mode) {
      algo_param->is_valid = 1;
      algo_param->is_prev_key_valid = gtm->algo_output.is_key_valid;
      algo_param->prev_key          = gtm->algo_output.key;
      algo_param->prev_max_v_hist   = gtm->algo_output.max_v_hist;
      algo_param->prev_min_v_hist   = gtm->algo_output.min_v_hist;
      /*enable temporal filter once confirm with system team*/
      algo_param->temporal_filter_enable   = FALSE;

      if (!gtm->is_aec_update_valid) {
        /* default to NORMAL_LIGHT when aec_update isn't available yet */
        algo_param->params = chromatix_gtm->gtm[2];
      } else {
        /* interpolate to get the chromatix parameters for algo */

        for (i = 0; i < MAX_SETS_FOR_TONE_NOISE_ADJ; i++) {

         if (i == MAX_SETS_FOR_TONE_NOISE_ADJ - 1) {
            /* falls within region 6 but we do not use trigger points in the region */
            ratio         = 0.0;
            trigger_index = MAX_SETS_FOR_TONE_NOISE_ADJ - 1;
            break;
          }

          if (chromatix_gtm->control_gtm == CONTROL_LUX_IDX) {
            /* lux index based */
            aec_reference = gtm->aec_update.lux_idx;
            gtm_trigger   = &(chromatix_gtm->gtm[i].gtm_trigger);
            start         = gtm_trigger->lux_index_start;
            end           = gtm_trigger->lux_index_end;
            ISP_DBG("lux base, lux idx %f",aec_reference);
          } else if (chromatix_gtm->control_gtm == CONTROL_GAIN) {
            /* Gain based */
            aec_reference = gtm->aec_update.real_gain;
            gtm_trigger   = &(chromatix_gtm->gtm[i].gtm_trigger);
            start         = gtm_trigger->gain_start;
            end           = gtm_trigger->gain_end;
            ISP_DBG("gain base, gain %f",aec_reference);
          } else if (chromatix_gtm->control_gtm == CONTROL_AEC_EXP_SENSITIVITY_RATIO) {
            /* AEC sensitivity ratio based*/
            aec_reference = gtm->aec_update.real_gain; // Need to use aec_sensitivity_ratio here from 3A
            gtm_trigger2  = &(chromatix_gtm->gtm[i].aec_sensitivity_ratio);
            start         = gtm_trigger2->start;
            end           = gtm_trigger2->end;
            ISP_DBG("gain base, gain %f",aec_reference);
          }

          /* index is within interpolation range, find ratio */
           if (aec_reference >= start && aec_reference < end) {
             ratio         = (end - aec_reference)/(end - start);
             trigger_index = i;
             ISP_HIGH("%s [%f - %f - %f] = %f", __func__, start, aec_reference, end,
               ratio);
             break;
           }
        }

        if (trigger_index + 1 >= MAX_SETS_FOR_TONE_NOISE_ADJ) {
           ISP_ERR("invalid trigger_index, no interpolation");
        } else {
           gtm46_interpolate(&chromatix_gtm->gtm[trigger_index],
                             &chromatix_gtm->gtm[trigger_index + 1],
                             &algo_param->params,
                             ratio);
        }

        gtm->is_aec_update_valid = 0;
      }

      /* pack DMI table */
      gtm46_pack_dmi(gtm->algo_output.gtm_yout, gtm_xin_tbl,
        gtm->dmi_tbl, gtm->base, gtm->slope);
    } else {
      /* pack DMI table */
      gtm46_pack_dmi_fix_curve(gtm->dmi_tbl,
        yratio_base, yratio_slope);
    }
  }
  isp_sub_module->trigger_update_pending = FALSE;

  ret = gtm46_do_hw_update(isp_sub_module, gtm, output);
  if (ret == FALSE) {
    ISP_ERR("failed: gtm_do_hw_update ret %d", ret);
    PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);
    return FALSE;
  }

FILL_METADATA:
  if (output && isp_sub_module->vfe_diag_enable &&
      (USE_GTM46_FIXED_CURVE == 0)) {
    ret = gtm46_fill_vfe_diag_data(gtm, &algo_param->params,
                                   isp_sub_module, output);
    if (ret == FALSE) {
      ISP_ERR("failed: gtm46_fill_vfe_diag_data");
    }
  }

  if (output->metadata_dump_enable == 1) {
    /*fill in DMI info*/
    gtm_dmi_info = &output->
      meta_dump_params->meta_entry[ISP_META_GTM_TBL];
    gtm_dmi_info->len =
      sizeof(gtm->dmi_tbl);
    /*dmi type */
    gtm_dmi_info->dump_type  = ISP_META_GTM_TBL;
    gtm_dmi_info->start_addr = 0;
    output->meta_dump_params->frame_meta.num_entry++;
    memcpy(gtm_dmi_info->isp_meta_dump,
      gtm->dmi_tbl,
      sizeof(gtm->dmi_tbl));

    output->meta_dump_params->frame_meta.adrc_info.gtm_ratio =
     gtm->aec_update.gtm_ratio;
    output->meta_dump_params->frame_meta.adrc_info.reserved_data[0] =
      gtm->aec_update.total_drc_gain;

  }

  if (output->frame_meta)
    output->frame_meta->sensor_hdr =
      gtm->sensor_hdr_mode;

  PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);

  return TRUE;
} /* gtm_trigger_update */

/** gtm_reset:
 *
 *  @mod: Pointer to gtm module
 *
 *  Perform reset of gtm module
 *
 *  Return void
 **/
static void gtm46_reset(gtm46_t *mod)
{
  uint32_t i;
  memset(mod, 0, sizeof(gtm46_t));
  /* reset default algo look up table - unity gain */
  for (i = 0; i < sizeof(gtm_xin_tbl) / sizeof(uint32_t); i++)
    mod->algo_output.gtm_yout[i] = (float)gtm_xin_tbl[i];
}

/** gtm46_stats_aec_update:
 *
 *  @module: demosaic module
 *  @isp_sub_module: ISP sub module handle
 *  @event: mct event handle
 *
 *  Handle AEC update event
 *
 *  Return TRUE on success and FALSE on failure
 **/
boolean gtm46_stats_aec_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event)
{
  boolean         ret = FALSE;
  gtm46_t        *gtm = NULL;
  stats_update_t *stats_update = NULL;

  RETURN_IF_NULL(module);
  RETURN_IF_NULL(isp_sub_module);
  RETURN_IF_NULL(event);

  gtm = (gtm46_t *)isp_sub_module->private_data;
  RETURN_IF_NULL(gtm);

  stats_update = (stats_update_t *)event->u.module_event.module_event_data;
  RETURN_IF_NULL(stats_update);

  PTHREAD_MUTEX_LOCK(&isp_sub_module->mutex);

  gtm->enable_adrc =
    isp_sub_module_util_is_adrc_mod_enable(stats_update->aec_update.gtm_ratio);

  if (gtm->enable_adrc == TRUE) {
    if (!F_EQUAL(stats_update->aec_update.total_drc_gain,
        gtm->aec_update.total_drc_gain) ||
        !F_EQUAL(gtm->aec_update.gtm_ratio, stats_update->aec_update.gtm_ratio)) {
      gtm->aec_update = stats_update->aec_update;
      isp_sub_module->trigger_update_pending = TRUE;
    }
  } else {

    if (!F_EQUAL(gtm->aec_update.lux_idx, stats_update->aec_update.lux_idx)     ||
        !F_EQUAL(gtm->aec_update.real_gain, stats_update->aec_update.real_gain) ||
        !F_EQUAL(gtm->aec_update.exp_ratio,
               stats_update->aec_update.exp_ratio) ||
        !F_EQUAL(gtm->aec_update.hdr_sensitivity_ratio,
        stats_update->aec_update.hdr_sensitivity_ratio)) {
      gtm->aec_update = stats_update->aec_update;
      isp_sub_module->trigger_update_pending = TRUE;
      gtm->is_aec_update_valid = 1;
    }
  }

  ret = TRUE;
error:
  PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);
  return ret;
}

/** gtm46_algo_update:
 *
 *  @module: mct module handle
 *  @isp_sub_module: isp sub module handle
 *  @event: mct event handle
 *
 *  This function handles the algo update event
 *  by saving the result to the module internal storage.
 *
 **/
boolean gtm46_algo_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event)
{
  gtm46_t  *mod = NULL;
  isp_saved_gtm_params_t *algo_result;

  RETURN_IF_NULL(module);
  RETURN_IF_NULL(isp_sub_module);
  RETURN_IF_NULL(event);

  mod = (gtm46_t *)isp_sub_module->private_data;
  RETURN_IF_NULL(mod);

  algo_result =
    (isp_saved_gtm_params_t *)event->u.module_event.module_event_data;
  RETURN_IF_NULL(algo_result);

  PTHREAD_MUTEX_LOCK(&isp_sub_module->mutex);
  mod->algo_output = *algo_result;
  isp_sub_module->trigger_update_pending = TRUE;
  PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);

  return TRUE;
}

/** gtm46_streamon:
 *
 *  @module: mct module handle
 *  @isp_sub_module: isp sub module handle
 *  @event: mct event handle
 *
 *  This function makes initial configuration during first
 *  stream ON
 *
 *  Return: TRUE on success and FALSE on failure
 **/
boolean gtm46_streamon(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event)
{
  boolean                ret = TRUE;
  gtm46_t               *mod = NULL;
  isp_sub_module_priv_t *isp_sub_module_priv = NULL;

  RETURN_IF_NULL(module);
  RETURN_IF_NULL(isp_sub_module);
  RETURN_IF_NULL(event);

  isp_sub_module_priv = (isp_sub_module_priv_t *)MCT_OBJECT_PRIVATE(module);
  RETURN_IF_NULL(isp_sub_module_priv);

  mod = (gtm46_t *)isp_sub_module->private_data;
  RETURN_IF_NULL(mod);

  PTHREAD_MUTEX_LOCK(&isp_sub_module->mutex);

  if (isp_sub_module->stream_on_count++) {
    PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);
    return TRUE;
  }

  if (isp_sub_module->submod_enable == FALSE) {
    ISP_DBG("gtm enable = %d", isp_sub_module->submod_enable);
    PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);
    return TRUE;
  }

  isp_sub_module->trigger_update_pending = TRUE;
  PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);
  return ret;

ERROR:
  PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);
  return ret;
} /* gtm46_config */

/** gtm46_streamoff:
 *
 *  @module: mct module handle
 *  @isp_sub_module: isp sub module handle
 *  @event: mct event handle
 *
 *  This function resets configuration during last stream OFF
 *
 *  Return: TRUE on success and FALSE on failure
 **/
boolean gtm46_streamoff(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event)
{
  gtm46_t *mod = NULL;

  RETURN_IF_NULL(module);
  RETURN_IF_NULL(isp_sub_module);
  RETURN_IF_NULL(event);

  mod = (gtm46_t *)isp_sub_module->private_data;
  RETURN_IF_NULL(mod);

  PTHREAD_MUTEX_LOCK(&isp_sub_module->mutex);

  if (--isp_sub_module->stream_on_count) {
    PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);
    return TRUE;
  }

  gtm46_reset(mod);
  isp_sub_module->trigger_update_pending = FALSE;

  PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);

  return TRUE;
} /* gtm46_streamoff */

/** gtm46_set_chromatix_ptr:
 *
 *  @isp_sub_module: isp sub module handle
 *  @data: module event data
 *
 *  This function makes initial configuration
 *
 *  Return: TRUE on success and FALSE on failure
 **/
boolean gtm46_set_chromatix_ptr(isp_sub_module_t *isp_sub_module, void *data)
{
  boolean             ret = TRUE;
  gtm46_t             *gtm = NULL;
  modulesChromatix_t *chromatix_ptrs = NULL;
  chromatix_parms_type       *chromatix_param = NULL;
  chromatix_videoHDR_type    *chromatix_VHDR = NULL;
  if (!isp_sub_module || !data) {
    ISP_ERR("failed: %p %p", isp_sub_module, data);
    return FALSE;
  }

  gtm = (gtm46_t *)isp_sub_module->private_data;
  if (!gtm) {
    ISP_ERR("failed: mod %p", gtm);
    return FALSE;
  }
  chromatix_ptrs = (modulesChromatix_t *)data;
  if (!chromatix_ptrs) {
    ISP_ERR("failed: chromatix_ptrs %p", chromatix_ptrs);
    return FALSE;
  }

  PTHREAD_MUTEX_LOCK(&isp_sub_module->mutex);

  isp_sub_module->chromatix_ptrs = *chromatix_ptrs;
  chromatix_param =
    (chromatix_parms_type *)isp_sub_module->chromatix_ptrs.chromatixPtr;
  chromatix_VHDR =
      &chromatix_param->chromatix_post_processing.chromatix_video_HDR;

  ret = isp_sub_module_util_configure_from_chromatix_bit(isp_sub_module);
  if (ret == FALSE) {
    ISP_ERR("failed: updating module enable bit for hw %d",
      isp_sub_module->hw_module_id);
    isp_sub_module->submod_enable = FALSE;
    PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);
    return FALSE;
  }

  isp_sub_module->trigger_update_pending = TRUE;

  PTHREAD_MUTEX_UNLOCK(&isp_sub_module->mutex);

  return ret;
} /* gtm44_set_chromatix_ptr */

/** gtm46_set_hdr_mode:
*
*  @module:
*  @isp_sub_module: isp sub module handle
*  @event: module event data
*
*  This function sets hdr mode.
*
*  Return: TRUE on success and FALSE on failure
**/
boolean gtm46_set_hdr_mode(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event)
{
  boolean ret = TRUE;
  mct_event_control_t      *ctrl_event = NULL;
  mct_event_control_parm_t *param = NULL;
  cam_sensor_hdr_type_t     *hdr_mode = NULL;
  gtm46_t                 *gtm = NULL;

  if (!module || !event || !isp_sub_module) {
    ISP_ERR("failed: module %p isp_sub_module %p event %p", module,
      isp_sub_module, event);
    return FALSE;
  }
  ctrl_event = &event->u.ctrl_event;

  if (!ctrl_event) {
    ISP_ERR("failed: ctrl_event %p", ctrl_event);
    return FALSE;
  }
  gtm = (gtm46_t *)isp_sub_module->private_data;
  RETURN_IF_NULL(gtm);

  param = ctrl_event->control_event_data;
  RETURN_IF_NULL(param);
  hdr_mode = ((cam_sensor_hdr_type_t *)param->parm_data);
  RETURN_IF_NULL(hdr_mode);
  if (*hdr_mode == CAM_SENSOR_HDR_ZIGZAG){
      isp_sub_module->is_zzhdr_mode = TRUE;
  } else {
      isp_sub_module->is_zzhdr_mode = FALSE;
  }
  gtm->sensor_hdr_mode = *hdr_mode;
#ifdef ZZHDR_CHROMATIX_EXTN
  if (isp_sub_module->is_zzhdr_mode &&
    gtm->zzhdr_chromatix_extn->
      chromatix_post_processing.chromatix_hdr_gtm.gtm_enable > 0) {
    ISP_DBG("Enable GTM ");
    isp_sub_module->submod_enable = TRUE;
  }
#endif

  ISP_ERR("zzhdr_gtm is_zzhdr_mode %d ", isp_sub_module->is_zzhdr_mode);
  return TRUE;
}


/** gtm_init:
 *
 *  @module: mct module handle
 *  @isp_sub_module: isp sub module handle
 *
 *  Initialize the gtm module
 *
 *  Return TRUE on Success, FALSE on failure
 **/
boolean gtm46_init(mct_module_t *module, isp_sub_module_t *isp_sub_module)
{
  gtm46_t *gtm = NULL;

  RETURN_IF_NULL(module);
  RETURN_IF_NULL(isp_sub_module);

  gtm = (gtm46_t *)malloc(sizeof(gtm46_t));
  if (!gtm) {
    ISP_ERR("failed: gtm %p", gtm);
    return FALSE;
  }

  gtm46_reset(gtm);
  isp_sub_module->private_data = (void *)gtm;

  return TRUE;
}/* gtm_init */

/** gtm46_destroy:
 *
 *  @module: mct module handle
 *  @isp_sub_module: isp sub module handle
 *
 *  Destroy dynamic resources
 *
 *  Return none
 **/
void gtm46_destroy(mct_module_t *module, isp_sub_module_t *isp_sub_module)
{
  if (isp_sub_module && isp_sub_module->private_data) {
    free(isp_sub_module->private_data);
    isp_sub_module->private_data = NULL;
  }
} /* gtm46_destroy */
