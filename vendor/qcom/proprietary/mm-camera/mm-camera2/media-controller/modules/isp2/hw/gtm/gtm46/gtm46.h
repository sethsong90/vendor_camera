/* gtm46.h
 *
 * Copyright (c) 2014 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __GTM46_H__
#define __GTM46_H__

/* mctl headers */
#include "chromatix.h"

/* isp headers */
#include "isp_sub_module_common.h"
#include "isp_defs.h"
#include "gtm_reg.h"

/** gtm46_t:
 *
 *  @ISP_GTMCfgCmd: GTM register setting
 *  @is_aec_update_valid: indicate whether aec_update is valid
 *  @aec_update: stored aec update from stats update
 *  @algo_ran_once: flag indicating whether algo
 *                  has sent event to module at least once
 *                  indicating that the first LUT has been
 *                  calculated (key
 **/
typedef struct  {
  ISP_GTM_Cfg            reg_cfg;
  uint8_t                is_aec_update_valid;
  aec_update_t           aec_update;
  isp_saved_gtm_params_t algo_output;
  uint64_t               dmi_tbl[GTM_LUT_NUM_BIN-1];
  uint32_t               base[GTM_LUT_NUM_BIN-1];
  int32_t                slope[GTM_LUT_NUM_BIN-1];
  boolean                enable_adrc;
  cam_sensor_hdr_type_t  sensor_hdr_mode;
} gtm46_t;

boolean gtm46_trigger_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gtm46_stats_aec_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gtm46_streamon(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gtm46_streamoff(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gtm46_algo_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gtm46_set_chromatix_ptr(isp_sub_module_t *isp_sub_module,
  void *data);

boolean gtm46_init(mct_module_t *module, isp_sub_module_t *isp_sub_module);

void gtm46_destroy(mct_module_t *module, isp_sub_module_t *isp_sub_module);

#endif //__GTM46_H__
