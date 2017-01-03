/* sce_algo.c
 *
 * Copyright (c) 2016 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */

/* std headers */
#include <unistd.h>

#undef ISP_DBG
#define ISP_DBG(fmt, args...) \
  ISP_DBG_MOD(ISP_LOG_CHROMA_ENHANCE, fmt, ##args)
#undef ISP_HIGH
#define ISP_HIGH(fmt, args...) \
  ISP_HIGH_MOD(ISP_LOG_CHROMA_ENHANCE, fmt, ##args)

/* isp headers */
#include "isp_sub_module_log.h"
#include "isp_defs.h"
#include "isp_sub_module_util.h"
#include "sce40.h"


/** sce40_adjust_dest_triangle_by_adrc
 *
 *  @sce: adrc configuration from Chromatix Header
 *
 * updates the sce triangles based on ADRC configuration.
 *
 **/
boolean sce_adjust_dest_triangle_by_adrc(sce40_t *sce,
  Chromatix_ADRC_SCE_type *adrc_sce_data, aec_update_t aec_update)
{
  boolean ret = TRUE;
  chromatix_SCE_type chromatix_SCE_data_1;
  ADRC_SCE_CCT_type sce_cct1, sce_cct2, sce_cct_final;
  float trigger, Interp_ratio, trigger_start[MAX_SETS_FOR_ADRC_SCE_ADJ],
    trigger_end[MAX_SETS_FOR_ADRC_SCE_ADJ];
  int RegionIdxStrt, RegionIdxEnd, i;

  if ((adrc_sce_data->control_adrc_sce == CONTROL_AEC_EXP_SENSITIVITY_RATIO) ||
      (adrc_sce_data->control_adrc_sce == CONTROL_DRC_GAIN)) {
    for (i = 0; i < MAX_SETS_FOR_ADRC_SCE_ADJ; i++) {
      if (adrc_sce_data->control_adrc_sce ==
          CONTROL_AEC_EXP_SENSITIVITY_RATIO) {
        trigger = aec_update.hdr_sensitivity_ratio;
        trigger_start[i] = (float)adrc_sce_data->
          adrc_sce_core_data[i].aec_sensitivity_ratio.start;
        trigger_end[i] = (float)adrc_sce_data->
          adrc_sce_core_data[i].aec_sensitivity_ratio.end;
      } else {
        trigger = aec_update.color_drc_gain;
        trigger_start[i] =
          (float)adrc_sce_data->adrc_sce_core_data[i].drc_gain_trigger.start;
        trigger_end[i] =
          (float)adrc_sce_data->adrc_sce_core_data[i].drc_gain_trigger.end;
      }
    }
    isp_sub_module_util_find_region_index_spatial(trigger, trigger_start,
      trigger_end, &Interp_ratio,&RegionIdxStrt, &RegionIdxEnd,
      MAX_SETS_FOR_ADRC_SCE_ADJ);
    sce_cct1 =
      adrc_sce_data->adrc_sce_core_data[RegionIdxStrt].adrc_sce_cct_data;
    if (RegionIdxStrt != RegionIdxEnd) {
      sce_cct2 =
        adrc_sce_data->adrc_sce_core_data[RegionIdxEnd].adrc_sce_cct_data;

      trigger_interpolate_sce_triangles_int(&sce_cct2.origin_triangles_TL84,
        &sce_cct1.origin_triangles_TL84,
        &chromatix_SCE_data_1.origin_triangles_TL84, Interp_ratio);

      trigger_interpolate_sce_triangles_int(
        &sce_cct2.destination_triangles_TL84,
        &sce_cct1.destination_triangles_TL84,
        &chromatix_SCE_data_1.destination_triangles_TL84, Interp_ratio);

      trigger_interpolate_sce_triangles_int(&sce_cct2.origin_triangles_A,
        &sce_cct1.origin_triangles_A,
        &chromatix_SCE_data_1.origin_triangles_A, Interp_ratio);

      trigger_interpolate_sce_triangles_int(&sce_cct2.destination_triangles_A,
        &sce_cct1.destination_triangles_A,
        &chromatix_SCE_data_1.destination_triangles_A, Interp_ratio);

      trigger_interpolate_sce_triangles_int(&sce_cct2.origin_triangles_H,
        &sce_cct1.origin_triangles_H,
        &chromatix_SCE_data_1.origin_triangles_H, Interp_ratio);

      trigger_interpolate_sce_triangles_int(&sce_cct2.destination_triangles_H,
        &sce_cct1.destination_triangles_H,
        &chromatix_SCE_data_1.destination_triangles_H, Interp_ratio);

      trigger_interpolate_sce_triangles_int(&sce_cct2.origin_triangles_D65,
        &sce_cct1.origin_triangles_D65,
        &chromatix_SCE_data_1.origin_triangles_D65, Interp_ratio);

      trigger_interpolate_sce_triangles_int(
        &sce_cct2.destination_triangles_D65,
        &sce_cct1.destination_triangles_D65,
        &chromatix_SCE_data_1.destination_triangles_D65, Interp_ratio);

      trigger_interpolate_sce_vectors(&sce_cct2.shift_vector_TL84,
        &sce_cct1.shift_vector_TL84,
        &chromatix_SCE_data_1.shift_vector_TL84, Interp_ratio);
      trigger_interpolate_sce_vectors(&sce_cct2.shift_vector_A,
        &sce_cct1.shift_vector_A,
        &chromatix_SCE_data_1.shift_vector_A, Interp_ratio);
      trigger_interpolate_sce_vectors(&sce_cct2.shift_vector_H,
        &sce_cct1.shift_vector_H,
        &chromatix_SCE_data_1.shift_vector_H, Interp_ratio);
      trigger_interpolate_sce_vectors(&sce_cct2.shift_vector_D65,
        &sce_cct1.shift_vector_D65,
        &chromatix_SCE_data_1.shift_vector_D65,Interp_ratio);
    } else {
      sce_cct_final =
        adrc_sce_data->adrc_sce_core_data[RegionIdxStrt].adrc_sce_cct_data;
      chromatix_SCE_data_1.origin_triangles_TL84 =
        sce_cct1.origin_triangles_TL84;
      chromatix_SCE_data_1.destination_triangles_TL84 =
        sce_cct1.destination_triangles_TL84;
      chromatix_SCE_data_1.shift_vector_TL84 = sce_cct1.shift_vector_TL84;
      chromatix_SCE_data_1.origin_triangles_A = sce_cct1.origin_triangles_A;
      chromatix_SCE_data_1.destination_triangles_A =
        sce_cct1.destination_triangles_A;
      chromatix_SCE_data_1.shift_vector_A = sce_cct1.shift_vector_A;
      chromatix_SCE_data_1.origin_triangles_H = sce_cct1.origin_triangles_H;
      chromatix_SCE_data_1.destination_triangles_H =
        sce_cct1.destination_triangles_H;
      chromatix_SCE_data_1.shift_vector_H = sce_cct1.shift_vector_H;
      chromatix_SCE_data_1.origin_triangles_D65 =
        sce_cct1.origin_triangles_D65;
      chromatix_SCE_data_1.destination_triangles_D65 =
        sce_cct1.destination_triangles_D65;
      chromatix_SCE_data_1.shift_vector_D65 = sce_cct1.shift_vector_D65;
    }
    memcpy(&chromatix_SCE_data_1.SCE_A_trigger, &adrc_sce_data->SCE_A_trigger,
      sizeof(chromatix_CCT_trigger_type));
    memcpy(&chromatix_SCE_data_1.SCE_D65_trigger,
      &adrc_sce_data->SCE_D65_trigger, sizeof(chromatix_CCT_trigger_type));
    memcpy(&chromatix_SCE_data_1.SCE_H_trigger, &adrc_sce_data->SCE_H_trigger,
      sizeof(chromatix_CCT_trigger_type));

    /* Now copy the triangle from chromatix_SCE_data_1 to sce.*/
    sce_copy_triangles_from_chromatix(sce, &chromatix_SCE_data_1);

    /*CCT interpolate*/
    ret =
      trigger_sce_get_triangles(sce, &chromatix_SCE_data_1, sce->cur_cct_type);
    if (ret == FALSE) {
      ISP_ERR("failed, trigger_sce_get_triangles");
      return FALSE;
    }
  }
  return ret;
}

