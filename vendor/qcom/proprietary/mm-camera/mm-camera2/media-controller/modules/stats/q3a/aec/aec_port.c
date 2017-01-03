/* aec_port.c
 *
 * Copyright (c) 2013-2016 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#include "aec_module.h"
#include "aec_port.h"
#include "aec_biz.h"
#include "aec_ext.h"
#include "q3a_port.h"
#include <pthread.h>
#include "modules.h"
#include "stats_event.h"
#include <math.h>
#include "sensor_lib.h"
#include "stats_util.h"

#define AEC_UNDEF -1
#undef  LOG_TAG
#define LOG_TAG "AEC_PORT"

/* Define 3A debug info sw version number here. */
#define MAJOR_NUM             (0x05)
#define MINOR_NUM             (0x00)
#define PATCH_NUM             (0x00)
#define FEATURE_DESIGNATOR    (0x00)

#define MAX_INTEGRATION_MARGIN   50L
#define Q8                       256L

/* determin if AEC is currently using the divert stats */
inline boolean aec_port_using_HDR_divert_stats(aec_port_private_t *private)
{
  if (!private) {
    return FALSE;
  }

  return (private->video_hdr != 0 ||
     private->snapshot_hdr == AEC_SENSOR_HDR_IN_SENSOR);
}

inline void aec_port_reset_output_index(aec_port_private_t *private)
{
  private->state_update.sof_output_index = 0xFF; //invalid value
  private->state_update.cb_output_index  = 0xFF; //invalid value
  AEC_LOW("Reset output index");
}

/** aec_port_malloc_msg:
 *    @msg_type:   TODO
 *    @param_type: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static q3a_thread_aecawb_msg_t* aec_port_malloc_msg(int msg_type,
  int param_type)
{
  q3a_thread_aecawb_msg_t *aec_msg = (q3a_thread_aecawb_msg_t *)
    malloc(sizeof(q3a_thread_aecawb_msg_t));

  if (aec_msg == NULL) {
    return NULL;
  }
  memset(aec_msg, 0 , sizeof(q3a_thread_aecawb_msg_t));

  aec_msg->type = msg_type;
  if (msg_type == MSG_AEC_SET || msg_type == MSG_AEC_SEND_EVENT) {
    aec_msg->u.aec_set_parm.type = param_type;
  } else if (msg_type == MSG_AEC_GET) {
    aec_msg->u.aec_get_parm.type = param_type;
  }
  return aec_msg;
}

boolean aec_port_dummy_set_param(aec_set_parameter_t *param,
  aec_output_data_t *output, void *aec_obj)
{
  (void)param;
  (void)output;
  (void)aec_obj;
  AEC_ERR("Error: Uninitialized interface been use");
  return FALSE;
}

boolean aec_port_dummy_get_param(aec_get_parameter_t *param,
  void *aec_obj)
{
  (void)param;
  (void)aec_obj;
  AEC_ERR("Error: Uninitialized interface been use");
  return FALSE;
}

boolean aec_port_dummy_process(stats_t *stats, void *aec_obj,
  aec_output_data_t *output)
{
  (void)stats;
  (void)aec_obj;
  (void)output;
  AEC_ERR("Error: Uninitialized interface been use");
  return FALSE;
}
void *aec_port_dummy_init(void *lib)
{
  (void)lib;
  AEC_ERR("Error: Uninitialized interface been use");
  return NULL;
}

void aec_port_dummy_destroy(void *aec)
{
  (void)aec;
  AEC_ERR("Error: Uninitialized interface been use");
  return;
}

float aec_port_dummy_map_iso_to_real_gain(void *aec_obj, uint32_t iso)
{
  (void)aec_obj;
  (void)iso;
  AEC_ERR("Error: Uninitialized interface been use");
  return 0; /* Returning gain equal to zero as an error */
}

static void aec_port_update_aec_flash_state(
  mct_port_t *port, aec_output_data_t *output) {

  if (output && port) {
    aec_port_private_t  *private = (aec_port_private_t *)(port->port_private);
    aec_led_est_state_t tmp_state = private->est_state;

    /* HAL uses flash_needed flag to determine if prepare snapshot
     * will be called, hence AEC will turn LED on when the call comes
     * As a result, AEC_EST_NO_LED_DONE is not used (still kept below) */
    if (output->stats_update.aec_update.led_state == Q3A_LED_OFF &&
      private->est_state == AEC_EST_START) {
      if (private->aec_precap_for_af == TRUE) {
        private->est_state = AEC_EST_DONE_FOR_AF;
        private->aec_precap_for_af = FALSE;
      } else {
        private->est_state = AEC_EST_DONE;
      }
      private->aec_precap_start = FALSE;
    } else if (output->stats_update.aec_update.led_state == Q3A_LED_LOW) {
      private->est_state = AEC_EST_START;
      if (private->aec_precap_for_af != TRUE) {
        private->aec_precap_start = TRUE;
      }
    } else if (output->stats_update.aec_update.prep_snap_no_led == TRUE) {
      private->est_state = AEC_EST_NO_LED_DONE;
    } else if (output->force_prep_snap_done) {
      private->force_prep_snap_done = TRUE;
    } else {
      private->est_state = AEC_EST_OFF;
    }

    /* No LED CASE, reset the precapture flag */
    if (output->stats_update.aec_update.led_state == Q3A_LED_OFF &&
        private->est_state == AEC_EST_OFF &&
        output->stats_update.aec_update.flash_needed == FALSE) {
      private->aec_precap_start = FALSE;
    }

    if (tmp_state != private->est_state) {
      AEC_LOW("AEC EST state change: Old=%d New=%d", tmp_state, private->est_state);
    }

    output->stats_update.aec_update.est_state = private->est_state;
  }
}

static boolean aec_port_is_aec_locked(aec_port_private_t *private)
{
  if(private->locked_from_hal || private->locked_from_algo) {
    return TRUE;
  }

  return FALSE;
}

/** aec_port_print_log
 *
 **/
inline void aec_port_print_log(aec_output_data_t *output, char *event_name,
  aec_port_private_t *private, int8 output_index)
{
  if (output) {
    aec_update_t *aec = &output->stats_update.aec_update;
    AEC_HIGH("AEUPD: %10s: SOF=%03d,Stat=%03d,outSOF=%03d,outFrameID=%03d,outInd=%d,"
      "capt=%d,Update=%d,TL=%03d,CL=%03d,"
      "EI=%03d,LI=%03.3f, Gains: SG:RG:LC=(%02.3f:%02.3f:%04d), ET=%01.3f,settled=%d,"
      "HDR(Long:G=%02.3f,LC=%04d,Short:G=%02.3f,LC=%04d),"
      "estSt=%d,aecSt=%d,ledSt=%d,preCap=%d,flashSnap=%d,"
      "LLS=%d,LLC=%d,flashMode=%d,gamma=%d,nr=%d, "
      "DRC_Gains: (TG:CG=%02.3f:%02.3f), DRC_Ratios(%1.2f,%1.2f,%1.2f,%1.2f)",
      event_name, private->cur_sof_id, private->cur_stats_id, aec->sof_id, aec->frame_id, output_index,
      private->still.capture_sof, private->aec_update_flag,
      aec->target_luma,aec->cur_luma, aec->exp_index, aec->lux_idx, aec->sensor_gain,
      aec->real_gain, aec->linecount, aec->exp_time, aec->settled, aec->l_real_gain,
      aec->l_linecount, aec->s_real_gain, aec->s_linecount, aec->est_state, private->aec_state,
      aec->led_state, private->aec_precap_start, output->snap.is_flash_snapshot,
      private->low_light_shutter_flag, aec->low_light_capture_update_flag, aec->flash_hal,
      aec->gamma_flag, aec->nr_flag, aec->total_drc_gain, aec->color_drc_gain, aec->gtm_ratio,
      aec->ltm_ratio, aec->la_ratio, aec->gamma_ratio);
  }
}

/** aec_port_print_bus
 *
 */
static inline void aec_port_print_bus(const char* trigger, aec_port_private_t *private)
{
  /* Print all bus info in one message */
  AEC_LOW("BSUPD:%15s SOF=%d,streamTy=%d,"
    "AEINFO:ET=%f,iso=%d,flashNeed=%d,settled=%d,"
    "IMME:aeSt=%d,tr=%d,trId=%d,ledM=%d,onOffM=%d,"
    "AEC:expCm=%d,lock=%d", trigger,
    private->cur_sof_id,private->stream_type, // Other info
    private->aec_info.exp_time, private->aec_info.iso_value,
    private->aec_info.flash_needed, private->aec_info.settled,// AE_INFO end
    private->aec_state, private->aec_trigger.trigger,
    private->aec_trigger.trigger_id, private->led_mode,
    private->aec_on_off_mode,// IMM end
    private->exp_comp, aec_port_is_aec_locked(private)// AE end
    );
}

/** aec_port_print_manual
 *
 **/
static inline void aec_port_print_manual(aec_port_private_t *private,
  char *event_name, aec_manual_update_t *output)
{
  AEC_HIGH("AEUPD: %15s: SOF=%03d,update=%d,LI=%03.3f,G=%02.3f,LC=%04d,iso=%d,",
    event_name, private->cur_sof_id, private->aec_update_flag,
    output->sensor_gain, output->linecount, output->lux_idx,
    output->exif_iso);
}

/**
 * aec_port_load_dummy_default_func
 *
 * @aec_object: structure with function pointers to be assign
 *
 * Return: TRUE on success
 **/
boolean aec_port_load_dummy_default_func(aec_object_t *aec_object)
{
  boolean rc = FALSE;
  if (aec_object) {
    aec_object->set_parameters = aec_port_dummy_set_param;
    aec_object->get_parameters = aec_port_dummy_get_param;
    aec_object->process = aec_port_dummy_process;
    aec_object->init = aec_port_dummy_init;
    aec_object->deinit = aec_port_dummy_destroy;
    aec_object->iso_to_real_gain = aec_port_dummy_map_iso_to_real_gain;
    rc = TRUE;
  }
  return rc;
}

/** aec_port_load_function
 *
 *    @aec_object: structure with function pointers to be assign
 *
 * Return: Handler to AEC interface library
 **/
void* aec_port_load_function(aec_object_t *aec_object)
{
  if (!aec_object) {
    return FALSE;
  }

  return aec_biz_load_function(aec_object);
}


/** aec_port_unload_function
 *
 *    @private: Port private structure
 *
 *  Free resources allocated by aec_port_load_function
 *
 * Return: void
 **/
void aec_port_unload_function(aec_port_private_t *private)
{
  if (!private) {
    return;
  }

  aec_biz_unload_function(&private->aec_object, private->aec_iface_lib);
  aec_port_load_dummy_default_func(&private->aec_object);
  private->aec_iface_lib = NULL;
}

/** aec_port_query_capabilities:
 *    @port: aec's port
 *
 *  Provide session data information for algo library set-up.
 **/
boolean aec_port_query_capabilities(mct_port_t *port,
  mct_pipeline_stats_cap_t *stats_cap)
{
  aec_port_private_t *private = NULL;
  Q3a_version_t aec_version;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    return FALSE;
  }
  private = port->port_private;
  if (!private) {
    return FALSE;
  }
  if (NULL == private->aec_object.get_version) {
    /* This is an optional function, not an error */
    AEC_ERR("Get AEC version function not implemented");
    return TRUE;
  }

   /* For now, dictate the overall 3A version base on AEC algo version*/
  /* TODO: Add version for every algorithm */
  if (!private->aec_object.get_version(private->aec_object.aec, &aec_version)){
    AEC_ERR("Fail to get AEC version");
    return FALSE;
  }
  stats_cap->q3a_version.major_version =
    aec_version.major_version;
  stats_cap->q3a_version.minor_version =
    aec_version.minor_version;
  stats_cap->q3a_version.patch_version =
    aec_version.patch_version;
  stats_cap->q3a_version.new_feature_des =
    aec_version.new_feature_des;

  return TRUE;
}

/** aec_port_set_session_data:
 *    @port: aec's sink port to be initialized
 *    @q3a_lib_info: Q3A session data information
 *    @cam_position: Camera position
 *    @sessionid: session identity
 *
 *  Provide session data information for algo library set-up.
 **/
boolean aec_port_set_session_data(mct_port_t *port, void *q3a_lib_info,
  cam_position_t cam_position, unsigned int *sessionid)
{
  aec_port_private_t *private = NULL;
  mct_pipeline_session_data_q3a_t *q3a_session_data = NULL;
  Q3a_version_t aec_version;
  boolean rc = FALSE;
  unsigned int session_id = (((*sessionid) >> 16) & 0x00ff);
  if (!port || !port->port_private || strcmp(MCT_OBJECT_NAME(port), "aec_sink") ||
    !q3a_lib_info) {
    return rc;
  }

  q3a_session_data = (mct_pipeline_session_data_q3a_t *)q3a_lib_info;

  AEC_LOW("aec_libptr %p session_id %d", q3a_session_data->aec_libptr, session_id);

  private = port->port_private;
  /* Query to verify if extension use is required and if using default algo */
  private->aec_extension_use =
    aec_port_ext_is_extension_required(q3a_session_data->aec_libptr,
      cam_position, &private->use_default_algo);
  if (FALSE == private->aec_extension_use) {
    AEC_LOW("Load AEC interface functions");
    private->aec_iface_lib = aec_port_load_function(&private->aec_object);
  } else { /* Use extension */
    AEC_LOW("Load AEC EXTENSION interface functions");
    private->aec_iface_lib = aec_port_ext_load_function(&private->aec_object,
      q3a_session_data->aec_libptr, cam_position, private->use_default_algo);
  }

  /* Verify that all basic fields were populated */
  if (!(private->aec_iface_lib && private->aec_object.init &&
    private->aec_object.deinit &&
    private->aec_object.set_parameters &&
    private->aec_object.get_parameters &&
    private->aec_object.process &&
    private->aec_object.get_version)) {
    AEC_ERR("Error: setting algo iface functions");
    /* Resetting default interface to clear things */
    if (FALSE == private->aec_extension_use) {
      aec_port_unload_function(private);
    } else {
      aec_port_ext_unload_function(private);
    }
    return FALSE;
  }

  private->aec_object.aec =
    private->aec_object.init(private->aec_iface_lib);
  rc = private->aec_object.aec ? TRUE : FALSE;
  if (FALSE == rc) {
    AEC_ERR("Error: fail to init AEC algo");
    return rc;
  }

  if (private->aec_extension_use) {
    rc = aec_port_ext_update_func_table(private);
    if (rc && private->func_tbl.ext_init) {
      stats_ext_return_type ret = STATS_EXT_HANDLING_FAILURE;
      ret = private->func_tbl.ext_init(port, session_id);
      if (ret != STATS_EXT_HANDLING_FAILURE) {
        rc = TRUE;
      }
    }
  }

  /* Print 3A version */
  rc = private->aec_object.get_version(private->aec_object.aec, &aec_version);
  if (rc) {
    AEC_HIGH("3A VERSION --> %d.%d.%d.r%d",
      aec_version.major_version,
      aec_version.minor_version,
      aec_version.patch_version,
      aec_version.new_feature_des);
  } else {
    AEC_ERR("Fail to get_version");
  }

  AEC_LOW("aec = %p", private->aec_object.aec);
  return rc;
}

/** aec_set_skip_stats:
 *    @private: internal port structure
 *    @skip_stats_start: Stat id to start skip logic
 *    @skip_count: Number of stats to skip
 *
 * Setup all skip stats related variables
 *
 * Return: nothing
 **/
static void aec_set_skip_stats(aec_port_private_t *private,
  uint32_t skip_stats_start, uint8_t skip_count)
{
  AEC_LOW("start: %d,skip_count: %d", skip_stats_start, skip_count);
  private->aec_skip.skip_stats_start = skip_stats_start;
  private->aec_skip.skip_count = skip_count;
}

/** aec_set_skip_stats:
 *    @private: internal port structure
 *
 * Verify if stats skip is required.
 *
 * Return: TRUE if AEC stat skip is required
 **/
static boolean is_aec_stats_skip_required(aec_port_private_t *private,
  uint32 stats_id)
{
  boolean rc = FALSE;
  aec_skip_t *skip = &private->aec_skip;
  AEC_LOW("sof: %d, start: %d, skip: %d, stats_id: %d",
    private->cur_sof_id, skip->skip_stats_start, skip->skip_count, stats_id);

  if (skip->skip_count && stats_id >= skip->skip_stats_start) {
    skip->skip_count--;
    rc = TRUE;
  } else if (private->aec_auto_mode == AEC_MANUAL) /* skip stats in Manual mode */
    rc = TRUE;

  /* if it is in fast aec mode and AWB is runing, skip sending the stats to aec core. */
  if(private->fast_aec_data.enable &&
    private->fast_aec_data.state == Q3A_FAST_AEC_STATE_AWB_RUNNING)
    rc = TRUE;

  return rc;
}

static boolean aec_port_is_aec_searching(aec_output_data_t *output)
{
  if(output->stats_update.aec_update.settled) {
    return FALSE;
  }
  return TRUE;
}

static boolean aec_port_is_precapture_start(aec_port_private_t *private)
{
  if(private->aec_precap_start == TRUE) {
    return TRUE;
  }

  return FALSE;
}

static boolean aec_port_is_converged(aec_output_data_t *output,
  boolean *low_light)
{
  if(output->stats_update.aec_update.settled) {
    if(!output->stats_update.aec_update.flash_needed) {
      *low_light = FALSE;
    } else {
      *low_light = TRUE;
    }
    return TRUE;
  }

  return FALSE;
}

void aec_send_bus_message(mct_port_t *port,
  mct_bus_msg_type_t bus_msg_type, void* payload, int size, int sof_id)
{
  aec_port_private_t *aec_port = (aec_port_private_t *)(port->port_private);
  mct_event_t        event;
  mct_bus_msg_t      bus_msg;

  memset(&bus_msg, 0, sizeof(mct_bus_msg_t));
  bus_msg.sessionid = (aec_port->reserved_id >> 16);
  bus_msg.type = bus_msg_type;
  bus_msg.msg = payload;
  bus_msg.size = size;

  /* pack into an mct_event object*/
  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = aec_port->reserved_id;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.u.module_event.type = MCT_EVENT_MODULE_STATS_POST_TO_BUS;
  event.u.module_event.current_frame_id = sof_id;
  event.u.module_event.module_event_data = (void *)(&bus_msg);

  MCT_PORT_EVENT_FUNC(port)(port, &event);
  return;
}
/** aec_port_set_aec_mode:
 *  @aec_meta_mode: OFF/AUTO/SCENE_MODE- Main 3a switch
 *  @aec_on_off_mode: AEC OFF/ON switch
 **/
static void aec_port_set_aec_mode(aec_port_private_t *private) {
  uint8_t aec_meta_mode = private->aec_meta_mode;
  uint8_t aec_on_off_mode = private->aec_on_off_mode;
  aec_auto_mode_t prev_aec_auto_mode = private->aec_auto_mode;
  switch(aec_meta_mode){
    case CAM_CONTROL_OFF:
      private->aec_auto_mode = AEC_MANUAL;
      break;
    case CAM_CONTROL_AUTO:
      if(aec_on_off_mode)
        private->aec_auto_mode = AEC_AUTO;
      else
        private->aec_auto_mode = AEC_MANUAL;
      break;
    case CAM_CONTROL_USE_SCENE_MODE:
      private->aec_auto_mode = AEC_AUTO;
      break;
    default:{
      private->aec_auto_mode = AEC_AUTO;
    }
  }
  if (prev_aec_auto_mode != private->aec_auto_mode) {
    aec_state_update_data_t state_update = {0};
    state_update.type = AEC_PORT_STATE_MODE_UPDATE;
    state_update.u.trigger_new_mode = TRUE;
    aec_port_update_aec_state(private, state_update);
  }
}

/** aec_port_set_bestshot_mode:
 *    @aec_mode:     scene mode to be set
 *    @mode: scene mode coming from HAL
 *
 * Set the bestshot mode for algo.
 *
 * Return TRUE on success, FALSE on failure.
 **/
static boolean aec_port_set_bestshot_mode(
  aec_bestshot_mode_type_t *aec_mode, cam_scene_mode_type mode)
{
  boolean rc = TRUE;
  *aec_mode = AEC_BESTSHOT_OFF;
  AEC_LOW("Set scene mode: %d", mode);

  /* We need to translate Android scene mode to the one
   * AEC algorithm understands.
   **/
  switch (mode) {
  case CAM_SCENE_MODE_OFF: {
    *aec_mode = AEC_BESTSHOT_OFF;
  }
    break;

  case CAM_SCENE_MODE_AUTO: {
    *aec_mode = AEC_BESTSHOT_AUTO;
  }
    break;

  case CAM_SCENE_MODE_LANDSCAPE: {
    *aec_mode = AEC_BESTSHOT_LANDSCAPE;
  }
    break;

  case CAM_SCENE_MODE_SNOW: {
    *aec_mode = AEC_BESTSHOT_SNOW;
  }
    break;

  case CAM_SCENE_MODE_BEACH: {
    *aec_mode = AEC_BESTSHOT_BEACH;
  }
    break;

  case CAM_SCENE_MODE_SUNSET: {
    *aec_mode = AEC_BESTSHOT_SUNSET;
  }
    break;

  case CAM_SCENE_MODE_NIGHT: {
    *aec_mode = AEC_BESTSHOT_NIGHT;
  }
    break;

  case CAM_SCENE_MODE_PORTRAIT: {
    *aec_mode = AEC_BESTSHOT_PORTRAIT;
  }
    break;

  case CAM_SCENE_MODE_BACKLIGHT: {
    *aec_mode = AEC_BESTSHOT_BACKLIGHT;
  }
    break;

  case CAM_SCENE_MODE_SPORTS: {
    *aec_mode = AEC_BESTSHOT_SPORTS;
  }
    break;

  case CAM_SCENE_MODE_ANTISHAKE: {
    *aec_mode = AEC_BESTSHOT_ANTISHAKE;
  }
    break;

  case CAM_SCENE_MODE_FLOWERS: {
    *aec_mode = AEC_BESTSHOT_FLOWERS;
  }
    break;

  case CAM_SCENE_MODE_CANDLELIGHT: {
    *aec_mode = AEC_BESTSHOT_CANDLELIGHT;
  }
    break;

  case CAM_SCENE_MODE_FIREWORKS: {
    *aec_mode = AEC_BESTSHOT_FIREWORKS;
  }
    break;

  case CAM_SCENE_MODE_PARTY: {
    *aec_mode = AEC_BESTSHOT_PARTY;
  }
    break;

  case CAM_SCENE_MODE_NIGHT_PORTRAIT: {
    *aec_mode = AEC_BESTSHOT_NIGHT_PORTRAIT;
  }
    break;

  case CAM_SCENE_MODE_THEATRE: {
    *aec_mode = AEC_BESTSHOT_THEATRE;
  }
    break;

  case CAM_SCENE_MODE_ACTION: {
    *aec_mode = AEC_BESTSHOT_ACTION;
  }
    break;

  case CAM_SCENE_MODE_AR: {
    *aec_mode = AEC_BESTSHOT_AR;
  }
    break;
  case CAM_SCENE_MODE_FACE_PRIORITY: {
    *aec_mode = AEC_BESTSHOT_FACE_PRIORITY;
  }
    break;
  case CAM_SCENE_MODE_BARCODE: {
    *aec_mode = AEC_BESTSHOT_BARCODE;
  }
    break;
  case CAM_SCENE_MODE_HDR: {
    *aec_mode = AEC_BESTSHOT_HDR;
  }
    break;
  default: {
    rc = FALSE;
  }
    break;
  }

  return rc;
} /* aec_port_set_bestshot_mode */

/** aec_send_batch_bus_message:
 *    @port:   TODO
 *    @output: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_send_batch_bus_message(mct_port_t *port, uint32_t urgent_sof_id,
  uint32_t regular_sof_id)
{
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  mct_bus_msg_aec_immediate_t aec_urgent;
  mct_bus_msg_aec_t aec_bus_msg;

  aec_urgent.aec_state = private->aec_state;

  /* if the aec_reset_precap_start_flag is true, we need to send the CAM_AE_STATE_PRECAPTURE
   * to HAL per HAL3 spec, but we are not in AEC preflash sequence, so after we send out the
   * CAM_AE_STATE_PRECAPTURE state to HAL, here we reset the aec_precap_start flag, so the
   * state machine could move the state to converged. otherwise, the state will be stuck at
   * CAM_AE_STATE_PRECAPTURE due to aec_precap_start flag
   */
  if (aec_urgent.aec_state == CAM_AE_STATE_PRECAPTURE && private->aec_reset_precap_start_flag) {
    private->aec_reset_precap_start_flag = false;
    private->aec_precap_start = false;
  }

  aec_urgent.aec_trigger = private->aec_trigger;
  aec_urgent.led_mode = private->led_mode;
  aec_urgent.aec_mode =  private->aec_on_off_mode;
  aec_urgent.touch_ev_status = private->touch_ev_status;

  aec_bus_msg.exp_comp =  private->exp_comp;
  aec_bus_msg.ae_lock   = aec_port_is_aec_locked(private);
  /* HAL 3 manual mode */
  if (private->aec_auto_mode == AEC_MANUAL) {
    /* Reporting ROI weight as 0 in manual mode */
    memset(&aec_bus_msg.aec_roi, 0, sizeof(aec_bus_msg.aec_roi));
    aec_bus_msg.aec_roi_valid = TRUE;
  } else {
    aec_bus_msg.aec_roi = private->aec_roi;
    aec_bus_msg.aec_roi_valid = TRUE;
  }
  aec_bus_msg.fps.min_fps = private->fps.min_fps/256.0;
  aec_bus_msg.fps.max_fps = private->fps.max_fps/256.0;
  aec_bus_msg.lls_flag = private->low_light_shutter_flag;

  /* Print all bus messages info */
  aec_port_print_bus("SOF", private);

  AEC_LOW("send exif update zsl_cap=%d stream_type=%d",
    private->in_zsl_capture, private->stream_type);

  if (!private->in_zsl_capture) {
    if (private->stream_type != CAM_STREAM_TYPE_SNAPSHOT) {
      /* Ensure that exif information is not send between
         START_ZSL_SNAP to STOP_ZSL_SNAP and also not send during snapshot mode */
      AEC_LOW("SOF: StreamType=%d exp_time=%f iso %d sof_id=%d",
        private->stream_type, private->aec_info.exp_time,
        private->aec_info.iso_value, private->cur_sof_id);

      aec_send_bus_message(port, MCT_BUS_MSG_AE_INFO,
        &private->aec_info, sizeof(cam_3a_params_t), private->cur_sof_id);
    }
  }
  aec_send_bus_message(port, MCT_BUS_MSG_AEC_IMMEDIATE,
    (void*)&aec_urgent, sizeof(mct_bus_msg_aec_immediate_t), urgent_sof_id);

  aec_send_bus_message(port, MCT_BUS_MSG_AEC,
    (void*)&aec_bus_msg, sizeof(mct_bus_msg_aec_t), regular_sof_id);
}
/** aec_port_pack_exif_info:
 *    @port:   TODO
 *    @output: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
void aec_port_pack_exif_info(mct_port_t *port,
  aec_output_data_t *output)
{
  aec_port_private_t *private;

  if (!output || !port) {
    AEC_ERR("input error");
    return;
  }

  private = (aec_port_private_t *)(port->port_private);
  private->aec_info.exp_time = output->stats_update.aec_update.exp_time;
  private->aec_info.iso_value = output->stats_update.aec_update.exif_iso;
  private->aec_info.flash_needed = output->stats_update.aec_update.flash_needed;
  private->aec_info.settled = output->stats_update.aec_update.settled;
  switch (output->metering_type) {
      default:
          private->aec_info.metering_mode = CAM_METERING_MODE_UNKNOWN;
          break;
      case AEC_METERING_FRAME_AVERAGE:
          private->aec_info.metering_mode = CAM_METERING_MODE_AVERAGE;
          break;
      case AEC_METERING_CENTER_WEIGHTED:
      case AEC_METERING_CENTER_WEIGHTED_ADV:
          private->aec_info.metering_mode = CAM_METERING_MODE_CENTER_WEIGHTED_AVERAGE;
          break;
      case AEC_METERING_SPOT_METERING:
      case AEC_METERING_SPOT_METERING_ADV:
          private->aec_info.metering_mode = CAM_METERING_MODE_SPOT;
          break;
      case AEC_METERING_SMART_METERING:
          private->aec_info.metering_mode = CAM_METERING_MODE_MULTI_SPOT;
          break;
      case AEC_METERING_USER_METERING:
          private->aec_info.metering_mode = CAM_METERING_MODE_PATTERN;
          break;
  }
  private->aec_info.exposure_program = output->snap.exp_program;
  private->aec_info.exposure_mode = output->snap.exp_mode;
  if(private->aec_info.exposure_mode <= 2)
    private->aec_info.scenetype = 0x1;
  else
   private->aec_info.scenetype = 0xFFFF;
  private->aec_info.brightness = output->Bv_Exif;
}


/** aec_port_send_exif_debug_data:
 *    @port:   TODO
 *    @stats_update_t: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_send_exif_debug_data(mct_port_t *port)
{
  mct_event_t          event;
  mct_bus_msg_t        bus_msg;
  cam_ae_exif_debug_t  *aec_info;
  aec_port_private_t   *private;
  int                  size;

  if (!port) {
    AEC_ERR("input error");
    return;
  }
  private = (aec_port_private_t *)(port->port_private);
  if (private == NULL) {
    return;
  }

  /* Send exif data if data size is valid */
  if (!private->aec_debug_data_size) {
    AEC_LOW("aec_port: Debug data not available");
    return;
  }
  aec_info = (cam_ae_exif_debug_t *)malloc(sizeof(cam_ae_exif_debug_t));
  if (!aec_info) {
    AEC_ERR("Failure allocating memory for debug data");
    return;
  }

  memset(&bus_msg, 0, sizeof(mct_bus_msg_t));
  bus_msg.sessionid = (private->reserved_id >> 16);
  bus_msg.type = MCT_BUS_MSG_AE_EXIF_DEBUG_INFO;
  bus_msg.msg = (void *)aec_info;
  size = (int)sizeof(cam_ae_exif_debug_t);
  bus_msg.size = size;
  memset(aec_info, 0, size);
  aec_info->aec_debug_data_size = private->aec_debug_data_size;
  aec_info->sw_version_number = (uint64_t)(((uint64_t)MAJOR_NUM & 0xFFFF) |
    (((uint64_t)MINOR_NUM & 0xFFFF) << 16) |
    (((uint64_t)PATCH_NUM & 0xFFFF) << 32) |
    (((uint64_t)FEATURE_DESIGNATOR & 0xFFFF) << 48));
  AEC_LOW("aec_debug_data_size: %d, sw version number: %llx",
   private->aec_debug_data_size, aec_info->sw_version_number);
  memcpy(&(aec_info->aec_private_debug_data[0]), private->aec_debug_data_array,
    private->aec_debug_data_size);
  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = private->reserved_id;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.u.module_event.type = MCT_EVENT_MODULE_STATS_POST_TO_BUS;
  event.u.module_event.module_event_data = (void *)(&bus_msg);
  MCT_PORT_EVENT_FUNC(port)(port, &event);
  if (aec_info) {
    free(aec_info);
  }
}


/** aec_port_send_aec_info_to_metadata
 *  update aec info which required by eztuning
 **/

void aec_port_send_aec_info_to_metadata(mct_port_t *port,
  aec_output_data_t *output)
{
  mct_event_t        event;
  mct_bus_msg_t      bus_msg;
  aec_ez_tune_t      aec_info;
  aec_port_private_t *private;
  int                size;

  if (!output || !port) {
    AEC_ERR("input error");
    return;
  }

  private = (aec_port_private_t *)(port->port_private);
  memset(&bus_msg, 0, sizeof(mct_bus_msg_t));
  bus_msg.sessionid = (private->reserved_id >> 16);
  bus_msg.type = MCT_BUS_MSG_AE_EZTUNING_INFO;
  bus_msg.msg = (void *)&aec_info;
  size = (int)sizeof(aec_ez_tune_t);
  bus_msg.size = size;

  memcpy(&aec_info, &output->eztune, size);

  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = private->reserved_id;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.u.module_event.type = MCT_EVENT_MODULE_STATS_POST_TO_BUS;
  event.u.module_event.module_event_data = (void *)(&bus_msg);

  MCT_PORT_EVENT_FUNC(port)(port, &event);
}

void aec_port_configure_stats(aec_output_data_t *output,
  mct_port_t *port)
{
  aec_port_private_t *private = NULL;
  mct_event_t        event;
  aec_config_t       aec_config;

  private = (aec_port_private_t *)(port->port_private);

  aec_config = output->config;
  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = private->reserved_id;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.u.module_event.type = MCT_EVENT_MODULE_STATS_AEC_CONFIG_UPDATE;
  event.u.module_event.module_event_data = (void *)(&aec_config);

  MCT_PORT_EVENT_FUNC(port)(port, &event);
}

/** aec_port_adjust_for_fixed_fps
 *
 **/
static void aec_port_adjust_for_fixed_fps(aec_output_data_t *output, aec_port_private_t *private)
{
  if ((NULL != private) && (NULL != output))
  {
    if (TRUE == private->apply_fixed_fps_adjustment) {
      float factor = 1.00f;
      float fixed_fps_frame_duration = (float)(Q8) / (float)(private->fps.min_fps);

      /* If the current exposure time exceeds the fixed fps frame duration, the current stats update
      is adjusted so that the line count is capped to avoid dropping the frame rate. Also, the gain
      is increased accordingly to maintain the sensitivity */
      if (output->stats_update.aec_update.exp_time > fixed_fps_frame_duration) {
        /* The line count corresponding to the fixed fps frame duration is computed here*/
        uint32_t line_count = ((float)fixed_fps_frame_duration / (float)output->stats_update.aec_update.exp_time) *
                              (float)(output->stats_update.aec_update.linecount);

        /* The sensor driver adds an offset (around ~20) to the line count before applying the value
        in the registers and prior to the calculation of the frame duration. This causes the frame
        duration to vary slightly from the desired value. This margin value is subtracted here to
        ensure that the frame duration is exactly at the value desired.
        IMPORTANT: This margin value has been selected conservatively based on the current sensor
        drivers being used. It needs to be updated if a new sensor driver has a bigger margin value */
        line_count = line_count - MAX_INTEGRATION_MARGIN;

        factor = (float)line_count / (float)output->stats_update.aec_update.linecount;
      }

      /* Offset the gain, line count and exposure time by this factor */
      output->stats_update.aec_update.real_gain = output->stats_update.aec_update.real_gain / factor;
      output->stats_update.aec_update.linecount = output->stats_update.aec_update.linecount * factor;
      output->stats_update.aec_update.exp_time  = fixed_fps_frame_duration;
      output->stats_update.aec_update.exif_iso  = output->stats_update.aec_update.exif_iso / factor;
      private->apply_fixed_fps_adjustment = FALSE;
    }
  }

  else
  {
    AEC_ERR("Null pointer check failed!!! output = %p private = %p", output, private);
  }
}

/** aec_port_send_event:
 *    @port:         TODO
 *    @evt_type:     TODO
 *    @sub_evt_type: TODO
 *    @data:         TODO
 *
 * TODO description
 *
 * Return nothing
 **/
void aec_port_send_event(mct_port_t *port, int evt_type,
  int sub_evt_type, void *data, uint32_t sof_id)
{
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  mct_event_t        event;

  MCT_OBJECT_LOCK(port);

  /*
   * in traditional non-ZSL mode while the stream_type is CAM_STREAM_TYPE_SNAPSHOT
   * AEC shall not send AEC update outside.
   * In HAL3, framework could configure stream_type to SNAPSHOT mode but it's not
   * non-ZSL mode, in this mode, AEC still need to sent out the update
   * That's the reason put (private->stream_type == CAM_STREAM_TYPE_SNAPSHOT &&
   * !private->still.is_capture_intent) here
   */
  if (private->aec_auto_mode == AEC_MANUAL &&
      private->aec_update_flag == TRUE) {
    private->aec_update_flag = FALSE;
  } else if (private->aec_update_flag == FALSE ||
      (private->in_zsl_capture == TRUE && private->in_longshot_mode == 0) ||
      (private->stream_type == CAM_STREAM_TYPE_SNAPSHOT &&
      !private->still.is_capture_intent)) {
    AEC_LOW("No AEC update event to send");
    MCT_OBJECT_UNLOCK(port);
    return;
  } else {
    private->aec_update_flag = FALSE;
  }
  MCT_OBJECT_UNLOCK(port);
  /* Pack into an mct_event object */
  event.direction = MCT_EVENT_UPSTREAM;
  event.identity = private->reserved_id;
  event.type = evt_type;
  event.u.module_event.current_frame_id = sof_id;
  event.u.module_event.type = sub_evt_type;
  event.u.module_event.module_event_data = data;

  MCT_PORT_EVENT_FUNC(port)(port, &event);
}

/** aec_port_update_roi
 *
**/
static void aec_port_update_roi(aec_port_private_t *private,
  aec_interested_region_t roi)
{
  if (roi.enable) {
    private->aec_roi.rect.left = roi.r[0].x;
    private->aec_roi.rect.top = roi.r[0].y;
    private->aec_roi.rect.width = roi.r[0].dx;
    private->aec_roi.rect.height = roi.r[0].dy;
    private->aec_roi.weight = roi.weight;
  } else {
    private->aec_roi.rect.left = 0;
    private->aec_roi.rect.top = 0;
    private->aec_roi.rect.width = private->sensor_info.sensor_res_width;
    private->aec_roi.rect.height = private->sensor_info.sensor_res_height;
    private->aec_roi.weight = 0;
  }
}

/** aec_port_pack_update:
 *    @port:   TODO
 *    @output: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
void aec_port_pack_update(mct_port_t *port, aec_output_data_t *output,
  uint8_t aec_output_index)
{
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  aec_output_index %= AEC_OUTPUT_ARRAY_MAX_SIZE;
  output->stats_update.flag = STATS_UPDATE_AEC;

  if (output->snap.is_flash_snapshot) {
    /* Save flash snapshot data */
    private->still.is_flash_snap_data = TRUE;
    private->still.flash_sensor_gain = output->snap.sensor_gain;
    private->still.flash_real_gain = output->snap.real_gain;
    private->still.flash_line_cnt = output->snap.line_count;
    private->still.flash_lux_index = output->snap.lux_index;
    private->still.flash_exif_iso = output->snap.exif_iso;
    private->still.flash_drc_gains = output->snap.drc_gains;
    private->still.flash_exp_time = output->snap.exp_time;
  }
  if (private->still.is_capture_intent) {
    /* Update with still capture intent data */
    if (private->still.is_flash_snap_data) {
      output->stats_update.aec_update.sensor_gain = private->still.flash_sensor_gain;
        output->stats_update.aec_update.real_gain =
        output->stats_update.aec_update.s_real_gain =
        output->stats_update.aec_update.l_real_gain =
        private->still.flash_real_gain;
      output->stats_update.aec_update.linecount =
        output->stats_update.aec_update.s_linecount =
        output->stats_update.aec_update.l_linecount =
        private->still.flash_line_cnt;
      output->stats_update.aec_update.lux_idx   = private->still.flash_lux_index;
      output->stats_update.aec_update.exif_iso  = private->still.flash_exif_iso;
      output->stats_update.aec_update.exp_time = private->still.flash_exp_time;
      /* drc gains */
      output->stats_update.aec_update.total_drc_gain =
        private->still.flash_drc_gains.total_drc_gain;
      output->stats_update.aec_update.color_drc_gain =
        private->still.flash_drc_gains.color_drc_gain;
      output->stats_update.aec_update.gtm_ratio =
        private->still.flash_drc_gains.gtm_ratio;
      output->stats_update.aec_update.ltm_ratio =
        private->still.flash_drc_gains.ltm_ratio;
      output->stats_update.aec_update.la_ratio =
        private->still.flash_drc_gains.la_ratio;
      output->stats_update.aec_update.gamma_ratio =
        private->still.flash_drc_gains.gamma_ratio;
      private->still.is_flash_snap_data = FALSE;
    } else {
      output->stats_update.aec_update.sensor_gain = output->snap.sensor_gain;
        output->stats_update.aec_update.real_gain =
        output->stats_update.aec_update.s_real_gain =
        output->stats_update.aec_update.l_real_gain =
        output->snap.real_gain;
      output->stats_update.aec_update.linecount =
        output->stats_update.aec_update.s_linecount =
        output->stats_update.aec_update.l_linecount =
        output->snap.line_count;
      output->stats_update.aec_update.exif_iso = output->snap.exif_iso;
      output->stats_update.aec_update.lux_idx  = output->snap.lux_index;
      output->stats_update.aec_update.exp_time = output->snap.exp_time;
      output->stats_update.aec_update.total_drc_gain =
        output->snap.drc_gains.total_drc_gain;
      output->stats_update.aec_update.color_drc_gain =
        output->snap.drc_gains.color_drc_gain;
      output->stats_update.aec_update.gtm_ratio =
        output->snap.drc_gains.gtm_ratio;
      output->stats_update.aec_update.ltm_ratio =
        output->snap.drc_gains.ltm_ratio;
      output->stats_update.aec_update.la_ratio =
        output->snap.drc_gains.la_ratio;
      output->stats_update.aec_update.gamma_ratio =
        output->snap.drc_gains.gamma_ratio;
    }
  }

  if (private->fast_aec_data.enable &&
    output->stats_update.aec_update.sof_id >= private->fast_aec_forced_cnt) {
    AEC_HIGH("Fast-AEC: force settled to run AWB, fid %d",
      output->stats_update.aec_update.sof_id);
    output->stats_update.aec_update.settled = TRUE;
  }

  private->touch_ev_status = output->stats_update.aec_update.touch_ev_status;
  private->low_light_shutter_flag =
    output->stats_update.aec_update.low_light_shutter_flag;
  output->stats_update.aec_update.est_state = private->est_state;
  output->stats_update.aec_update.led_mode = private->led_mode;

  private->locked_from_algo = output->locked_from_algo;
  private->core_aec_locked = output->aec_locked;

  /* Save algo update */
  private->state_update.type = AEC_PORT_STATE_ALGO_UPDATE;
  memcpy(&private->state_update.u.output[aec_output_index],
    output, sizeof(aec_output_data_t));
  private->state_update.u.output[aec_output_index].aec_custom_param =
    output->aec_custom_param;

  /* Keep this aec state update call for legacy not needed in HAL3 */
  aec_port_update_aec_state(private, private->state_update);

  /* Save the debug data in private data struct to be sent out later */
  private->aec_debug_data_size = output->aec_debug_data_size;
  if (output->aec_debug_data_size) {
    memcpy(private->aec_debug_data_array, output->aec_debug_data_array,
      output->aec_debug_data_size);
  }
  /* Save the AEC update to stored param for next camera startup init.
  */
  if (private->stored_params && !((private->est_state != AEC_EST_OFF) ||
    output->stats_update.aec_update.use_led_estimation) && (AEC_AUTO == private->aec_auto_mode)) {
    private->stored_params->exp_index = output->stats_update.aec_update.exp_index;
  }
}

void aec_port_update_aec_state(aec_port_private_t *private,
  aec_state_update_data_t aec_update_state)
{
  pthread_mutex_lock(&private->update_state_lock);
  uint8_t last_state        = private->aec_state;
  boolean low_light         = FALSE;
  aec_output_data_t *output = NULL;
  if (aec_update_state.type == AEC_PORT_STATE_MODE_UPDATE) {
    if (aec_update_state.u.trigger_new_mode) {
      if (private->aec_auto_mode != AEC_MANUAL)
        private->aec_state = CAM_AE_STATE_SEARCHING;
      else
        private->aec_state = CAM_AE_STATE_INACTIVE;
    }
  } else if (aec_update_state.type == AEC_PORT_STATE_ALGO_UPDATE) {

    uint8_t output_index = private->state_update.cb_output_index;
    output_index %= AEC_OUTPUT_ARRAY_MAX_SIZE;
    output = &aec_update_state.u.output[output_index];

    /* state transition logic */
    switch(private->aec_state) {
    case CAM_AE_STATE_INACTIVE: {
      if (aec_port_is_aec_locked(private)) {
        private->aec_state = CAM_AE_STATE_LOCKED;
      } else if (aec_port_is_aec_searching(output)) {
        private->aec_state = CAM_AE_STATE_SEARCHING;
      } else {
        //no change
      }
      if(aec_port_is_precapture_start(private)) {
        private->aec_state = CAM_AE_STATE_PRECAPTURE;
      }
    }
      break;

    case CAM_AE_STATE_SEARCHING: {
      if (aec_port_is_aec_locked(private)) {
        private->aec_state = CAM_AE_STATE_LOCKED;
      } else if (aec_port_is_converged(output, &low_light)) {
        if (low_light == TRUE) {
          private->aec_state = CAM_AE_STATE_FLASH_REQUIRED;
        } else {
          private->aec_state = CAM_AE_STATE_CONVERGED;
        }
      } else {
        //no change
      }
      if (aec_port_is_precapture_start(private)) {
        private->aec_state = CAM_AE_STATE_PRECAPTURE;
      }
    }
      break;

    case CAM_AE_STATE_CONVERGED: {
      if (aec_port_is_aec_locked(private)) {
        private->aec_state = CAM_AE_STATE_LOCKED;
      } else {
        if (aec_port_is_converged(output, &low_light)) {
          if (low_light == TRUE) {
            private->aec_state = CAM_AE_STATE_FLASH_REQUIRED;
          } else {
            private->aec_state = CAM_AE_STATE_CONVERGED;
          }
        } else {
          private->aec_state = CAM_AE_STATE_SEARCHING;
        }
      }

      if (aec_port_is_precapture_start(private)) {
        private->aec_state = CAM_AE_STATE_PRECAPTURE;
      }
    }
      break;

    case CAM_AE_STATE_LOCKED: {
      if ((FALSE == aec_port_is_aec_locked(private)) &&
          (FALSE == private->core_aec_locked)) {
        if (aec_port_is_converged(output, &low_light)) {
          if (low_light == TRUE) {
            private->aec_state = CAM_AE_STATE_FLASH_REQUIRED;
          } else {
            private->aec_state = CAM_AE_STATE_CONVERGED;
          }
        } else {
          private->aec_state = CAM_AE_STATE_SEARCHING;
        }
      }
      if(aec_port_is_precapture_start(private)) {
        private->aec_state = CAM_AE_STATE_PRECAPTURE;
      }
    }
      break;

    case CAM_AE_STATE_FLASH_REQUIRED: {
      if (aec_port_is_aec_locked(private)) {
        private->aec_state = CAM_AE_STATE_LOCKED;
      } else {
        if (aec_port_is_converged(output, &low_light)) {
          if (low_light == TRUE) {
            private->aec_state = CAM_AE_STATE_FLASH_REQUIRED;
          } else {
            private->aec_state = CAM_AE_STATE_CONVERGED;
          }
        } else {
          private->aec_state = CAM_AE_STATE_SEARCHING;
        }
      }
      if (aec_port_is_precapture_start(private)) {
        private->aec_state = CAM_AE_STATE_PRECAPTURE;
      }
    }
      break;

    case CAM_AE_STATE_PRECAPTURE: {
      if (aec_port_is_precapture_start(private)) {
        AEC_LOW("Still in Precatpure!!");
      } else if (aec_port_is_aec_locked(private)) {
        private->aec_state = CAM_AE_STATE_LOCKED;
      } else if (aec_port_is_converged(output, &low_light)) {
        if (low_light == TRUE) {
          private->aec_state = CAM_AE_STATE_FLASH_REQUIRED;
        } else {
          private->aec_state = CAM_AE_STATE_CONVERGED;
        }
      } else {
        /* Optimization: During LED estimation and Prepare snapshot is done
           led is turned off and it takes 4~5 frames to converge. Uncomment
           below to optimize this delay. But this doesn't follow State machine
           table*/
        if (private->force_prep_snap_done == TRUE) {
          if (output->stats_update.aec_update.flash_needed) {
            private->aec_state = CAM_AE_STATE_FLASH_REQUIRED;
          } else {
            private->aec_state = CAM_AE_STATE_CONVERGED;
          }
        }
      }
      private->force_prep_snap_done = FALSE;
    }
      break;

    default: {
      AEC_ERR("Error, AEC last state is unknown: %d",
        private->aec_last_state);
    }
      break;
    }
  } else {
    AEC_ERR("Invalid state transition type");
  }

  if (private->aec_state != last_state) {
    AEC_LOW("AE state transition Old=%d New=%d aec_precap_start=%d",
      last_state, private->aec_state, private->aec_precap_start);
  }

  private->aec_last_state = last_state;
error_update_aec_state:
  pthread_mutex_unlock(&private->update_state_lock);
}

/** aec_port_forward_update_event_if_linked:
  *    @mct_port:   MCT port object
  *    @aec_update: AEC update info
  *
  * Forward the AEC update event to the slave session if: dual camera is in use, the
  * cameras are linked, and this method is called from the master session
  *
  * Return: boolean value indicating success or failure
  */
static boolean aec_port_forward_update_event_if_linked(
  mct_port_t* mct_port,
  stats_update_t* aec_update)
{
  aec_port_private_t* aec_port = (aec_port_private_t *)(mct_port->port_private);
  boolean result = true;

  /* Only forward the AEC update if we are the master camera, and the peer ID is set,
     indicating dual camera is in use */
  if (aec_port != NULL &&
      aec_port->dual_cam_sensor_info == CAM_TYPE_MAIN &&
      aec_port->intra_peer_id != 0)
  {
    /* Populate the peer info with master's AEC update and anti-banding state */
    aec_port_peer_aec_update aec_peer_update;
    aec_peer_update.update       = (*aec_update);
    aec_peer_update.anti_banding = aec_port->aec_object.output.cur_atb;

    /* Forward the AEC update info to the slave session */
    result = stats_util_post_intramode_event(mct_port,
                                             aec_port->intra_peer_id,
                                             MCT_EVENT_MODULE_STATS_PEER_AEC_UPDATE,
                                             (void*)&aec_peer_update);
    if (!result)
    {
      AEC_MSG_ERROR("Error! failed to forward the AEC Update event to the slave (id=%d)",
                    aec_port->intra_peer_id);
    }
  }

  return result;
}

void aec_port_send_aec_update(mct_port_t *port, aec_port_private_t
  *private, char *trigger_name, uint8_t output_index, uint32_t aec_update_frame_id)
{
  private->still.is_capture_intent = FALSE;
  output_index %= AEC_OUTPUT_ARRAY_MAX_SIZE;

  /* check if the gains are valid */
  if (private->state_update.u.output[output_index].stats_update.aec_update.sensor_gain == 0 ||
      private->state_update.u.output[output_index].stats_update.aec_update.linecount == 0) {
    AEC_HIGH("WARNING: Sending invalid AEC Update: Skipped...");
    return;
  }

  /* This function ensures that the AEC line count does not change the frame duration if the
  the max and min frame rate values have been forced to be the same. This helps resolve the FPS
  CTS test failure under low-light conditions */
  aec_port_adjust_for_fixed_fps(&private->state_update.u.output[output_index],
    private);

  /* exif, metadata, stats config handling moved to here */
  aec_port_pack_exif_info(port,
    &private->state_update.u.output[output_index]);
  if (private->state_update.u.output[output_index].eztune.running) {
    aec_port_send_aec_info_to_metadata(port,
    &private->state_update.u.output[output_index]);
  }

  aec_port_update_aec_flash_state(port, &private->state_update.u.output[output_index]);

  aec_port_print_log(&private->state_update.u.output[output_index], trigger_name, private, output_index);

  AEC_LOW("AEC_UPDATE_DBG: %s: Sending AEC Update for Frame_ID:%d"
    " Curr_SOF_ID:%d ouput_index=%d", trigger_name, aec_update_frame_id,
    private->cur_sof_id, output_index);

  private->state_update.type = AEC_PORT_STATE_ALGO_UPDATE;
  private->state_update.u.output[output_index].stats_update.flag = STATS_UPDATE_AEC;
  private->aec_update_flag = TRUE;
  aec_port_send_event(port, MCT_EVENT_MODULE_EVENT,
    MCT_EVENT_MODULE_STATS_AEC_UPDATE,
   (void *)(&(private->state_update.u.output[output_index].stats_update)), aec_update_frame_id);

  /* Forward AEC update data to slave session if there is a slave */
  (void)aec_port_forward_update_event_if_linked(port, &private->state_update.u.output[output_index].stats_update);
}

static void aec_port_stats_done_callback(void* p, void* stats)
{
  mct_port_t         *port = (mct_port_t *)p;
  aec_port_private_t *private = NULL;
  stats_t            *aec_stats = (stats_t *)stats;
  if (!port) {
    AEC_ERR("input error");
    return;
  }

  private = (aec_port_private_t *)(port->port_private);
  if (!private) {
    return;
  }

  AEC_LOW("DONE AEC STATS ACK back");
  if (aec_stats) {
    circular_stats_data_done(aec_stats->ack_data, port,
                             private->reserved_id, private->cur_sof_id);
  }
}

/** aec_port_apply_antibanding:
  *
  *    @antibanding: Antibanding mode to use to apply antibanding on the exposure time
  *    @exp_time:    Exposure time to apply antibanding on
  *
  * Applies antibanding on the given exposure time and returns the adjusted exposure time.
  * If not banding was applied, the unmodified exposure time is returned.
  *
  * Return: adjusted exposure time which takes antibanding into account
  */
static float aec_port_apply_antibanding(
  aec_antibanding_type_t antibanding,
  float exp_time)
{
  if (antibanding == STATS_PROC_ANTIBANDING_60HZ ||
      antibanding == STATS_PROC_ANTIBANDING_50HZ)
  {
    int freq = (antibanding == STATS_PROC_ANTIBANDING_60HZ ? (60*2) : (50*2));
    float band_period = 1.0f / freq;
    if (exp_time > band_period)
      exp_time = band_period * (int)(exp_time / band_period);
  }

  return exp_time;
}

/** aec_port_interpolate_aec_update_from_peer:
  *
  *    @aec_port:           AEC port handle
  *    @master_update_info: Input parameter; information on the AEC update from the master
  *    @slave_stats_update: Output parameter; slave update event to interpolate the results into
  *
  * Interpolate the given master update event to match the slave's characteristics
  *
  * Return: void
  */
static void aec_port_interpolate_aec_update_from_peer(
  aec_port_private_t*       aec_port,
  aec_port_peer_aec_update* master_update_info,
  stats_update_t*           slave_stats_update)
{
  aec_antibanding_type_t master_antibanding = master_update_info->anti_banding;
  aec_update_t* master_update = &(master_update_info->update.aec_update);
  aec_update_t* slave_update  = &(slave_stats_update->aec_update);
  float    max_gain           = aec_port->sensor_info.max_gain;
  int      AEC_Q8             = 0x00000100;
  float    max_exp_time       = (float)AEC_Q8 / (float)aec_port->fps.min_fps;
  float    slave_exp_time     = 1.0f;
  float    slave_gain         = 1.0f;
  uint16_t slave_linecount    = 1;
  float    multiplier         = aec_port->dual_cam_exp_multiplier;
  float    res_multiplier     = 1.0f;


  /* Make a shallow copy to initialize the slave data */
  (*slave_stats_update) = master_update_info->update;

  if (master_update->exp_time == 0)
    return;

  /* Interpolate exposure time and gain based on the given multiplier */
  if (multiplier >= 1.0f){
    /* Master is brighter than the slave. Preference is to adjust exposure time first, then
       any residual exposure will be adjusted via gain. This is just a preference and can be
       changed in the future */
    /* Calculate exposure time by using the multiplier, then clip it to support the current min-fps */
    slave_exp_time = master_update->exp_time * multiplier;
    slave_exp_time = MIN(slave_exp_time, max_exp_time);
    /* Apply anti-banding */
    slave_exp_time = aec_port_apply_antibanding(master_antibanding, slave_exp_time);
    /* Calculate the residual multiplier and multiply gain by it */
    res_multiplier = (master_update->exp_time * multiplier) / slave_exp_time;
    slave_gain     = master_update->real_gain * res_multiplier;
  }
  else {
    /* Slave is brighter than the master. Preference is to adjust gain first so as to try to
       keep exposure time the same between master and slave. If gain reaches its cap however,
       there'll be some residual that would require exposure time to be recalculated. */
    /* First, try to reduce gain*/
    slave_gain = master_update->real_gain * multiplier;
    /* Clip the gain value between 1.0 and max_gain */
    slave_gain = MAX(1.0f, MIN(max_gain, slave_gain));

    /* Reduce exposure time with the gain residual multiplier and clip it*/
    res_multiplier = (master_update->real_gain * multiplier) / slave_gain;
    slave_exp_time = master_update->exp_time * res_multiplier;
    slave_exp_time = MIN(slave_exp_time, max_exp_time);

    /* Apply anti-banding */
    slave_exp_time = aec_port_apply_antibanding(master_antibanding, slave_exp_time);

    /* Calculate the exposure residual to adjust gain again */
    res_multiplier = (master_update->exp_time * res_multiplier) / slave_exp_time;
    slave_gain *= res_multiplier;
  }

  /* Clip the gain value between 1.0 and max_gain */
  slave_gain = MAX(1.0f, MIN(max_gain, slave_gain));

  /* Calculate line count based on exposure time */
  slave_linecount = (slave_exp_time *
                     aec_port->sensor_info.pixel_clock /
                     aec_port->sensor_info.pixel_clock_per_line);
  if (slave_linecount < 1) {
    slave_linecount = 1;
  }

  slave_update->exp_time  = slave_exp_time;
  slave_update->real_gain = slave_gain;
  slave_update->linecount = slave_linecount;

  AEC_LOW("\nDual interpolation: Master exp time %f gain %f linecount %d\n"
          "Dual interpolation: Slave  exp time %f gain %f linecount %d",
          master_update->exp_time,
          master_update->real_gain,
          master_update->linecount,
          slave_update->exp_time,
          slave_update->real_gain,
          slave_update->linecount);
}

/** aec_port_handle_peer_aec_update:
  *    @mct_port:   MCT port object
  *    @aec_update: AEC update info
  *
  * Handles a forwarded peer event for AEC update, where the master session has
  * forwraded the AEC update info to the slave session
  *
  * Return: void
  */
static void aec_port_handle_peer_aec_update(
  mct_port_t* mct_port,
  aec_port_peer_aec_update* aec_update)
{
  stats_update_t interpolated_update;
  aec_port_private_t* aec_port = (aec_port_private_t *)(mct_port->port_private);

  memset(&interpolated_update, 0, sizeof(interpolated_update));
  /* Interpolate the peer event to match the slave's AEC characteristics */
  aec_port_interpolate_aec_update_from_peer(aec_port, aec_update, &interpolated_update);

  /* Send the interpolated update event */
  aec_port_send_event(mct_port,
      MCT_EVENT_MODULE_EVENT,
      MCT_EVENT_MODULE_STATS_AEC_UPDATE,
      (void*)&interpolated_update,
      aec_update->update.aec_update.sof_id);
}

/** aec_port_callback:
 *    @output: TODO
 *    @p:      TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_callback(aec_output_data_t *output, void *p)
{
  mct_port_t         *port = (mct_port_t *)p;
  aec_port_private_t *private = NULL;
  mct_event_t        event;
  boolean            low_light = FALSE;
  boolean            send_update_now = FALSE;

  if (!output || !port) {
    AEC_ERR("input error");
    return;
  }

  private = (aec_port_private_t *)(port->port_private);
  if (!private) {
    return;
  }

  /* First handle callback in extension if available */
  if (private->func_tbl.ext_callback) {
    stats_ext_return_type ret;
    ret = private->func_tbl.ext_callback(
      port, output, &output->stats_update.aec_update);
    if (STATS_EXT_HANDLING_COMPLETE == ret) {
      AEC_LOW("Callback handled. Skipping rest!");
      return;
    }
  }

  /* populate the stats_upate object to be sent out*/
  if (AEC_UPDATE == output->type) {

    /* skip stats for Unified Flash capture */
    if (private->stats_frame_capture.frame_capture_mode) {
      AEC_LOW("Skip CB processing in Unified Flash mode");
      return;
    } else {
      /* Do not send Auto update event in following cases:
        * 1. Manual mode: If callback comes after setting the manual mode,
        * then two aec updates(auto and manual) are sent on same frame.
        * 2. If stats skip count is non zero */
      if (private->aec_skip.skip_count || AEC_MANUAL == private->aec_auto_mode) {
        AEC_LOW("Send AEC Update: Skip sending update, skip count=%d, ae mode=%d",
          private->aec_skip.skip_count, private->aec_auto_mode);
        return;
      }
    }

    MCT_OBJECT_LOCK(port);

    uint32_t cur_sof_id = private->cur_sof_id;
    uint32_t cur_aec_out_frame_id = output->stats_update.aec_update.frame_id;
    uint8_t cb_output_index = 0;

    /* Initialization, after hitting first CB from algo */
    if (private->state_update.sof_output_index == 0xFF)
      private->state_update.sof_output_index = 0;
    if (private->state_update.cb_output_index == 0xFF)
      private->state_update.cb_output_index = 0;

    private->aec_update_flag = TRUE;
    /* Produce  AEC Update @ CB out index position, and update SOF index with CB index
       Consume AEC update @ SOF with SOF index */
    cb_output_index = private->state_update.cb_output_index;
    private->state_update.sof_output_index = cb_output_index;

    /* AEC Update goes out for "CurrSOF-1" always, in case of CB */
    if ((cur_sof_id > 1) && (cur_sof_id-1 == cur_aec_out_frame_id))
      send_update_now = TRUE;

    /* Update the output index only if we dont send out AEC update in CB */
    if (send_update_now == FALSE) {
        private->state_update.cb_output_index++;
        private->state_update.cb_output_index %= AEC_OUTPUT_ARRAY_MAX_SIZE;
    }
    MCT_OBJECT_UNLOCK(port);

    aec_port_pack_update(port, output, cb_output_index);
    if(send_update_now == TRUE) {

      aec_port_send_aec_update(port, private, "CB: ", cb_output_index, cur_aec_out_frame_id);
    }
    else {
      if (cur_sof_id == cur_aec_out_frame_id) {
        AEC_LOW("AEC_UPDATE_DBG: PORT_CB: Normal: Send this update in next SOF_ID:%d"
          " OutputFrameId:%d output_index=%d -> Do Nothing",
          cur_sof_id+1, cur_aec_out_frame_id, cb_output_index);
      }
      else {
        AEC_ERR("AEC_UPDATE_DBG: PORT_CB: WARNING: threading issue: SOF_ID:%d"
          " OutputFrameId:%d output_index=%d -> Debug",
          cur_sof_id, cur_aec_out_frame_id, cb_output_index);
        aec_port_send_aec_update(port, private, "CB-WAR: ",
          cb_output_index,
          cur_aec_out_frame_id);
      }
    }
  }
  else if (AEC_SEND_EVENT == output->type) {
    /* Update start-up values in both outputs structs */
    int32 idx;

    /* Initialization, after hitting first CB from algo */
    if (private->state_update.sof_output_index == 0xFF)
      private->state_update.sof_output_index = 0;
    if (private->state_update.cb_output_index == 0xFF)
      private->state_update.cb_output_index = 0;

    aec_port_pack_update(port, output, 0);
    for(idx = 1; idx < AEC_OUTPUT_ARRAY_MAX_SIZE; idx++) {
      memcpy(&private->state_update.u.output[idx],
        &private->state_update.u.output[0], sizeof(aec_output_data_t));
    }
    aec_port_send_aec_update(port, private, "ALGO_STORE_UP: ", 0, 0);
  }

  /* Set configuration if required */
  if (output->need_config &&
      (output->type == AEC_UPDATE) &&
      (private->dual_cam_sensor_info == CAM_TYPE_MAIN)) {
    aec_port_configure_stats(output, port);
    output->need_config = 0;
  }

  return;
}

/** aec_port_parse_RDI_stats_AE:
 *    @destLumaBuff: TODO
 *    @rawBuff:      TODO
 *
 * TODO description
 *
 * TODO Return
 **/

static int32_t aec_port_parse_RDI_stats_AE(aec_port_private_t *private,
  uint32_t *destLumaBuff, void *rawBuff)
{
  int32_t rc = -1;

  if (private->parse_RDI_stats.parse_VHDR_stats_callback != NULL) {
    rc = private->parse_RDI_stats.parse_VHDR_stats_callback(destLumaBuff,
        rawBuff);
  } else {
    AEC_LOW(" parse_VHDR_stats_callback not supported");
    rc = -1;
  }

  return rc;
}

/** aec_port_check_session
 *  @data1: session identity;
 *  @data2: new identity to compare.
 **/
static boolean aec_port_check_session(void *data1, void *data2)
 {
  return (((*(unsigned int *)data1) & 0xFFFF0000) ==
          ((*(unsigned int *)data2) & 0xFFFF0000) ?
          TRUE : FALSE);
}

static boolean aec_port_conv_fromhal_flashmode(cam_flash_mode_t flashmode ,
  flash_mode_t *internel_mode){

  switch (flashmode) {
    case CAM_FLASH_MODE_OFF: {
     *internel_mode = FLASH_MODE_OFF;
     break;
    }

    case CAM_FLASH_MODE_AUTO: {
     *internel_mode = FLASH_MODE_AUTO;
     break;
    }

    case CAM_FLASH_MODE_ON: {
      *internel_mode = FLASH_MODE_ON;
      break;
    }

    case CAM_FLASH_MODE_TORCH: {
      *internel_mode = FLASH_MODE_TORCH;
      break;
    }

    case CAM_FLASH_MODE_SINGLE: {
      *internel_mode = FLASH_MODE_SINGLE;
      break;
    }

    default: {
      *internel_mode = FLASH_MODE_MAX;
      break;
    }

  }
  return TRUE;
}


static boolean aec_port_conv_tohal_flashmode(cam_flash_mode_t *flashmode ,
  flash_mode_t internel_mode){

  switch (internel_mode) {
    case FLASH_MODE_OFF: {
     *flashmode = CAM_FLASH_MODE_OFF;
     break;
    }

    case FLASH_MODE_AUTO: {
     *flashmode = CAM_FLASH_MODE_AUTO;
     break;
    }

    case FLASH_MODE_ON: {
      *flashmode = CAM_FLASH_MODE_ON;
      break;
    }

    case FLASH_MODE_TORCH: {
      *flashmode = CAM_FLASH_MODE_TORCH;
      break;
    }

    case FLASH_MODE_SINGLE: {
      *flashmode = CAM_FLASH_MODE_SINGLE;
      break;
    }

    default: {
      *flashmode = CAM_FLASH_MODE_MAX;
      break;
    }
  }
  return TRUE;
}

/** aec_port_check_identity:
 *    @data1: session identity;
 *    @data2: new identity to compare.
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_check_identity(unsigned int data1, unsigned int data2)
{
  return ((data1 & 0xFFFF0000) == (data2 & 0xFFFF0000)) ? TRUE : FALSE;
}
/** aec_port_update_manual_setting:
 *    @private:   Private data of the port
 *    @aec_update: manula aec settings.
 *
 * This function converts manual settings from HAL3 to gain /linecount.
 *
 * Return: bool
 **/
static boolean aec_port_update_manual_setting(aec_port_private_t  *private ,
  aec_manual_update_t * aec_update)
{
  float new_sensitivity;

  if (!private->manual.is_exp_time_valid || !private->manual.is_gain_valid) {
    AEC_ERR(" both exp_time & gain need to be set");
    return FALSE;
  }

  if (private->manual.is_exp_time_valid) {
    aec_update->linecount = (1.0 * private->manual.exp_time *
      private->sensor_info.pixel_clock / private->sensor_info.pixel_clock_per_line);
    if (aec_update->linecount < 1) {
      aec_update->linecount = 1;
    }
  }
  if (private->manual.is_gain_valid) {
    aec_update->sensor_gain = private->manual.gain;
  }
  /* Update exp idx */
  new_sensitivity = aec_update->sensor_gain *
    aec_update->linecount;
  aec_update->lux_idx =
      log10(new_sensitivity/ private->init_sensitivity)/ log10(1.03);
  aec_update->exif_iso = (uint32_t)(aec_update->sensor_gain * 100 / private->ISO100_gain);
  AEC_LOW("Manual mode called gain %f  linecnt %d lux idx %f",
     aec_update->sensor_gain, aec_update->linecount, aec_update->lux_idx);
  return TRUE;
}

/** aec_port_unified_fill_manual:
 *    @frame_batch:  Frame data to be populated (output)
 *    @private: Private port data (input)
 *    @manual_3A_mode: Manual parameters to be configure (input)
 *    @capture_type: Unified capture type
 *
 * This function fill the batch structure using manual parameters.
 *
 * Return: TRUE on success
 **/
static boolean aec_port_unified_fill_manual(aec_capture_frame_info_t *frame_batch,
  aec_port_private_t  *private,
  cam_capture_manual_3A_t manual_3A_mode,
  cam_capture_type capture_type)
{
  float new_sensitivity = 0.0;


  if (CAM_CAPTURE_MANUAL_3A == capture_type) {
    if (!(CAM_SETTINGS_TYPE_ON == manual_3A_mode.exp_mode) ||
      !(CAM_SETTINGS_TYPE_ON == manual_3A_mode.iso_mode)) {
      AEC_ERR("Invalid parameter: exp_mode: %d, iso_mode: %d",
        manual_3A_mode.exp_mode, manual_3A_mode.iso_mode);
      return FALSE;
    }
    frame_batch->exp_time = (float)manual_3A_mode.exp_time / 1000000000;
    frame_batch->sensor_gain = manual_3A_mode.iso_value;
  } else if (CAM_CAPTURE_RESET == capture_type) {
    if (!private->manual.is_exp_time_valid || !private->manual.is_gain_valid) {
      AEC_ERR("Error: both exp_time & gain must be set");
      return FALSE;
    }
    frame_batch->exp_time = private->manual.exp_time;
    frame_batch->sensor_gain = private->manual.gain;
  } else {
    AEC_ERR("Error: Invalid capture type");
    return FALSE;
  }

  frame_batch->line_count = (1.0 * frame_batch->exp_time *
    private->sensor_info.pixel_clock / private->sensor_info.pixel_clock_per_line);
  if (frame_batch->line_count < 1) {
    frame_batch->line_count = 1;
  }

  new_sensitivity = frame_batch->sensor_gain * frame_batch->line_count;
  frame_batch->lux_idx =
    log10(new_sensitivity / private->init_sensitivity) / log10(1.03);

  frame_batch->iso = (uint32_t)(frame_batch->sensor_gain * 100 / private->ISO100_gain);

  return TRUE;
}

/** aec_port_unifed_request_batch_data:
 *    @private:   Private data of the port
 *
 * This function request to the algoritm the data to fill the batch information.
 *
 * Return: TRUE on success
 **/
static boolean aec_port_unifed_request_batch_data_to_algo(
  aec_port_private_t *private)
{
  boolean rc = FALSE;
  aec_frame_batch_t *priv_frame_info = NULL, *algo_frame_info = NULL;
  stats_update_t *stats_update = NULL;
  int i = 0, j = 0;

  q3a_thread_aecawb_msg_t *aec_msg =
    aec_port_malloc_msg(MSG_AEC_GET, AEC_GET_PARAM_UNIFIED_FLASH);

  if (!aec_msg) {
    AEC_ERR("Not enough memory");
    rc = FALSE;
    return rc;
  }
  aec_msg->sync_flag = TRUE;
  memcpy(&aec_msg->u.aec_get_parm.u.frame_info,
    &private->stats_frame_capture.frame_info, sizeof(aec_frame_batch_t));
  AEC_LOW("No. of Batch = %d",
    aec_msg->u.aec_get_parm.u.frame_info.num_batch);
  rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
  if (FALSE == rc) {
    AEC_ERR("Fail to get UNIFIED_FLASH");
    free(aec_msg);
    return rc;
  }

  priv_frame_info = &private->stats_frame_capture.frame_info;
  algo_frame_info = &aec_msg->u.aec_get_parm.u.frame_info;

  /* Verify that read-only data was not change by algo */
  if (priv_frame_info->num_batch != algo_frame_info->num_batch){
    AEC_ERR("Error: AEC_GET_PARAM_UNIFIED_FLASH call modified read-only param");
    free(aec_msg);
    return FALSE;
  }
  for (i = 0; i < priv_frame_info->num_batch; i++) {
    if ((priv_frame_info->frame_batch[i].capture_type !=
          algo_frame_info->frame_batch[i].capture_type) ||
        (priv_frame_info->frame_batch[i].flash_hal !=
          algo_frame_info->frame_batch[i].flash_hal) ||
        (priv_frame_info->frame_batch[i].flash_mode !=
          algo_frame_info->frame_batch[i].flash_mode) ||
        (priv_frame_info->frame_batch[i].hdr_exp !=
          algo_frame_info->frame_batch[i].hdr_exp)) {
      AEC_ERR("Error: AEC_GET_PARAM_UNIFIED_FLASH call modified capture_type param");
      free(aec_msg);
      return FALSE;
    }
  }

  /* Copy algo data to internal private sturcture */
  private->stats_frame_capture.frame_info = aec_msg->u.aec_get_parm.u.frame_info;

  for (i = 0; i < priv_frame_info->num_batch; i++) {

    if (priv_frame_info->frame_batch[i].drc_gains.ltm_ratio != -1) { /* ADRC enabled */
      AEC_HIGH("AEDBG,UnifiedFlash: Frame_capture_mode idx[%i] type=%d"
        " SensorGain:RealGain:LineCount = %f:%f:%d drc_gains=(%f, %f), "
        "drc_ratios=(%f,%f,%f,%f) lux_idx=%f", i,
        priv_frame_info->frame_batch[i].capture_type,
        priv_frame_info->frame_batch[i].sensor_gain,
        priv_frame_info->frame_batch[i].real_gain,
        priv_frame_info->frame_batch[i].s_line_count,
        priv_frame_info->frame_batch[i].drc_gains.total_drc_gain,
        priv_frame_info->frame_batch[i].drc_gains.color_drc_gain,
        priv_frame_info->frame_batch[i].drc_gains.gtm_ratio,
        priv_frame_info->frame_batch[i].drc_gains.ltm_ratio,
        priv_frame_info->frame_batch[i].drc_gains.la_ratio,
        priv_frame_info->frame_batch[i].drc_gains.gamma_ratio,
        priv_frame_info->frame_batch[i].lux_idx);
    }
    else { /* ADRC disabled */
      AEC_HIGH("AEDBG,UnifiedFlash: Frame_capture_mode idx[%i] type=%d s_line_count=%d real_gain=%f "
        "sensor_gain=%f", i,
        priv_frame_info->frame_batch[i].capture_type,
        priv_frame_info->frame_batch[i].s_line_count,
        priv_frame_info->frame_batch[i].real_gain,
        priv_frame_info->frame_batch[i].sensor_gain,
        priv_frame_info->frame_batch[i].lux_idx);
    }
  }

  private->frame_capture_update = private->state_update;

  stats_update =
    &private->frame_capture_update.u.output[0].stats_update;
  stats_update->flag = STATS_UPDATE_AEC;
  /*Initializing the est state variable to OFF by default,
    as in case of frame capture frame, there can never be
    estimation ON */
  stats_update->aec_update.est_state = AEC_EST_OFF;

  stats_update->aec_update.sof_id = private->cur_sof_id;
  stats_update->aec_update.use_led_estimation =
    priv_frame_info->use_led_estimation;

  stats_update->aec_update.led_off_params.linecnt = priv_frame_info->led_off_linecount;
  stats_update->aec_update.led_off_params.s_gain = priv_frame_info->led_off_s_gain;
  stats_update->aec_update.led_off_params.s_linecnt = priv_frame_info->led_off_s_linecount;
  stats_update->aec_update.led_off_params.l_gain = priv_frame_info->led_off_l_gain;
  stats_update->aec_update.led_off_params.l_linecnt = priv_frame_info->led_off_l_linecount;

  /* Update adrc specific fields here start*/
  stats_update->aec_update.led_off_params.real_gain = priv_frame_info->led_off_real_gain;
  stats_update->aec_update.led_off_params.sensor_gain = priv_frame_info->led_off_sensor_gain;
  stats_update->aec_update.led_off_params.total_drc_gain =
    priv_frame_info->led_off_drc_gains.total_drc_gain;
  stats_update->aec_update.led_off_params.color_drc_gain =
    priv_frame_info->led_off_drc_gains.color_drc_gain;
  stats_update->aec_update.led_off_params.gtm_ratio =
    priv_frame_info->led_off_drc_gains.gtm_ratio;
  stats_update->aec_update.led_off_params.ltm_ratio =
    priv_frame_info->led_off_drc_gains.ltm_ratio;
  stats_update->aec_update.led_off_params.la_ratio =
    priv_frame_info->led_off_drc_gains.la_ratio;
  stats_update->aec_update.led_off_params.gamma_ratio =
    priv_frame_info->led_off_drc_gains.gamma_ratio;

  if (stats_update->aec_update.led_off_params.ltm_ratio != -1) { /* ADRC enabled */
    AEC_LOW("UnifiedFlash: Off Gains: real_gain=%f"
      " sensor_gain=%f linecount=%d DRC gains OFF=(%f, %f)",
      stats_update->aec_update.led_off_params.real_gain,
      stats_update->aec_update.led_off_params.sensor_gain,
      stats_update->aec_update.led_off_params.linecnt,
      stats_update->aec_update.led_off_params.total_drc_gain,
      stats_update->aec_update.led_off_params.color_drc_gain);
  }
  /* Update adrc specific fields here end*/

  AEC_LOW("Current Batch no. =%d, frame_capture_mode set to true",
    private->stats_frame_capture.current_batch_count);
  free(aec_msg);

  return rc;
}

/** aec_port_common_set_unified_flash:
 *    @private:   Private data of the port
 *    @cam_capture_frame_config_t: frame_info.
 *
 * This function converts gets the Frame batch info from HAL.
 *
 * Return: void
 **/
void aec_port_common_set_unified_flash(aec_port_private_t  *private,
  cam_capture_frame_config_t *frame_info)
{
  boolean rc = FALSE;
  int i = 0;
  boolean request_data_to_algo = FALSE;

  private->stats_frame_capture.frame_info.num_batch = frame_info->num_batch;
  AEC_LOW("No. of Batch from HAL =%d",
    private->stats_frame_capture.frame_info.num_batch);
  for (i = 0; i < private->stats_frame_capture.frame_info.num_batch; i++) {
    private->stats_frame_capture.frame_info.frame_batch[i].capture_type =
      frame_info->configs[i].type;
    AEC_LOW("frame_batch[%d]: type=%d, aec_auto_mode=%d",
      i, frame_info->configs[i].type, private->aec_auto_mode);

    if (AEC_MANUAL == private->aec_auto_mode) { /* Full manual mode: preview and capture */
      rc = aec_port_unified_fill_manual(
        &private->stats_frame_capture.frame_info.frame_batch[i],
        private,
        frame_info->configs[i].manual_3A_mode,
        frame_info->configs[i].type);
      if (!rc) {
        AEC_ERR("Error, fail to fill manual data");
      }

      private->stats_frame_capture.frame_info.frame_batch[i].flash_mode = FALSE;
      private->stats_frame_capture.frame_info.frame_batch[i].hdr_exp = 0;
      continue; /* End of loop for Full manual mode */
    }

    if (frame_info->configs[i].type == CAM_CAPTURE_BRACKETING) {
      private->stats_frame_capture.frame_info.frame_batch[i].hdr_exp =
       frame_info->configs[i].hdr_mode.values;
    } else if (frame_info->configs[i].type == CAM_CAPTURE_FLASH) {
      AEC_HIGH("CAM_CAPTURE_FLASH: flash_mode=%d",
        frame_info->configs[i].flash_mode);
      aec_port_conv_fromhal_flashmode(frame_info->configs[i].flash_mode,
        &private->stats_frame_capture.frame_info.frame_batch[i].flash_hal);
      if ((frame_info->configs[i].flash_mode == CAM_FLASH_MODE_ON) ||
        (frame_info->configs[i].flash_mode == CAM_FLASH_MODE_TORCH) ||
        (frame_info->configs[i].flash_mode == CAM_FLASH_MODE_AUTO)) {
        private->stats_frame_capture.frame_info.frame_batch[i].flash_mode = TRUE;
      } else{
        private->stats_frame_capture.frame_info.frame_batch[i].flash_mode = FALSE;
      }
      private->stats_frame_capture.frame_info.frame_batch[i].hdr_exp = 0;
    } else if (frame_info->configs[i].type == CAM_CAPTURE_LOW_LIGHT) {
      private->stats_frame_capture.frame_info.frame_batch[i].flash_mode = FALSE;
      private->stats_frame_capture.frame_info.frame_batch[i].hdr_exp = 0;
    } else if (frame_info->configs[i].type == CAM_CAPTURE_MANUAL_3A ||
        (frame_info->configs[i].type == CAM_CAPTURE_RESET &&
        private->aec_auto_mode == AEC_MANUAL)) {
      /* Use manual value only for specific batch */
      rc = aec_port_unified_fill_manual(
        &private->stats_frame_capture.frame_info.frame_batch[i],
        private,
        frame_info->configs[i].manual_3A_mode,
        frame_info->configs[i].type);
      if (!rc) {
        AEC_ERR("Error: fail to fill manual data");
      }
      private->stats_frame_capture.frame_info.frame_batch[i].flash_mode = FALSE;
      private->stats_frame_capture.frame_info.frame_batch[i].hdr_exp = 0;
    }

    /* Verify if we need to call algo for data or not */
    if (frame_info->configs[i].type == CAM_CAPTURE_NORMAL ||
        frame_info->configs[i].type == CAM_CAPTURE_FLASH ||
        frame_info->configs[i].type == CAM_CAPTURE_BRACKETING ||
        frame_info->configs[i].type == CAM_CAPTURE_LOW_LIGHT) {
      /* Query algo only if necesary */
      request_data_to_algo = TRUE;
    }
  }

  if (request_data_to_algo) {
    /* Request batch data */
    rc = aec_port_unifed_request_batch_data_to_algo(private);
    if (FALSE == rc) {
      AEC_ERR("Fail to get batch data from AEC algo");
      memset(&private->stats_frame_capture, 0, sizeof(aec_frame_capture_t));
      return;
    }
  }

  private->stats_frame_capture.frame_capture_mode = FALSE;
  private->stats_frame_capture.streamon_update_done = FALSE;
}

/** aec_port_unified_update_auto_aec_frame_batch:
 *    @aec_update: Destination, AEC update for the batch
 *    @frame_info: Source, AEC frame info from algo
 *    @curr_batch_cnt: batch to update
 *
 * Fill AEC update with current frame batch data
 *
 * Return: void
 **/
void aec_port_unified_update_auto_aec_frame_batch(
  aec_update_t *aec_update, aec_frame_batch_t *frame_info, uint8_t curr_batch_cnt)
{
  aec_update->linecount = frame_info->frame_batch[curr_batch_cnt].line_count;
  aec_update->real_gain = frame_info->frame_batch[curr_batch_cnt].real_gain;
  aec_update->sensor_gain = frame_info->frame_batch[curr_batch_cnt].sensor_gain;
  aec_update->s_real_gain = frame_info->frame_batch[curr_batch_cnt].s_gain;
  aec_update->s_linecount = frame_info->frame_batch[curr_batch_cnt].s_line_count;
  aec_update->l_real_gain = frame_info->frame_batch[curr_batch_cnt].l_gain;
  aec_update->l_linecount = frame_info->frame_batch[curr_batch_cnt].l_line_count;
  aec_update->lux_idx = frame_info->frame_batch[curr_batch_cnt].lux_idx;
  aec_update->gamma_flag = frame_info->frame_batch[curr_batch_cnt].gamma_flag;
  aec_update->nr_flag = frame_info->frame_batch[curr_batch_cnt].nr_flag;
  aec_update->exp_time = frame_info->frame_batch[curr_batch_cnt].exp_time;
  aec_update->exif_iso = frame_info->frame_batch[curr_batch_cnt].iso;

  /* Update adrc specific fields here start*/
  aec_update->total_drc_gain =
    frame_info->frame_batch[curr_batch_cnt].drc_gains.total_drc_gain;
  aec_update->color_drc_gain =
    frame_info->frame_batch[curr_batch_cnt].drc_gains.color_drc_gain;
  aec_update->gtm_ratio =
    frame_info->frame_batch[curr_batch_cnt].drc_gains.gtm_ratio;
  aec_update->ltm_ratio =
    frame_info->frame_batch[curr_batch_cnt].drc_gains.ltm_ratio;
  aec_update->la_ratio =
    frame_info->frame_batch[curr_batch_cnt].drc_gains.la_ratio;
  aec_update->gamma_ratio =
    frame_info->frame_batch[curr_batch_cnt].drc_gains.gamma_ratio;
  /* Update adrc specific fields here end*/

  AEC_LOW("UnifiedFlash: AEC_Update: capture_mode curr_idx[%d]"
    " SensorGain:RealGain:LineCount = %f:%f:%d drc_gains=(%f, %f), "
    "drc_ratios=(%f,%f,%f,%f) lux_idx=%f", curr_batch_cnt,
    aec_update->sensor_gain, aec_update->real_gain, aec_update->s_linecount,
    aec_update->total_drc_gain, aec_update->color_drc_gain, aec_update->gtm_ratio,
    aec_update->ltm_ratio ,aec_update->la_ratio ,aec_update->gamma_ratio,
    aec_update->lux_idx);

  aec_port_conv_tohal_flashmode(&aec_update->flash_hal,
    frame_info->frame_batch[curr_batch_cnt].flash_hal);
}

/** aec_port_unified_update_manual_aec_frame_batch:
 *    @manual_update: Destination, AEC manual update for the batch
 *    @frame_info: Source, AEC manual frame data
 *    @curr_batch_cnt: batch to update
 *
 * Fill AEC update with current frame batch data
 *
 * Return: void
 **/
void aec_port_unified_update_manual_aec_frame_batch (
  aec_manual_update_t *manual_update, aec_frame_batch_t *frame_info, uint8_t curr_batch_cnt)
{
  manual_update->linecount = frame_info->frame_batch[curr_batch_cnt].line_count;
  manual_update->sensor_gain = frame_info->frame_batch[curr_batch_cnt].sensor_gain;
  manual_update->lux_idx = frame_info->frame_batch[curr_batch_cnt].lux_idx;
  manual_update->exif_iso = frame_info->frame_batch[curr_batch_cnt].iso;
}

/** aec_port_update_frame_capture_mode_data:
 *    @port: MCT port and private data of the AEC port
 *    @cur_sof_id: current SOF ID
 *
 * Populate AEC update with frame batch data and send AEC update.
 *
 * Return: TRUE on success
 **/
static boolean aec_port_update_frame_capture_mode_data(
  mct_port_t *port, uint32_t cur_sof_id)
{
  boolean rc = FALSE;
  aec_port_private_t  *private = (aec_port_private_t *)(port->port_private);
  mct_event_module_type_t update_type = MCT_EVENT_MODULE_STATS_AEC_UPDATE;
  aec_manual_update_t manual_update;

  /* Fill current batch data into AEC update */
  uint8_t curr_batch_cnt =
    private->stats_frame_capture.current_batch_count;
  aec_update_t *aec_update =
    &private->frame_capture_update.u.output[0].stats_update.aec_update;
  aec_frame_batch_t *frame_info =
    &private->stats_frame_capture.frame_info;

  /* Handle AEC updates */
  switch (frame_info->frame_batch[curr_batch_cnt].capture_type) {
  case Q3A_CAPTURE_MANUAL_3A: {
    update_type = MCT_EVENT_MODULE_STATS_AEC_MANUAL_UPDATE;
    aec_port_unified_update_manual_aec_frame_batch(&manual_update, frame_info,
      curr_batch_cnt);
  }
    break;
  case Q3A_CAPTURE_RESET: {
    if (private->aec_auto_mode == AEC_MANUAL) {
      /* Manual reset case */
      update_type = MCT_EVENT_MODULE_STATS_AEC_MANUAL_UPDATE;
      aec_port_unified_update_manual_aec_frame_batch(&manual_update, frame_info,
        curr_batch_cnt);
    } else {
      /* Default reset case*/
      update_type = MCT_EVENT_MODULE_STATS_AEC_UPDATE;
      private->frame_capture_update.u.output[0].stats_update.flag =
        STATS_UPDATE_AEC;
      aec_update->sof_id = private->cur_sof_id;
      aec_port_unified_update_auto_aec_frame_batch(aec_update, frame_info,
        curr_batch_cnt);
    }
  }
    break;
  default: { /* All other cases where algorithm was query */
    update_type = MCT_EVENT_MODULE_STATS_AEC_UPDATE;
    private->frame_capture_update.u.output[0].stats_update.flag =
      STATS_UPDATE_AEC;
    aec_update->sof_id = private->cur_sof_id;
    aec_port_unified_update_auto_aec_frame_batch(aec_update, frame_info,
      curr_batch_cnt);
  }
    break;
  }


  /* Handle status/mode flags */
  switch (frame_info->frame_batch[curr_batch_cnt].capture_type) {
  case Q3A_CAPTURE_LOW_LIGHT: {
    /* Handle low_light_capture update */
    aec_update->low_light_capture_update_flag = TRUE;
  }
    break;
  case Q3A_CAPTURE_RESET: {
    private->stats_frame_capture.frame_capture_mode = FALSE;
    private->stats_frame_capture.streamon_update_done = FALSE;
    aec_update->low_light_capture_update_flag = FALSE;
    private->stats_frame_capture.frame_info.num_batch = 0;
  }
    break;
  default: {
    aec_update->low_light_capture_update_flag = FALSE;
  }
    break;
  }

  /* Update modules */
  if (MCT_EVENT_MODULE_STATS_AEC_UPDATE == update_type) {
    aec_port_print_log(&private->frame_capture_update.u.output[0],
      "FC-AEC_UP", private, -1);
    /* Send AEC update event */
    mct_event_t        event;
    event.direction = MCT_EVENT_UPSTREAM;
    event.identity = private->reserved_id;
    event.type = MCT_EVENT_MODULE_EVENT;
    event.u.module_event.current_frame_id = cur_sof_id;
    event.u.module_event.type = update_type;
    event.u.module_event.module_event_data =
      (void *)(&private->frame_capture_update.u.output[0].stats_update);
    MCT_PORT_EVENT_FUNC(port)(port, &event);
    rc = TRUE;
  } else if (MCT_EVENT_MODULE_STATS_AEC_MANUAL_UPDATE == update_type) {
    /* Send manual update */
    private->aec_update_flag = TRUE;
    aec_port_print_manual(private, "FC", &manual_update);
    aec_port_send_event(port, MCT_EVENT_MODULE_EVENT,
      update_type,
      (void *)(&manual_update), cur_sof_id);
    rc = TRUE;
  } else {
    AEC_ERR("Fail: invalid update type");
    rc = FALSE;
  }

  if (TRUE == rc) {
    /* UPDATE: exif and metadata */
    aec_port_pack_exif_info(port, &private->frame_capture_update.u.output[0]);
    if (private->frame_capture_update.u.output[0].eztune.running) {
      aec_port_send_aec_info_to_metadata(port,
        &private->frame_capture_update.u.output[0]);
    }
    /* Print all bus messages info */
    aec_port_print_bus("FC:INFO", private);
    aec_send_bus_message(port, MCT_BUS_MSG_AE_INFO,
      &private->aec_info, sizeof(cam_3a_params_t), private->cur_sof_id);
  }

  return rc;
}

/** aec_port_is_adrc_supported
 *    @private:   Private data of the port
 *
 * Return: ADRC Feature Enable/Disable
 **/
static boolean aec_port_is_adrc_supported(aec_port_private_t  *private)
{
  if(private->adrc_settings.adrc_force_disable != TRUE
    && private->adrc_settings.effect_mode == CAM_EFFECT_MODE_OFF
    && (private->adrc_settings.bestshot_mode == CAM_SCENE_MODE_OFF
    || private->adrc_settings.bestshot_mode == CAM_SCENE_MODE_FACE_PRIORITY))
    return TRUE;

  return FALSE;
}

/** aec_port_set_adrc_enable
 *    @private:   Private data of the port
 *
 * Return: TRUE if no error
 **/
static boolean aec_port_set_adrc_enable(aec_port_private_t  *private)
{
  q3a_core_result_type rc = TRUE;
  boolean adrc_supported = aec_port_is_adrc_supported(private);

  if(adrc_supported != private->adrc_settings.is_adrc_feature_supported) {
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_ADRC_ENABLE);
    if (aec_msg != NULL) {
      private->adrc_settings.is_adrc_feature_supported = adrc_supported;
      aec_msg->u.aec_set_parm.u.adrc_enable = adrc_supported;
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
  }

  return rc;
}

/** aec_port_link_to_peer:
 *    @port: private AEC port data
 *    @event: module event received
 *
 * Links to peer AEC Port
 *
 * Return boolean
 **/
static boolean aec_port_link_to_peer(mct_port_t *port,
  mct_event_t *event) {
  boolean                                rc = FALSE;
  mct_port_t                            *this_port = port;
  mct_port_t                            *peer_port = NULL;
  cam_sync_related_sensors_event_info_t *link_param = NULL;
  uint32_t                               peer_identity = 0;
  aec_port_private_t  *private = (aec_port_private_t *)(port->port_private);

  link_param = (cam_sync_related_sensors_event_info_t *)
    (event->u.ctrl_event.control_event_data);
  peer_identity = link_param->related_sensor_session_id;

  AEC_LOW("AEC got MCT_EVENT_CONTROL_LINK_INTRA_SESSION to session %x", peer_identity);

  rc = stats_util_get_peer_port(event, peer_identity,this_port,
    &peer_port);

  if (rc == FALSE) {
    AEC_ERR("FAIL to Get Peer Port");
  } else {
    private->intra_peer_id = peer_identity;
    private->dual_cam_sensor_info = link_param->type;
    MCT_PORT_INTRALINKFUNC(this_port)(peer_identity, this_port, peer_port);
  }
  return rc;
}


/** aec_port_proc_downstream_ctrl:
 *    @port:   TODO
 *    @eventl: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_proc_downstream_ctrl(mct_port_t *port,
  mct_event_t *event)
{
  boolean             rc = TRUE;
  aec_port_private_t  *private = (aec_port_private_t *)(port->port_private);
  mct_event_control_t *mod_ctrl = &(event->u.ctrl_event);

  AEC_LOW("type =%d", event->u.ctrl_event.type);

  /* check if there's need for extended handling. */
  if (private->func_tbl.ext_handle_control_event) {
    stats_ext_return_type ret;
    AEC_LOW("Handle extended control event!");
    ret = private->func_tbl.ext_handle_control_event(port, mod_ctrl);
    /* Check if this event has been completely handled. If not we'll process it further here. */
    if (STATS_EXT_HANDLING_COMPLETE == ret) {
      AEC_LOW("Control event %d handled by extended functionality!",
        mod_ctrl->type);
      return rc;
    }
  }

  switch (mod_ctrl->type) {

  case MCT_EVENT_CONTROL_SOF: {
    mct_bus_msg_isp_sof_t *sof_event;
       sof_event =(mct_bus_msg_isp_sof_t *)(event->u.ctrl_event.control_event_data);

    uint32_t cur_stats_id = 0;
    uint32_t cur_sof_id = 0;
    uint32_t cur_aec_out_frame_id = 0;
    uint8_t sof_output_index = 0;
    boolean is_valid_index = FALSE;

    MCT_OBJECT_LOCK(port);
    cur_sof_id = private->cur_sof_id = sof_event->frame_id;
    cur_stats_id = private->cur_stats_id;
    sof_output_index = private->state_update.sof_output_index;
    is_valid_index = (sof_output_index != 0xFF);
    sof_output_index %= AEC_OUTPUT_ARRAY_MAX_SIZE;

    /* Start doing AEC update processing, only after first CB hit */
    if (is_valid_index) {
      cur_aec_out_frame_id =
        private->state_update.u.output[sof_output_index].stats_update.aec_update.frame_id;

      AEC_LOW("AEC_UPDATE_DBG: IN SOF: Curr: SOF_ID:%d Stats_ID:%d"
        " OutputFrameId:%d sof_output_idex:%d",
        cur_sof_id, cur_stats_id, cur_aec_out_frame_id, sof_output_index);
    }

    MCT_OBJECT_UNLOCK(port);

    if (private->stats_frame_capture.frame_capture_mode == TRUE ||
        private->stats_frame_capture.streamon_update_done == TRUE) {
      rc = aec_port_update_frame_capture_mode_data(port, cur_sof_id);
      if (FALSE == rc) {
        AEC_ERR("Fail to update frame_capture_mode data");
        break;
      }
    } else if (private->aec_auto_mode == AEC_MANUAL) {
      /* If HAL3 manual mode is set, then send immediately and done enqueue sof msg
       * On HAL1 preview may also be required to be updated with manual AEC values */
      AEC_LOW("SOF: %d: AEC_MANUAL", cur_sof_id);
      aec_manual_update_t manual_update;
      rc = aec_port_update_manual_setting(private, &manual_update);
      if(rc) {
        private->aec_update_flag = TRUE;
        aec_port_print_manual(private, "SOF", &manual_update);
        aec_port_send_event(port, MCT_EVENT_MODULE_EVENT,
          MCT_EVENT_MODULE_STATS_AEC_MANUAL_UPDATE,
          (void *)(&manual_update), sof_event->frame_id);
      }
      q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
        AEC_SET_MANUAL_AUTO_SKIP);
      if (aec_msg != NULL)
        rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
     } else {
       /* Reset manual valid flag if mode is auto*/
       if (private->aec_auto_mode == AEC_AUTO) {
         private->manual.is_exp_time_valid = FALSE;
         private->manual.is_gain_valid = FALSE;
       }

       if (private->still.is_capture_intent) {
         private->still.capture_sof = private->cur_sof_id;
       }

       /* If there is capture intent for current SOF, just send it
        * out using saved snapshot gain */
       if(private->still.is_capture_intent &&
         private->state_update.type == AEC_PORT_STATE_ALGO_UPDATE) {
         if (is_valid_index) {
           private->aec_update_flag = TRUE;
           aec_port_pack_update(port, &private->state_update.u.output[sof_output_index],
             sof_output_index);
           aec_port_send_aec_update(port, private, "SOF_CI: ", sof_output_index, cur_sof_id-1);
         } else {
           AEC_LOW("AEC_UPDATE_DBG: SOF_UPDATE_C: AEC Update not yet available for SOF_ID:%d"
            " Curr_SOF_ID=%d Curr_Stats_ID:%d Output index=%d",
            cur_sof_id-1, cur_sof_id, cur_stats_id, sof_output_index);
         }
       } else {
         /* Two use cases to send AEC_UPDATE event:
                1. In MCT thread context (in SOF context itself):
                    If AEC update is available for last frame, send right away in SOF context itself
                2. In AECAWB thread context:
                    If AEC update is NOT available, do nothing here in SOF, wait for CB to happen and send
                    update in AEC thread context
              */

         /* Start doing AEC update processing, only after first CB hit */
         if ((is_valid_index) && cur_sof_id > 0) {
           if((cur_sof_id-1 == cur_aec_out_frame_id) ||
               aec_port_using_HDR_divert_stats(private)) {
             aec_port_send_aec_update(port, private, "SOF: ", sof_output_index, cur_sof_id-1);
           } else {
             AEC_LOW("AEC_UPDATE_DBG: SOF: AEC Update not yet available for SOF_ID:%d"
               " Curr_SOF_ID=%d Curr_Stats_ID:%d Output index=%d",
               cur_sof_id-1, cur_sof_id, cur_stats_id, sof_output_index);
           }
         }
       }
    }/*else*/

    /* Send exif info update from SoF */
    aec_port_send_exif_debug_data(port);
    aec_port_update_aec_state(private, private->state_update);
    aec_send_batch_bus_message(port, STATS_REPORT_IMMEDIATE, sof_event->frame_id);
  }
    break;

  case MCT_EVENT_CONTROL_PREPARE_SNAPSHOT: {
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_PREPARE_FOR_SNAPSHOT);
    if (aec_msg != NULL) {
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
      /* Starting precapture */
      private->aec_precap_start = TRUE;
      aec_port_print_bus("PRE_SNAP:STATE", private);
      aec_send_bus_message(port, MCT_BUS_MSG_SET_AEC_STATE, &private->aec_state,
        sizeof(cam_ae_state_t), private->cur_sof_id );
    }
  } /* MCT_EVENT_CONTROL_PREPARE_SNAPSHOT */
    break;

  case MCT_EVENT_CONTROL_STOP_ZSL_SNAPSHOT: {
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_LED_RESET);
    if (aec_msg != NULL) {
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
    memset(&private->aec_get_data, 0, sizeof(private->aec_get_data));

    /* Unlock AEC update after the ZSL snapshot */
    private->in_zsl_capture = FALSE;
    private->stats_frame_capture.frame_capture_mode = FALSE;
    private->stats_frame_capture.streamon_update_done = FALSE;
    private->frame_capture_update.u.output[0].stats_update.aec_update.flash_hal =
     CAM_FLASH_MODE_OFF;
    private->frame_capture_update.u.output[1].stats_update.aec_update.flash_hal =
     CAM_FLASH_MODE_OFF;
  } /* MCT_EVENT_CONTROL_STOP_ZSL_SNAPSHOT */
    break;

  case MCT_EVENT_CONTROL_STREAMON: {
    /* Custom optimization, provide manual exposure update */
    if (Q3A_CAPTURE_MANUAL_3A ==
      private->stats_frame_capture.frame_info.frame_batch[0].capture_type) {
      uint8_t curr_batch_cnt = 0;
      aec_update_t *aec_update =
        &private->frame_capture_update.u.output[0].stats_update.aec_update;
      aec_frame_batch_t *frame_info =
        &private->stats_frame_capture.frame_info;
      aec_manual_update_t manual_update;
      private->aec_update_flag = TRUE;
      manual_update.linecount = frame_info->frame_batch[curr_batch_cnt].line_count;
      manual_update.sensor_gain = frame_info->frame_batch[curr_batch_cnt].sensor_gain;
      manual_update.lux_idx = frame_info->frame_batch[curr_batch_cnt].lux_idx;
      manual_update.exif_iso = frame_info->frame_batch[curr_batch_cnt].iso;
      AEC_HIGH("AEUPD: STREAMON-MAN_AEC_UP: SOF ID=%d G=%f, lc=%u, lux_idx %f, exif_iso: %u",
        private->cur_sof_id, manual_update.sensor_gain, manual_update.linecount, manual_update.lux_idx,
        manual_update.exif_iso);
      private->stats_frame_capture.streamon_update_done = TRUE;
      aec_port_send_event(port, MCT_EVENT_MODULE_EVENT,
        MCT_EVENT_MODULE_STATS_AEC_MANUAL_UPDATE,
        (void *)(&manual_update), 0);
    }
  }
    break;

  case MCT_EVENT_CONTROL_STREAMOFF: {
    q3a_thread_aecawb_msg_t *aec_msg;

    mct_stream_info_t *stream_info =
      (mct_stream_info_t*)event->u.ctrl_event.control_event_data;

    aec_port_print_bus("STMOFF:STATE", private);
    aec_send_bus_message(port, MCT_BUS_MSG_SET_AEC_STATE,
      &private->aec_state, sizeof(cam_ae_state_t), private->cur_sof_id );
    aec_msg = aec_port_malloc_msg(MSG_AEC_SET, AEC_SET_PARAM_RESET_STREAM_INFO);
    if (aec_msg != NULL ) {
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
    memset(&private->aec_get_data, 0, sizeof(private->aec_get_data));
    if (private->aec_on_off_mode == FALSE) {
      /* Reset only for HAL3: on stream-off reset manual params */
      private->aec_on_off_mode = TRUE; /* This variable is not used by HAL1 */
      private->aec_auto_mode = AEC_AUTO;
    }
    private->stats_frame_capture.frame_capture_mode = FALSE;
    private->stats_frame_capture.streamon_update_done = FALSE;
    private->frame_capture_update.u.output[0].stats_update.aec_update.flash_hal =
     CAM_FLASH_MODE_OFF;
    private->frame_capture_update.u.output[1].stats_update.aec_update.flash_hal =
     CAM_FLASH_MODE_OFF;

    /* Reset flash exposure settings for RAW and normal non-zsl snapshot.
     * For ZSL, its handled in MCT_EVENT_CONTROL_STOP_ZSL_SNAPSHOT */
    if (stream_info && (stream_info->stream_type == CAM_STREAM_TYPE_RAW ||
        stream_info->stream_type == CAM_STREAM_TYPE_SNAPSHOT)) {
      aec_port_reset_output_index(private);
      q3a_thread_aecawb_msg_t *aec_msg_led_reset = aec_port_malloc_msg(MSG_AEC_SET,
        AEC_SET_PARAM_LED_RESET);
      if (aec_msg_led_reset != NULL) {
        rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg_led_reset);
      }
      AEC_LOW("StreamOff for RAW StreamType Reset LED");
    }
  } /* MCT_EVENT_CONTROL_STREAMOFF */
    break;

  case MCT_EVENT_CONTROL_SET_PARM: {
    /* TODO: some logic shall be handled by stats and q3a port to achieve that,
     * we need to add the function to find the desired sub port;
     * however since it is not in place, for now, handle it here
     **/
    stats_set_params_type *stat_parm =
      (stats_set_params_type *)mod_ctrl->control_event_data;
    if (stat_parm->param_type == STATS_SET_Q3A_PARAM) {
      q3a_set_params_type *q3a_param = &(stat_parm->u.q3a_param);
      if (q3a_param->type == Q3A_SET_AEC_PARAM) {
        q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
          q3a_param->u.aec_param.type);
        if (aec_msg != NULL ) {
          aec_msg->u.aec_set_parm = q3a_param->u.aec_param;
          /* for some events we need to peak here */
          switch(q3a_param->u.aec_param.type) {
          case AEC_SET_PARAM_LOCK: {
            if (private->locked_from_hal == q3a_param->u.aec_param.u.aec_lock) {
              AEC_LOW("AEC_SET_PARAM_LOCK: %d, same hal value, do not update",
                 private->locked_from_hal);
              rc = FALSE;
            } else {
              private->locked_from_hal = q3a_param->u.aec_param.u.aec_lock;
              AEC_LOW("AEC_SET_PARAM_LOCK: %d new value, update algo",
                 private->locked_from_hal);
            }
          }
            break;

          case AEC_SET_PARAM_PREPARE_FOR_SNAPSHOT: {
            private->aec_trigger.trigger =
              q3a_param->u.aec_param.u.aec_trigger.trigger;
            private->aec_trigger.trigger_id =
              q3a_param->u.aec_param.u.aec_trigger.trigger_id;
            if (q3a_param->u.aec_param.u.aec_trigger.trigger ==
              AEC_PRECAPTURE_TRIGGER_START) {
              AEC_LOW(" SET Prepare SNAPSHOT");
              private->aec_trigger.trigger_id =
                q3a_param->u.aec_param.u.aec_trigger.trigger_id;
              private->aec_precap_start = TRUE;
            if (private->aec_precap_for_af) {
                /* Ignore precapture sequence in the AEC algo since it is
                 * already running for the AF estimation */
                AEC_LOW("Ignoring precapture trigger in the algo!!!");
	        q3a_param->u.aec_param.u.aec_trigger.trigger =
                  AEC_PRECAPTURE_TRIGGER_IDLE;
              }
            } else if (q3a_param->u.aec_param.u.aec_trigger.trigger ==
              AEC_PRECAPTURE_TRIGGER_CANCEL) {
              AEC_LOW("CAM_INTF_META_AEC_PRECAPTURE_TRIGGER CANCEL");
              q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
                AEC_SET_PARAM_LED_RESET);
              if (aec_msg != NULL) {
                rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
              }
            }
          }
            break;

          case AEC_SET_PARAM_PREP_FOR_SNAPSHOT_NOTIFY: {
            private->aec_trigger.trigger =
              q3a_param->u.aec_param.u.aec_trigger.trigger;
            if(q3a_param->u.aec_param.u.aec_trigger.trigger ==
              AEC_PRECAPTURE_TRIGGER_START) {
              private->aec_trigger.trigger_id =
                q3a_param->u.aec_param.u.aec_trigger.trigger_id;

              AEC_LOW(" Ignoring precapture trigger (notify) in the algo!!!");
            }
          }
            break;

          case AEC_SET_PARAM_PRECAPTURE_START: {
            private->aec_precap_start = TRUE;

            /* we got a precapture trigger right after LED AF, the AEC preflash sequence won't run
             * again in this case per HAL3 spec, we need to send out the CAM_AE_STATE_PRECAPTURE
             * state to HAL on receiving AEC_TRIGGER, here we set the aec_precap_start flag to true
             * and also aec_reset_precap_start_flag to true, in this way, we could reset the
             * aec_precap_start after we send out the CAM_AE_STATE_PRECAPTURE, so the state machine
             * could move the state to converged otherwise, the state will be stuck at
             * CAM_AE_STATE_PRECAPTURE due to aec_precap_start flag is true and preflash is not
             * running
             *
             * if est_state ==  AEC_EST_START means we are currently undergoing pre-flash, the
             * aec_precap_start will be reset upon pre-flash done.
             *
             */
            if (private->est_state != AEC_EST_START) {
              private->aec_reset_precap_start_flag = true;
            }
            AEC_LOW("EC_SET_PARAM_PRECAPTURE_START");
          }
            break;

          case AEC_SET_PARAM_MANUAL_EXP_TIME: { /* HAL3 */
            /*convert nano sec to sec*/
            private->manual.exp_time = (float)q3a_param->u.aec_param.u.manual_expTime/1000000000;
            private->manual.is_exp_time_valid = TRUE;
          }
          break;
          /* HAL1 manual AEC: set exposure time */
          case AEC_SET_PARAM_EXP_TIME: { /* HAL1 */
            private->manual.exp_time =
              (float)q3a_param->u.aec_param.u.manual_exposure_time.value/1000000000;
            if (q3a_param->u.aec_param.u.manual_exposure_time.value !=
                AEC_MANUAL_EXPOSURE_TIME_AUTO) {
              private->manual.is_exp_time_valid = TRUE;
              private->manual.exp_time_on_preview =
                q3a_param->u.aec_param.u.manual_exposure_time.previewOnly;
              if (private->manual.exp_time_on_preview &&
                  private->manual.gain_on_preview) {
                private->aec_auto_mode = AEC_MANUAL;
              } else {
                private->aec_auto_mode = AEC_PARTIAL_AUTO;
              }
            } else {
              if (!private->manual.is_gain_valid) {
                private->aec_auto_mode = AEC_AUTO;
              }
              private->manual.is_exp_time_valid = FALSE;
              private->manual.exp_time_on_preview = FALSE;
            }
          }
          break;

          /*HAL1 manual AEC: set iso mode / continuous iso */
          case AEC_SET_PARAM_ISO_MODE: {
            if (q3a_param->u.aec_param.u.iso.value != AEC_ISO_AUTO &&
                q3a_param->u.aec_param.u.iso.value != AEC_ISO_DEBLUR) {
              private->aec_auto_mode = AEC_PARTIAL_AUTO;
              private->manual.gain = private->aec_object.iso_to_real_gain
                (private->aec_object.aec, (uint32_t)q3a_param->u.aec_param.u.iso.value);
              if (private->manual.gain) {
                private->manual.is_gain_valid = TRUE;
                private->manual.gain_on_preview =
                  q3a_param->u.aec_param.u.iso.previewOnly;
                if (private->manual.exp_time_on_preview &&
                  private->manual.gain_on_preview) {
                  private->aec_auto_mode = AEC_MANUAL;
                } else {
                  private->aec_auto_mode = AEC_PARTIAL_AUTO;
                }
              } else {
                private->manual.is_gain_valid = FALSE;
                private->manual.gain_on_preview = FALSE;
                AEC_HIGH("Error getting ISO to real gain");
              }
            } else {
              /* Setting back to auto iso */
              private->manual.is_gain_valid = FALSE;
              private->manual.gain_on_preview = FALSE;
              if (!private->manual.is_exp_time_valid) {
                private->aec_auto_mode = AEC_AUTO;
              }
            }
          }
          break;
          case AEC_SET_PARAM_MANUAL_GAIN: {
            /* Convert from ISO to real gain*/
            private->manual.gain = (float)q3a_param->u.aec_param.u.manual_gain * private->ISO100_gain / 100.0;
            if (private->manual.gain < 1) {
              AEC_LOW("AEC_SET_PARAM_MANUAL_GAIN: ISO lower that expected: %d, using min gain",
                 q3a_param->u.aec_param.u.manual_gain);
              private->manual.gain = 1.0;
            }
            private->manual.is_gain_valid = TRUE;
          }
          break;
          case AEC_SET_PARAM_ON_OFF: {
            private->aec_on_off_mode = q3a_param->u.aec_param.u.enable_aec;
            aec_port_set_aec_mode(private);
          }
           break;
         case AEC_SET_PARAM_EXP_COMPENSATION:
           private->exp_comp = q3a_param->u.aec_param.u.exp_comp;
           break;
         case AEC_SET_PARAM_LED_MODE:
           private->led_mode = q3a_param->u.aec_param.u.led_mode;
           break;
         case AEC_SET_PARAM_FPS:
           private->fps = q3a_param->u.aec_param.u.fps;
           if (private->fps.min_fps == private->fps.max_fps)
           {
             private->apply_fixed_fps_adjustment = TRUE;
           }
           break;
         case AEC_SET_PARAM_SENSOR_ROI:
           aec_port_update_roi(private, q3a_param->u.aec_param.u.aec_roi);
           break;
         case AEC_SET_PARM_FAST_AEC_DATA:
           private->fast_aec_data = q3a_param->u.aec_param.u.fast_aec_data;
           break;
         case AEC_SET_PARAM_EFFECT:
           private->adrc_settings.effect_mode=
             q3a_param->u.aec_param.u.effect_mode;
           rc = aec_port_set_adrc_enable(private);
           break;
         case AEC_SET_PARAM_ADRC_FEATURE_DISABLE_FROM_APP:
           private->adrc_settings.adrc_force_disable =
             q3a_param->u.aec_param.u.adrc_enable?FALSE:TRUE;
           rc = aec_port_set_adrc_enable(private);
           break;
         default: {
          }
           break;
          }
          if (rc) {
            rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
          } else {
            AEC_LOW("MCT_EVENT_CONTROL_SET_PARM: Skiped %d",
               q3a_param->u.aec_param.type);
            free(aec_msg);
            rc = TRUE;
          }
        }
      } else if (q3a_param->type == Q3A_ALL_SET_PARAM) {
        switch (q3a_param->u.q3a_all_param.type) {
        case Q3A_ALL_SET_EZTUNE_RUNNIG: {
          q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
            AEC_SET_PARAM_EZ_TUNE_RUNNING);
          if (aec_msg != NULL ) {
            aec_msg->u.aec_set_parm.type = AEC_SET_PARAM_EZ_TUNE_RUNNING;
            aec_msg->u.aec_set_parm.u.ez_running =
              q3a_param->u.q3a_all_param.u.ez_runnig;
            rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
          }
        }
          break;

        case Q3A_ALL_SET_DO_LED_EST_FOR_AF: {
          q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
            AEC_SET_PARAM_DO_LED_EST_FOR_AF);
          if (aec_msg == NULL ) {
            break;
          }
          aec_msg->u.aec_set_parm.u.est_for_af = q3a_param->u.q3a_all_param.u.est_for_af;
          rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
          if (rc) {
            private->aec_precap_for_af = q3a_param->u.q3a_all_param.u.est_for_af;
          }
        }
          break;

        case Q3A_ALL_SET_DUAL_LED_CALIB_MODE: {
          q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
            AEC_SET_PARM_DUAL_LED_CALIB_MODE);
          if (aec_msg != NULL ) {
            aec_msg->u.aec_set_parm.type = AEC_SET_PARM_DUAL_LED_CALIB_MODE;
            aec_msg->u.aec_set_parm.u.dual_led_calib_mode =
              q3a_param->u.q3a_all_param.u.dual_led_calib_mode;
            rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
          }
        }
          break;

        default: {
        }
          break;
        }
      }
    }
    /* If it's common params shared by many modules */
    else if (stat_parm->param_type == STATS_SET_COMMON_PARAM) {
      stats_common_set_parameter_t *common_param = &(stat_parm->u.common_param);
      if (common_param->type == COMMON_SET_PARAM_BESTSHOT ||
        common_param->type == COMMON_SET_PARAM_VIDEO_HDR ||
        common_param->type == COMMON_SET_PARAM_SNAPSHOT_HDR ||
        common_param->type == COMMON_SET_PARAM_STATS_DEBUG_MASK ||
        common_param->type == COMMON_SET_PARAM_STREAM_ON_OFF ||
        common_param->type == COMMON_SET_PARAM_INSTANT_AEC_DATA) {
        q3a_thread_aecawb_msg_t *aec_msg = NULL;
        AEC_HIGH("AEDBG,Commn,type=%d",common_param->type);
        switch(common_param->type) {
        case COMMON_SET_PARAM_BESTSHOT: {
          aec_msg = aec_port_malloc_msg(MSG_AEC_SET,AEC_SET_PARAM_BESTSHOT);
          if (aec_msg != NULL ) {
            aec_port_set_bestshot_mode(&aec_msg->u.aec_set_parm.u.bestshot_mode,
              common_param->u.bestshot_mode);
            private->adrc_settings.bestshot_mode =
              common_param->u.bestshot_mode;
            rc = aec_port_set_adrc_enable(private);
          }
        }
          break;

        case COMMON_SET_PARAM_VIDEO_HDR: {
          aec_msg = aec_port_malloc_msg(MSG_AEC_SET,AEC_SET_PARAM_VIDEO_HDR);
          if (aec_msg != NULL ) {
            aec_msg->u.aec_set_parm.u.video_hdr = common_param->u.video_hdr;
            private->video_hdr = common_param->u.video_hdr;
          }
        }
          break;

        case COMMON_SET_PARAM_SNAPSHOT_HDR: {
          aec_msg = aec_port_malloc_msg(MSG_AEC_SET,AEC_SET_PARAM_SNAPSHOT_HDR);
          if (aec_msg != NULL ) {
            aec_snapshot_hdr_type snapshot_hdr;
            if (common_param->u.snapshot_hdr == CAM_SENSOR_HDR_IN_SENSOR)
              snapshot_hdr = AEC_SENSOR_HDR_IN_SENSOR;
            else if (common_param->u.snapshot_hdr == CAM_SENSOR_HDR_ZIGZAG)
              snapshot_hdr = AEC_SENSOR_HDR_DRC;
            else
              snapshot_hdr = AEC_SENSOR_HDR_OFF;

            aec_msg->u.aec_set_parm.u.snapshot_hdr = snapshot_hdr;
            private->snapshot_hdr = common_param->u.snapshot_hdr;
            }
          }
            break;

        case COMMON_SET_PARAM_STATS_DEBUG_MASK: {
          //do nothing.
        }
          break;
        case COMMON_SET_PARAM_STREAM_ON_OFF: {
          AEC_LOW("Stream_On_Off: stream_on=%d fast_aec_data.enable=%d",
            common_param->u.stream_on, private->fast_aec_data.enable);
          if (!common_param->u.stream_on && !private->fast_aec_data.enable) {
            aec_port_reset_output_index(private);
          }
        }
          break;
        case COMMON_SET_PARAM_INSTANT_AEC_DATA: {
          aec_msg = aec_port_malloc_msg(MSG_AEC_SET, AEC_SET_PARM_INSTANT_AEC_DATA);
          if (aec_msg != NULL ) {
            aec_msg->u.aec_set_parm.u.instant_aec_type = common_param->u.instant_aec_type;
            private->instant_aec_type = common_param->u.instant_aec_type;
          }
        }
          break;
        default: {
        }
          break;
        }

        if (aec_msg != NULL ) {
          rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
        }
      } else if (common_param->type == COMMON_SET_PARAM_META_MODE) {
          q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
            AEC_SET_PARAM_CTRL_MODE);
          if (aec_msg == NULL) {
             break;
          }
          AEC_HIGH("AEDBG,META_MODE=%d",private->aec_meta_mode);
          aec_msg->u.aec_set_parm.u.aec_ctrl_mode = common_param->u.meta_mode;
          private->aec_meta_mode = common_param->u.meta_mode;
          aec_port_set_aec_mode(private);
          rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
      } else if(common_param->type == COMMON_SET_CAPTURE_INTENT) {
        if (common_param->u.capture_type == CAM_INTENT_STILL_CAPTURE) {
          AEC_HIGH("AEDBG, CAPTURE_INTENT: sof_id: %d, stats: %d, aec_state: %d",
            private->cur_sof_id, private->cur_stats_id, private->aec_state);

          private->still.is_capture_intent = TRUE;
          if (private->still.is_flash_snap_data) {
            /* Skip stats under flash exposure */
            aec_set_skip_stats(private, private->cur_sof_id, STATS_FLASH_DELAY +
              STATS_FLASH_ON);
            /* Reset to algo to LED OFF */
            q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
              AEC_SET_PARAM_LED_RESET);
            if (aec_msg != NULL) {
              rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
            }
            /* Reset precapture trigger ID*/
            private->aec_trigger.trigger = 0;
            private->aec_trigger.trigger_id = 0;
          }
        }
      } else if (common_param->type == COMMON_SET_PARAM_UNIFIED_FLASH) {
        cam_capture_frame_config_t *frame_info =
         (cam_capture_frame_config_t *)&common_param->u.frame_info;
        if (frame_info->num_batch != 0 &&
            private->stats_frame_capture.frame_capture_mode) {
          AEC_LOW("frame_capture in progress, don't process %d, num_batch %d",
            private->stats_frame_capture.frame_capture_mode, frame_info->num_batch);
          break;
        }
        AEC_HIGH("AEDBG: UNIFIED_FLASH");
        memset(&private->stats_frame_capture.frame_info, 0, sizeof(aec_frame_batch_t));
        aec_port_common_set_unified_flash(private, frame_info);
      } else if (common_param->type == COMMON_SET_PARAM_LONGSHOT_MODE) {
        q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
          AEC_SET_PARAM_LONGSHOT_MODE);
        if (aec_msg == NULL) {
          break;
        }
        private->in_longshot_mode = common_param->u.longshot_mode;
        aec_msg->u.aec_set_parm.u.longshot_mode = private->in_longshot_mode;
        AEC_HIGH("AEDBG: longshot_mode: %d", private->in_longshot_mode);
        if (aec_msg != NULL) {
          rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
        }
      }
    }
  }
  break;

  case MCT_EVENT_CONTROL_START_ZSL_SNAPSHOT: {
    /* Lock AEC update only during the ZSL snapshot */
    private->in_zsl_capture = TRUE;

    memset(&private->aec_get_data, 0, sizeof(private->aec_get_data));
    /*TO do: moving the get exposure handling to here */
  }
    break;
 case MCT_EVENT_CONTROL_SET_SUPER_PARM: {
   private->super_param_id = event->u.ctrl_event.current_frame_id;
  }
    break;

  case MCT_EVENT_CONTROL_LINK_INTRA_SESSION: {
    aec_port_link_to_peer(port,event);
  }
    break;

  case MCT_EVENT_CONTROL_UNLINK_INTRA_SESSION: {
    private->intra_peer_id = 0;
    private->dual_cam_sensor_info = CAM_TYPE_MAIN;
    AEC_LOW("AEC got intra unlink Command");
  }
    break;

  default: {
  }
    break;
  }
  AEC_LOW("X rc = %d", rc);

  return rc;
}

/** aec_port_handle_asd_update:
 *    @thread_data: TODO
 *    @mod_evt:     TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_handle_asd_update(q3a_thread_data_t *thread_data,
  mct_event_module_t *mod_evt)
{
  aec_set_asd_param_t *asd_parm;
  stats_update_t      *stats_event;

  stats_event = (stats_update_t *)(mod_evt->module_event_data);

  AEC_LOW("Handle ASD update!");
  q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
    AEC_SET_PARAM_ASD_PARM);
  if (aec_msg == NULL) {
    return;
  }
  asd_parm = &(aec_msg->u.aec_set_parm.u.asd_param);
  asd_parm->backlight_detected = stats_event->asd_update.backlight_detected;
  asd_parm->backlight_luma_target_offset =
    stats_event->asd_update.backlight_luma_target_offset;
  asd_parm->snow_or_cloudy_scene_detected =
    stats_event->asd_update.snow_or_cloudy_scene_detected;
  asd_parm->snow_or_cloudy_luma_target_offset =
    stats_event->asd_update.snow_or_cloudy_luma_target_offset;
  asd_parm->landscape_severity = stats_event->asd_update.landscape_severity;
  asd_parm->soft_focus_dgr = stats_event->asd_update.asd_soft_focus_dgr;
  asd_parm->enable = stats_event->asd_update.asd_enable;
  AEC_LOW("backling_detected: %d offset: %d snow_detected: %d offset: %d"
    "landscape_severity: %d soft_focus_dgr: %f",
    asd_parm->backlight_detected, asd_parm->backlight_luma_target_offset,
    asd_parm->snow_or_cloudy_scene_detected,
    asd_parm->snow_or_cloudy_luma_target_offset,
    asd_parm->landscape_severity, asd_parm->soft_focus_dgr);
  q3a_aecawb_thread_en_q_msg(thread_data, aec_msg);
}

/** aec_port_handle_afd_update:
 *    @thread_data: TODO
 *    @mod_evt:     TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_handle_afd_update(q3a_thread_data_t *thread_data,
  mct_event_module_t *mod_evt)
{
  aec_set_afd_parm_t *aec_afd_parm;
  stats_update_t     *stats_event;

  stats_event = (stats_update_t *)(mod_evt->module_event_data);
  q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
    AEC_SET_PARAM_AFD_PARM);
  if (aec_msg == NULL) {
    return;
  }

  aec_afd_parm = &(aec_msg->u.aec_set_parm.u.afd_param);
  aec_msg->is_priority = TRUE;
  aec_afd_parm->afd_enable = stats_event->afd_update.afd_enable;


  switch (stats_event->afd_update.afd_atb) {
  case AFD_TBL_OFF:
    aec_afd_parm->afd_atb =  STATS_PROC_ANTIBANDING_OFF;
    break;

  case AFD_TBL_60HZ:
    aec_afd_parm->afd_atb = STATS_PROC_ANTIBANDING_60HZ;
    break;

  case AFD_TBL_50HZ:
    aec_afd_parm->afd_atb = STATS_PROC_ANTIBANDING_50HZ;
    break;

  default:
    aec_afd_parm->afd_atb =  STATS_PROC_ANTIBANDING_OFF;
    break;
  }
  q3a_aecawb_thread_en_q_msg(thread_data, aec_msg);
}

/** aec_port_proc_get_aec_data:
 *    @port:           TODO
 *    @stats_get_data: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_proc_get_aec_data(mct_port_t *port,
  stats_get_data_t *stats_get_data)
{
  boolean            rc = FALSE;
  aec_output_data_t  output;
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  stats_custom_update_t *custom_data = (stats_custom_update_t *)
    &stats_get_data->aec_get.aec_get_custom_data;

  memset(&output, 0, sizeof(aec_output_data_t));
  if (private->aec_get_data.valid_entries) {
    stats_get_data->aec_get = private->aec_get_data;
    stats_get_data->flag = STATS_UPDATE_AEC;
  } else {
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_GET,
      AEC_GET_PARAM_EXPOSURE_PARAMS);

    if (aec_msg) {
      aec_msg->sync_flag = TRUE;
      AEC_LOW("in payload %p size %d", custom_data->data, custom_data->size);
      if (custom_data->data && custom_data->size > 0) {
        aec_msg->u.aec_get_parm.u.exp_params.custom_param.data =
          custom_data->data;
        aec_msg->u.aec_get_parm.u.exp_params.custom_param.size =
          custom_data->size;
      }
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
      if (aec_msg) {
        uint32_t i;
        uint32_t tmp_cur_sof_id = private->cur_sof_id;
        stats_get_data->flag = STATS_UPDATE_AEC;
        stats_get_data->aec_get.valid_entries =
          aec_msg->u.aec_get_parm.u.exp_params.valid_exp_entries;
        for (i=0; i<stats_get_data->aec_get.valid_entries; i++) {
          stats_get_data->aec_get.real_gain[i] =
            aec_msg->u.aec_get_parm.u.exp_params.real_gain[i];
          stats_get_data->aec_get.sensor_gain[i] =
            aec_msg->u.aec_get_parm.u.exp_params.sensor_gain[i];
          stats_get_data->aec_get.linecount[i] =
            aec_msg->u.aec_get_parm.u.exp_params.linecount[i];
        }
        stats_get_data->aec_get.total_drc_gain =
          aec_msg->u.aec_get_parm.u.exp_params.drc_gains.total_drc_gain;
        stats_get_data->aec_get.color_drc_gain =
          aec_msg->u.aec_get_parm.u.exp_params.drc_gains.color_drc_gain;
        stats_get_data->aec_get.gtm_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.drc_gains.gtm_ratio;
        stats_get_data->aec_get.ltm_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.drc_gains.ltm_ratio;
        stats_get_data->aec_get.la_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.drc_gains.la_ratio;
        stats_get_data->aec_get.gamma_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.drc_gains.gamma_ratio;
        stats_get_data->aec_get.l_linecount =
          aec_msg->u.aec_get_parm.u.exp_params.l_linecount;
        stats_get_data->aec_get.l_real_gain =
          aec_msg->u.aec_get_parm.u.exp_params.l_gain;
        stats_get_data->aec_get.s_linecount =
          aec_msg->u.aec_get_parm.u.exp_params.s_linecount;
        stats_get_data->aec_get.s_real_gain =
          aec_msg->u.aec_get_parm.u.exp_params.s_gain;
        stats_get_data->aec_get.lux_idx =
          aec_msg->u.aec_get_parm.u.exp_params.lux_idx;
        stats_get_data->aec_get.led_off_real_gain =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_real_gain;
        stats_get_data->aec_get.led_off_sensor_gain =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_sensor_gain;
        stats_get_data->aec_get.led_off_total_drc_gain =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_drc_gains.total_drc_gain;
        stats_get_data->aec_get.led_off_color_drc_gain =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_drc_gains.color_drc_gain;
        stats_get_data->aec_get.led_off_gtm_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_drc_gains.gtm_ratio;
        stats_get_data->aec_get.led_off_ltm_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_drc_gains.ltm_ratio;
        stats_get_data->aec_get.led_off_la_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_drc_gains.la_ratio;
        stats_get_data->aec_get.led_off_gamma_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_drc_gains.gamma_ratio;
        stats_get_data->aec_get.led_off_linecount =
          aec_msg->u.aec_get_parm.u.exp_params.led_off_linecount;
        stats_get_data->aec_get.trigger_led =
          aec_msg->u.aec_get_parm.u.exp_params.use_led_estimation;
        stats_get_data->aec_get.exp_time =
          aec_msg->u.aec_get_parm.u.exp_params.exp_time[0];
        stats_get_data->aec_get.hdr_sensitivity_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.hdr_sensitivity_ratio;
        stats_get_data->aec_get.hdr_exp_time_ratio =
          aec_msg->u.aec_get_parm.u.exp_params.hdr_exp_time_ratio;
        output.stats_update.aec_update.exif_iso =
          aec_msg->u.aec_get_parm.u.exp_params.iso[0];
        output.stats_update.aec_update.flash_needed =
          aec_msg->u.aec_get_parm.u.exp_params.flash_needed;
        output.metering_type =
          aec_msg->u.aec_get_parm.u.exp_params.metering_type;
        free(aec_msg);

        output.stats_update.aec_update.exp_time =
          stats_get_data->aec_get.exp_time;

        MCT_OBJECT_LOCK(port);

        if(private->stream_type == CAM_STREAM_TYPE_SNAPSHOT) {
          private->aec_update_flag = FALSE;
          tmp_cur_sof_id = STATS_REPORT_IMMEDIATE;
          float real_gain = stats_get_data->aec_get.real_gain[0];
          float sensor_gain = stats_get_data->aec_get.sensor_gain[0];
          uint32_t linecount = stats_get_data->aec_get.linecount[0];
          float exp_time = stats_get_data->aec_get.exp_time;
          /* HAL 1 manual 3A applies only to non-ZSL snapshot */
          /* In Manual AEC, ADRC is disabled*/
          if (private->manual.is_exp_time_valid &&
              private->manual.is_gain_valid) {
            /* Fully manual */
            real_gain = private->manual.gain;
            sensor_gain = real_gain;
            linecount = (1.0 * private->manual.exp_time *
                        private->sensor_info.pixel_clock /
                         private->sensor_info.pixel_clock_per_line);
            exp_time = private->manual.exp_time;
          } else if (private->manual.is_exp_time_valid &&
                     !private->manual.is_gain_valid) {
            /* Manual exposure time, calculate gain using sensitivity*/
            linecount = (1.0 * private->manual.exp_time *
                         private->sensor_info.pixel_clock /
                         private->sensor_info.pixel_clock_per_line);
            real_gain = (stats_get_data->aec_get.real_gain[0] *
                   stats_get_data->aec_get.linecount[0]) / linecount;
            sensor_gain = real_gain;
            exp_time = private->manual.exp_time;
          } else if (!private->manual.is_exp_time_valid &&
                      private->manual.is_gain_valid) {
            /* Manual gain/iso, calculate linecount using sensitivity*/
            real_gain = private->manual.gain;
            linecount = (stats_get_data->aec_get.real_gain[0] *
                         stats_get_data->aec_get.linecount[0])/ real_gain;
            exp_time =  (stats_get_data->aec_get.real_gain[0] *
                         stats_get_data->aec_get.exp_time) / real_gain;
            sensor_gain = real_gain;
          }
          if (linecount < 1) {
            linecount = 1;
          }
          stats_get_data->aec_get.real_gain[0] = real_gain;
          stats_get_data->aec_get.sensor_gain[0] = sensor_gain;
          stats_get_data->aec_get.linecount[0] = linecount;
          stats_get_data->aec_get.exp_time     = exp_time;

          private->aec_get_data = stats_get_data->aec_get;
        } else if(private->in_zsl_capture == TRUE) {
          private->aec_get_data = stats_get_data->aec_get;
        }
        MCT_OBJECT_UNLOCK(port);
        AEC_HIGH("AEUPD: GETDATA-AEC: G(%f, %f), LC=%u, DRC Gain(%f, %f), DRC Ratios(%f, %f, %f, %f), lux=%f, ET=%f",
          stats_get_data->aec_get.real_gain[0], stats_get_data->aec_get.sensor_gain[0],
          stats_get_data->aec_get.linecount[0], stats_get_data->aec_get.total_drc_gain,
          stats_get_data->aec_get.color_drc_gain, stats_get_data->aec_get.gtm_ratio,
          stats_get_data->aec_get.ltm_ratio, stats_get_data->aec_get.la_ratio,
          stats_get_data->aec_get.gamma_ratio,stats_get_data->aec_get.lux_idx,
          stats_get_data->aec_get.exp_time);
        /* Send exif after manual aec values have been computed  */
        output.stats_update.aec_update.exif_iso =
          (stats_get_data->aec_get.real_gain[0] * 100) / private->ISO100_gain;
        output.stats_update.aec_update.exp_time =
          stats_get_data->aec_get.exp_time;
        AEC_LOW(" exp %f iso %d", stats_get_data->aec_get.exp_time,
          output.stats_update.aec_update.exif_iso);
        aec_port_pack_exif_info(port, &output);

        /* Print all bus messages info */
        aec_port_print_bus("GETDATA:INFO", private);
        aec_send_bus_message(port, MCT_BUS_MSG_AE_INFO,
          &private->aec_info, sizeof(cam_3a_params_t), tmp_cur_sof_id );
      }
    } else {
      AEC_ERR("Not enough memory");
    }
  }

  return rc;
}

/** aec_port_handle_vhdr_buf:
 *    @port:  TODO
 *    @event: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_handle_vhdr_buf( aec_port_private_t *private, isp_buf_divert_t *hdr_stats_buff )
{
    q3a_thread_aecawb_msg_t *aec_msg =
      (q3a_thread_aecawb_msg_t *)malloc(sizeof(q3a_thread_aecawb_msg_t));

    AEC_LOW("aec_msg=%p, HDR stats", aec_msg);

    if (aec_msg == NULL ) {
      AEC_ERR("Not enough memory");
      return;
    }
    memset(aec_msg, 0, sizeof(q3a_thread_aecawb_msg_t));

    stats_t *aec_stats = (stats_t *)calloc(1, sizeof(stats_t));
    if (aec_stats == NULL) {
      AEC_ERR("Not enough memory");
      free(aec_msg);
      return;
    }
    aec_stats->yuv_stats.p_q3a_aec_stats =
      (q3a_aec_stats_t*)calloc(1, sizeof(q3a_aec_stats_t));
    if (aec_stats->yuv_stats.p_q3a_aec_stats == NULL) {
      AEC_ERR("Not enough memory");
      free(aec_stats);
      free(aec_msg);
      return;
    }

    aec_msg->u.stats = aec_stats;
    aec_msg->type = MSG_AEC_STATS_HDR;
    aec_stats->stats_type_mask |= STATS_HDR_VID;
    aec_stats->yuv_stats.p_q3a_aec_stats->ae_region_h_num = 16;
    aec_stats->yuv_stats.p_q3a_aec_stats->ae_region_v_num = 16;
    aec_stats->frame_id = hdr_stats_buff->buffer.sequence;
    aec_port_parse_RDI_stats_AE(private, (uint32_t *)aec_stats->yuv_stats.p_q3a_aec_stats->SY,
      hdr_stats_buff->vaddr);

    q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
}

/** aec_port_handle_stats_data
 *    @private: private aec port data
 *    @stats_mask: type of stats provided
 *
 * Return: TRUE if stats required
 **/
static boolean aec_port_is_handle_stats_required(aec_port_private_t *private,
  uint32_t stats_mask)
{
  if (private->stream_type == CAM_STREAM_TYPE_SNAPSHOT  ||
    private->stream_type == CAM_STREAM_TYPE_RAW) {
    return FALSE;
  }
  /* skip stats in ZSL Flash capture */
  if( (private->in_zsl_capture == TRUE) && (private->in_longshot_mode == FALSE) ){
    AEC_LOW("Skipping STATS in ZSL mode");
    return FALSE;
  }

  /* skip stats for Unified Flash capture */
  if (private->stats_frame_capture.frame_capture_mode) {
    AEC_LOW("Skipping STATS in Unified Flash mode");
    return FALSE;
  }

  if (!((stats_mask & (1 << MSM_ISP_STATS_BHIST)) ||
    (stats_mask & (1 << MSM_ISP_STATS_HDR_BHIST)) ||
    (stats_mask & (1 << MSM_ISP_STATS_BG)) ||
    (stats_mask & (1 << MSM_ISP_STATS_AEC_BG)) ||
    (stats_mask & (1 << MSM_ISP_STATS_AEC)) ||
    (stats_mask & (1 << MSM_ISP_STATS_BE)) ||
    (stats_mask & (1 << MSM_ISP_STATS_HDR_BE)) ||
    (stats_mask & (1 << MSM_ISP_STATS_IHIST)))) {
    return FALSE;
  }
  /* We don't want to process ISP stats if video HDR mode is ON */
  if (((stats_mask & (1 << MSM_ISP_STATS_BG)) ||
    (stats_mask & (1 << MSM_ISP_STATS_AEC_BG)) ||
    (stats_mask & (1 << MSM_ISP_STATS_AEC))) &&
    aec_port_using_HDR_divert_stats(private)) {
    return FALSE;
  }

  return TRUE;
}

/** aec_port_handle_stats_data
 *    @port: MCT port data
 *    @event: MCT event data
 *
 * Return: TRUE if no error
 **/
static boolean aec_port_handle_stats_data(mct_port_t *port, mct_event_t *event)
{
  boolean rc = FALSE;
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  mct_event_module_t *mod_evt = &(event->u.module_event);
  mct_event_stats_ext_t *stats_ext_event;
  mct_event_stats_isp_t *stats_event;
  uint32_t aec_stats_mask = 0;

  stats_ext_event = (mct_event_stats_ext_t *)(mod_evt->module_event_data);
  if (!stats_ext_event || !stats_ext_event->stats_data) {
    return rc;
  }
  stats_event = stats_ext_event->stats_data;

  /* Filter by the stats that algo has requested */
  aec_stats_mask = stats_event->stats_mask & private->required_stats_mask;

  AEC_LOW("event stats_mask = 0x%x, AEC requested stats = 0x%x",
    stats_event->stats_mask, aec_stats_mask);
  if (!aec_port_is_handle_stats_required(private, aec_stats_mask)) {
    return TRUE; /* Non error case */
  }

  if (is_aec_stats_skip_required(private, stats_event->frame_id)) {
    AEC_LOW("skip stats: %d", stats_event->frame_id);
    return TRUE; /* Non error case */
  }

  q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(AEC_UNDEF, AEC_UNDEF);
  if (aec_msg == NULL) {
    return rc;
  }
  stats_t *aec_stats = (stats_t *)calloc(1, sizeof(stats_t));
  if (aec_stats == NULL) {
    free(aec_msg);
    return rc;
  }

  aec_msg->u.stats = aec_stats;
  aec_stats->stats_type_mask = 0;
  aec_stats->frame_id = stats_event->frame_id;

  if (aec_stats_mask & (1 << MSM_ISP_STATS_AEC)) {
    aec_msg->type = MSG_AEC_STATS;
    aec_stats->stats_type_mask |= STATS_AEC;
    aec_stats->yuv_stats.p_q3a_aec_stats =
      stats_event->stats_data[MSM_ISP_STATS_AEC].stats_buf;
    rc = TRUE;
  } else if (aec_stats_mask & (1 << MSM_ISP_STATS_BG)) {
    aec_stats->stats_type_mask |= STATS_BG;
    aec_msg->type = MSG_BG_AEC_STATS;
    aec_stats->bayer_stats.p_q3a_bg_stats =
      stats_event->stats_data[MSM_ISP_STATS_BG].stats_buf;
    rc = TRUE;
  } else if (aec_stats_mask & (1 << MSM_ISP_STATS_AEC_BG)) {
    aec_stats->stats_type_mask |= STATS_BG_AEC;
    aec_msg->type = MSG_BG_AEC_STATS;
    aec_stats->bayer_stats.p_q3a_bg_stats =
      stats_event->stats_data[MSM_ISP_STATS_AEC_BG].stats_buf;
    rc = TRUE;
   } else if (aec_stats_mask & (1 << MSM_ISP_STATS_HDR_BE)) {
    aec_stats->stats_type_mask |= STATS_HDR_BE;
    aec_msg->type = MSG_HDR_BE_AEC_STATS;
    aec_stats->bayer_stats.p_q3a_hdr_be_stats =
      stats_event->stats_data[MSM_ISP_STATS_HDR_BE].stats_buf;
    rc = TRUE;
  } else if (aec_stats_mask & (1 << MSM_ISP_STATS_BE)) {
    aec_stats->stats_type_mask |= STATS_BE;
    aec_msg->type = MSG_BE_AEC_STATS;
    aec_stats->bayer_stats.p_q3a_be_stats =
      stats_event->stats_data[MSM_ISP_STATS_BE].stats_buf;
    rc = TRUE;
  }

  /* Ensure BG or AEC stats are preset to propagate to AEC algorithm.
  If it is missing then ignore the composite stats */
  if (rc && aec_stats_mask & (1 << MSM_ISP_STATS_HDR_BHIST)) {
    aec_stats->stats_type_mask |= STATS_HBHISTO;
    aec_stats->bayer_stats.p_q3a_bhist_stats =
      stats_event->stats_data[MSM_ISP_STATS_HDR_BHIST].stats_buf;
  } else if (rc && aec_stats_mask & (1 << MSM_ISP_STATS_BHIST)) {
    aec_stats->stats_type_mask |= STATS_BHISTO;
    aec_stats->bayer_stats.p_q3a_bhist_stats =
      stats_event->stats_data[MSM_ISP_STATS_BHIST].stats_buf;
  }
  if (rc && aec_stats_mask & (1 << MSM_ISP_STATS_IHIST)) {
    aec_stats->yuv_stats.p_q3a_ihist_stats =
      stats_event->stats_data[MSM_ISP_STATS_IHIST].stats_buf;
  }
  if (!rc) {
    free(aec_stats);
    free(aec_msg);
    return rc;
  }
  uint32_t cur_stats_id = 0;
  uint32_t cur_sof_id = 0;

  MCT_OBJECT_LOCK(port);
  cur_stats_id = private->cur_stats_id = stats_event->frame_id;
  cur_sof_id = private->cur_sof_id;
  AEC_LOW("AEC_UPDATE_DBG: IN STATS_DATA: Curr: SOF_ID:%d Stats_ID:%d",
    private->cur_sof_id, private->cur_stats_id);
  MCT_OBJECT_UNLOCK(port);

  if (aec_msg->type == MSG_BG_AEC_STATS ||
    aec_msg->type == MSG_BE_AEC_STATS ||
    aec_msg->type == MSG_HDR_BE_AEC_STATS) {
    aec_stats->ack_data = stats_ext_event;
    circular_stats_data_use(stats_ext_event);
  }

  rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
  /* If the aecawb thread is inactive, it will not enqueue our
  * message and instead will free it. Then we need to manually
  * free the payload */
  if (rc == FALSE) {
    if (aec_msg->type == MSG_BG_AEC_STATS ||
    aec_msg->type == MSG_BE_AEC_STATS ||
    aec_msg->type == MSG_HDR_BE_AEC_STATS) {
      circular_stats_data_done(stats_ext_event, 0, 0, 0);
    }
    /* In enqueue fail, memory is free inside q3a_aecawb_thread_en_q_msg() *
     * Return back from here */
    aec_stats = NULL;
    return rc;
  }

  return rc;
}

/** aec_port_unified_flash_trigger:
 *    @port:  mct port type containing aec port private data
 *
 * The first call to this function, will set-up unified capture sequence.
 *
 * Return: TRUE on success
 **/
static boolean aec_port_unified_flash_trigger(mct_port_t *port)
{
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  aec_frame_batch_t *frame_info = &private->stats_frame_capture.frame_info;
  int i = 0;

  if (0 == frame_info->num_batch) {
    AEC_ERR("No. of num_batch is zero");
    return FALSE;
  }

  if (FALSE == private->stats_frame_capture.frame_capture_mode) {
    /* Start unified sequence */
    private->stats_frame_capture.current_batch_count = 0;
    private->stats_frame_capture.frame_capture_mode = TRUE;
  } else {
    private->stats_frame_capture.current_batch_count++;
    AEC_LOW("Incremented Current Batch no. =%d",
      private->stats_frame_capture.current_batch_count);
    if (private->stats_frame_capture.current_batch_count >
        private->stats_frame_capture.frame_info.num_batch) {
      private->stats_frame_capture.current_batch_count =
        private->stats_frame_capture.frame_info.num_batch;
      AEC_HIGH("Limit the batch count to HAL value: %d, cur batch cnt: %d",
        private->stats_frame_capture.frame_info.num_batch,
        private->stats_frame_capture.current_batch_count);
    }
  }
  return TRUE;
}
/** aec_port_proc_downstream_event:
 *    port:  TODO
 *    event: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_proc_downstream_event(mct_port_t *port,
  mct_event_t *event)
{
  boolean            rc = TRUE;
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  mct_event_module_t *mod_evt = &(event->u.module_event);
  stats_ext_return_type ret = STATS_EXT_HANDLING_PARTIAL;

  /* Check if extended handling to be performed */
  if (private->func_tbl.ext_handle_module_event) {
    ret = private->func_tbl.ext_handle_module_event(port, mod_evt);
    if (STATS_EXT_HANDLING_COMPLETE == ret) {
      AEC_LOW("Module event handled in extension function!");
      return TRUE;
    }
  }

  switch (mod_evt->type) {
  case MCT_EVENT_MODULE_STATS_GET_THREAD_OBJECT: {
    q3a_thread_aecawb_data_t *data =
      (q3a_thread_aecawb_data_t *)(mod_evt->module_event_data);

    private->thread_data = data->thread_data;

    data->aec_port = port;
    data->aec_cb   = aec_port_callback;
    data->aec_stats_cb = aec_port_stats_done_callback;
    data->aec_obj  = &(private->aec_object);
    rc = TRUE;
  } /* case MCT_EVENT_MODULE_STATS_GET_THREAD_OBJECT */
    break;

  case MCT_EVENT_MODULE_STATS_EXT_DATA: {
    rc = aec_port_handle_stats_data(port, event);
  }
    break;

  case MCT_EVENT_MODULE_ISP_DIVERT_TO_3A:
  case MCT_EVENT_MODULE_BUF_DIVERT: {
    isp_buf_divert_t        *stats_buff =
      (isp_buf_divert_t *)mod_evt->module_event_data;
    if(stats_buff->stats_type != HDR_STATS){
      /* Only AEC module only handles VHDR buffer */
      break;
    }
    aec_port_handle_vhdr_buf(private, stats_buff);
    /*Since this buffer is consumed in same thread context,
        * piggy back  buffer by setting ack_flag to TRUE*/
    stats_buff->ack_flag = TRUE;
    stats_buff->is_buf_dirty = TRUE;
  }
    break;

  case MCT_EVENT_MODULE_SET_RELOAD_CHROMATIX:
  case MCT_EVENT_MODULE_SET_CHROMATIX_PTR: {
    modulesChromatix_t *mod_chrom =
      (modulesChromatix_t *)mod_evt->module_event_data;
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_INIT_CHROMATIX_SENSOR);

    AEC_LOW("aec_msg=%p,SET_CHROMATIX", aec_msg);

    if (aec_msg != NULL ) {

      switch (private->stream_type) {
      case CAM_STREAM_TYPE_VIDEO: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_CAMCORDER;
      }
        break;

      case CAM_STREAM_TYPE_PREVIEW:
      case CAM_STREAM_TYPE_CALLBACK: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_PREVIEW;
      }
        break;

      case CAM_STREAM_TYPE_RAW:
      case CAM_STREAM_TYPE_SNAPSHOT: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_SNAPSHOT;
      }
        break;

      default: {
      }
        break;
      } /* switch (private->stream_type) */

      AEC_LOW("stream_type=%d op_mode=%d",
        private->stream_type, aec_msg->u.aec_set_parm.u.init_param.op_mode);

      aec_msg->u.aec_set_parm.u.init_param.chromatix = mod_chrom->chromatix3APtr;
      aec_msg->u.aec_set_parm.u.init_param.warm_start.stored_params = private->stored_params;

      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);

      chromatix_3a_parms_type *chromatix =  mod_chrom->chromatix3APtr;
      private->init_sensitivity =
         (chromatix->AEC_algo_data.aec_exposure_table.exposure_entries[0].gain /256.0) *
         chromatix->AEC_algo_data.aec_exposure_table.exposure_entries[0].line_count;
      private->ISO100_gain =
        chromatix->AEC_algo_data.aec_generic.ISO100_gain;
      private->fast_aec_forced_cnt =
        (uint16_t)chromatix->AEC_algo_data.aec_generic.reserved[48];
      if (private->fast_aec_data.num_frames == 0) {
        private->fast_aec_forced_cnt = 0;
      } else if (private->fast_aec_forced_cnt == 0 ||
        private->fast_aec_forced_cnt >= private->fast_aec_data.num_frames) {
        private->fast_aec_forced_cnt = private->fast_aec_data.num_frames - 2;
      }
      AEC_HIGH("Fast_AEC: forced cnt %d num_frames %d",
        private->fast_aec_forced_cnt, private->fast_aec_data.num_frames);

      /* Read the Dual Camera exposure multiplier from reserved tuning parameters */
      private->dual_cam_exp_multiplier = chromatix->AEC_algo_data.aec_generic.reserved[2];
      char value[PROPERTY_VALUE_MAX];
      /* Multiplier can be overwritten via setprops */
      property_get("persist.camera.dual.expmult", value, "0.0");
      if (atof(value) != 0.0f){
        private->dual_cam_exp_multiplier = atof(value);
      }
      if (private->dual_cam_exp_multiplier <= 0.0f){
        private->dual_cam_exp_multiplier = 1.0f;
      }
      AEC_HIGH("DualCamera Exposure Multiplier: %f", private->dual_cam_exp_multiplier);

      private->cur_stats_id = 0;
      private->cur_sof_id = 0;

      /* Trigger to store initial parameters */
      q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SEND_EVENT,
        AEC_SET_PARAM_PACK_OUTPUT);
      if (aec_msg != NULL) {
        rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
      }
    } /* if (aec_msg != NULL ) */
  } /* case MCT_EVENT_MODULE_SET_CHROMATIX_PTR */
    break;

  case MCT_EVENT_MODULE_PREVIEW_STREAM_ID: {
    mct_stream_info_t  *stream_info =
      (mct_stream_info_t *)(mod_evt->module_event_data);

    AEC_LOW("Preview stream-id event: stream_type: %d width: %d height: %d",
      stream_info->stream_type, stream_info->dim.width, stream_info->dim.height);

    private->preview_width = stream_info->dim.width;
    private->preview_height = stream_info->dim.height;
    private->stream_identity = stream_info->identity;
  }
    break;

  case MCT_EVENT_MODULE_SET_STREAM_CONFIG: {
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_INIT_SENSOR_INFO);
    sensor_out_info_t       *sensor_info =
      (sensor_out_info_t *)(mod_evt->module_event_data);

    private->parse_RDI_stats =
        (sensor_RDI_parser_func_t)sensor_info->parse_RDI_statistics;

    private->max_sensor_delay = sensor_info->sensor_immediate_pipeline_delay +
      sensor_info->sensor_additive_pipeline_delay;

    /* TBG to change to sensor */
    float fps, max_fps;

    if (aec_msg != NULL ) {
      fps = sensor_info->max_fps * 0x00000100;

      /*max fps supported by sensor*/
      max_fps = (float)sensor_info->vt_pixel_clk * 0x00000100 /
        (float)(sensor_info->ll_pck * sensor_info->fl_lines);

      /* Sanity check*/
      if(fps <= 0) {
        AEC_ERR(" Sensor fps is 0!!");
        /* default to 30*/
        fps = 30 * 256;
      }
      switch (private->stream_type) {
      case CAM_STREAM_TYPE_VIDEO: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_CAMCORDER;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.video_fps = fps;
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_CAMCORDER;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.video_fps = fps;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.preview_linesPerFrame =
          sensor_info->fl_lines;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.max_preview_fps = max_fps;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.preview_fps = fps;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.prev_max_line_cnt =
          sensor_info->max_linecount;
      }
        break;

      case CAM_STREAM_TYPE_PREVIEW:
      case CAM_STREAM_TYPE_CALLBACK: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_PREVIEW;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.preview_linesPerFrame =
          sensor_info->fl_lines;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.max_preview_fps = max_fps;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.preview_fps = fps;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.prev_max_line_cnt =
          sensor_info->max_linecount;
      }
        break;

      case CAM_STREAM_TYPE_RAW:
      case CAM_STREAM_TYPE_SNAPSHOT: {
        aec_msg->u.aec_set_parm.u.init_param.op_mode =
          AEC_OPERATION_MODE_SNAPSHOT;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.snap_linesPerFrame =
          sensor_info->fl_lines;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.snap_max_line_cnt =
          sensor_info->max_linecount;
        aec_msg->u.aec_set_parm.u.init_param.sensor_info.snapshot_fps = fps;
      }
        break;

      default: {
      }
        break;
      } /* switch (private->stream_type) */
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.max_gain =
        sensor_info->max_gain;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.current_fps = fps;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.pixel_clock =
        sensor_info->vt_pixel_clk;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.pixel_clock_per_line =
        sensor_info->ll_pck;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.sensor_res_height =
        sensor_info->request_crop.last_line -
        sensor_info->request_crop.first_line + 1;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.sensor_res_width =
        sensor_info->request_crop.last_pixel -
        sensor_info->request_crop.first_pixel + 1;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.pixel_sum_factor =
        sensor_info->pixel_sum_factor;
      aec_msg->u.aec_set_parm.u.init_param.sensor_info.f_number =
        sensor_info->af_lens_info.f_number;
      memcpy(&private->sensor_info,
        &aec_msg->u.aec_set_parm.u.init_param.sensor_info,
        sizeof(aec_sensor_info_t));
      /* Initialize ROI as disable*/
      aec_interested_region_t roi = {0};
      aec_port_update_roi(private, roi);

      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
      /* Also send the stream dimensions for preview */
      if ((private->stream_type == CAM_STREAM_TYPE_PREVIEW) ||
        (private->stream_type == CAM_STREAM_TYPE_CALLBACK)||
        (private->stream_type == CAM_STREAM_TYPE_VIDEO)) {
        aec_set_parameter_init_t *init_param;
        q3a_thread_aecawb_msg_t  *dim_msg;

        dim_msg = aec_port_malloc_msg(MSG_AEC_SET, AEC_SET_PARAM_UI_FRAME_DIM);
        if (!dim_msg) {
          AEC_ERR(" malloc failed for dim_msg");
          break;
        }
        init_param = &(dim_msg->u.aec_set_parm.u.init_param);
        init_param->frame_dim.width = private->preview_width;
        init_param->frame_dim.height = private->preview_height;
        AEC_LOW("enqueue msg update ui width %d and height %d",
          init_param->frame_dim.width, init_param->frame_dim.height);

        rc = q3a_aecawb_thread_en_q_msg(private->thread_data, dim_msg);
      }
    }
  } /* MCT_EVENT_MODULE_SET_STREAM_CONFIG*/
    break;

  case MCT_EVENT_MODULE_PPROC_GET_AEC_UPDATE: {
    stats_get_data_t *stats_get_data =
      (stats_get_data_t *)mod_evt->module_event_data;
    if (!stats_get_data) {
      AEC_ERR("failed\n");
      break;
    }

    if (TRUE == private->stats_frame_capture.streamon_update_done) {
      AEC_LOW("MCT_EVENT_MODULE_PPROC_GET_AEC_UPDATE: skip update: streamon_update_done");
      break;
    }

    if(private->stream_type == CAM_STREAM_TYPE_VIDEO)
      memset(&private->aec_get_data, 0, sizeof(private->aec_get_data));
    aec_port_proc_get_aec_data(port, stats_get_data);
  } /* MCT_EVENT_MODULE_PPROC_GET_AEC_UPDATE X*/
    break;

  case MCT_EVENT_MODULE_ISP_OUTPUT_DIM: {
    mct_stream_info_t *stream_info =
      (mct_stream_info_t *)(event->u.module_event.module_event_data);
    if(!stream_info) {
      AEC_ERR("failed\n");
      break;
    }

    if (stream_info->identity == private->stream_identity) {
      private->vfe_out_width  = stream_info->dim.width;
      private->vfe_out_height = stream_info->dim.height;
    }
  }
    break;

  case MCT_EVENT_MODULE_STREAM_CROP: {
    mct_bus_msg_stream_crop_t *stream_crop =
      (mct_bus_msg_stream_crop_t *)mod_evt->module_event_data;

    if (!stream_crop) {
      AEC_ERR("failed\n");
      break;
    }
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_CROP_INFO);
    if (aec_msg != NULL ) {
      aec_msg->u.aec_set_parm.u.stream_crop.pp_x = stream_crop->x;
      aec_msg->u.aec_set_parm.u.stream_crop.pp_y = stream_crop->y;
      aec_msg->u.aec_set_parm.u.stream_crop.pp_crop_out_x =
        stream_crop->crop_out_x;
      aec_msg->u.aec_set_parm.u.stream_crop.pp_crop_out_y =
        stream_crop->crop_out_y;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_map_x = stream_crop->x_map;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_map_y = stream_crop->y_map;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_map_width =
        stream_crop->width_map;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_map_height =
        stream_crop->height_map;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_out_width =
        private->vfe_out_width;
      aec_msg->u.aec_set_parm.u.stream_crop.vfe_out_height =
        private->vfe_out_height;
      AEC_LOW("Crop Event from ISP received. PP (%d %d %d %d)", stream_crop->x,
        stream_crop->y, stream_crop->crop_out_x, stream_crop->crop_out_y);
      AEC_LOW("vfe map: (%d %d %d %d) vfe_out: (%d %d)", stream_crop->x_map,
        stream_crop->y_map, stream_crop->width_map, stream_crop->height_map,
        private->vfe_out_width, private->vfe_out_height);
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
  }
    break;

  case MCT_EVENT_MODULE_FACE_INFO: {
    mct_face_info_t *face_info = (mct_face_info_t *)mod_evt->module_event_data;
    if (!face_info) {
      AEC_ERR("error: Empty event");
      break;
    }

    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_FD_ROI);
    if (aec_msg != NULL) {
      uint8_t idx = 0;
      uint32_t face_count = face_info->face_count;
      if (face_count > MAX_STATS_ROI_NUM) {
        AEC_HIGH("face_count %d exceed stats roi limitation, cap to max", face_count);
        face_count = MAX_STATS_ROI_NUM;
      }
      if (face_count > MAX_ROI) {
        AEC_HIGH("face_count %d exceed max roi limitation, cap to max", face_count);
        face_count = MAX_ROI;
      }

      /* Copy original face coordinates, do the transform in aec_set.c */
      aec_msg->u.aec_set_parm.u.fd_roi.type = ROI_TYPE_GENERAL;
      aec_msg->u.aec_set_parm.u.fd_roi.num_roi = face_count;
      for (idx = 0; idx < aec_msg->u.aec_set_parm.u.fd_roi.num_roi; idx++) {
        aec_msg->u.aec_set_parm.u.fd_roi.roi[idx].x =
          face_info->orig_faces[idx].roi.left;
        aec_msg->u.aec_set_parm.u.fd_roi.roi[idx].y =
          face_info->orig_faces[idx].roi.top;
        aec_msg->u.aec_set_parm.u.fd_roi.roi[idx].dx =
          face_info->orig_faces[idx].roi.width;
        aec_msg->u.aec_set_parm.u.fd_roi.roi[idx].dy =
          face_info->orig_faces[idx].roi.height;
      }
      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
  }
    break;

  case MCT_EVENT_MODULE_STATS_GET_DATA: {
    stats_get_data_t *stats_get_data =
      (stats_get_data_t *)mod_evt->module_event_data;
    if (!stats_get_data) {
      AEC_ERR("failed\n");
      break;
    }

    if (TRUE == private->stats_frame_capture.streamon_update_done) {
      AEC_LOW("MCT_EVENT_MODULE_STATS_GET_DATA: skip update: streamon_update_done");
      break;
    }
    aec_port_proc_get_aec_data(port, stats_get_data);
  }
    break;

  case MCT_EVENT_MODULE_STATS_GET_LED_DATA: {
  }
    break;

  case MCT_EVENT_MODULE_STATS_AFD_UPDATE: {
    /* Do not send the AFD update to core if AEC locked by HAL */
    if (FALSE == private->locked_from_hal) {
      aec_port_handle_afd_update(private->thread_data, mod_evt);
    }
  }
    break;

  case MCT_EVENT_MODULE_STATS_AWB_UPDATE: {
    stats_update_t *stats_update =
      (stats_update_t *)(mod_evt->module_event_data);
    if (stats_update->flag != STATS_UPDATE_AWB) {
      break;
    }
    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_AWB_PARM);
    if (aec_msg != NULL ) {
      aec_set_awb_parm_t *awb_param;
      awb_param = &(aec_msg->u.aec_set_parm.u.awb_param);
      awb_param->r_gain = stats_update->awb_update.gain.r_gain;
      awb_param->g_gain = stats_update->awb_update.gain.g_gain;
      awb_param->b_gain = stats_update->awb_update.gain.b_gain;
      awb_param->unadjusted_r_gain = stats_update->awb_update.unadjusted_gain.r_gain;
      awb_param->unadjusted_g_gain = stats_update->awb_update.unadjusted_gain.g_gain;
      awb_param->unadjusted_b_gain = stats_update->awb_update.unadjusted_gain.b_gain;
      awb_param->dual_led_flux_gain = stats_update->awb_update.dual_led_flux_gain;
      if (CAM_WB_MODE_INCANDESCENT == (cam_wb_mode_type)stats_update->awb_update.wb_mode)
        awb_param->is_wb_mode_incandescent = TRUE;
      else
        awb_param->is_wb_mode_incandescent = FALSE;

      /* Handle custom parameters update (3a ext) */
      if (stats_update->awb_update.awb_custom_param_update.data &&
        stats_update->awb_update.awb_custom_param_update.size) {
        awb_param->awb_custom_param_update.data =
          malloc(stats_update->awb_update.awb_custom_param_update.size);
        if (awb_param->awb_custom_param_update.data) {
          awb_param->awb_custom_param_update.size =
            stats_update->awb_update.awb_custom_param_update.size;
          memcpy(awb_param->awb_custom_param_update.data,
            stats_update->awb_update.awb_custom_param_update.data,
            awb_param->awb_custom_param_update.size);
        } else {
          AEC_ERR("Error: Fail to allocate memory for custom parameters");
          free(aec_msg);
          rc = FALSE;
          break;
        }
      }

      rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
      if (!rc) {
        AEC_ERR("Fail to queue msg");
      }
    } /* if (aec_msg != NULL ) */
  } /* MCT_EVENT_MODULE_STATS_AWB_UPDATE*/
    break;

  case MCT_EVENT_MODULE_STATS_ASD_UPDATE: {
    aec_port_handle_asd_update(private->thread_data, mod_evt);
  }
    break;
  case MCT_EVENT_MODULE_STATS_GYRO_STATS: {
    aec_algo_gyro_info_t *gyro_info = NULL;
    mct_event_gyro_stats_t *gyro_update =
      (mct_event_gyro_stats_t *)mod_evt->module_event_data;
    int i = 0;

    q3a_thread_aecawb_msg_t *aec_msg = (q3a_thread_aecawb_msg_t *)
      malloc(sizeof(q3a_thread_aecawb_msg_t));
    if (aec_msg != NULL) {
      memset(aec_msg, 0, sizeof(q3a_thread_aecawb_msg_t));
      aec_msg->type = MSG_AEC_SET;
      aec_msg->u.aec_set_parm.type = AEC_SET_PARAM_GYRO_INFO;
      /* Copy gyro data now */
      gyro_info = &(aec_msg->u.aec_set_parm.u.gyro_info);
      gyro_info->q16_ready = TRUE;
      gyro_info->float_ready = TRUE;

      for (i = 0; i < 3; i++) {
        gyro_info->q16[i] = (long) gyro_update->q16_angle[i];
        gyro_info->flt[i] = (float) gyro_update->q16_angle[i] / (1 << 16);
        AEC_LOW("i: %d q16: %d flt: %f", i,
          gyro_info->q16[i], gyro_info->flt[i]);
      }
      q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    }
  }
    break;
  case MCT_EVENT_MODULE_MODE_CHANGE: {
    /* Stream mode has changed */
    private->stream_type =
      ((stats_mode_change_event_data*)
      (event->u.module_event.module_event_data))->stream_type;
    private->reserved_id =
      ((stats_mode_change_event_data*)
      (event->u.module_event.module_event_data))->reserved_id;
  }
    break;
  case MCT_EVENT_MODULE_LED_STATE_TIMEOUT: {
    AEC_HIGH("Received LED state timeout. Reset LED state!");
    q3a_thread_aecawb_msg_t *reset_led_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_RESET_LED_EST);

    if (!reset_led_msg) {
      AEC_ERR("malloc failed for dim_msg");
      break;
    }
    rc = q3a_aecawb_thread_en_q_msg(private->thread_data, reset_led_msg);
  }
    break;
  case MCT_EVENT_MODULE_REQUEST_STATS_TYPE: {
    uint32_t required_stats_mask = 0;
    isp_rgn_skip_pattern rgn_skip_pattern = RGN_SKIP_PATTERN_MAX;
    mct_event_request_stats_type *stats_info =
      (mct_event_request_stats_type *)mod_evt->module_event_data;

    if (ISP_STREAMING_OFFLINE == stats_info->isp_streaming_type) {
      AEC_HIGH("AEC doesn't support offline processing yet. Returning.");
      break;
    }

    q3a_thread_aecawb_msg_t *aec_msg = aec_port_malloc_msg(MSG_AEC_GET,
      AEC_GET_PARAM_REQUIRED_STATS);
    if (!aec_msg) {
      AEC_ERR("malloc failed for AEC_GET_PARAM_REQUIRED_STATS");
      rc = FALSE;
      break;
    }

    /* Fill msg with the supported stats data */
    aec_msg->u.aec_get_parm.u.request_stats.supported_stats_mask =
      stats_info->supported_stats_mask;
    aec_msg->u.aec_get_parm.u.request_stats.supported_rgn_skip_mask =
      stats_info->supported_rgn_skip_mask;
    /* Get the list of require stats from algo library */
    aec_msg->sync_flag = TRUE;
    rc = q3a_aecawb_thread_en_q_msg(private->thread_data, aec_msg);
    required_stats_mask = aec_msg->u.aec_get_parm.u.request_stats.enable_stats_mask;
    rgn_skip_pattern =
      (isp_rgn_skip_pattern)(aec_msg->u.aec_get_parm.u.request_stats.enable_rgn_skip_pattern);
    free(aec_msg);
    aec_msg = NULL;
    if (!rc) {
      AEC_ERR("Error: fail to get required stats");
      return rc;
    }

    /* Verify if require stats are supported */
    if (required_stats_mask !=
      (stats_info->supported_stats_mask & required_stats_mask)) {
      AEC_ERR("Error: Stats not supported: 0x%x, supported stats = 0x%x",
        required_stats_mask, stats_info->supported_stats_mask);
      rc = FALSE;
      break;
    }

    /* Update query and save internally */
    stats_info->enable_stats_mask |= required_stats_mask;
    private->required_stats_mask = required_stats_mask;

    AEC_LOW("MCT_EVENT_MODULE_REQUEST_STATS_TYPE:Required AEC stats mask = 0x%x",
      private->required_stats_mask);

    /* Set requested tap locations. Only interested in AEC_BG */

    /* Note for 8998: AEC's HDR_BE stats will still be called as AEC_BG in stats sw,
     * since the name "HDR_BE" has been taken by ISP for other purpose, hence
     * not available for stats sw.
     */

    isp_stats_tap_loc supported_tap_mask =
      stats_info->supported_tap_location[MSM_ISP_STATS_AEC_BG];
    isp_stats_tap_loc requested_tap_mask = ISP_STATS_TAP_DEFAULT;

    if (supported_tap_mask & ISP_STATS_TAP_AFTER_LENS_ROLLOFF) {
      requested_tap_mask = ISP_STATS_TAP_AFTER_LENS_ROLLOFF;
    }
    else if (supported_tap_mask & ISP_STATS_TAP_BEFORE_LENS_ROLLOFF) {
      requested_tap_mask = ISP_STATS_TAP_BEFORE_LENS_ROLLOFF;
    }

    /* Update query and save internally */
    stats_info->requested_tap_location[MSM_ISP_STATS_AEC_BG] |= requested_tap_mask;
    memcpy(private->requested_tap_location,
           stats_info->requested_tap_location,
           sizeof(uint32_t) * MSM_ISP_STATS_MAX);
    stats_info->enable_rgn_skip_pattern[MSM_ISP_STATS_AEC_BG] = rgn_skip_pattern;
  }
    break;
  case MCT_EVENT_MODULE_ISP_STATS_INFO: {
    mct_stats_info_t *stats_info =
      (mct_stats_info_t *)mod_evt->module_event_data;

    q3a_thread_aecawb_msg_t *stats_msg = aec_port_malloc_msg(MSG_AEC_SET,
      AEC_SET_PARAM_STATS_DEPTH);

    if (!stats_msg) {
      AEC_ERR("malloc failed for stats_msg");
      break;
    }
    stats_msg->u.aec_set_parm.u.stats_depth = stats_info->stats_depth;
    rc = q3a_aecawb_thread_en_q_msg(private->thread_data, stats_msg);
  }
    break;

  case MCT_EVENT_MODULE_TRIGGER_CAPTURE_FRAME: {
    AEC_LOW("MCT_EVENT_MODULE_TRIGGER_CAPTURE_FRAME!");
    rc = aec_port_unified_flash_trigger(port);
  }
    break;

  default: {
  }
    break;
  } /* switch (mod_evt->type) */
  return rc;
}

/** aec_port_event:
 *    @port:  TODO
 *    @event: TODO
 *
 * aec sink module's event processing function. Received events could be:
 * AEC/AWB/AF Bayer stats;
 * Gyro sensor stat;
 * Information request event from other module(s);
 * Informatin update event from other module(s);
 * It ONLY takes MCT_EVENT_DOWNSTREAM event.
 *
 * Return TRUE if the event is processed successfully.
 **/
static boolean aec_port_event(mct_port_t *port, mct_event_t *event)
{
  boolean rc = FALSE;
  aec_port_private_t *private;

  /* sanity check */
  if (!port || !event) {
    return FALSE;
  }

  private = (aec_port_private_t *)(port->port_private);
  if (!private) {
    return FALSE;
  }

  /* sanity check: ensure event is meant for port with same identity*/
  if (!aec_port_check_identity(private->reserved_id, event->identity)) {
    return FALSE;
  }

  AEC_LOW("AEC_EVENT: %s Dir %d",
    event->type == MCT_EVENT_CONTROL_CMD ?
    stats_port_get_mct_event_ctrl_string(event->u.ctrl_event.type):
    (event->type == MCT_EVENT_MODULE_EVENT ?
    stats_port_get_mct_event_module_string(event->u.module_event.type):
    "INVALID EVENT"), MCT_EVENT_DIRECTION(event));

  switch (MCT_EVENT_DIRECTION(event)) {
  case MCT_EVENT_DOWNSTREAM: {
    switch (event->type) {
    case MCT_EVENT_MODULE_EVENT: {
      rc = aec_port_proc_downstream_event(port, event);
    } /* case MCT_EVENT_MODULE_EVENT */
      break;

    case MCT_EVENT_CONTROL_CMD: {
      rc = aec_port_proc_downstream_ctrl(port,event);
    }
      break;

    default: {
    }
      break;
    }
  } /* case MCT_EVENT_TYPE_DOWNSTREAM */
    break;

  case MCT_EVENT_UPSTREAM: {
    mct_port_t *peer = MCT_PORT_PEER(port);
    MCT_PORT_EVENT_FUNC(peer)(peer, event);
  }
    break;

  default: {
    rc = FALSE;
  }
    break;
  }

  return rc;
}

/** aec_port_intra_event:
 *    @port:  MCT port
 *    @event: MCT module
 *
 * Handles the intra-module events sent between AEC master and slave sessions
 *
 * Return TRUE if the event is processed successfully.
 **/
static boolean aec_port_intra_event(mct_port_t *port, mct_event_t *event)
{
  aec_port_private_t *private;
  AEC_LOW("Received AEC intra-module peer event");

  /* sanity check */
  if (!port || !event) {
    return FALSE;
  }

  private = (aec_port_private_t *)(port->port_private);
  if (!private) {
    return FALSE;
  }

  /* sanity check: ensure event is meant for port with same identity*/
  if (!aec_port_check_identity(private->reserved_id, event->identity)) {
    return FALSE;
  }

  switch(event->u.module_event.type)
  {
    case MCT_EVENT_MODULE_STATS_PEER_AEC_UPDATE:
      AEC_LOW("Received MCT_EVENT_MODULE_STATS_PEER_AEC_UPDATE");
      aec_port_handle_peer_aec_update(
          port,
          (aec_port_peer_aec_update*)event->u.module_event.module_event_data);
      break;

    default:
      AEC_ERR("Error! Received unknown intra-module event type: %d",
              event->u.module_event.type);
      break;
  }

  return TRUE;
}

/** aec_port_ext_link:
 *    @identity: session id + stream id
 *    @port:  aec module's sink port
 *    @peer:  q3a module's sink port
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_ext_link(unsigned int identity,
  mct_port_t *port, mct_port_t *peer)
{
  boolean            rc = FALSE;
  aec_port_private_t *private;

  /* aec sink port's external peer is always q3a module's sink port */
  if (!port || !peer ||
    strcmp(MCT_OBJECT_NAME(port), "aec_sink") ||
    strcmp(MCT_OBJECT_NAME(peer), "q3a_sink")) {
    AEC_ERR("Invalid parameters!");
    return FALSE;
  }

  private = (aec_port_private_t *)port->port_private;
  if (!private) {
    AEC_ERR("Null Private port!");
    return FALSE;
  }

  MCT_OBJECT_LOCK(port);
  switch (private->state) {
  case AEC_PORT_STATE_RESERVED:
    /* Fall through, no break */
  case AEC_PORT_STATE_UNLINKED:
    /* Fall through, no break */
  case AEC_PORT_STATE_LINKED: {
    if (!aec_port_check_identity(private->reserved_id, identity)) {
      break;
    }
  }
  /* Fall through, no break */
  case AEC_PORT_STATE_CREATED: {
    rc = TRUE;
  }
    break;

  default: {
  }
    break;
  }

  if (rc == TRUE) {
    private->state = AEC_PORT_STATE_LINKED;
    MCT_PORT_PEER(port) = peer;
    MCT_OBJECT_REFCOUNT(port) += 1;
  }
  MCT_OBJECT_UNLOCK(port);
  mct_port_add_child(identity, port);

  AEC_LOW("X rc=%d", rc);
  return rc;
}

/** aec_port_ext_unlink
 *    @identity: TODO
 *    @port:     TODO
 *    @peer:     TODO
 *
 * TODO description
 *
 * Return nothing
 **/
static void aec_port_ext_unlink(unsigned int identity, mct_port_t *port,
  mct_port_t *peer)
{
  aec_port_private_t *private;

  if (!port || !peer || MCT_PORT_PEER(port) != peer) {
    return;
  }

  private = (aec_port_private_t *)port->port_private;
  if (!private) {
    return;
  }

  MCT_OBJECT_LOCK(port);
  if (private->state == AEC_PORT_STATE_LINKED &&
    aec_port_check_identity(private->reserved_id, identity)) {
    MCT_OBJECT_REFCOUNT(port) -= 1;
    if (!MCT_OBJECT_REFCOUNT(port)) {
      private->state = AEC_PORT_STATE_UNLINKED;
      private->aec_update_flag = FALSE;
    }
  }
  MCT_OBJECT_UNLOCK(port);
  mct_port_remove_child(identity, port);

  return;
}

/** aec_port_set_caps:
 *    @port: TODO
 *    @caps: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_set_caps(mct_port_t *port, mct_port_caps_t *caps)
{
  if (strcmp(MCT_PORT_NAME(port), "aec_sink")) {
    return FALSE;
  }

  port->caps = *caps;
  return TRUE;
}

/** aec_port_check_caps_reserve:
 *    @port:        TODO
 *    @caps:        TODO
 *    @stream_info: TODO
 *
 *  AEC sink port can ONLY be re-used by ONE session. If this port
 *  has been in use, AEC module has to add an extra port to support
 *  any new session(via module_aec_request_new_port).
 *
 * TODO Return
 **/
boolean aec_port_check_caps_reserve(mct_port_t *port, void *caps,
  void *info)
{
  boolean            rc = FALSE;
  mct_port_caps_t    *port_caps;
  aec_port_private_t *private;
  mct_stream_info_t *stream_info = (mct_stream_info_t *) info;

  AEC_LOW("E");
  MCT_OBJECT_LOCK(port);
  if (!port || !caps || !stream_info ||
    strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    rc = FALSE;
    goto reserve_done;
  }

  port_caps = (mct_port_caps_t *)caps;
  if (port_caps->port_caps_type != MCT_PORT_CAPS_STATS) {
    rc = FALSE;
    goto reserve_done;
  }

  private = (aec_port_private_t *)port->port_private;
  switch (private->state) {
  case AEC_PORT_STATE_LINKED: {
    if (aec_port_check_identity(private->reserved_id, stream_info->identity)) {
      rc = TRUE;
    }
  }
    break;

  case AEC_PORT_STATE_CREATED:
  case AEC_PORT_STATE_UNRESERVED: {
    private->reserved_id = stream_info->identity;
    private->stream_type = stream_info->stream_type;
    private->stream_info = *stream_info;
    private->state       = AEC_PORT_STATE_RESERVED;
    rc = TRUE;
  }
    break;

  case AEC_PORT_STATE_RESERVED: {
    if (aec_port_check_identity(private->reserved_id, stream_info->identity)) {
      rc = TRUE;
    }
  }
    break;

  default: {
    rc = FALSE;
  }
    break;
  }

reserve_done:
  MCT_OBJECT_UNLOCK(port);
  return rc;
}

/** aec_port_check_caps_unreserve:
 *    port:     TODO
 *    identity: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
static boolean aec_port_check_caps_unreserve(mct_port_t *port,
  unsigned int identity)
{
  aec_port_private_t *private;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    return FALSE;
  }

  private = (aec_port_private_t *)port->port_private;
  if (!private) {
    return FALSE;
  }

  MCT_OBJECT_LOCK(port);
  if ((private->state == AEC_PORT_STATE_UNLINKED ||
    private->state == AEC_PORT_STATE_LINKED ||
    private->state == AEC_PORT_STATE_RESERVED) &&
    aec_port_check_identity(private->reserved_id, identity)) {
    if (!MCT_OBJECT_REFCOUNT(port)) {
      private->state       = AEC_PORT_STATE_UNRESERVED;
      private->reserved_id = (private->reserved_id & 0xFFFF0000);
    }
  }
  MCT_OBJECT_UNLOCK(port);

  return TRUE;
}

/** aec_port_find_identity:
 *    @port:     TODO
 *    @identity: TODO
 *
 * TODO description
 *
 * TODO Return
 **/
boolean aec_port_find_identity(mct_port_t *port, unsigned int identity)
{
  aec_port_private_t *private;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    return FALSE;
  }

  private = port->port_private;

  if (private) {
    return ((private->reserved_id & 0xFFFF0000) == (identity & 0xFFFF0000) ?
      TRUE : FALSE);
  }
  return FALSE;
}

/** aec_port_deinit
 *    @port: TODO
 *
 * TODO description
 *
 * Return nothing
 **/
void aec_port_deinit(mct_port_t *port)
{
  aec_port_private_t *private;

  if (!port || strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    return;
  }

  private = port->port_private;
  if (!private) {
    return;
  }

  AEC_DESTROY_LOCK(&private->aec_object);
  pthread_mutex_destroy(&private->update_state_lock);
  private->aec_object.deinit(private->aec_object.aec);
  if (private->func_tbl.ext_deinit) {
    private->func_tbl.ext_deinit(port);
  }
  if (FALSE == private->aec_extension_use) {
    aec_port_unload_function(private);
  } else {
    aec_port_ext_unload_function(private);
  }

  free(private);
  private = NULL;
}

/** aec_port_update_func_table:
 *    @ptr: pointer to internal aec pointer object
 *
 * Update extendable function pointers.
 *
 * Return None
 **/
boolean aec_port_update_func_table(aec_port_private_t *private)
{
  private->func_tbl.ext_init = NULL;
  private->func_tbl.ext_deinit = NULL;
  private->func_tbl.ext_callback = NULL;
  private->func_tbl.ext_handle_module_event = NULL;
  private->func_tbl.ext_handle_control_event = NULL;
  return TRUE;
}

/** aec_port_init:
 *    @port:      aec's sink port to be initialized
 *    @sessionid: TODO
 *
 *  aec port initialization entry point. Becase AEC module/port is
 *  pure software object, defer aec_port_init when session starts.
 *
 * TODO Return
 **/
boolean aec_port_init(mct_port_t *port, unsigned int *sessionid)
{
  boolean            rc = TRUE;
  mct_port_caps_t    caps;
  unsigned int       *session;
  mct_list_t         *list;
  aec_port_private_t *private;
  unsigned int       session_id =(((*sessionid) >> 16) & 0x00ff);

  if (!port || strcmp(MCT_OBJECT_NAME(port), "aec_sink")) {
    return FALSE;
  }

  private = (void *)malloc(sizeof(aec_port_private_t));
  if (!private) {
    return FALSE;
  }
  memset(private, 0, sizeof(aec_port_private_t));
  pthread_mutex_init(&private->update_state_lock, NULL);
  /* initialize AEC object */
  AEC_INITIALIZE_LOCK(&private->aec_object);

  private->aec_auto_mode = AEC_AUTO;
  private->aec_on_off_mode = TRUE;

  private->reserved_id = *sessionid;
  private->state       = AEC_PORT_STATE_CREATED;
  private->dual_cam_sensor_info = CAM_TYPE_MAIN;

  aec_port_reset_output_index(private);

  port->port_private   = private;
  port->direction      = MCT_PORT_SINK;
  caps.port_caps_type  = MCT_PORT_CAPS_STATS;
  caps.u.stats.flag    = (MCT_PORT_CAP_STATS_Q3A | MCT_PORT_CAP_STATS_CS_RS);
  private->adrc_settings.is_adrc_feature_supported = TRUE;

  /* Set default functions to keep clean & bug free code*/
  rc &= aec_port_load_dummy_default_func(&private->aec_object);
  rc &= aec_port_update_func_table(private);

  /* this is sink port of aec module */
  mct_port_set_event_func(port, aec_port_event);
  mct_port_set_intra_event_func(port, aec_port_intra_event);
  mct_port_set_ext_link_func(port, aec_port_ext_link);
  mct_port_set_unlink_func(port, aec_port_ext_unlink);
  mct_port_set_set_caps_func(port, aec_port_set_caps);
  mct_port_set_check_caps_reserve_func(port, aec_port_check_caps_reserve);
  mct_port_set_check_caps_unreserve_func(port, aec_port_check_caps_unreserve);

  if (port->set_caps) {
    port->set_caps(port, &caps);
  }

  return rc;
}

/** aec_port_set_stored_parm:
 *    @port: AEC port pointer
 *    @stored_params: Previous session stored parameters.
 *
 * This function stores the previous session parameters.
 *
 **/
void aec_port_set_stored_parm(mct_port_t *port, aec_stored_params_t* stored_params)
{
  aec_port_private_t *private =(aec_port_private_t *)port->port_private;

  if (!stored_params || !private) {
    AEC_ERR("aec port or init param pointer NULL");
    return;
  }

  private->stored_params = stored_params;
}

