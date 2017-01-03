/**********************************************************************
*  Copyright (c) 2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/

#include "cpp_wnr_params.h"
#include "cpp_hw_params.h"
#include "cpp_log.h"

/**
 * Function: cpp_wnr_params_calc_rnr_r_square_shift
 *
 * Description: This function computes radial square shift
 *
 * Arguments:
 *   @p_rnr_int: rnr plane info
 *   @r_sq_q_factor: q factor for r_square
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
static void cpp_wnr_params_calc_rnr_r_square_shift(
  cpp_wnr_rnr_internal_pln_t *p_rnr_int,
  const uint32_t r_sq_q_factor)
{
  double significand, frac;
  int32_t exponent;
  p_rnr_int->r_sq = CPP_SQUARE_SUM(p_rnr_int->center);
  frac = frexp((double)p_rnr_int->r_sq, &exponent);
  significand = CPP_F_TO_Q(frac, r_sq_q_factor);

  if (CPP_IS_POW_2(p_rnr_int->r_sq))
    exponent++;

  p_rnr_int->r_sq_shift =
    CPP_MAX(0, (int32_t)(exponent - (r_sq_q_factor + 1)));
}

/**
 * Function: cpp_wnr_params_calc_rnr_slope_shift
 *
 * Description: This function computes radial slope and shift
 *      for bilateral filers and thresholding
 *
 * Arguments:
 *   @p_adj: pointer to bilateral adjustment factor
 *   @slope: slope w.r.t to bilateral param sq adj w.r.t radial
 *         square
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
static void cpp_wnr_params_calc_rnr_slope_shift(
  cpp_bf_rnr_bilat_adj_t *p_adj,
  double slope)
{
  double fractional;
  uint32_t pseudo_mantissa;
  int32_t exponent;

  fractional = frexp(slope, &exponent);

  pseudo_mantissa = CPP_F_TO_Q_CEIL(fractional, 8);

  if (pseudo_mantissa >= 256) {
    fractional = 0.5f;
    exponent++;
  }
  if ((-exponent - 1) >= 0) {
    p_adj->cf_shift =  (uint8_t)(-exponent - 1);
    p_adj->slope =  CPP_F_TO_Q_CEIL(fractional, 8);
  } else {
    p_adj->cf_shift = 0;
    p_adj->slope =
      CPP_F_TO_Q_CEIL(fractional, 8 + exponent + 1);
  }
}

/**
 * Function: cpp_wnr_params_prepare_rnr_info
 *
 * Description: This function updates RNR segments for bilateral
 *        filters
 *
 * Arguments:
 *   @p_wnr_info: pointer to internal WNR info structure
 *   @p_hw_params: CPP hardware params
 *   @p_noise_prof: pointer to the noise profile
 *
 * Return values:
 *   CPP error values
 *
 * Notes: none
 **/
static int32_t cpp_wnr_params_prepare_rnr_info(
  cpp_wnr_internal_t *p_wnr_info,
  cpp_hardware_params_t *p_hw_params,
  void *p_noise_prof)
{
  cpp_bf_rnr_segment_t *p_out_seg_info;
  cpp_wnr_rnr_internal_seg_t *p_int_seg, *p_int_seg_n;
  uint32_t i, j;
  uint32_t rnr_pln_cnt = CPP_ARRAY_SIZE(p_wnr_info->rnr_pln_int);
  cpp_wnr_rnr_plane_t *p_rnr;
  cpp_wnr_frame_info_t *p_wnr_frame_info =
    &p_hw_params->wnr_frame_info;

  for (i = 0; i < rnr_pln_cnt; i++) {

    p_rnr = &p_hw_params->wnr_frame_info.wnr_pl_info[i].rnr;

    /* update bitlat RNR control */
    if (i == 0) { /*luma*/
      for (j = 0; j < CPP_ARRAY_SIZE(p_rnr->bilat_info); j++) {
        p_rnr->bilat_info[j].weight1_rnr_disable =
          WNR_RADIAL_AND_LEVEL_WEIGHT_Y_ENABLE(p_noise_prof, j);;
      }
    } else { /*chroma*/
      for (j = 0; j < CPP_ARRAY_SIZE(p_rnr->bilat_info); j++) {
        p_rnr->bilat_info[j].weight1_rnr_disable =
          WNR_RADIAL_WEIGHT_CHROMA_ENABLE(p_noise_prof, j);
      }
    }

    for (j = 0; j < CPP_ARRAY_SIZE(p_hw_params->wnr_frame_info.wnr_pl_info[0].
      rnr.seg_info); j++) {
      p_out_seg_info = &p_rnr->seg_info[j];
      p_int_seg = &p_wnr_info->rnr_pln_int[i].seg_info[j];
      p_int_seg_n = &p_wnr_info->rnr_pln_int[i].seg_info[j + 1];

      p_out_seg_info->r_sq_lut = CPP_CLAMP(p_int_seg->r_sq_tbl, 0, 4095);
      p_out_seg_info->bilat_scale_adj.cf =
        CPP_CLAMP(CPP_F_TO_Q(p_int_seg->edgesoftness_adj_factor, 8), 0, 255);
      p_out_seg_info->weight1_adj.cf =
        CPP_CLAMP(CPP_F_TO_Q(p_int_seg->weight_adj_factor, 8), 0, 255);

      if (p_int_seg_n->r_sq_tbl <= p_int_seg->r_sq_tbl) {
        CPP_ERR("[WNR_DBG_%x_%d] Invalid r_sq_tbl level[%d][%d] %f %f",
          p_hw_params->identity, p_hw_params->frame_id, i, j,
          p_int_seg_n->r_sq_tbl, p_int_seg->r_sq_tbl);
        goto error;
      }

      /* calculate slope/shift */
      cpp_wnr_params_calc_rnr_slope_shift(
        &p_out_seg_info->bilat_scale_adj,
        ((p_int_seg_n->edgesoftness_adj_factor -
        p_int_seg->edgesoftness_adj_factor) /
        (p_int_seg_n->r_sq_tbl - p_int_seg->r_sq_tbl)));

      cpp_wnr_params_calc_rnr_slope_shift(
        &p_out_seg_info->weight1_adj,
        ((p_int_seg_n->weight_adj_factor -
        p_int_seg->weight_adj_factor) /
        (p_int_seg_n->r_sq_tbl - p_int_seg->r_sq_tbl)));
    }
  }

  /* duplicate data for other planes */
  p_rnr = &p_hw_params->wnr_frame_info.wnr_pl_info[rnr_pln_cnt - 1].rnr;
  for (i = rnr_pln_cnt;
    i < CPP_ARRAY_SIZE(p_hw_params->wnr_frame_info.wnr_pl_info); i++) {
    p_wnr_frame_info->wnr_pl_info[i].rnr = *p_rnr;
  }
  /* start offset */
  p_wnr_frame_info->rnr_offset_x = 0;
  p_wnr_frame_info->rnr_offset_y = 0;

  return CPP_SUCCESS;
error:
  return CPP_ERR_OUT_OF_BOUNDS;
}

/**
 * Function: cpp_wnr_params_rnr_update
 *
 * Description: This external function is used to
 *          calculate BF RNR params.
 *
 * Arguments:
 *   @p_hw_params: CPP hardware params
 *   @p_noise_prof_start: Start pointer for HW noise profile
 *   @p_noise_prof_end: End pointer for HW wavelet profile
 *   @int_ratio: interpolation ratio
 *
 * Return values:
 *   CPP error values
 *
 * Notes: none
 **/
int32_t cpp_wnr_params_rnr_update(void *p_params,
  void *p_noise_prof_start,
  void *p_noise_prof_end,
  float int_ratio)
{
  uint32_t i, j;
  int32_t rc;
  cpp_wnr_internal_t l_wnr_info; /* local as the context is not saved */
  cpp_wnr_rnr_internal_pln_t *p_rnr_int;
  cpp_hardware_params_t *p_hw_params = (cpp_hardware_params_t *)p_params;
  cpp_bf_rnr_segment_t *p_seg_info;
  cpp_wnr_rnr_internal_seg_t *p_int_seg_info;

  if (!WNR_RNR_LNR_VALID(p_noise_prof_start)) {
    CPP_DBG("[WNR_DBG_%x_%d] Error updating RNR params",
      p_hw_params->identity,
      p_hw_params->frame_id);
    (void)p_noise_prof_start;
    (void)p_noise_prof_end;
    return CPP_SUCCESS;
  }

  /* update scale factors */
  l_wnr_info.rnr_pln_int[0].scale_factor = 1.0f;
  l_wnr_info.rnr_pln_int[1].scale_factor = 0.5f;

  /* compute internal params */
  for (i = 0; i < CPP_ARRAY_SIZE(l_wnr_info.rnr_pln_int); i++) {
    p_rnr_int = &l_wnr_info.rnr_pln_int[i];

    p_rnr_int->center.x = round(p_hw_params->input_info.width *
      p_rnr_int->scale_factor);
    p_rnr_int->center.y = round(p_hw_params->input_info.height *
      p_rnr_int->scale_factor);
    cpp_wnr_params_calc_rnr_r_square_shift(p_rnr_int, 12);

    /* calc intermediate segment results */
    for (j = 0; j < CPP_ARRAY_SIZE(l_wnr_info.rnr_pln_int[0].seg_info); j++) {
      p_int_seg_info = &l_wnr_info.rnr_pln_int[i].seg_info[j];
      p_int_seg_info->r_sq_tbl =
        round(WNR_RADIAL_PT_TABLE(p_noise_prof_start, i)
        * p_rnr_int->r_sq / CPP_TO_Q(p_rnr_int->r_sq_shift + 1));

      if (i == 0) {
        p_int_seg_info->edgesoftness_adj_factor =
          LINEAR_INTERPOLATE(int_ratio,
            WNR_RADIAL_EDGESOFTNESS_ADJ_FACTOR_Y(p_noise_prof_start, j),
            WNR_RADIAL_EDGESOFTNESS_ADJ_FACTOR_Y(p_noise_prof_end, j));
        p_int_seg_info->weight_adj_factor =
          LINEAR_INTERPOLATE(int_ratio,
            WNR_RADIAL_WEIGHT_ADJ_FACTOR_Y(p_noise_prof_start, j),
            WNR_RADIAL_WEIGHT_ADJ_FACTOR_Y(p_noise_prof_end, j));
      } else {
        p_int_seg_info->edgesoftness_adj_factor =
          LINEAR_INTERPOLATE(int_ratio,
            WNR_RADIAL_EDGESOFTNESS_ADJ_FACTOR_CHROMA(p_noise_prof_start, j),
            WNR_RADIAL_EDGESOFTNESS_ADJ_FACTOR_CHROMA(p_noise_prof_end, j));
        p_int_seg_info->weight_adj_factor =
          LINEAR_INTERPOLATE(int_ratio,
            WNR_RADIAL_WEIGHT_ADJ_FACTOR_CHROMA(p_noise_prof_start, j),
            WNR_RADIAL_WEIGHT_ADJ_FACTOR_CHROMA(p_noise_prof_end, j));
      }
    }
  }

  rc = cpp_wnr_params_prepare_rnr_info(&l_wnr_info, p_hw_params,
    p_noise_prof_start);
  /* start filling the output */
  if (IS_CPP_ERROR(rc)) {
    CPP_ERR("[WNR_DBG_%x_%d] Error updating RNR params",
      p_hw_params->identity,
      p_hw_params->frame_id);
  }

  return rc;
}

/**
 * Function: cpp_wnr_params_prepare_lnr_pt
 *
 * Description: This function is used to compute the LNR curve
 *
 * Arguments:
 *   @p_in_point: current point
 *   @p_in_n_point: next point
 *   @p_out_point: output info
 *
 * Return values:
 *   none
 *
 * Notes: none
 **/
static inline void cpp_wnr_params_prepare_lnr_pt(
  cpp_bf_lnr_curve_t *p_in_point,
  cpp_bf_lnr_curve_t *p_in_n_point,
  cpp_bf_lnr_curve_t *p_out_point)
{
  p_out_point->cf    = p_in_point->cf;
  p_out_point->slope = CPP_CLAMP(p_in_n_point->cf - p_in_point->cf, -128, 127);
}

/**
 * Function: cpp_wnr_params_prepare_lnr_info
 *
 * Description: This function is used to
 *          update BF LNR params.
 *
 * Arguments:
 *   @p_points: list of LNR points
 *   @p_hw_params: CPP hardware params
 *
 * Return values:
 *   CPP error values
 *
 * Notes: updates only the luma
 **/
int32_t cpp_wnr_params_prepare_lnr_info(cpp_bf_lnr_region_t *p_points,
  cpp_hardware_params_t *p_hw_params)
{
  uint32_t i;
  cpp_wnr_lnr_plane_t *p_lnr = &p_hw_params->wnr_frame_info.wnr_pl_info[0].lnr;
  cpp_wnr_frame_info_t *p_wnr_frame_info =
    &p_hw_params->wnr_frame_info;

  for (i = 0; i < CPP_ARRAY_SIZE(p_lnr->lregion_info); i++) {
    cpp_wnr_params_prepare_lnr_pt(&p_points[i].bilat_scale,
      &p_points[i].bilat_scale,
      &p_lnr->lregion_info[i].bilat_scale);
    cpp_wnr_params_prepare_lnr_pt(&p_points[i].weight1,
      &p_points[i].weight1,
      &p_lnr->lregion_info[i].weight1);
  }

  /* update data for other planes */
  for (i = 1; i < CPP_ARRAY_SIZE(p_hw_params->wnr_frame_info.wnr_pl_info);
    i++) {
    memset(&p_wnr_frame_info->wnr_pl_info[i].lnr, 0x0,
      sizeof(p_wnr_frame_info->wnr_pl_info[i].lnr));
  }
  return CPP_SUCCESS;
}

/**
 * Function: cpp_wnr_params_dbg_chromatix_np
 *
 * Description: This function is used to dump chromatix params
 *
 * Arguments:
 *   @p_np: pointer to noise profile
 *   @deb_str: debug string
 *   @p_params: pointer to CPP hw params
 *
 * Return values:
 *   none
 *
 * Notes: updates only the luma
 **/
void cpp_wnr_params_dbg_chromatix_np(void *p_np __unused,
  const char *deb_str,
  void *p_params)
{
  uint32_t i;
  cpp_hardware_params_t *p_hw_params = (cpp_hardware_params_t *)p_params;
  uint32_t id = p_hw_params->identity;
  uint32_t f_id = p_hw_params->frame_id;

  CPP_DBG("[WNR_DBG_CHRX_%x_%d] CPP debug %s", deb_str, id, f_id);
  for (i = 0; i < BILITERAL_LAYERS; i++) {
    CPP_DBG("[WNR_DBG_CHRX] WNR_RADIAL_AND_LEVEL_WEIGHT_Y_ENABLE[%d] = %d",
      i, WNR_RADIAL_AND_LEVEL_WEIGHT_Y_ENABLE(p_np, i));
  }
  for (i = 0; i < BILITERAL_LAYERS; i++) {
    CPP_DBG("[WNR_DBG_CHRX] WNR_RADIAL_WEIGHT_CHROMA_ENABLE[%d] = %d",
      i, WNR_RADIAL_WEIGHT_CHROMA_ENABLE(p_np, i));
  }
  for (i = 0; i < WNR_LNR_POINTS; i++) {
    CPP_DBG("[WNR_DBG_CHRX] WNR_LEVEL_EDGESOFTNESS_ADJ_FACTOR[%d] = %d",
      i, WNR_LEVEL_EDGESOFTNESS_ADJ_FACTOR(p_np, i));
  }
  for (i = 0; i < WNR_LNR_POINTS; i++) {
    CPP_DBG("[WNR_DBG_CHRX] WNR_LEVEL_WEIGHT_ADJ_FACTOR[%d] = %d",
      i, WNR_LEVEL_WEIGHT_ADJ_FACTOR(p_np, i));
  }
  for (i = 0; i < WNR_RNR_POINTS; i++) {
    CPP_DBG("[WNR_DBG_CHRX] WNR_RADIAL_EDGESOFTNESS_ADJ_FACTOR_Y[%d] = %d",
      i, WNR_RADIAL_EDGESOFTNESS_ADJ_FACTOR_Y(p_np, i));
  }
  for (i = 0; i < WNR_RNR_POINTS; i++) {
    CPP_DBG("[WNR_DBG_CHRX] WNR_RADIAL_EDGESOFTNESS_ADJ_FACTOR_CHROMA[%d] = %d",
      i, WNR_RADIAL_EDGESOFTNESS_ADJ_FACTOR_CHROMA(p_np, i));
  }
  for (i = 0; i < WNR_RNR_POINTS; i++) {
    CPP_DBG("[WNR_DBG_CHRX] WNR_RADIAL_WEIGHT_ADJ_FACTOR_Y[%d] = %d",
      i, WNR_RADIAL_WEIGHT_ADJ_FACTOR_Y(p_np, i));
  }
  for (i = 0; i < WNR_RNR_POINTS; i++) {
    CPP_DBG("[WNR_DBG_CHRX] WNR_RADIAL_WEIGHT_ADJ_FACTOR_CHROMA[%d] = %d",
      i, WNR_RADIAL_WEIGHT_ADJ_FACTOR_CHROMA(p_np, i));
  }
  for (i = 0; i < WNR_RNR_POINTS; i++) {
    CPP_DBG("[WNR_DBG_CHRX] WNR_RADIAL_PT_TABLE[%d] = %d",
      i, WNR_RADIAL_PT_TABLE(p_np, i));
  }
}

/**
 * Function: cpp_wnr_params_lnr_update
 *
 * Description: This external function is used to
 *          calculate BF LNR params.
 *
 * Arguments:
 *   @p_hw_params: CPP hardware params
 *   @p_noise_prof_start: Start pointer for HW noise profile
 *   @p_noise_prof_end: End pointer for HW wavelet profile
 *   @int_ratio: interpolation ratio
 *
 * Return values:
 *   CPP error values
 *
 * Notes: none
 **/
int32_t cpp_wnr_params_lnr_update(void *p_params,
  void *p_noise_prof_start,
  void *p_noise_prof_end,
  float int_ratio)
{
  cpp_hardware_params_t *p_hw_params = (cpp_hardware_params_t *)p_params;
  cpp_bf_lnr_region_t lpoints[WNR_LNR_POINTS];
  uint32_t i;
  int32_t rc = CPP_SUCCESS;

  /* interpolate params */
  for (i = 0; i < CPP_ARRAY_SIZE(lpoints); i++) {
    if (p_noise_prof_start != p_noise_prof_end) {
      lpoints[i].bilat_scale.cf = CPP_F_TO_Q(LINEAR_INTERPOLATE(int_ratio,
        WNR_LEVEL_EDGESOFTNESS_ADJ_FACTOR(p_noise_prof_start, i),
        WNR_LEVEL_EDGESOFTNESS_ADJ_FACTOR(p_noise_prof_end, i)), 8);
      lpoints[i].weight1.cf = CPP_F_TO_Q(LINEAR_INTERPOLATE(int_ratio,
        WNR_LEVEL_WEIGHT_ADJ_FACTOR(p_noise_prof_start, i),
        WNR_LEVEL_WEIGHT_ADJ_FACTOR(p_noise_prof_end, i)), 8);
    } else {
      lpoints[i].bilat_scale.cf = CPP_F_TO_Q(WNR_LEVEL_EDGESOFTNESS_ADJ_FACTOR(
        p_noise_prof_start, i), 8);
      lpoints[i].weight1.cf = CPP_F_TO_Q(WNR_LEVEL_WEIGHT_ADJ_FACTOR(
        p_noise_prof_start, i), 8);
    }
    lpoints[i].bilat_scale.cf = CPP_CLAMP(lpoints[i].bilat_scale.cf, 0, 128);
    lpoints[i].weight1.cf = CPP_CLAMP(lpoints[i].weight1.cf, 0, 128);
  }

  /* update */
  rc = cpp_wnr_params_prepare_lnr_info(lpoints, p_hw_params);
  if (IS_CPP_ERROR(rc)) {
    CPP_ERR("LNR update failed");
  }
  return rc;
}

/**
 * Function: cpp_wnr_params_dbg_output
 *
 * Description: This function is used to dump chromatix params
 *
 * Arguments:
 *   @p_params: pointer to CPP hw params
 *
 * Return values:
 *   none
 *
 * Notes: updates only the luma
 **/
void cpp_wnr_params_dbg_output(void *p_params)
{
  uint32_t i, j;
  cpp_hardware_params_t *p_hw_params = (cpp_hardware_params_t *)p_params;
  cpp_wnr_plane_t *p_wnr;
  uint32_t id = p_hw_params->identity;
  uint32_t f_id = p_hw_params->frame_id;

  for (i = 0; CPP_ARRAY_SIZE(p_hw_params->wnr_frame_info.wnr_pl_info); i++) {
    p_wnr = &p_hw_params->wnr_frame_info.wnr_pl_info[i];

    /* RNR */
    CPP_DBG("[WNR_DBG_OUT_%x_%d] r_sq_shift %u",
      id, f_id, p_wnr->rnr.r_sq_shift);
    for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->rnr.bilat_info); j++) {
      CPP_DBG("[WNR_DBG_OUT_%x_%d] weight1_rnr_disable[%d] %u", id, f_id, j,
        p_wnr->rnr.bilat_info[j].weight1_rnr_disable);
    }
    for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->rnr.seg_info); j++) {
      CPP_DBG("[WNR_DBG_OUT_%x_%d] bilat_scale_adj.cf[%d] %u", id, f_id, j,
        p_wnr->rnr.seg_info[j].bilat_scale_adj.cf);
    }
    for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->rnr.seg_info); j++) {
      CPP_DBG("[WNR_DBG_OUT_%x_%d] bilat_scale_adj.cf_shift[%d] %u",
        id, f_id, j,
        p_wnr->rnr.seg_info[j].bilat_scale_adj.cf_shift);
    }
    for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->rnr.seg_info); j++) {
      CPP_DBG("[WNR_DBG_OUT_%x_%d] bilat_scale_adj.slope[%d] %u", id, f_id, j,
        p_wnr->rnr.seg_info[j].bilat_scale_adj.slope);
    }
    for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->rnr.bilat_info); j++) {
      CPP_DBG("[WNR_DBG_OUT_%x_%d] weight1_rnr_disable[%d] %u", id, f_id, j,
        p_wnr->rnr.bilat_info[j].weight1_rnr_disable);
    }
    for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->rnr.seg_info); j++) {
      CPP_DBG("[WNR_DBG_OUT_%x_%d] weight1_adj.cf[%d] %u", id, f_id, j,
        p_wnr->rnr.seg_info[j].weight1_adj.cf);
    }
    for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->rnr.seg_info); j++) {
      CPP_DBG("[WNR_DBG_OUT_%x_%d] weight1_adj.cf_shift[%d] %u", id, f_id, j,
        p_wnr->rnr.seg_info[j].weight1_adj.cf_shift);
    }
    for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->rnr.seg_info); j++) {
      CPP_DBG("[WNR_DBG_OUT_%x_%d] weight1_adj.slope[%d] %u", id, f_id, j,
        p_wnr->rnr.seg_info[j].weight1_adj.slope);
    }
    for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->rnr.seg_info); j++) {
      CPP_DBG("[WNR_DBG_OUT_%x_%d] weight1_adj.r_sq_lut[%d] %u", id, f_id, j,
        p_wnr->rnr.seg_info[j].r_sq_lut);
    }
  }

  /* LNR */
  for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->lnr.lregion_info); j++) {
    CPP_DBG("[WNR_DBG_OUT_%x_%d] bilat_scale.cf[%d] %u", id, f_id, j,
      p_wnr->lnr.lregion_info[j].bilat_scale.cf);
  }
  for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->lnr.lregion_info); j++) {
    CPP_DBG("[WNR_DBG_OUT_%x_%d] bilat_scale.slope[%d] %u", id, f_id, j,
      p_wnr->lnr.lregion_info[j].bilat_scale.slope);
  }
  for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->lnr.lregion_info); j++) {
    CPP_DBG("[WNR_DBG_OUT_%x_%d] weight1.cf[%d] %u", id, f_id, j,
      p_wnr->lnr.lregion_info[j].weight1.cf);
  }
  for (j = 0; j < CPP_ARRAY_SIZE(p_wnr->lnr.lregion_info); j++) {
    CPP_DBG("[WNR_DBG_OUT_%x_%d] weight1.slope[%d] %u", id, f_id, j,
      p_wnr->lnr.lregion_info[j].weight1.slope);
  }
}
