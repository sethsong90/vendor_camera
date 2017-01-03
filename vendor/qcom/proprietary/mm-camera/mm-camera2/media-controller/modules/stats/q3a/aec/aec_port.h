/* aec_port.h
 *
 * Copyright (c) 2013-2016 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __PORT_AEC_H__
#define __PORT_AEC_H__

#include <mct_stream.h>
#include "q3a_thread.h"
#include "modules.h"

#define AEC_OUTPUT_ARRAY_MAX_SIZE 2 //Can accommodate upto two outputs in parallel

/* Every AEC sink port ONLY corresponds to ONE session */

typedef enum {
  AEC_PORT_STATE_CREATED,
  AEC_PORT_STATE_RESERVED,
  AEC_PORT_STATE_LINKED,
  AEC_PORT_STATE_UNLINKED,
  AEC_PORT_STATE_UNRESERVED
} aec_port_state_t;


typedef enum {
  AEC_AUTO,
  AEC_PARTIAL_AUTO,
  AEC_MANUAL
} aec_auto_mode_t;

typedef enum {
  AEC_PORT_STATE_ALGO_UPDATE,
  AEC_PORT_STATE_MODE_UPDATE,
} aec_port_state_update_type_t;

/** aec_port_manual_setting_t:
 *    @is_gain_valid:   is valid
 *    @gain_on_preview: Apply gain also in preview not only in capture
 *    @gain:   manual gain in float
 *    @is_exp_time_valid: is valid
 *    @exp_time_on_preview: Apply exposure time also in preview not only in capture
 *    @exp_time: manual exp time in ms
**/
typedef struct {
  boolean is_gain_valid;
  boolean gain_on_preview;
  float   gain;
  boolean is_exp_time_valid;
  boolean exp_time_on_preview;
  float   exp_time; /* in sec */
}aec_port_manual_setting_t;

typedef struct {
  aec_port_state_update_type_t type;
  uint8_t cb_output_index; // aec output index used from CB
  uint8_t sof_output_index; // aec output index used from SOF
  union {
    aec_output_data_t output[AEC_OUTPUT_ARRAY_MAX_SIZE];
    boolean           trigger_new_mode;
  } u;
} aec_state_update_data_t;

/** aec_port_capture_intent_t:
 *    @capture_sof:         Indicate the SOF # of the last capture intent
 *    @is_capture_intent:  Indicate if it's still capture intent for HAL3
 *    @is_flash_snap_data:  Flash snap data available
 *    @flash_real_gain:     Flash gain
 *    @flash_sensor_gain:   Flash sensor gain
 *    @flash_line_cnt:      Flash line count
 *    @flash_lux_index:     Flash lux index use by ISP
 *    @flash_drc_gains:     Flash drc gains
 *    @flash_exp_time:      Flash expsoure time
 *    @flash_exif_iso:      Flash exif
**/
typedef struct {
  uint32_t  capture_sof;
  uint8_t   is_capture_intent;
  boolean   is_flash_snap_data;
  float     flash_real_gain;
  float     flash_sensor_gain;
  uint32_t  flash_line_cnt;
  float       flash_lux_index;
  aec_adrc_gain_params_t flash_drc_gains;
  float     flash_exp_time;
  uint32_t  flash_exif_iso;
} aec_port_capture_intent_t;

/** aec_skip_t:
 *    @skip_stats_start_id:  Stat id to start skip logic
 *    @skip_count:      Number of stats to skip
**/
typedef struct {
  uint32_t  skip_stats_start;
  uint8_t   skip_count;
} aec_skip_t;


/** aec_frame_capture_t:
 *    @frame_info:  Batch information
 *    @frame_capture_mode: capture mode in progress
 *    @current_batch_count: Current count
 *    @streamon_update_done: If update is done on streamon, set this flag.
**/
typedef struct {
  aec_frame_batch_t   frame_info;
  boolean             frame_capture_mode;
  int8_t              current_batch_count;
  boolean             streamon_update_done;
} aec_frame_capture_t;


/** aec_adrc_settings_t
 *    @is_adrc_feature_enabled: ADRC feature enabled
 *    @adrc_force_disable:        Force disable adrc
**/
typedef struct {
  boolean              is_adrc_feature_supported;
  boolean              adrc_force_disable;
  cam_scene_mode_type  bestshot_mode;
  cam_effect_mode_type effect_mode;
} aec_adrc_settings_t;


/** _aec_port_private:
 *    @reserved_id:     TODO
 *    @stream_type:     TODO
 *    @vfe_out_width:   TODO
 *    @vfe_out_height:  TODO
 *    @cur_sof_id:      TODO
 *    @state:           TODO
 *    @aec_update_data: TODO
 *    @aec_update_flag: TODO
 *    @aec_object:      session index
 *    @thread_data:     TODO
 *    @stream_info:     a copy, not a reference, can this be a const ptr
 *    @aec_get_data:    TODO
 *    @video_hdr:       TODO
 *
 *    @aec_state:  the state to return to HAL3
 *    @in_zsl_capture: set to TRUE to stop sending updates while in ZSL
 *                     snapshot mode.
 *
 * Each aec moduld object should be used ONLY for one Bayer
 * session/stream set - use this structure to store session
 * and stream indices information.
 **/
typedef struct _aec_port_private {
  unsigned int        reserved_id;
  cam_stream_type_t   stream_type;
  unsigned int        stream_identity;
  void                *aec_iface_lib;
  boolean             aec_extension_use;
  boolean             use_default_algo;
  int                 vfe_out_width;
  int                 vfe_out_height;
  uint32_t            cur_sof_id;
  uint32_t            cur_stats_id;
  uint32_t            super_param_id;
  aec_port_state_t    state;
  stats_update_t      aec_update_data;
  boolean             aec_update_flag;
  aec_object_t        aec_object;
  q3a_thread_data_t   *thread_data;
  mct_stream_info_t   stream_info;
  aec_get_t           aec_get_data;
  aec_snapshot_hdr_type snapshot_hdr;
  int32_t             video_hdr;
  sensor_RDI_parser_func_t  parse_RDI_stats;
  uint8_t             aec_state;
  cam_ae_state_t      aec_last_state;
  boolean             locked_from_hal;
  boolean             in_longshot_mode;
  boolean             locked_from_algo;
  boolean             aec_reset_precap_start_flag;
  boolean             aec_precap_start;
  boolean             aec_precap_for_af;
  boolean             force_prep_snap_done;
  cam_trigger_t       aec_trigger;
  uint32_t            max_sensor_delay;
  boolean             in_zsl_capture;
  int                 preview_width;
  int                 preview_height;
  uint32_t            required_stats_mask;
  isp_stats_tap_loc   requested_tap_location[MSM_ISP_STATS_MAX];
  int32_t             low_light_shutter_flag;
  char                aec_debug_data_array[AEC_DEBUG_DATA_SIZE];
  uint32_t            aec_debug_data_size;
  /* HAL 3*/
  cam_area_t          aec_roi;
  aec_led_est_state_t est_state;
  cam_3a_params_t     aec_info;
  aec_sensor_info_t    sensor_info;
  aec_port_manual_setting_t manual;
  float               init_sensitivity;
  aec_auto_mode_t     aec_auto_mode; // final mode
  uint8_t             aec_meta_mode; // main 3a switch
  uint8_t             aec_on_off_mode;// individual 3a switch
  int32_t             exp_comp;
  int32_t             led_mode;
  aec_fps_range_t     fps;
  aec_skip_t          aec_skip;
  aec_port_capture_intent_t still;
  pthread_mutex_t     update_state_lock;
  aec_state_update_data_t state_update;
  float               ISO100_gain;
  float               max_gain;
  aec_frame_capture_t stats_frame_capture;
  aec_state_update_data_t frame_capture_update;
  q3a_fast_aec_data_t fast_aec_data;
  int32_t             touch_ev_status;
  uint16_t            fast_aec_forced_cnt;
  float               dual_cam_exp_multiplier;
  aec_adrc_settings_t adrc_settings;
  aec_convergence_type instant_aec_type;
  boolean             apply_fixed_fps_adjustment;
  boolean             core_aec_locked;

  /* Hook to extend functionality */
  stats_util_override_func_t func_tbl;
  void                *ext_param;
  cam_sync_type_t      dual_cam_sensor_info;
  uint32_t             intra_peer_id;

  /* Params will get updated in current sessiona and will be used in next
  session for improving the launch convergence */
  aec_stored_params_t  *stored_params;
} aec_port_private_t;

/** aec_port_peer_aec_update
 * Contains the peer AEC Update information
 *
 *    @update:              Peer's AEC Update
 *    @anti_banding:        Peer's Anti-banding state
**/
typedef struct
{
  stats_update_t         update;
  aec_antibanding_type_t anti_banding;
} aec_port_peer_aec_update;

void    aec_port_deinit(mct_port_t *port);
boolean aec_port_find_identity(mct_port_t *port, unsigned int identity);
boolean aec_port_init(mct_port_t *port, unsigned int *session_id);
boolean aec_port_query_capabilities(mct_port_t *port,
  mct_pipeline_stats_cap_t *stats_cap);
void aec_port_update_aec_state(aec_port_private_t *private,
  aec_state_update_data_t aec_update_state);
void aec_send_bus_message(mct_port_t *port,
  mct_bus_msg_type_t bus_msg_type, void* payload, int size, int sof_id);
void* aec_port_load_function(aec_object_t *aec_object);
void aec_port_unload_function(aec_port_private_t *private);
boolean aec_port_set_session_data(mct_port_t *port, void *q3a_lib_info,
  cam_position_t cam_position, unsigned int *sessionid);
boolean aec_port_load_dummy_default_func(aec_object_t *aec_object);

void aec_port_send_aec_info_to_metadata(mct_port_t *port,
  aec_output_data_t *output);
void aec_port_pack_update(mct_port_t *port, aec_output_data_t *output,
  uint8_t aec_output_index);
void aec_port_send_event(mct_port_t *port, int evt_type,
  int sub_evt_type, void *data, uint32_t sof_id);
void aec_port_configure_stats(aec_output_data_t *output, mct_port_t *port);
void aec_port_pack_exif_info(mct_port_t *port, aec_output_data_t *output);
void aec_port_print_log(aec_output_data_t *output, char *event_name,
  aec_port_private_t *private, int8 output_index);
void aec_port_set_stored_parm(mct_port_t *port, aec_stored_params_t* stored_parm);

#endif /* __PORT_AEC_H__ */
