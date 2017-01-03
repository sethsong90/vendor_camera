/* awb_port_ext.c
 *
 * Copyright (c) 2015 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 */
#include "awb_port.h"
#include "awb_ext.h"
#include "camera_config.h"

#include <dlfcn.h>
#include "ext_awb_wrapper.h"

/** awb_port_ext_update_opaque_input_params
 *
 *  @awb_port: port information
 *  @type: type of the parameter
 *  @data: payload
 *
 *  Package custom parameter inside opaque payload.
 *
 **/
static boolean awb_port_ext_update_opaque_input_params(
  awb_port_private_t *awb_port,
  int16_t type,
  q3a_custom_data_t *payload)
{
  boolean rc = FALSE;

  if (!payload->size) {
    AWB_ERR("Error: payload size zero");
    return rc;
  }

  /* Pass directly as set param call */
  awb_set_parameter_t *set_parm;
  q3a_custom_data_t input_param = {0};
  q3a_thread_aecawb_msg_t *awb_msg = (q3a_thread_aecawb_msg_t *)
    malloc(sizeof(q3a_thread_aecawb_msg_t));
  if (NULL == awb_msg) {
    AWB_ERR("Memory allocation failure!");
    return rc;
  }

  /* populate af message to post to thread */
  memset(awb_msg, 0, sizeof(q3a_thread_aecawb_msg_t));
  awb_msg->type = MSG_AWB_SET;
  set_parm = &awb_msg->u.awb_set_parm;
  set_parm->type = type;
  input_param.size = payload->size;
    input_param.data = malloc(payload->size);
  if (NULL == input_param.data) {
    AWB_ERR("Failure allocating memory to store data!");
    free(awb_msg);
    return rc;
  }
  memcpy(input_param.data, payload->data, payload->size);
  set_parm->u.awb_custom_data = input_param;
  rc = q3a_aecawb_thread_en_q_msg(awb_port->thread_data, awb_msg);

  return rc;
}


/** awb_port_ext_handle_set_parm_awb_event
 *
 *  @port: port information
 *  @evt_param: set parameter received.
 *
 *  Extend handling of awb specific set parameters as filtered
 *  by stats port.
*/
static stats_ext_return_type awb_port_ext_handle_set_parm_awb_event(
  mct_port_t *port,
  void *evt_param)
{
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;
  boolean ret = TRUE;
  awb_port_private_t  *awb_port = (awb_port_private_t *)(port->port_private);
  awb_set_parameter_t *param =
      (awb_set_parameter_t *)evt_param;

  /* Handle other set parameters here if required to extend. */
  return rc;
}


/** awb_port_ext_handle_set_parm_common_event
 *
 *  @port: port information
 *  @param: set parameter received.
 *
 *  Extend handling of set parameter call of type common.
 *
 **/
static stats_ext_return_type awb_port_ext_handle_set_parm_common_event(
  mct_port_t *port,
  void *evt_param)
{
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;
  boolean ret = TRUE;
  awb_port_private_t  *awb_port = (awb_port_private_t *)(port->port_private);
  stats_common_set_parameter_t *param =
      (stats_common_set_parameter_t *)evt_param;
  q3a_custom_data_t payload;


  switch (param->type) {
  case COMMON_SET_PARAM_CUSTOM: {
    /* Sample on how to handle existing HAL events */
    q3a_thread_aecawb_msg_t *awb_msg = (q3a_thread_aecawb_msg_t*)
      malloc(sizeof(q3a_thread_af_msg_t));
    if (NULL == awb_msg) {
      AEC_ERR("Memory allocation failure!");
      ret = FALSE;
    } else {
      /* populate af message to post to thread */
      memset(awb_msg, 0, sizeof(q3a_thread_aecawb_msg_t));
      awb_msg->type = MSG_AWB_SET;
      awb_msg->u.awb_set_parm.type = COMMON_SET_PARAM_BESTSHOT;
      /* This value is usually map at port to convert from HAL to algo enum types,
       * in this case passing the value directly since port doesn't know the
       * required mapping for custom algo */
      /* 3rd party could do the mapping here if prefered*/
      awb_msg->u.awb_set_parm.u.awb_best_shot = param->u.bestshot_mode;

      ret = q3a_aecawb_thread_en_q_msg(awb_port->thread_data, awb_msg);
      if (!ret) {
        AWB_ERR("Fail to queue msg");
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


/** awb_port_ext_handle_set_parm_event
 *
 *  @port: port information
 *  @param: set parameter received.
 *
 *  Extend handling of set parameter call from HAL.
 *
 **/
static stats_ext_return_type awb_port_ext_handle_set_parm_event(
  mct_port_t *port,
  void *evt_param)
{
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;
  boolean ret = TRUE;
  awb_port_private_t  *awb_port = (awb_port_private_t *)(port->port_private);
  stats_set_params_type *stat_parm = (stats_set_params_type *)evt_param;

  if (!stat_parm || !awb_port) {
    AWB_ERR("Invalid parameters!");
    return FALSE;
  }

  AWB_LOW("Extended handling set param event of type: %d", stat_parm->param_type);
  /* These cases can be extended to handle other parameters here if required */
  if (stat_parm->param_type == STATS_SET_COMMON_PARAM) {
    rc = awb_port_ext_handle_set_parm_common_event(
      port, (void *)&(stat_parm->u.common_param));
  } else {
    /* Handle af specific set parameters here if different from af port handling */
    if (stat_parm->param_type == STATS_SET_Q3A_PARAM) {
      q3a_set_params_type  *q3a_param = &(stat_parm->u.q3a_param);
      if (q3a_param->type == Q3A_SET_AWB_PARAM) {
        rc = awb_port_ext_handle_set_parm_awb_event(
          port, (void *)&q3a_param->u.awb_param);
      }
    }
  }

  return rc;
} /* awb_port_ext_handle_set_parm_event */


/** awb_port_ext_handle_control_event:
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
static stats_ext_return_type awb_port_ext_handle_control_event(
  mct_port_t *port,
  mct_event_control_t *ctrl_evt)
{
  awb_port_private_t *awb_port =
    (awb_port_private_t *)(port->port_private);
  q3a_custom_data_t payload;
  mct_custom_data_payload_t *cam_custom_ctr =
    (mct_custom_data_payload_t *)ctrl_evt->control_event_data;
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;

  switch (ctrl_evt->type) {
  case MCT_EVENT_CONTROL_CUSTOM: {
    boolean ret = FALSE;
    payload.data = cam_custom_ctr->data;
    payload.size = cam_custom_ctr->size;
    ret = awb_port_ext_update_opaque_input_params(awb_port,
      AF_SET_PARM_CUSTOM_EVT_CTRL, &payload);
    if (FALSE == ret) {
      AF_ERR("Failure handling the custom parameter!");
      rc = STATS_EXT_HANDLING_FAILURE;
    } else {
      rc = STATS_EXT_HANDLING_COMPLETE;
    }
  }
    break;
  case MCT_EVENT_CONTROL_SET_PARM: {
    rc = awb_port_ext_handle_set_parm_event(port,
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
} /* awb_port_ext_handle_control_event */


/** awb_port_ext_handle_module_event:
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
static stats_ext_return_type awb_port_ext_handle_module_event(
  mct_port_t *port,
  mct_event_module_t *mod_evt)
{
  q3a_custom_data_t payload;
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;
  awb_port_private_t *awb_port =
    (awb_port_private_t *)(port->port_private);
  mct_custom_data_payload_t *cam_custom_evt =
    (mct_custom_data_payload_t *)mod_evt->module_event_data;

  AWB_LOW("Handle AWB module event of type: %d", mod_evt->type);

  switch (mod_evt->type) {
  case MCT_EVENT_MODULE_CUSTOM_STATS_DATA_AWB: {
    boolean ret = FALSE;
    payload.data = cam_custom_evt->data;
    payload.size = cam_custom_evt->size;
    rc = awb_port_ext_update_opaque_input_params(awb_port,
      AWB_SET_PARM_CUSTOM_EVT_MOD, &payload);
    if (FALSE == ret) {
      AWB_ERR("Failure handling the custom parameter!");
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
      ret = awb_port_ext_update_opaque_input_params(awb_port,
        AF_SET_PARM_CUSTOM_EVT_MOD, &payload);
      if (FALSE == ret) {
        AF_ERR("Failure handling the custom parameter!");
        rc = STATS_EXT_HANDLING_FAILURE;
      } else {
        rc = STATS_EXT_HANDLING_COMPLETE;
      }
    }
      break;
  default: {
    AWB_LOW("Default. no action!");
  }
    break;
  }

  return rc;
} /* awb_port_ext_handle_module_event */


/** awb_port_ext_callback:
 *    @port: port info
 *    @awb_out: Output parameters from core algorithm
 *    @update: awb updates to be sent to other modules.
 *
 * Extension of awb core callback. Here OEM can process output
 * parameters received from AF core. There might be OEM specific
 * parameters as well as standard output which OEM might want to
 * handle on its own.
 *
 * Return one of the return type defined by
 * stats_ext_return_type
 **/
static stats_ext_return_type awb_port_ext_callback(
  mct_port_t *port,
  void *core_out,
  void *update)
{
  awb_port_private_t *private = (awb_port_private_t *)(port->port_private);
  awb_update_t *awb_update = (awb_update_t *)update;
  awb_output_data_t *awb_out = (awb_output_data_t *)core_out;
  stats_ext_return_type ret = STATS_EXT_HANDLING_PARTIAL;

  /* Handle custom params */
  /* Send the custom parameters as it is. Receiver will handle as required */
  awb_update->awb_custom_param_update.data = awb_out->awb_custom_param.data;
  awb_update->awb_custom_param_update.size = awb_out->awb_custom_param.size;

  /* handle other parameters if required. */

  return ret;
}


/** awb_port_ext_init:
 *    @port: port info
 *    @session_id: current session id
 *
 * Extension of awb port init.
 *
 * Return one of the return type defined by
 * stats_ext_return_type
 **/
static stats_ext_return_type awb_port_ext_init(
  mct_port_t *port,
  unsigned int session_id)
{
  (void)session_id;
  awb_port_private_t *private = (awb_port_private_t *)(port->port_private);
  stats_ext_return_type rc = STATS_EXT_HANDLING_PARTIAL;
  awb_ext_param_t *ext_param = NULL;

  ext_param = (awb_ext_param_t *) calloc(1, sizeof(awb_ext_param_t));
  if (NULL == ext_param) {
    AF_ERR("Error allocating memory for extension init!");
    rc = STATS_EXT_HANDLING_FAILURE;
  } else {
    /* Output of core algorithm will have void pointer to point to custom output
    parameters. Assign space to save those parameters. */
    private->awb_object.output.awb_custom_param.data =
      (void *)&ext_param->custom_output;
    private->awb_object.output.awb_custom_param.size =
      sizeof(ext_param->custom_output);
    private->ext_param = ext_param;
  }

  return rc;
}

/** awb_port_ext_deinit:
 *    @port: port info
 *
 * Extension of awb port init.
 *
 * Return: none
 **/
static void awb_port_ext_deinit(mct_port_t *port)
{
  awb_port_private_t *private = (awb_port_private_t *)(port->port_private);
  if (private->ext_param) {
    free(private->ext_param);
  }
}

/** awb_port_ext_update_func_table:
 *    @ptr: pointer to internal awb pointer object
 *
 * Update extendable function pointers.
 *
 * Return: TRUE in sucess
 **/
boolean awb_port_ext_update_func_table(void *ptr)
{
  awb_port_private_t *private = (awb_port_private_t *)ptr;
  private->func_tbl.ext_init = awb_port_ext_init;
  private->func_tbl.ext_deinit = awb_port_ext_deinit;
  private->func_tbl.ext_callback = awb_port_ext_callback;
  private->func_tbl.ext_handle_module_event = awb_port_ext_handle_module_event;
  private->func_tbl.ext_handle_control_event =
    awb_port_ext_handle_control_event;

  return TRUE;
}

/**
 * awb_port_ext_is_extension_required
 *
 * @awb_libptr: Pointer to the vendor library
 * @cam_position: Camera position
 * @use_default_algo: The decision to use or not default (QC) algo is returned by this flag
 *
 * Return: TRUE is extension is required
 **/
boolean awb_port_ext_is_extension_required(void **awb_libptr,
  cam_position_t cam_position, boolean *use_default_algo)
{
  boolean rc = FALSE;
  (void)cam_position;
  *use_default_algo = FALSE;

  if (*awb_libptr) {
    rc = TRUE;
  } else {
#ifdef _AWB_EXTENSION_
    *awb_libptr = dlopen(_AWB_EXT_ALGO_FNAME, RTLD_NOW);
    if (*awb_libptr) {
      *use_default_algo = FALSE;
    } else {
      AWB_HIGH("dl_ret_code %s, set use_default_algo.", dlerror());
      *use_default_algo = TRUE;
    }
    AWB_HIGH("handle:%p, use_default_alg:%d", *awb_libptr, *use_default_algo);
    rc = TRUE;
#endif
  }

  return rc;
}

/**
 * awb_port_ext_load_function
 *
 * @awb_ops: structure with function pointers to be assign
 * @awb_libptr: Parameter to provide the pointer to the aec library (optional)
 * @cam_position: Camera position
 * @use_default_algo: Using or not default algo flag
 *
 *  This function is intended to be used by OEM.
 *  The OEM must use this fuction to populate the algo interface
 *  function pointers.
 *
 * Return: Handle to AWB interface library
 **/
void * awb_port_ext_load_function(awb_ops_t *awb_ops, void *awb_libptr,
  cam_position_t cam_position, boolean use_default_algo)
{
  void *awb_handle = NULL;

  if (use_default_algo) {
    AWB_HIGH("Load default algo functions");
    awb_handle = awb_port_load_function(awb_ops);
  } else {
    if (awb_libptr) {
      if (cam_position == CAM_POSITION_BACK) {
        awb_ops->init            = ext_awb_algo_rear_init;
        awb_ops->set_parameters  = ext_awb_algo_set_param;
        awb_ops->get_parameters  = ext_awb_algo_get_param;
        awb_ops->process         = ext_awb_algo_process;
        awb_ops->deinit          = ext_awb_algo_deinit;
        awb_ops->estimate_cct    = ext_awb_algo_estimate_cct;
        awb_ops->estimate_ccm    = ext_awb_algo_estimate_ccm;
        AWB_HIGH("Load External algo functions:%p, for BACK", awb_libptr);
        awb_handle = awb_libptr;
      } else {
        AWB_HIGH("Load default algo functions, for FRONT");
        awb_handle = awb_port_load_function(awb_ops);
      }
    } else {
      AWB_ERR("Error: This is a DUMMY function, used only for reference");
      awb_handle = NULL;
    }
  }

  return awb_handle;
}

/** awb_port_ext_unload_function
 *
 *    @private: Port private structure
 *
 *  This function is intended to be used by OEM.
 *  The OEM must use this fuction to free resources allocated at
 *  awb_port_ext_load_function()
 *
 * Return: void
 **/
void awb_port_ext_unload_function(awb_port_private_t *private)
{
  if (!private) {
    return;
  }

  if (private->use_default_algo) {
    awb_port_unload_function(private);
  } else {
    if (private->awb_iface_lib) {
      awb_ops_t *awb_ops = &private->awb_object.awb_ops;
      awb_ops->set_parameters = NULL;
      awb_ops->get_parameters = NULL;
      awb_ops->process = NULL;
      awb_ops->init = NULL;
      awb_ops->deinit = NULL;
      awb_ops->estimate_cct = NULL;
      awb_ops->estimate_gains = NULL;

      dlclose(private->awb_iface_lib);
      private->awb_iface_lib = NULL;
      awb_port_load_dummy_default_func(&private->awb_object.awb_ops);
      AWB_HIGH("Unload External algo");
    } else {
      AWB_ERR("Error: This is a DUMMY function, used only for reference");
      /* Reset original value of interface */
      awb_port_load_dummy_default_func(&private->awb_object.awb_ops);
    }
  }

  return;
}
