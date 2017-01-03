/* awb_port.h
 *
 * Copyright (c) 2013-2015 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __AWB_PORT_H__
#define __AWB_PORT_H__
#include "modules.h"
#include "awb.h"
#include "q3a_thread.h"

#define AWB_PORT_CONVERGE_REQ 2 /* Requirement to consider AWB converged */

typedef enum {
  AWB_PORT_STATE_CREATED,
  AWB_PORT_STATE_RESERVED,
  AWB_PORT_STATE_LINKED,
  AWB_PORT_STATE_UNLINKED,
  AWB_PORT_STATE_UNRESERVED
} awb_port_state_t;

typedef struct {
  awb_manual_update_t u;
  boolean valid;
  /* cct, gain or max */
  manual_wb_mode_type         manual_wb_type;
} awb_port_m_gain_t;

typedef struct {
  uint32_t frames_req; /* Tuning: Frames req to be consider converged */
  uint32_t frame_counter; /* Counter of frames while converging */
  int32_t previous_color_temp; /* Previous color temperature reported by algo */
  uint32_t prev_stats_id;
} awb_port_converge_t;

/** awb_frame_capture_t:
 *    @skip_stats_start_id:  Stat id to start skip logic
 *    @skip_count:      Number of stats to skip
**/
typedef struct {
  awb_frame_batch_t   frame_info;
  boolean             frame_capture_mode;
  int8_t              current_batch_count;
} awb_frame_capture_t;

/** awb_port_private
 *    @awb_object: session index
 *    @port:       stream index
 *
 * Each awb moduld object should be used ONLY for one Bayer
 * serssin/stream set - use this structure to store session
 * and stream indices information.
 **/
typedef struct _awb_port_private {
  unsigned int         reserved_id;
  cam_stream_type_t    stream_type;
  void                 *awb_iface_lib;
  boolean              awb_extension_use;
  boolean              use_default_algo;
  awb_port_state_t     state;
  boolean              awb_update_flag;
  awb_object_t         awb_object;
  q3a_thread_data_t    *thread_data;
  unsigned int         cur_sof_id;
  uint32_t             cur_stats_id;
  uint8_t              awb_state;
  cam_awb_state_t      awb_last_state;
  boolean              awb_locked;
  awb_operation_mode_t op_mode;
  int32_t              current_wb;
  cam_area_t           awb_roi;
  uint32_t             max_sensor_delay;
  boolean              bg_stats_enabled;
  char                 awb_debug_data_array[AWB_DEBUG_DATA_SIZE];
  uint32_t             awb_debug_data_size;
  awb_port_converge_t  awb_converge;
  uint32_t             vfe_out_width;
  uint32_t             vfe_out_height;
  uint32_t             preview_width;
  uint32_t             preview_height;
  uint32_t             required_stats_mask;
  uint32_t             required_stats_mask_offline;
  /* HAL3 */
  awb_port_m_gain_t    manual;
  uint8_t              awb_auto_mode;
  uint8_t              awb_on_off_mode;
  uint8_t              awb_meta_mode;
  boolean              awb_ccm_enable;
  /* save the output, so we can send out the awb update for snapshot right away */
  awb_output_data_t    awb_output;
  /* flag to indicate if there is a capture intent for current SOF */
  uint32_t             awb_still_capture_sof;
  awb_frame_capture_t  stats_frame_capture;
  q3a_fast_aec_data_t  fast_aec_data;
  sem_t                sem_offline_proc;
  awb_output_data_t    offline_output;
  /* Hook to extend functionality */
  stats_util_override_func_t func_tbl;
  void                 *ext_param;
  cam_sync_type_t      dual_cam_sensor_info;
  uint32_t             intra_peer_id;
  boolean              is_still_capture;
  boolean              flash_on;
  /* Params will get updated in current sessiona and will be used in next
  session for improving the launch convergence */
  awb_stored_params_type *stored_params;
  uint16_t             fast_aec_forced_cnt;
  uint32               capture_intent_skip;
  aec_convergence_type instant_aec_type;
  awb_output_data_t    output_buffer;
} awb_port_private_t;


void    awb_port_deinit(mct_port_t *port);
boolean awb_port_find_identity(mct_port_t *port, unsigned int identity);
boolean awb_port_init(mct_port_t *port, unsigned int *session_id);
void    awb_port_set_stored_parm(mct_port_t *port, awb_stored_params_type* stored_params);
void awb_send_bus_msg(mct_port_t *port, mct_bus_msg_type_t bus_msg_type,
  void *payload, int size , int sof_id, q3a_stats_stream_type stream_type);
void * awb_port_load_function(awb_ops_t *awb_ops);
void awb_port_unload_function(awb_port_private_t *private);
boolean awb_port_set_session_data(mct_port_t *port, void *q3a_lib_info,
  cam_position_t cam_position, unsigned int *sessionid);
boolean awb_port_load_dummy_default_func(awb_ops_t *awb_ops);

#endif /* __AWB_PORT_H__ */
