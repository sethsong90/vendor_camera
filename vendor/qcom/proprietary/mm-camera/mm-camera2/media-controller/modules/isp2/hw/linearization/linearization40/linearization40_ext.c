/* linearization40_ext.c
 *
 * Copyright (c) 2014-2015 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */


/* isp headers */
#include "isp_log.h"
#include "linearization40.h"

/** linearization40_ext_calc_ratio:
 *
 *  @af_ratio: output of this method
 *  @linearization: linearization handle
 *
 * Calculate clamping value
 **/
boolean linearization40_ext_calc_clamp(void *data1,
  void *data2, void *data3)
{
  boolean                  ret = TRUE;
  linearization40_t       *linearization = NULL;
  isp_sub_module_t        *isp_sub_module = NULL;
  isp_sub_module_output_t *sub_module_output = NULL;
  float                    clamping = 0.0f;

  if (!data1|| !data2 || !data3) {
    ISP_ERR("failed: %p %p %p", data1, data2, data3);
    return FALSE;
  }

  linearization = (linearization40_t *)data1;
  isp_sub_module = (isp_sub_module_t *)data2;
  sub_module_output = (isp_sub_module_output_t *)data3;


  if (!linearization->pedestal_enable) {
    ISP_DBG("gr_lut_p: %d, gb_lut_p: %d",
    linearization->linear_lut.gr_lut_p[0],
        linearization->linear_lut.gb_lut_p[0]);
    if ((linearization->linear_lut.gr_lut_delta[0] == 0.0f) &&
      (linearization->linear_lut.gb_lut_delta[0] == 0.0f)) {
      if(linearization->linear_lut.gr_lut_p[0] ==
        linearization->linear_lut.gb_lut_p[0])
        clamping = linearization->linear_lut.gr_lut_p[0];
      else
        clamping = (linearization->linear_lut.gr_lut_p[0] +
          linearization->linear_lut.gb_lut_p[0]) / 2.0f;
    }
  } else {
    ISP_DBG("gr_lut_p: %f, gb_lut_p: %f",
    linearization->blk_level_applied.gr_val,
    linearization->blk_level_applied.gb_val);
    clamping = (linearization->blk_level_applied.gr_val +
          linearization->blk_level_applied.gb_val) / 2.0f;
  }

  ISP_DBG("demux_dbg clamping: %f", clamping);
  if (sub_module_output->algo_params) {
    sub_module_output->algo_params->clamping = clamping;
    sub_module_output->algo_params->linearization_max_val =
      (float)LINEAR_MAX_VAL;
  }
  return ret;
}

/** linearization40_ext_calc_ratio:
 *
 *  @af_ratio: output of this method
 *  @linearization: linearization handle
 *
 * Calculate last slope delta
 **/
boolean linearization40_ext_compute_delta(void *data1,
  void *data2, void *data3)
{
  boolean                  ret = TRUE;
  float                   *base = NULL;
  float                   *lut_p = NULL;
  unsigned int            *delta = NULL;
  int                      i;

  if (!data1|| !data2 || !data3) {
    ISP_ERR("failed: %p %p %p", data1, data2, data3);
    return FALSE;
  }

  delta = (unsigned int *)data1;
  base = (float *)data2;
  lut_p = (float *)data3;

  if(lut_p[0] == 0)
    delta[0] = (1 << DELATQ_FACTOR);
  else
    delta[0] = (unsigned int)(1 << DELATQ_FACTOR) * (float)(base[1] - base[0]) /
      (float)(lut_p[0]);

  for (i = 1; i < 8; i++ ) {
    delta[i] = (unsigned int)Round((float)(1 << DELATQ_FACTOR) *
                    (float)(base[i+1] - base[i]) /
                    (float)(lut_p[i] - lut_p[i-1]));
  }
  if (lut_p[7] < LINEAR_MAX_VAL) {
    delta[8] = (unsigned int)Round((float)(1 << DELATQ_FACTOR));
  } else {
    delta[8] = 0;
  }
  return ret;
}

static ext_override_func linearization_override_func_ext = {
  .ext_calc_clamp         = linearization40_ext_calc_clamp,
  .compute_delta         = linearization40_ext_compute_delta,
};

boolean linearization40_fill_func_table_ext(linearization40_t *linearization)
{
  linearization->ext_func_table = &linearization_override_func_ext;
  return TRUE;
} /* linearization40_fill_func_table_ext */
