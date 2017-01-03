/*============================================================================
Copyright (c) 2013 Qualcomm Technologies, Inc. All Rights Reserved.
Qualcomm Technologies Proprietary and Confidential.
============================================================================*/

#ifndef __ISPIF_CORE_H__
#define __ISPIF_CORE_H__

#include "cam_intf.h"
#include "mct_controller.h"
#include "modules.h"
#include "iface_def.h"
#include "isp_event.h"
#include "iface.h"

uint32_t iface_ispif_util_find_primary_cid(sensor_out_info_t *sensor_cfg,
  sensor_src_port_cap_t *sensor_cap);
void iface_ispif_destroy(iface_ispif_t *ispif);
int iface_ispif_proc_streamon(iface_t *iface, iface_session_t *session,
  iface_port_t *iface_sink_port, int num_hw_streams, uint32_t *hw_stream_ids);
int iface_ispif_streamoff(iface_t *iface, iface_session_t *session,
  iface_port_t *iface_sink_port, uint32_t num_hw_streams,
  uint32_t *hw_stream_ids);
int iface_ispif_get_cfg_params_from_hw_streams(iface_t *iface,
  iface_port_t *iface_sink_port, iface_session_t *session,
  int num_hw_streams, uint32_t *hw_stream_ids,
  struct ispif_cfg_data *cfg_cmd, boolean start);
int iface_ispif_hw_reset(iface_t *iface,
  iface_port_t *iface_sink_port, iface_session_t *session);
#endif /* __ISPIF_CORE_H__ */

