/* aec_port_ext.c
 *
 * Copyright (c) 2015 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#include "aec_port.h"
#include "aec_ext.h"


/** aec_port_ext_update_opaque_input_params
 *
 *  @aec_port: port information
 *  @type: type of the parameter
 *  @data: payload
 *
 *  Package custom parameter inside opaque payload.
 *
 **/
static boolean aec_port_ext_update_opaque_input_params(
  aec_port_private_t *aec_port,
  int16_t type,
  q3a_custom_data_t *payload)
{
  boolean rc = FALSE;

  if (!payload->size) {
    AEC_ERR("Error: payload size zero");
    return rc;
  }

  /* Pass directly as set param call */
  aec_set_parameter_t *set_parm;
  q3a_custom_data_t input_param = {0};
  q3a_thread_aecawb_msg_t *aec_msg = (q3a_thread_aecawb_msg_t *)
    malloc(sizeof(q3a_thread_aecawb_msg_t));
  if (NULL == aec_msg) {
    AEC_ERR("Memory allocation failure!");
    return rc;
  }

  /* populate af message to post to thread */
  memset(aec_msg, 0, sizeof(q3a_thread_aecawb_msg_t));
  aec_msg->type = MSG_AEC_SET;
  set_parm = &aec_msg->u.aec_set_parm;
  set_parm->type = type;
  input_param.size = payload->size;
  input_param.data = malloc(payload->size);
  if (NULL == input_param.data) {
    AEC_ERR("Failure allocating memory to store data!");
    free(aec_msg);
    return rc;
  }
  memcpy(input_param.data, payload->data, payload->size);
  set_parm->u.aec_custom_data = input_param;
  rc = q3a_aecawb_thread_en_q_msg(aec_port->thread_data, aec_msg);

  return rc;
}

/** aec_port_ext_handle_set_parm_aec_event
 *
 *  @port: port information
 *  @evt_param: set parameter received.
 *
 *  Extend handling of AEC specific set parameters as filtered
 *  by stats port.
*/
static stats_ext_return_type aec_port_ext_handle_set_parm_aec_event(
  mct_port_t *port,
  void *evt_param)
{
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;
  boolean ret = TRUE;
  aec_port_private_t  *aec_port = (aec_port_private_t *)(port->port_private);
  aec_set_parameter_t *param = (aec_set_parameter_t *)evt_param;
  aec_ext_param_t *ext_param = aec_port->ext_param;

  /* Handle other set parameters here if required to extend. */
  switch (param->type) {
  case AEC_SET_PARAM_LED_MODE: {
    ext_param->led_mode = param->u.led_mode;
  }
    break;
  default:
    break;
  }

  return rc;
}


/** aec_port_ext_handle_set_parm_common_event
 *
 *  @port: port information
 *  @param: set parameter received.
 *
 *  Extend handling of set parameter call of type common.
 *
 **/
static stats_ext_return_type aec_port_ext_handle_set_parm_common_event(
  mct_port_t *port,
  void *evt_param)
{
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;
  boolean ret = TRUE;
  aec_port_private_t  *aec_port = (aec_port_private_t *)(port->port_private);
  stats_common_set_parameter_t *param =
      (stats_common_set_parameter_t *)evt_param;
  q3a_custom_data_t payload;

  switch (param->type) {
  case COMMON_SET_PARAM_CUSTOM: {
    /* For custom parameters from HAL, we'll save in a list and then
       send to core algorithm every frame at once during stats
       trigger. */
    payload.data = param->u.custom_param.data;
    payload.size = param->u.custom_param.size;
    ret = aec_port_ext_update_opaque_input_params(aec_port,
      AEC_SET_PARM_CUSTOM_EVT_HAL, &payload);
    if (FALSE == ret) {
      AEC_ERR("Failure handling the custom parameter!");
      rc = STATS_EXT_HANDLING_FAILURE;
    }
  }
    break;
  case COMMON_SET_PARAM_BESTSHOT: {
    /* Sample on how to handle existing HAL events */
    q3a_thread_aecawb_msg_t *aec_msg = (q3a_thread_aecawb_msg_t*)
      malloc(sizeof(q3a_thread_af_msg_t));
    if (NULL == aec_msg) {
      AEC_ERR("Memory allocation failure!");
      ret = FALSE;
    } else {
      /* populate af message to post to thread */
      memset(aec_msg, 0, sizeof(q3a_thread_aecawb_msg_t));
      aec_msg->type = MSG_AEC_SET;
      aec_msg->u.aec_set_parm.type = COMMON_SET_PARAM_BESTSHOT;
      /* This value is usually map at port to convert from HAL to algo enum types,
       * in this case passing the value directly since port doesn't know the
       * required mapping for custom algo */
      /* 3rd party could do the mapping here if prefered*/
      aec_msg->u.aec_set_parm.u.bestshot_mode = param->u.bestshot_mode;

      ret = q3a_aecawb_thread_en_q_msg(aec_port->thread_data, aec_msg);
      if (!ret) {
        AEC_ERR("Fail to queue msg");
      }
    }
    /* Marking as complete since no further processing required */
    rc = STATS_EXT_HANDLING_COMPLETE;
  }
    break;

  default: {
    rc = STATS_EXT_HANDLING_PARTIAL;
  }
    break;
  }

  return rc;
}


/** aec_port_ext_handle_set_parm_event
 *
 *  @port: port information
 *  @param: set parameter received.
 *
 *  Extend handling of set parameter call from HAL.
 *
 **/
static stats_ext_return_type aec_port_ext_handle_set_parm_event(
  mct_port_t *port,
  void *evt_param)
{
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;
  boolean ret = TRUE;
  aec_port_private_t  *aec_port = (aec_port_private_t *)(port->port_private);
  stats_set_params_type *stat_parm = (stats_set_params_type *)evt_param;

  if (!evt_param || !aec_port) {
    AEC_ERR("Invalid parameters!");
    return FALSE;
  }

  AEC_LOW("Extended handling set param event of type: %d", stat_parm->param_type);
  /* These cases can be extended to handle other parameters here if required. */
  if (stat_parm->param_type == STATS_SET_COMMON_PARAM) {
    rc = aec_port_ext_handle_set_parm_common_event(
      port, (void *)&(stat_parm->u.common_param));
  } else {
    /* Handle af specific set parameters here if different from af port handling */
    if (stat_parm->param_type == STATS_SET_Q3A_PARAM) {
      q3a_set_params_type  *q3a_param = &(stat_parm->u.q3a_param);
      if (q3a_param->type == Q3A_SET_AEC_PARAM) {
        rc = aec_port_ext_handle_set_parm_aec_event(
          port, (void *)&q3a_param->u.aec_param);
      }
    }
  }

  return rc;
} /* aec_port_ext_handle_set_parm_event */


/** aec_port_ext_handle_control_event:
 *    @port: port info
 *    @ctrl_evt: control event
 *
 * Extension of control event handling. Here OEM can further
 * handle/process control events. The control events can be OEM
 * specific or general. If it's OEM specific, OEM can either
 * process it here and send to core algorithm if required; or
 * just send the payload to core algorithm to process.
 *
 * Return stats_ext_return_type value.
 */
static stats_ext_return_type aec_port_ext_handle_control_event(
  mct_port_t *port,
  mct_event_control_t *ctrl_evt)
{
  aec_port_private_t *aec_port =
    (aec_port_private_t *)(port->port_private);
  q3a_custom_data_t payload;
  mct_custom_data_payload_t *cam_custom_ctr =
    (mct_custom_data_payload_t *)ctrl_evt->control_event_data;
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;

  switch (ctrl_evt->type) {
  case MCT_EVENT_CONTROL_CUSTOM: {
    boolean ret = FALSE;
    payload.data = cam_custom_ctr->data;
    payload.size = cam_custom_ctr->size;
    ret = aec_port_ext_update_opaque_input_params(aec_port,
      AF_SET_PARM_CUSTOM_EVT_CTRL, &payload);
    if (FALSE == ret) {
      AEC_ERR("Failure handling the custom parameter!");
      rc = STATS_EXT_HANDLING_FAILURE;
    } else {
      rc = STATS_EXT_HANDLING_COMPLETE;
    }
  }
    break;
  case MCT_EVENT_CONTROL_SET_PARM: {
    rc = aec_port_ext_handle_set_parm_event(port,
      ctrl_evt->control_event_data);
  }
    break;
  case MCT_EVENT_CONTROL_STREAMON: {
    rc = STATS_EXT_HANDLING_PARTIAL;
  }
    break;

  default: {
    rc = STATS_EXT_HANDLING_FAILURE;
  }
    break;
  }

  return rc;
} /* aec_port_ext_handle_control_event */


/** aec_port_ext_handle_module_event:
 *    @port: port info
 *    @ctrl_evt: control event
 *
 * Extension of module event handling. Here OEM can further
 * handle/process module events. The module events can be OEM
 * specific or general. If it's OEM specific, OEM can either
 * process it here and send to core algorithm if required; or
 * just send the payload to core algorithm to process.
 *
 * Return one of the return type defined by
 * stats_ext_return_type
 **/
static stats_ext_return_type aec_port_ext_handle_module_event(
  mct_port_t *port,
  mct_event_module_t *mod_evt)
{
  q3a_custom_data_t payload;
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;

  aec_port_private_t *aec_port =
    (aec_port_private_t *)(port->port_private);
  mct_custom_data_payload_t *cam_custom_evt =
    (mct_custom_data_payload_t *)mod_evt->module_event_data;

  AEC_LOW("Handle AEC module event of type: %d", mod_evt->type);

  switch (mod_evt->type) {

  case MCT_EVENT_MODULE_CUSTOM_STATS_DATA_AEC: {
    boolean ret = FALSE;
    payload.data = cam_custom_evt->data;
    payload.size = cam_custom_evt->size;
    rc = aec_port_ext_update_opaque_input_params(aec_port,
      AEC_SET_PARM_CUSTOM_EVT_MOD, &payload);
    if (FALSE == ret) {
      AEC_ERR("Failure handling the custom parameter!");
      rc = STATS_EXT_HANDLING_FAILURE;
    } else {
      rc = STATS_EXT_HANDLING_COMPLETE;
    }
  }
    break;

  case MCT_EVENT_MODULE_CUSTOM: {
    boolean ret = FALSE;
    payload.data = cam_custom_evt->data;
    payload.size = cam_custom_evt->size;
    ret = aec_port_ext_update_opaque_input_params(aec_port,
      AF_SET_PARM_CUSTOM_EVT_MOD, &payload);
    if (FALSE == ret) {
      AEC_ERR("Failure handling the custom parameter!");
      rc = STATS_EXT_HANDLING_FAILURE;
    } else {
      rc = STATS_EXT_HANDLING_COMPLETE;
    }
  }
    break;

  default: {
    AEC_LOW("Default. no action!");
  }
    break;
  }

  return rc;
} /* aec_port_ext_handle_module_event */


/** aec_port_ext_callback:
 *    @port: port info
 *    @aec_out: Output parameters from core algorithm
 *    @update: AEC updates to be sent to other modules.
 *    @output_handled: mask of output type handled here.
 *
 * Extension of AEC core callback. Here OEM can process output
 * parameters received from AF core. There might be OEM specific
 * parameters as well as standard output which OEM might want to
 * handle on its own.
 *
 * Return one of the return type defined by
 * stats_ext_return_type
 **/
static stats_ext_return_type aec_port_ext_callback(
  mct_port_t *port,
  void *core_out,
  void *update)
{
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  aec_update_t *aec_update = (aec_update_t *)update;
  aec_output_data_t *aec_out = (aec_output_data_t *)core_out;
  stats_ext_return_type ret = STATS_EXT_HANDLING_PARTIAL;

  /* handle custom params */
  /* Send the custom parameters as it is. Receiver will handle as required. */
  aec_update->aec_custom_param_update.data = aec_out->aec_custom_param.data;
  aec_update->aec_custom_param_update.size = aec_out->aec_custom_param.size;

  /* handle other parameters if required. */
  if (AEC_UPDATE == aec_out->type) {
    MCT_OBJECT_LOCK(port);
    private->aec_update_flag = TRUE;
    MCT_OBJECT_UNLOCK(port);
    ret = STATS_EXT_HANDLING_COMPLETE;

  } else if (AEC_SEND_EVENT == aec_out->type) {
    aec_ext_param_t *ext_param = private->ext_param;

    aec_port_pack_exif_info(port, aec_out);
    aec_port_pack_update(port, aec_out, 0);

    /* For triggering Flash during preflash */
    if (ext_param->led_mode == CAM_FLASH_MODE_ON)
      aec_out->stats_update.aec_update.led_needed = 1;

    /* For triggering AF during preflash */
    if (aec_out->stats_update.aec_update.use_led_estimation)
      aec_out->stats_update.aec_update.settled = 1;
    aec_port_print_log(aec_out, "CB-EXT_AEC_UP", private, -1);
    aec_port_send_event(port, MCT_EVENT_MODULE_EVENT,
      MCT_EVENT_MODULE_STATS_AEC_UPDATE,
      (void *)(&aec_out->stats_update),aec_out->stats_update.aec_update.sof_id);
    if (aec_out->need_config) {
      aec_port_configure_stats(aec_out, port);
      aec_out->need_config = 0;
    }
    ret = STATS_EXT_HANDLING_COMPLETE;
  }

  return ret;
}


/** aec_port_ext_init:
 *    @port: port info
 *    @session_id: current session id
 *
 * Extension of AEC port init.
 *
 * Return one of the return type defined by
 * stats_ext_return_type
 **/
static stats_ext_return_type aec_port_ext_init(
  mct_port_t *port,
  unsigned int session_id)
{
  (void)session_id;
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;
  aec_ext_param_t *ext_param = NULL;

  ext_param = (aec_ext_param_t *) calloc(1, sizeof(aec_ext_param_t));
  if (NULL == ext_param) {
    AEC_ERR("Error allocating memory for extension init!");
    rc = STATS_EXT_HANDLING_FAILURE;
  } else {
    /* Output of core algorithm will have void pointer to point to custom output
       parameters. Assign space to save those parameters. */
    private->aec_object.output.aec_custom_param.data =
      (void *)&ext_param->custom_output;
    private->aec_object.output.aec_custom_param.size =
      sizeof(ext_param->custom_output);
    private->ext_param = ext_param;
  }

  return rc;
}

/** aec_port_ext_deinit:
 *    @port: port info
 *
 * Extension of AEC port de-init.
 *
 * Return: none
 **/
void aec_port_ext_deinit(mct_port_t *port)
{
  aec_port_private_t *private = (aec_port_private_t *)(port->port_private);
  if (private->ext_param) {
    free(private->ext_param);
  }
}

/** aec_port_ext_update_func_table:
 *    @ptr: pointer to internal aec pointer object
 *
 * Update extendable function pointers.
 *
 * Return: True on success
 **/
boolean aec_port_ext_update_func_table(void *ptr)
{
  aec_port_private_t *private = (aec_port_private_t *)ptr;
  private->func_tbl.ext_init = aec_port_ext_init;
  private->func_tbl.ext_deinit = aec_port_ext_deinit;
  private->func_tbl.ext_callback = aec_port_ext_callback;
  private->func_tbl.ext_handle_module_event = aec_port_ext_handle_module_event;
  private->func_tbl.ext_handle_control_event = aec_port_ext_handle_control_event;

  return TRUE;
}

/**
 * aec_port_ext_is_extension_required
 *
 * @aec_libptr: Pointer to the vendor library
 * @cam_position: Camera position
 * @use_default_algo: The decision to use or not default (QC) algo is returned by this flag
 *
 * Return: TRUE is extension is required
 **/
boolean aec_port_ext_is_extension_required(void *aec_libptr,
  cam_position_t cam_position, boolean *use_default_algo)
{
  boolean rc = FALSE;
  (void)cam_position;
  *use_default_algo = FALSE;
  if (aec_libptr) {
    rc = TRUE;
  } else {
#ifdef _AEC_EXTENSION_
    *use_default_algo = TRUE;
    rc = TRUE;
#endif
  }

  return rc;
}

/**
 * aec_ext_load_function
 *
 * @aec_object: structure with function pointers to be assign
 * @aec_lib: Parameter to provide the pointer to the aec library (optional)
 * @cam_position: Camera position
 * @use_default_algo: Using or not default algo flag
 *
 *  This function is intended to be used by OEM.
 *  The OEM must use this fuction to populate the algo interface
 *  function pointers.
 *
 * Return: Handler to AEC interface
 **/
void * aec_port_ext_load_function(aec_object_t *aec_object, void *aec_lib,
  cam_position_t cam_position, boolean use_default_algo)
{
  void *aec_handle = NULL;
  (void)aec_lib;
  (void)cam_position;

  if (use_default_algo) {
    AEC_LOW("Load default algo functions");
    aec_handle = aec_port_load_function(aec_object);
  } else {
    AEC_ERR("Error: This is a DUMMY function, used only for reference");
    aec_handle = NULL;
  }

  return aec_handle;
}

/** aec_port_ext_unload_function
 *
 *    @private: Port private structure
 *
 *  This function is intended to be used by OEM.
 *  The OEM must use this fuction to free resources allocated at
 *  aec_port_ext_load_function()
 *
 * Return: void
 **/
void aec_port_ext_unload_function(aec_port_private_t *private)
{

  if (!private) {
    return;
  }

  if (private->use_default_algo) {
    aec_port_unload_function(private);
  } else {
    AEC_ERR("Error: This is a DUMMY function, used only for reference");
    /* Reset original value of interface */
    aec_port_load_dummy_default_func(&private->aec_object);
  }

  return;
}
