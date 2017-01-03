/* mct_event.h
 *                                                           .
 * Copyright (c) 2013-2016 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */

#ifndef __MCT_EVENT_H__
#define __MCT_EVENT_H__

#include "cam_types.h"
#include "media_controller.h"

#define MESH_H_Q3A 16     // horizontal number of roll-off mesh blocks (grids)
#define MESH_V_Q3A 12     // vertical number of roll-off mesh blocks (grids)

/* 1st level event type */
typedef enum _mct_event_type {
  MCT_EVENT_INVALID      = 0,
  MCT_EVENT_CONTROL_CMD  = 1, /* Control command from Media Controller */
  MCT_EVENT_MODULE_EVENT = 2, /* Event among modules                   */
  MCT_EVENT_MAX   = 3
} mct_event_type;

/** Name: _mct_event_direction
 *
 *  Arguments/Fields:
 *    @MCT_EVENT_UPSTREAM: upstream event;
 *    @MCT_EVENT_DOWNSTREAM: downstream event;
 *    @MCT_EVENT_BOTH: both upstream and downstream event;
 *                      This is a rare case only used by a few modules.
 *    @MCT_EVENT_INTRA_MOD: intra-module (across sessions) event.
 *  Description:
        Event direction determines the flow of an event through modules
        and across ports.
 **/
typedef enum _mct_event_direction {
  MCT_EVENT_UPSTREAM,
  MCT_EVENT_DOWNSTREAM,
  MCT_EVENT_BOTH,
  MCT_EVENT_INTRA_MOD,
  MCT_EVENT_NONE
} mct_event_direction;

typedef struct {
  uint32_t frame_number;
  uint32_t frame_index;
  cam_stream_ID_t stream_ids;
  uint32_t is_valid;
} mct_event_frame_request_t;

typedef struct {
  float gain_g_odd;
  float gain_g_even;
  float gain_b;
  float gain_r;
  cam_format_t sensor_format;
} demux_info_t;

typedef struct {
  //Roll-off configuration information
  int nh;          //horizontal number of mesh blocks, hard coded as MESH_H
  int nv;          //vertical number of mesh blocks, hard coded as MESH_V
  int scale_cubic; //bicubic interpolation scale (8, 4, 2, 1) from grid to
                   //subgrid
  int deltah;      //horizontal offset (per-channel) of rolloff top-left point
  int deltav;      //vertical offset (per-channel) of rolloff top-left point
  int subgridh;    //horizontal size (per-channel) of rolloff subgrid
  int subgridv;    //vertical size (per-channel) of rolloff subgrid

  //Roll-off tables
  float r_gainf[(MESH_H_Q3A+1) * (MESH_V_Q3A+1)];  //mesh table (float)
                                                   //for R channel
  float gr_gainf[(MESH_H_Q3A+1) * (MESH_V_Q3A+1)]; //mesh table (float)
                                                   //for Gr channel
  float gb_gainf[(MESH_H_Q3A+1) * (MESH_V_Q3A+1)]; //mesh table (float)
                                                   //for Gb channel
  float b_gainf[(MESH_H_Q3A+1) * (MESH_V_Q3A+1)];  //mesh table (float)
                                                   //for b channel
} LensRolloffStruct;

typedef struct {
  LensRolloffStruct rolloff_info;
  demux_info_t      demux_info;
} q3a_be_info_t;

typedef struct {
  uint32_t size;
  void *data;
} mct_custom_data_payload_t;

#if 0
#define NUM_AUTOFOCUS_MULTI_WINDOW_GRIDS  9
#define NUM_AUTOFOCUS_HORIZONTAL_GRID 9
#define NUM_AUTOFOCUS_VERTICAL_GRID 9
#define AUTOFOCUS_STATS_BUFFER_MAX_ENTRIES 1056
#endif

/* To generate enum from macros */
#define MCT_EVENT_GENERATE_ENUM(ENUM) ENUM,
/* To generate string for enum from macros */
#define MCT_EVENT_GENERATE_STRING(STRING) #STRING,

/** _mct_event_module_type
 * All mct event are defined using macros
 * to support the log string for enums
 *
 * 2nd level event type with type of struct mct_event_module_t
 **/
#define MCT_EVENT_MODULE_ENUM_LIST(ADD_ENTRY)                                 \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_STREAM_CONFIG)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_STREAM_CONFIG_FOR_FLASH)                     \
  ADD_ENTRY(MCT_EVENT_MODULE_IFACE_SET_STREAM_CONFIG)                         \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_AF_ROLLOFF_PARAMS)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_USE_NATIVE_BUFFER) /* NULL payload */            \
  ADD_ENTRY(MCT_EVENT_MODULE_DIVERT_BUF_TO_STATS) /* NULL payload */          \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_CHROMATIX_PTR)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_GET_CHROMATIX_PTR)                               \
  /* EZTuning reload chromatix */                                             \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_RELOAD_CHROMATIX)                            \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_UPDATE_ISP)                                \
  /* shall be replaced by individual update */                                \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_UPDATE)                                    \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AEC_UPDATE)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AWB_UPDATE)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AF_UPDATE)                                 \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_PDAF_UPDATE)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_PDAF_AF_WINDOW_UPDATE)                     \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AEC_MANUAL_UPDATE)                         \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AWB_MANUAL_UPDATE)                         \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AF_MANUAL_UPDATE)                          \
  ADD_ENTRY(MCT_EVENT_MODULE_SENSOR_LENS_POSITION_UPDATE)                     \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AEC_CONFIG_UPDATE) /* aec_config_t */      \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AWB_CONFIG_UPDATE) /* awb_config_t */      \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AF_CONFIG_UPDATE) /* af_config_t */        \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_RS_CONFIG_UPDATE) /* rs_config_t */        \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_FLASH_MODE) /* cam_flash_mode_t */           \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AF_CONFIG)                                 \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_AFD_UPDATE)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_ASD_UPDATE)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_GYRO_STATS)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_DATA)                                      \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_EXT_DATA)                                  \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_DATA_ACK) /* mct_event_stats_isp_t */      \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_GET_DATA)                                  \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_GET_LED_DATA)                              \
  ADD_ENTRY(MCT_EVENT_MODULE_3A_GET_CUR_FPS)                                  \
  ADD_ENTRY(MCT_EVENT_MODULE_SOF)                                             \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_DIGITAL_GAIN) /* float * */                  \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_GET_THREAD_OBJECT)                         \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_AF_TUNE_PTR) /* af_algo_tune_parms_t * */    \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_RELOAD_AFTUNE) /* EZTuning reload aftune */  \
  ADD_ENTRY(MCT_EVENT_MODULE_REG_UPDATE_NOTIFY)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_SOF_NOTIFY) /* mct_bus_msg_isp_sof_t * */        \
  ADD_ENTRY(MCT_EVENT_MODULE_BUF_DIVERT) /* isp_buf_divert_t * */             \
  ADD_ENTRY(MCT_EVENT_MODULE_BUF_DIVERT_ACK) /* isp_buf_divert_ack_t * */     \
  ADD_ENTRY(MCT_EVENT_MODULE_RAW_STATS_DIVERT)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_RAW_STATS_DIVERT_ACK)                            \
  ADD_ENTRY(MCT_EVENT_MODULE_FRAME_IND)                                       \
  ADD_ENTRY(MCT_EVENT_MODULE_FRAME_DONE)                                      \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_DIS_CONFIG) /* isp_dis_config_info_t */      \
  /* isp_dis_config_info_t */                                                 \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_OFFLINE_CONFIG_OVERWRITE)                    \
  ADD_ENTRY(MCT_EVENT_MODULE_STREAM_CROP) /* mct_bus_msg_stream_crop_t */     \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_OFFLINE_PIPELINE_CONFIG)                     \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_OUTPUT_DIM) /* mct_stream_info_t */          \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_CDS_REQUEST) /* isp_cds_info_t */            \
  ADD_ENTRY(MCT_EVENT_MODULE_ISPIF_OUTPUT_INFO) /* ispif_output_info_t */     \
  ADD_ENTRY(MCT_EVENT_MODULE_GET_GYRO_DATA) /* mct_event_gyro_data_t */       \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_DIS_UPDATE)                                \
  /* isp_stream_skip_pattern_t */                                             \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_SKIP_PATTERN)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_POST_TO_BUS)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_FACE_INFO) /* mct_face_info_t */                 \
  ADD_ENTRY(MCT_EVENT_MODULE_MODE_CHANGE)                                     \
  ADD_ENTRY(MCT_EVENT_MODULE_PPROC_GET_AEC_UPDATE)                            \
  ADD_ENTRY(MCT_EVENT_MODULE_PPROC_GET_AWB_UPDATE)                            \
  ADD_ENTRY(MCT_EVENT_MODULE_PPROC_DIVERT_INFO) /* pproc_divert_info_t */     \
  /* meta_channel_buf_divert_request_t */                                     \
  ADD_ENTRY(MCT_EVENT_MODULE_META_CHANNEL_DIVERT)                             \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_DIVERT_TO_3A)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_PRIVATE_EVENT)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_LA_ALGO_UPDATE)                              \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_LTM_ALGO_UPDATE)                             \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_GTM_ALGO_UPDATE)                             \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_TINTLESS_ALGO_UPDATE)                        \
  ADD_ENTRY(MCT_EVENT_MODULE_GET_ISP_TABLES) /* mct_isp_table_t */            \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_AWB_UPDATE) /* awb_update_t */               \
  /* pointer to uint16_t table[64]; */                                        \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_GAMMA_UPDATE)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_STREAMON_DONE) /* no payload */              \
  ADD_ENTRY(MCT_EVENT_MODULE_SENSOR_META_CONFIG) /* sensor_meta_t */          \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_META_CONFIG) /* sensor_meta_t */             \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_CHANGE_OP_PIX_CLK) /* uint32_t */            \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_NO_RESOURCE) /* no payload */                \
  ADD_ENTRY(MCT_EVENT_MODULE_PREVIEW_STREAM_ID) /* int */                     \
  /* mct_event_stats_isp_rolloff_t */                                         \
  ADD_ENTRY(MCT_EVENT_MODULE_SENSOR_ROLLOFF)                                  \
  /* mct_fast_aec_mode_t */                                                   \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_FAST_AEC_CONVERGE_MODE)                      \
  ADD_ENTRY(MCT_EVENT_MODULE_FAST_AEC_CONVERGE_ACK)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_IFACE_REQUEST_OUTPUT_RESOURCE)                   \
  ADD_ENTRY(MCT_EVENT_MODULE_IFACE_REQUEST_PP_DIVERT) /* boolean */           \
  ADD_ENTRY(MCT_EVENT_MODULE_QUERY_DIVERT_TYPE) /* uint32_t divert_mask */    \
  ADD_ENTRY(MCT_EVENT_MODULE_PP_SUBMOD_POST_TO_BUS) /* mct_bus_msg_t */       \
  ADD_ENTRY(MCT_EVENT_MODULE_PPROC_SET_OUTPUT_BUFF)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_REQ_DIVERT) /* uint32_t */                       \
  ADD_ENTRY(MCT_EVENT_MODULE_IS_FRAME_VALID) /* mct_event_frame_request_t */  \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_FLASH_CTRL) /* cam_flash_ctrl_t */           \
  /* mct_event_frame_request_t */                                             \
  ADD_ENTRY(MCT_EVENT_MODULE_FRAME_DROP_NOTIFY)                               \
  /* sensor_set_output_info_t */                                              \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_SENSOR_OUTPUT_INFO)                          \
  ADD_ENTRY(MCT_EVENT_MODULE_LED_STATE_TIMEOUT)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_LED_AF_UPDATE)                                   \
  ADD_ENTRY(MCT_EVENT_MODULE_IMGLIB_AF_CONFIG) /* mct_imglib_af_config_t */   \
  ADD_ENTRY(MCT_EVENT_MODULE_IMGLIB_AF_OUTPUT) /* mct_imglib_af_output_t */   \
  /* mct_imglib_dcrf_result_t */                                              \
  ADD_ENTRY(MCT_EVENT_MODULE_IMGLIB_DCRF_OUTPUT)                              \
  /* img_dual_cam_init_params_t */                                            \
  ADD_ENTRY(MCT_EVENT_MODULE_IMGLIB_DCRF_CFG)                                 \
  /* mct_imglib_af_output_t */                                                \
  ADD_ENTRY(MCT_EVENT_MODULE_GET_AF_SW_STATS_FILTER_TYPE)                     \
  ADD_ENTRY(MCT_EVENT_MODULE_HFR_MODE_NOTIFY)                                 \
  ADD_ENTRY(MCT_EVENT_MODULE_GET_AEC_LUX_INDEX)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_FRAMESKIP)                                   \
  ADD_ENTRY(MCT_EVENT_MODULE_BRACKETING_UPDATE) /* mct_bracketing_update_t */ \
  /* msm_vfe_frame_skip_pattern */                                            \
  ADD_ENTRY(MCT_EVENT_MODULE_REQUEST_FRAME_SKIP)                              \
  /* software skip for frame */                                               \
  ADD_ENTRY(MCT_EVENT_MODULE_REQUEST_SW_FRAME_SKIP)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_UPDATE_STATS_SKIP)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_STATS_INFO) /* mct_stats_info_t */           \
  ADD_ENTRY(MCT_EVENT_MODULE_FRAME_SKIP_NOTIFY)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_GRAVITY_VECTOR_UPDATE)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_GRAVITY_VECTOR_ENABLE)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_IFACE_REQUEST_OFFLINE_OUTPUT_RESOURCE)           \
  ADD_ENTRY(MCT_EVENT_MODULE_START_STOP_STATS_THREADS)                        \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_DEFECTIVE_PIXELS)                            \
  /* mct_event_request_stats_type */                                          \
  ADD_ENTRY(MCT_EVENT_MODULE_REQUEST_STATS_TYPE)                              \
  ADD_ENTRY(MCT_EVENT_MODULE_BE_STATS_INFO)                                   \
  /* isp_preferred_streams */                                                 \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_PREFERRED_STREAMS_MAPPING)                   \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_META_PTR) /* metadata ptr */                 \
  /* cam_face_detection_data_t */                                             \
  ADD_ENTRY(MCT_EVENT_MODULE_EXTENDED_FACE_INFO)                              \
  /* event from eztune to sensor */                                           \
  ADD_ENTRY(MCT_EVENT_MODULE_EZTUNE_GET_CHROMATIX)                            \
  /* event from eztune to sensor */                                           \
  ADD_ENTRY(MCT_EVENT_MODULE_EZTUNE_SET_CHROMATIX)                            \
  /* event from eztune to sensor */                                           \
  ADD_ENTRY(MCT_EVENT_MODULE_EZTUNE_GET_AFTUNE)                               \
  /* event from eztune to sensor */                                           \
  ADD_ENTRY(MCT_EVENT_MODULE_EZTUNE_SET_AFTUNE)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_TRIGGER_CAPTURE_FRAME)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_DELAY_FRAME_SETTING)                             \
  ADD_ENTRY(MCT_EVENT_MODULE_GET_PARENT_MODULE) /* (mct_module_t*) */         \
  ADD_ENTRY(MCT_EVENT_MODULE_TOF_UPDATE)                                      \
  ADD_ENTRY(MCT_EVENT_MODULE_CUSTOM_STATS_DATA_AEC)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_CUSTOM_STATS_DATA_AF)                            \
  ADD_ENTRY(MCT_EVENT_MODULE_CUSTOM_STATS_DATA_AWB)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_CUSTOM)                                          \
  ADD_ENTRY(MCT_EVENT_MODULE_SET_DUAL_OTP_PTR)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_SENSOR_STREAM_ON)                                \
  ADD_ENTRY(MCT_EVENT_MODULE_SENSOR_ADJUST_FPS)                               \
  ADD_ENTRY(MCT_EVENT_MODULE_SENSOR_PDAF_CONFIG)                              \
  ADD_ENTRY(MCT_EVENTS_MODULE_PDAF_ISP_INFO)                                  \
  /* query meta stream info from downstream */                                \
  ADD_ENTRY(MCT_EVENT_MODULE_IFACE_REQUEST_META_STREAM_INFO)                  \
  ADD_ENTRY(MCT_EVENT_MODULE_IFACE_REQUEST_STREAM_MAPPING_INFO)               \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_INFORM_LPM)                                  \
  ADD_ENTRY(MCT_EVENT_MODULE_PPROC_DUMP_METADATA)                             \
  ADD_ENTRY(MCT_EVENT_MODULE_ISP_ADRC_MODULE_MASK)                            \
  /* Peer Event Sent by AF to its linked AF */                                \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_PEER_AF_FOCUS_UPDATE)                      \
  ADD_ENTRY(MCT_EVENT_MODULE_STATS_PEER_AEC_UPDATE)                           \
  ADD_ENTRY(MCT_EVENT_MODULE_MAX)                                             \

typedef enum _mct_event_module_type {
  MCT_EVENT_MODULE_ENUM_LIST(MCT_EVENT_GENERATE_ENUM)
} mct_event_module_type_t;

typedef struct {
  cam_intf_parm_type_t type;
  void *parm_data;
} mct_event_control_parm_t;

typedef struct {
  uint32_t identity;
  uint32_t frame_number;
  uint32_t num_of_parm_events;
  mct_event_control_parm_t *parm_events;
} mct_event_super_control_parm_t;

/** _mct_event_control_type
 *    @MCT_EVENT_CONTROL_TEST:
 *    @MCT_EVENT_CONTROL_STREAMON:
 *    @MCT_EVENT_CONTROL_STREAMOFF:
 *
 * All mct event are defined using macros
 * to support the log string for enums
 *
 * 2nd level event type with type of struct mct_event_control_t
 **/
#define MCT_EVENT_CONTROL_ENUM_LIST(ADD_ENTRY)                                \
  ADD_ENTRY(MCT_EVENT_CONTROL_TEST)                                           \
  ADD_ENTRY(MCT_EVENT_CONTROL_STREAMON)                                       \
  ADD_ENTRY(MCT_EVENT_CONTROL_STREAMOFF)                                      \
  ADD_ENTRY(MCT_EVENT_CONTROL_STREAMON_FOR_FLASH)                             \
  ADD_ENTRY(MCT_EVENT_CONTROL_STREAMOFF_FOR_FLASH)                            \
  ADD_ENTRY(MCT_EVENT_CONTROL_SET_PARM)                                       \
  ADD_ENTRY(MCT_EVENT_CONTROL_GET_PARM)                                       \
  ADD_ENTRY(MCT_EVENT_CONTROL_SET_SUPER_PARM)                                 \
  ADD_ENTRY(MCT_EVENT_CONTROL_SOF)                                            \
  ADD_ENTRY(MCT_EVENT_CONTROL_DO_AF)                                          \
  ADD_ENTRY(MCT_EVENT_CONTROL_CANCEL_AF)                                      \
  ADD_ENTRY(MCT_EVENT_CONTROL_PREPARE_SNAPSHOT)                               \
  ADD_ENTRY(MCT_EVENT_CONTROL_PARM_STREAM_BUF)                                \
  ADD_ENTRY(MCT_EVENT_CONTROL_START_ZSL_SNAPSHOT)                             \
  ADD_ENTRY(MCT_EVENT_CONTROL_STOP_ZSL_SNAPSHOT)                              \
  ADD_ENTRY(MCT_EVENT_CONTROL_UPDATE_BUF_INFO)                                \
  ADD_ENTRY(MCT_EVENT_CONTROL_REMOVE_BUF_INFO)                                \
  ADD_ENTRY(MCT_EVENT_CONTROL_METADATA_UPDATE)                                \
  ADD_ENTRY(MCT_EVENT_CONTROL_DEL_OFFLINE_STREAM)                             \
  ADD_ENTRY(MCT_EVENT_CONTROL_CUSTOM)                                         \
  ADD_ENTRY(MCT_EVENT_CONTROL_LINK_INTRA_SESSION)                             \
  ADD_ENTRY(MCT_EVENT_CONTROL_UNLINK_INTRA_SESSION)                           \
  ADD_ENTRY(MCT_EVENT_CONTROL_OFFLINE_METADATA)                               \
  ADD_ENTRY(MCT_EVENT_CONTROL_FLUSH_BUFFERS)                                  \
  ADD_ENTRY(MCT_EVENT_CONTROL_HW_ERROR)                                       \
  ADD_ENTRY(MCT_EVENT_CONTROL_MAX)                                            \

typedef enum _mct_event_control_type {
  MCT_EVENT_CONTROL_ENUM_LIST(MCT_EVENT_GENERATE_ENUM)
} mct_event_control_type_t;

typedef enum _mct_ctrl_set_pram_type {
  MCT_CTRL_SET_SENSOR_PARAM,
  MCT_CTRL_SET_ISP_PARAM,
  MCT_CTRL_SET_STATS_PARAM,
  MCT_CTRL_SET_FRAME_PARAM,
}mct_ctrl_set_pram_t;

typedef struct _mct_ctrl_prarm_set_type {
  mct_ctrl_set_pram_t type;
  uint32_t            size;
  void               *param_set_data;
} mct_ctrl_prarm_set_t;

typedef struct _mct_event_module {
  mct_event_module_type_t type;
  uint32_t current_frame_id;
  void *module_event_data;
} mct_event_module_t;

typedef struct _mct_event_control {
  mct_event_control_type_t type;
  uint32_t current_frame_id;
  void *control_event_data;
} mct_event_control_t;

typedef struct {
  boolean is_lpm_enabled;
} mct_event_inform_lpm_t;

/** _mct_event:
 *    @type: first level of event type
 *    @identity:   0x0000 0000 (session/stream index)
 *    @direction:  upstream or downstream
 *    @data:       event data type of first level type
 *    @timestamp:  system clock timestamp
 *    @logNestingIndex: index for system tracing multiple mct
 *                    events
 *
 * Event source can be Pipeline, Stream or
 * any of the Modules, event Ports
 **/
struct _mct_event {
  mct_event_type       type;
  uint32_t             identity;
  mct_event_direction  direction;

  union {
    mct_event_control_t ctrl_event;
    mct_event_module_t  module_event;
  } u;

  signed long long     timestamp;
  int                  logNestingIndex;
};

#define MCT_EVENT_CAST(obj)      ((mct_event_t *)(obj))
#define MCT_EVENT_DIRECTION(obj) (MCT_EVENT_CAST(obj)->direction)

mct_event_t *mct_event_package_event(/* FIX me */);

#endif /* __MCT_EVENT_H__*/
