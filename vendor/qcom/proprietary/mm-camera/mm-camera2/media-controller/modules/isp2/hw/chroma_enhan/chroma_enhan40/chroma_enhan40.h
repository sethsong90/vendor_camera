/* chroma_enhan40.h
 *
 * Copyright (c) 2012-2014 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __CHROMA_ENHAN40_H__
#define __CHROMA_ENHAN40_H__

/* isp headers */
#include "module_chroma_enhan40.h"
#include "isp_sub_module_common.h"

boolean chroma_enhan40_init(mct_module_t *module, isp_sub_module_t *isp_sub_module);

void chroma_enhan40_destroy(mct_module_t *module, isp_sub_module_t *isp_sub_module);

boolean chroma_enhan40_handle_isp_private_event(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

void chroma_enhan40_update_streaming_mode_mask(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, uint32_t streaming_mode_mask);

boolean chroma_enhan40_streamon(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean chroma_enhan40_streamoff(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean chroma_enhan40_trigger_update_aec(chroma_enhan40_t *mod,
  chromatix_parms_type *chromatix_ptr);

boolean chroma_enhan40_trigger_update_awb(chroma_enhan40_t *mod,
  chromatix_parms_type *chromatix_ptr);

boolean chroma_enhan40_trigger_update(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean chroma_enhan40_save_awb_params(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean chroma_enhan40_save_aec_params(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean chroma_enhan40_get_vfe_diag_info_user(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean chroma_enhan_set_bestshot(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean chroma_enhan_set_effect(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean chroma_enhan_set_spl_effect(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

boolean chroma_enhan_set_chromatix_ptr(mct_module_t *module,
  isp_sub_module_t *isp_sub_module, mct_event_t *event);

#endif /* __CHROMA_ENHAN40_H__ */
