/* gamma44.h
 * Copyright (c) 2012-2016 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */

#ifndef __GAMMA44_H__
#define __GAMMA44_H__

/* isp headers */
#include "module_gamma.h"
#include "isp_sub_module_common.h"

boolean gamma44_init(mct_module_t *module,
  isp_sub_module_t *isp_sub_module);

void gamma44_destroy(mct_module_t *module,
  isp_sub_module_t *isp_sub_module);

boolean gamma44_query_cap(mct_module_t *module,
  void *query_buf);

boolean gamma44_stats_aec_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gamma44_stats_asd_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gamma44_set_chromatix_ptr(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gamma44_trigger_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gamma44_get_vfe_diag_info_user(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gamma44_set_contrast(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gamma44_set_bestshot(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gamma44_set_spl_effect(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean gamma44_streamon(isp_sub_module_t *isp_sub_module, void *data);

boolean gamma44_get_interpolated_table(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

#endif /* __GAMMA44_H__ */
