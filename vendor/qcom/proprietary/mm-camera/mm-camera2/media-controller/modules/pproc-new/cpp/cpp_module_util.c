/*============================================================================

  Copyright (c) 2013 Qualcomm Technologies, Inc. All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

============================================================================*/
#include "eztune_diagnostics.h"
#include "cpp_port.h"
#include "cpp_module.h"
#include "camera_dbg.h"
#include "cpp_log.h"

#define CPP_UPSCALE_THRESHOLD(ptr) \
   ((chromatix_cpp_type *)ptr)->up_scale_threshold
#define CPP_DOWNSCALE_THRESHOLD(ptr) \
   ((chromatix_cpp_type *)ptr)->down_scale_threshold

#if defined (CHROMATIX_308E)
int32_t cpp_hw_params_is_tnr_enabled_ext(
  chromatix_cpp_type *cpp_chromatix_ptr, cpp_hardware_params_t *hw_params,
  cpp_params_aec_trigger_info_t *aec_trigger);
int32_t cpp_hw_params_is_pbf_enabled_ext(
  chromatix_cpp_type *cpp_chromatix_ptr, cpp_hardware_params_t *hw_params,
  cpp_params_aec_trigger_info_t *aec_trigger);

#endif


/* cpp_module_util_decide_divert_by_identity:
 *   @stream_params - pointer to the stream.
 *
 * this function decide if need divert
 * return divert idenetiy, if no need to divert, return 0
 **/
uint32_t cpp_module_util_decide_divert_id(mct_module_t *module,
  cpp_module_stream_params_t *stream_params)
{
  mct_module_type_t     mod_type = MCT_MODULE_FLAG_INVALID;
  uint32_t              div_identity = 0;

  mod_type = mct_module_find_type(module, stream_params->identity);
  if (((mod_type == MCT_MODULE_FLAG_SOURCE) ||
      (mod_type == MCT_MODULE_FLAG_INDEXABLE)) &&
      (stream_params->req_frame_divert ||
      (stream_params->stream_type == CAM_STREAM_TYPE_OFFLINE_PROC))) {
    return stream_params->identity;
  }

  return 0;
}
/* cpp_module_util_check_duplicate:
 *   @stream_params - pointer to the stream.
 *
 * this function decide if two stream are duplicate
 **/
boolean cpp_module_util_check_duplicate(
  cpp_module_stream_params_t *stream_params1,
  cpp_module_stream_params_t *stream_params2)
{

  cpp_hardware_params_t        *hw_params1;
  cpp_hardware_params_t        *hw_params2;

  if (stream_params1 == NULL || stream_params2 == NULL) {
    CPP_ERR("null pointer ! stream_params1 = %p, stream_params2 = %p\n",
      stream_params1, stream_params2);
    return FALSE;
  }

  hw_params1 = &stream_params1->hw_params;
  hw_params2 = &stream_params2->hw_params;
  if(hw_params1->output_info.width == hw_params2->output_info.width &&
     hw_params1->output_info.height == hw_params2->output_info.height &&
     hw_params1->output_info.stride == hw_params2->output_info.stride &&
     hw_params1->output_info.scanline == hw_params2->output_info.scanline &&
     hw_params1->rotation == hw_params2->rotation &&
     hw_params1->mirror== hw_params2->mirror &&
     hw_params1->output_info.plane_fmt == hw_params2->output_info.plane_fmt) {
    /* make the linked streams duplicates of each other */
    CPP_HIGH("linked streams formats match: output duplication enabled\n");
    return TRUE;
  }
  return FALSE;
}

/* cpp_module_util_decide_proc_frame_per_stream:
 *   @stream_params - pointer to the stream.
 *
 *   This function is called in list traverse. It make a sum of the loads of
 *   all streams that are on.
 **/
boolean cpp_module_util_decide_proc_frame_per_stream(mct_module_t *module,
  cpp_module_session_params_t *session_params,
  cpp_module_stream_params_t *stream_params, uint32_t frame_id)
{
  boolean  is_process_stream = FALSE;
  boolean  is_sw_skip = FALSE;
  uint32_t is_frame_ready = 0;

  if (stream_params == NULL || session_params == NULL) {
    CPP_ERR(" stream param = %p session_param = %p", stream_params, session_params);
    return FALSE;
  }

  if (stream_params->is_stream_on == TRUE)  {
    is_sw_skip =
      cpp_module_check_frame_skip(stream_params, session_params, frame_id);
    if (is_sw_skip == TRUE) {
      /*if sw skip is on for this frame, skip process*/
      return FALSE;
    }

    is_process_stream = TRUE;

    /*if HAL3, need to check if HAL queue the buffer or not*/
    if (session_params->hal_version == CAM_HAL_V3 &&
        cpp_module_get_frame_valid(module, stream_params->identity,
        frame_id, PPROC_GET_STREAM_ID(stream_params->identity),
        stream_params->stream_type) == 0) {
        is_process_stream = FALSE;
    }
  } else {
    /*if stream is not on then no process stream*/
    is_process_stream = FALSE;
  }

  return is_process_stream;
}

/* cpp_module_util_update_stream_frame_id:
 *   @stream_params - pointer to the stream.
 *
 *   This function is called in list traverse. It make a sum of the loads of
 *   all streams that are on.
 **/
void cpp_module_util_update_stream_frame_id(
  cpp_module_stream_params_t *stream_params, uint32_t frame_id)
{
  uint32_t i = 0;
  cpp_module_stream_params_t *linked_stream_params = NULL;
  if (stream_params == NULL) {
    CPP_ERR(" stream param = %p", stream_params);
    return;
  }

  stream_params->cur_frame_id = frame_id;
  for (i = 0; i < CPP_MODULE_MAX_STREAMS; i++) {
    linked_stream_params = stream_params->linked_streams[i];
    if (linked_stream_params != NULL && linked_stream_params->identity) {
       linked_stream_params->cur_frame_id = frame_id;
    }
  }

  return;
}

/* cpp_module_util_check_link_streamon:
 *   @stream_params - pointer to the stream.
 *
 *   This function is called in list traverse. It make a sum of the loads of
 *   all streams that are on.
 **/
boolean cpp_module_util_check_link_streamon(cpp_module_stream_params_t *stream_params)
{

  uint32_t i = 0;
  cpp_module_stream_params_t *linked_stream_params = NULL;

  if (stream_params == NULL) {
    CPP_ERR(" stream param = %p", stream_params);
    return FALSE;
  }

  if (stream_params->is_stream_on == TRUE) {
    return TRUE;
  }

  for (i = 0; i < CPP_MODULE_MAX_STREAMS; i++) {
    linked_stream_params = stream_params->linked_streams[i];
    if (linked_stream_params != NULL && linked_stream_params->identity) {
      if (linked_stream_params->is_stream_on == TRUE) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

/*cpp_module_dump_stream_param:
 *   @stream_params - pointer to the stream.
 *
 *   This function is called in list traverse. It make a sum of the loads of
 *   all streams that are on.
 **/
void cpp_module_dump_stream_param(cpp_module_stream_params_t *stream_params)
{
  if (stream_params == NULL) {
    CPP_ERR(" stream param = %p", stream_params);
    return;
  }

  CPP_HIGH("linked stream w=%d, h=%d, st=%d, sc=%d, fmt=%d, identity=0x%x",
    stream_params->hw_params.output_info.width,
    stream_params->hw_params.output_info.height,
    stream_params->hw_params.output_info.stride,
    stream_params->hw_params.output_info.scanline,
    stream_params->hw_params.output_info.plane_fmt,
    stream_params->identity);
}

/* cpp_module_dump_all_linked_stream_info:
 *   @stream_params - pointer to the stream.
 *
 *   This function is called in list traverse. It make a sum of the loads of
 *   all streams that are on.
 **/
void cpp_module_dump_all_linked_stream_info(
  cpp_module_stream_params_t *stream_params)
{
  int i = 0;
  cpp_module_stream_params_t *linked_stream_params = NULL;

  if (stream_params == NULL) {
    CPP_ERR(" stream param = %p", stream_params);
    return;
  }

  for (i = 0; i < CPP_MODULE_MAX_STREAMS; i++) {
    linked_stream_params = stream_params->linked_streams[i];
    if (linked_stream_params != NULL && linked_stream_params->identity) {
      cpp_module_dump_stream_param(linked_stream_params);
    }
  }
}

static boolean find_port_with_identity_find_func(void *data, void *user_data)
{
  if(!data || !user_data) {
    CPP_ERR("failed, data=%p, user_data=%p\n", data, user_data);
    return FALSE;
  }
  mct_port_t *port = (mct_port_t*) data;
  uint32_t identity = *(uint32_t*) user_data;

  cpp_port_data_t *port_data = (cpp_port_data_t *) MCT_OBJECT_PRIVATE(port);
  uint32_t i;
  for(i=0; i<CPP_MAX_STREAMS_PER_PORT; i++) {
    if(port_data->stream_data[i].port_state != CPP_PORT_STATE_UNRESERVED &&
        port_data->stream_data[i].identity == identity) {
      return TRUE;
    }
  }
  return FALSE;
}

mct_port_t* cpp_module_find_port_with_identity(mct_module_t *module,
  mct_port_direction_t dir, uint32_t identity)
{
  mct_port_t *port = NULL;
  mct_list_t *templist;
  switch(dir) {
  case MCT_PORT_SRC:
    templist = mct_list_find_custom(
       MCT_MODULE_SRCPORTS(module), &identity,
        find_port_with_identity_find_func);
    if(templist) {
        port = (mct_port_t*)(templist->data);
    }
    break;
  case MCT_PORT_SINK:
    templist = mct_list_find_custom(
       MCT_MODULE_SINKPORTS(module), &identity,
        find_port_with_identity_find_func);
    if(templist) {
      port = (mct_port_t*)(templist->data);
    }
    break;
  default:
    CPP_ERR("failed, bad port_direction=%d", dir);
    return NULL;
  }
  return port;
}

boolean ack_find_func(void* data, void* userdata)
{
  if(!data || !userdata) {
    CPP_ERR("failed, data=%p, userdata=%p\n", data, userdata);
    return FALSE;
  }
  cpp_module_ack_t* cpp_ack = (cpp_module_ack_t*) data;
  cpp_module_ack_key_t* key = (cpp_module_ack_key_t*) userdata;
  if(cpp_ack->isp_buf_divert_ack.identity == key->identity &&
     cpp_ack->isp_buf_divert_ack.buf_idx == key->buf_idx) {
    return TRUE;
  }
  return FALSE;
}

cpp_module_ack_t* cpp_module_find_ack_from_list(cpp_module_ctrl_t *ctrl,
  cpp_module_ack_key_t key)
{
  mct_list_t *templist;
  templist = mct_list_find_custom(ctrl->ack_list.list, &key, ack_find_func);
  if(templist) {
    return (cpp_module_ack_t*)(templist->data);
  }
  return NULL;
}

static
boolean clk_rate_find_by_identity_func(void* data, void* userdata)
{
  if(!data || !userdata) {
    CPP_ERR("failed, data=%p, userdata=%p\n", data, userdata);
    return FALSE;
  }

  cpp_module_stream_clk_rate_t *clk_rate_obj =
    (cpp_module_stream_clk_rate_t*) data;
  uint32_t identity = *(uint32_t*)userdata;

  if(clk_rate_obj->identity == identity) {
    return TRUE;
  }
  return FALSE;
}

cpp_module_stream_clk_rate_t *
cpp_module_find_clk_rate_by_identity(cpp_module_ctrl_t *ctrl,
  uint32_t identity)
{
  mct_list_t *templist;

  templist = mct_list_find_custom(ctrl->clk_rate_list.list, &identity,
    clk_rate_find_by_identity_func);
  if(templist) {
    return (cpp_module_stream_clk_rate_t *)(templist->data);
  }
  return NULL;
}

static
boolean clk_rate_find_by_value_func(void* data, void* userdata)
{
  if(!data) {
    CPP_ERR("failed, data=%p\n", data);
    return FALSE;
  }
  cpp_module_stream_clk_rate_t *curent_clk_obj =
    (cpp_module_stream_clk_rate_t *) data;
  cpp_module_stream_clk_rate_t **clk_obj =
    (cpp_module_stream_clk_rate_t **)userdata;

  if (NULL == *clk_obj) {
    *clk_obj = curent_clk_obj;
    return TRUE;
  }

  if (((cpp_module_stream_clk_rate_t *)(*clk_obj))->total_load <
    curent_clk_obj->total_load) {
    *clk_obj = curent_clk_obj;
  }
  return TRUE;
}

cpp_module_stream_clk_rate_t *
cpp_module_find_clk_rate_by_value(cpp_module_ctrl_t *ctrl)
{
  cpp_module_stream_clk_rate_t *clk_obj = NULL;
  int32_t rc;

  rc = mct_list_traverse(ctrl->clk_rate_list.list,
    clk_rate_find_by_value_func, &clk_obj);

  if (!rc) {
    return NULL;
  }
  if (clk_obj) {
    return (clk_obj);
  }
  return NULL;
}

/* cpp_module_clk_rate_find_by_value:
 *   @data - the member of the list
 *   @userdata - pointer to the variable that collects the result.
 *
 *   This function is called in list traverse. It make a sum of the loads of
 *   all streams that are on.
 **/
static
boolean cpp_module_clk_rate_find_by_value(void* data, void* userdata)
{
  uint64_t current_clk_obj_load = 0;
  cpp_module_stream_clk_rate_t *curent_clk_obj = NULL;
  cpp_module_total_load_t *current_load = NULL;
  uint64_t input_bw;
  uint64_t output_bw;

  if(!data || !userdata) {
    CPP_ERR("failed, data=%p, userdata %p\n", data, userdata);
    return FALSE;
  }

  curent_clk_obj = (cpp_module_stream_clk_rate_t *) data;
  current_load = (cpp_module_total_load_t *)userdata;

  /* The flag is set if the duplicate output is set and
     if the linked stream is also on. */
  if (curent_clk_obj->duplication_flag) {
    current_clk_obj_load = (float)(curent_clk_obj->total_load / 2);
    current_load->duplication_load += (float) current_clk_obj_load *
      curent_clk_obj->system_overhead;
  } else {
    current_clk_obj_load = curent_clk_obj->total_load;;
  }

  input_bw = current_clk_obj_load;
  output_bw = (float) current_clk_obj_load *
      curent_clk_obj->system_overhead;

  if (curent_clk_obj->tnr_on_flag) {
    current_load->input_ref_load += input_bw;
    current_load->output_ref_load += output_bw;
  }

  /*
   * If CPP cds (dsdn) is on only chroma is fetched.
   * Account for input load.for reference load.
   */
  if (curent_clk_obj->dsdn_on_flag) {
    current_load->input_ref_load +=  input_bw;
  }

  if (curent_clk_obj->ubwc_on_flag) {
    output_bw /= COMP_RATIO_NRT;
  }

  current_load->input_bw += input_bw;
  current_load->output_bw += output_bw;
  current_load->clk += ((float)current_clk_obj_load *
    curent_clk_obj->system_overhead);
  current_load->perf_mode |= curent_clk_obj->perf_mode;

  return TRUE;
}

/* cpp_module_get_total_load_by_value:
 *   @ctrl - odule's control data
 *   @current_load - pointer to the variable that keeps the sum of all loads
 *   of the streams.
 *
 *   This function traverses through the list of streams loads.
 **/
int32_t cpp_module_get_total_load_by_value(cpp_module_ctrl_t *ctrl,
  cpp_module_total_load_t *current_load)
{
  int32_t rc = 0;

  rc = mct_list_traverse(ctrl->clk_rate_list.list,
    cpp_module_clk_rate_find_by_value, current_load);

  return rc;
}

cam_streaming_mode_t cpp_module_get_streaming_mode(mct_module_t *module,
  uint32_t identity)
{
  if (!module) {
    CPP_ERR("failed\n");
    return -EINVAL;
  }
  mct_port_t* port = cpp_module_find_port_with_identity(module, MCT_PORT_SINK,
                       identity);
  if (!port) {
    CPP_ERR("port not found, identity=0x%x\n", identity);
    return -EINVAL;
  }
  cpp_port_data_t* port_data = (cpp_port_data_t*) MCT_OBJECT_PRIVATE(port);
  uint32_t i;
  for (i=0; i<CPP_MAX_STREAMS_PER_PORT; i++) {
    if (port_data->stream_data[i].identity == identity) {
      CPP_DBG("identity 0x%x, streaming_mode %d", identity,
        port_data->stream_data[i].streaming_mode);
      return port_data->stream_data[i].streaming_mode;
    }
  }
  return CAM_STREAMING_MODE_MAX;
}

int32_t cpp_module_get_params_for_identity(cpp_module_ctrl_t* ctrl,
  uint32_t identity, cpp_module_session_params_t** session_params,
  cpp_module_stream_params_t** stream_params)
{
  if(!ctrl || !session_params || !stream_params) {
    CPP_DBG("failed, ctrl=%p, session_params=%p, stream_params=%p",
      ctrl, session_params, stream_params);
    return -EINVAL;
  }
  uint32_t session_id;
  uint32_t i,j;
  boolean success = FALSE;
  session_id = CPP_GET_SESSION_ID(identity);
  for(i=0; i < CPP_MODULE_MAX_SESSIONS; i++) {
    if(ctrl->session_params[i]) {
      if(ctrl->session_params[i]->session_id == session_id) {
        *session_params = ctrl->session_params[i];
        for(j=0; j < CPP_MODULE_MAX_STREAMS; j++) {
          if(ctrl->session_params[i]->stream_params[j]) {
            if(ctrl->session_params[i]->stream_params[j]->identity ==
                identity) {
              *stream_params = ctrl->session_params[i]->stream_params[j];
              success = TRUE;
              break;
            }
          }
        }
      }
    }
    if(success == TRUE) {
      break;
    }
  }
  if(success == FALSE) {
    CPP_HIGH("failed, identity=0x%x", identity);
    return -EFAULT;
  }
  return 0;
}

int32_t cpp_module_get_params_for_session_id(cpp_module_ctrl_t* ctrl,
  uint32_t session_id, cpp_module_session_params_t** session_params)
{
  uint32_t i = 0;
  if (!ctrl || !session_params) {
    CPP_DBG("failed, ctrl=%p, session_params=%p\n",
      ctrl, session_params);
    return -EINVAL;
  }
  *session_params = NULL;
  for (i = 0; i < CPP_MODULE_MAX_SESSIONS; i++) {
    if (ctrl->session_params[i]) {
      if (ctrl->session_params[i]->session_id == session_id) {
        *session_params = ctrl->session_params[i];
        break;
      }
    }
  }
  if(!(*session_params)) {
    CPP_ERR("failed, session_id=0x%x", session_id);
    return -EFAULT;
  }
  return 0;
}

void cpp_module_dump_stream_params(cpp_module_stream_params_t *stream_params __unused,
  const char* func __unused, uint32_t line __unused)
{
  CPP_LOW("%s:%d:---------- Dumping stream params %p ------------", func, line, stream_params);
  if(!stream_params) {
    CPP_DBG("%s:%d: failed", func, line);
    return;
  }
  CPP_LOW("%s:%d: stream_params.identity=0x%x", func, line, stream_params->identity);
  CPP_LOW("%s:%d: ---------------------------------------------------------", func, line);
}

boolean cpp_module_util_map_buffer_info(void *d1, void *d2)
{
  mct_stream_map_buf_t          *img_buf = (mct_stream_map_buf_t *)d1;
  cpp_module_stream_buff_info_t *stream_buff_info =
    (cpp_module_stream_buff_info_t *)d2;
  cpp_module_buffer_info_t      *buffer_info;
  mct_list_t                    *list_entry = NULL;

  if (!img_buf || !stream_buff_info) {
    CPP_ERR("failed img_buf=%p stream_buff_info=%p", img_buf, stream_buff_info);
    return FALSE;
  }

  if ((img_buf->buf_type == CAM_MAPPING_BUF_TYPE_OFFLINE_META_BUF) ||
      (img_buf->buf_type == CAM_MAPPING_BUF_TYPE_MISC_BUF)) {
    return TRUE;
  }

  buffer_info = malloc(sizeof(cpp_module_buffer_info_t));
  if (NULL == buffer_info) {
    CPP_ERR("malloc() failed\n");
    return FALSE;
  }

  memset((void *)buffer_info, 0, sizeof(cpp_module_buffer_info_t));

  if (img_buf->common_fd == TRUE) {
    buffer_info->fd = img_buf->buf_planes[0].fd;
    buffer_info->index = img_buf->buf_index;
    /* Need to get this information from stream info stored in module.
       But because the structure is reused for all buffer operation viz.
       (Enqueue stream buffer list / process frame) the below fields can be
       set to default */
    buffer_info->offset = 0;;
    buffer_info->native_buff =
      (img_buf->buf_type == CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF);
    buffer_info->processed_divert = FALSE;
  } else {
    CPP_ERR("error in supporting multiple planar FD\n");
    free(buffer_info);
    return FALSE;
  }

  list_entry = mct_list_append(stream_buff_info->buff_list,
    buffer_info, NULL, NULL);
  if (NULL == list_entry) {
    CPP_ERR("Error appending node\n");
    free(buffer_info);
    return FALSE;
  }

  stream_buff_info->buff_list = list_entry;
  stream_buff_info->num_buffs++;
  return TRUE;
}

boolean cpp_module_util_free_buffer_info(void *d1, void *d2)
{
  cpp_module_buffer_info_t      *buffer_info =
    (cpp_module_buffer_info_t *)d1;
  cpp_module_stream_buff_info_t *stream_buff_info =
    (cpp_module_stream_buff_info_t *)d2;

  if (!buffer_info || !stream_buff_info) {
    CPP_ERR("error buffer_info:%p stream_buff_info:%p\n",
      buffer_info, stream_buff_info);
    return FALSE;
  }

  if (stream_buff_info->num_buffs == 0) {
    CPP_ERR("error in num of buffs\n");
    return FALSE;
  }

  free(buffer_info);
  stream_buff_info->num_buffs--;
  return TRUE;
}

boolean cpp_module_util_create_hw_stream_buff(void *d1, void *d2)
{
  cpp_module_buffer_info_t        *buffer_info =
    (cpp_module_buffer_info_t *)d1;
  cpp_hardware_stream_buff_info_t *hw_strm_buff_info =
    (cpp_hardware_stream_buff_info_t *)d2;
  uint32_t num_buffs;

  if (!buffer_info || !hw_strm_buff_info) {
    CPP_ERR("error buffer_info:%p hw_strm_buff_info:%p\n",
      buffer_info, hw_strm_buff_info);
    return FALSE;
  }

  /* We make an assumption that a linera array will be provided */
  num_buffs = hw_strm_buff_info->num_buffs;
  hw_strm_buff_info->buffer_info[num_buffs].fd =
    (unsigned long)buffer_info->fd;
  hw_strm_buff_info->buffer_info[num_buffs].index = buffer_info->index;
  hw_strm_buff_info->buffer_info[num_buffs].offset = buffer_info->offset;
  hw_strm_buff_info->buffer_info[num_buffs].native_buff =
    buffer_info->native_buff;
  hw_strm_buff_info->buffer_info[num_buffs].processed_divert =
    buffer_info->processed_divert;
  hw_strm_buff_info->buffer_info[num_buffs].identity =
    hw_strm_buff_info->identity;

  hw_strm_buff_info->num_buffs++;
  return TRUE;
}

/* cpp_module_invalidate_q_traverse_func:
 *
 * Invalidates queue entry and adds ack_key in key_list base on identity.
 *
 **/
boolean cpp_module_invalidate_q_traverse_func(void* qdata, void* userdata)
{
  if (!qdata || !userdata) {
    CPP_ERR("failed, qdata=%p input=%p\n", qdata, userdata);
    return FALSE;
  }
  void** input = (void**)userdata;
  cpp_module_event_t* cpp_event = (cpp_module_event_t *) qdata;
  cpp_module_ctrl_t*  ctrl = (cpp_module_ctrl_t *) input[0];
  uint32_t identity = *(uint32_t*) input[1];
  mct_list_t **key_list = (mct_list_t **) input[2];
  uint8_t do_ack = FALSE;

  if(!ctrl) {
    CPP_ERR("failed, ivalid ctrl\n");
    return FALSE;
  }
  /* invalidate the event and add key in key list */
  if(cpp_event->type != CPP_MODULE_EVENT_CLOCK) {
    if ((cpp_event->type != CPP_MODULE_EVENT_PARTIAL_FRAME) &&
      (cpp_event->ack_key.identity == identity)) {
      cpp_event->invalid = TRUE;
      do_ack = TRUE;
    } else if ((cpp_event->type == CPP_MODULE_EVENT_PARTIAL_FRAME) &&
      (cpp_event->u.partial_frame.frame->identity == identity)) {
      cpp_event->invalid = TRUE;
    }
    if (cpp_event->invalid == TRUE && do_ack) {
      cpp_module_ack_key_t *key =
        (cpp_module_ack_key_t *) malloc (sizeof(cpp_module_ack_key_t));
      if(!key) {
        CPP_ERR("failed, invalid key\n");
        return FALSE;
      }
      memcpy(key, &(cpp_event->ack_key), sizeof(cpp_module_ack_key_t));
      *key_list = mct_list_append(*key_list, key, NULL, NULL);
    }
  }
  return TRUE;
}


/* cpp_module_release_ack_traverse_func:
 *
 * traverses through the list of keys and updates ACK corresponding to the
 * key.
 *
 **/
boolean cpp_module_release_ack_traverse_func(void* data, void* userdata)
{
  int32_t rc;
  if (!data || !userdata) {
    CPP_ERR("failed, data=%p userdata=%p\n", data, userdata);
    return FALSE;
  }
  cpp_module_ack_key_t* key = (cpp_module_ack_key_t *) data;
  cpp_module_ctrl_t*  ctrl = (cpp_module_ctrl_t *) userdata;
  rc = cpp_module_do_ack(ctrl, *key);
  if(rc < 0) {
    CPP_ERR("failed, identity=0x%x\n", key->identity);
      return FALSE;
  }
  return TRUE;
}

/* cpp_module_key_list_free_traverse_func:
 *
 * traverses through the list of keys and frees the data.
 *
 **/
boolean cpp_module_key_list_free_traverse_func(void* data, void* userdata __unused)
{
  cpp_module_ack_key_t* key = (cpp_module_ack_key_t *) data;
  free(key);
  return TRUE;
}

/** cpp_module_update_hfr_skip:
 *
 *  Description:
 *    Based on input and output fps, calculte the skip count
 *    according to this formula,
 *      count = floor(input/output) - 1, if input > output
 *            = 0, otherwise
 *
 **/
int32_t cpp_module_update_hfr_skip(cpp_module_stream_params_t *stream_params)
{
  uint8_t cal_batchsize, linked_stream_batchsize = 0;
  cpp_module_stream_params_t *linked_stream_params = NULL;
  uint32_t i = 0;

  if(!stream_params) {
    CPP_ERR("failed");
    return -EINVAL;
  }

  stream_params->hfr_skip_info.skip_count =
    floor(stream_params->hfr_skip_info.input_fps /
          stream_params->hfr_skip_info.output_fps) - 1;

  /* For preview, If streams are bundled and the linked stream is a batch ,
   * then calculate skip count
   */

  /* support only one batch pattern in one port,
    loop for linked streams and find BATCH stream,
    use it to recalculate the skip pattern */
  if(stream_params->stream_type == CAM_STREAM_TYPE_PREVIEW) {
    /*find a batch mode stream*/
    for(i = 0; i < CPP_MODULE_MAX_STREAMS; i++) {
      linked_stream_params = stream_params->linked_streams[i];
      if (linked_stream_params && linked_stream_params->identity != 0) {
        if (linked_stream_params->stream_info->streaming_mode  == CAM_STREAMING_MODE_BATCH) {
          /*only one batch mode per port/per linked stream group*/
          break;
        }
      }
    }

    if(linked_stream_params && linked_stream_params->stream_info &&
      linked_stream_params->stream_info->streaming_mode ==
      CAM_STREAMING_MODE_BATCH) {
      CPP_HIGH("Batchmode enabled ipfps = %f opfps =%f batchsize = %d",
        stream_params->hfr_skip_info.input_fps,
        stream_params->hfr_skip_info.output_fps,
        linked_stream_params->stream_info-> user_buf_info.frame_buf_cnt);

      cal_batchsize = floor(stream_params->hfr_skip_info.input_fps /
        stream_params->hfr_skip_info.output_fps);

      linked_stream_batchsize = linked_stream_params->
        stream_info->user_buf_info.frame_buf_cnt;

      linked_stream_params->hfr_skip_info.skip_count = 0;
      if(cal_batchsize > linked_stream_batchsize) {
        stream_params->hfr_skip_info.skip_count =
          (cal_batchsize / linked_stream_batchsize) - 1;
      } else {
        /* In this case don't skip any */
        stream_params->hfr_skip_info.skip_count = 0;
      }
    }
  }

  if(stream_params->hfr_skip_info.skip_count < 0) {
    stream_params->hfr_skip_info.skip_count = 0;
  }

  CPP_DBG("Skip count = %d\n", stream_params->hfr_skip_info.skip_count);
  return 0;
}

/** cpp_module_set_output_duplication_flag:
 *
 *  Description:
 *    Based on stream's dimension info and existance of a linked
 *    stream, decide if output-duplication feature of cpp
 *    hardware can be utilized.
 *
 **/
int32_t cpp_module_set_output_duplication_flag(
  cpp_module_stream_params_t *stream_params)
{
  if(!stream_params) {
    CPP_ERR("failed");
    return -EINVAL;
  }
  stream_params->hw_params.duplicate_output = FALSE;
  stream_params->hw_params.duplicate_identity = 0x00;

  CPP_HIGH(
    "current stream w=%d, h=%d, st=%d, sc=%d, fmt=%d, identity=0x%x",
    stream_params->hw_params.output_info.width,
    stream_params->hw_params.output_info.height,
    stream_params->hw_params.output_info.stride,
    stream_params->hw_params.output_info.scanline,
    stream_params->hw_params.output_info.plane_fmt,
    stream_params->identity);

  /* if there is no linked stream, no need for duplication */
  if(stream_params->num_linked_streams == 0) {
    CPP_DBG("info: no linked stream");
    return 0;
  }

  cpp_module_dump_all_linked_stream_info(stream_params);

  return 0;
}

#ifdef ASF_OSD
int32_t cpp_module_util_convert_asf_region_type(
  cam_asf_trigger_regions_t *bus_regions, cpp_params_asf_region_t hw_parm_region)
{
    if(!bus_regions) {
      CPP_ERR("failed");
      return -EINVAL;
    }
    switch(hw_parm_region) {
    case CPP_PARAM_ASF_REGION_1:
      bus_regions->region1 = CPP_PARAM_ASF_REGION1;
      bus_regions->region2 = CPP_PARAM_ASF_REGION1;
      break;
    case CPP_PARAM_ASF_REGION_12_INTERPOLATE:
      bus_regions->region1 = CPP_PARAM_ASF_REGION1;
      bus_regions->region2 = CPP_PARAM_ASF_REGION2;
      break;
    case CPP_PARAM_ASF_REGION_2:
      bus_regions->region1 = CPP_PARAM_ASF_REGION2;
      bus_regions->region2 = CPP_PARAM_ASF_REGION2;
      break;
    case CPP_PARAM_ASF_REGION_23_INTERPOLATE:
      bus_regions->region1 = CPP_PARAM_ASF_REGION2;
      bus_regions->region2 = CPP_PARAM_ASF_REGION3;
      break;
    case CPP_PARAM_ASF_REGION_3:
      bus_regions->region1 = CPP_PARAM_ASF_REGION3;
      bus_regions->region2 = CPP_PARAM_ASF_REGION3;
      break;
    case CPP_PARAM_ASF_REGION_34_INTERPOLATE:
      bus_regions->region1 = CPP_PARAM_ASF_REGION3;
      bus_regions->region2 = CPP_PARAM_ASF_REGION4;
      break;
    case CPP_PARAM_ASF_REGION_4:
      bus_regions->region1 = CPP_PARAM_ASF_REGION4;
      bus_regions->region2 = CPP_PARAM_ASF_REGION4;
      break;
    case CPP_PARAM_ASF_REGION_45_INTERPOLATE:
      bus_regions->region1 = CPP_PARAM_ASF_REGION4;
      bus_regions->region2 = CPP_PARAM_ASF_REGION5;
      break;
    case CPP_PARAM_ASF_REGION_5:
      bus_regions->region1 = CPP_PARAM_ASF_REGION5;
      bus_regions->region2 = CPP_PARAM_ASF_REGION5;
      break;
    case CPP_PARAM_ASF_REGION_56_INTERPOLATE:
      bus_regions->region1 = CPP_PARAM_ASF_REGION5;
      bus_regions->region2 = CPP_PARAM_ASF_REGION6;
      break;
    case CPP_PARAM_ASF_REGION_6:
      bus_regions->region1 = CPP_PARAM_ASF_REGION6;
      bus_regions->region2 = CPP_PARAM_ASF_REGION6;
      break;
#ifdef LDS_ENABLE
    case CPP_PARAM_ASF_REGION_7:
      bus_regions->region1 = CPP_PARAM_ASF_REGION7;
      bus_regions->region2 = CPP_PARAM_ASF_REGION7;
      break;
    case CPP_PARAM_ASF_REGION_78_INTERPOLATE:
      bus_regions->region1 = CPP_PARAM_ASF_REGION7;
      bus_regions->region2 = CPP_PARAM_ASF_REGION8;
      break;
    case CPP_PARAM_ASF_REGION_8:
      bus_regions->region1 = CPP_PARAM_ASF_REGION8;
      bus_regions->region2 = CPP_PARAM_ASF_REGION8;
      break;
#endif
    default:
      CPP_ERR("failed, invalied region %d", hw_parm_region);
      return -EINVAL;
    }
    return 0;
}
#endif
/** cpp_module_get_divert_info:
 *
 *  Description:
 *    Based on streamon state of "this" stream and "linked"
 *      stream fetch the divert configuration sent by pproc
 *      module or otherwise return NULL
 *
 **/
pproc_divert_info_t *cpp_module_get_divert_info(uint32_t *identity_list,
  uint32_t identity_list_size, cpp_divert_info_t *cpp_divert_info)
{
  uint32_t i = 0, j = 0;
  uint8_t identity_mapped_idx = 0;
  uint8_t divert_info_config_table_idx = 0;
  pproc_divert_info_t *divert_info = NULL;

  /* Loop through the identity list to determine the corresponding index
     in the cpp_divert_info */
  for (i = 0; i < identity_list_size; i++) {
    if (identity_list[i] != PPROC_INVALID_IDENTITY) {
      /* Search the requested identity from the cpp_divert_info table */
      identity_mapped_idx = 0;
      for (j = 0; j < CPP_MAX_STREAMS_PER_PORT; j++) {
        if (cpp_divert_info->identity[j] == identity_list[i]) {
          identity_mapped_idx = j;
          break;
        }
      }
      if (j < CPP_MAX_STREAMS_PER_PORT) {
        divert_info_config_table_idx |= (1 << identity_mapped_idx);
      }
    }
  }

  if(divert_info_config_table_idx) {
    divert_info = &cpp_divert_info->config[divert_info_config_table_idx];
  }
  return divert_info;
}

int32_t cpp_module_util_post_diag_to_bus(mct_module_t *module,
  ez_pp_params_t *cpp_params, uint32_t identity)
{
  mct_bus_msg_t bus_msg_cpp_diag;
  mct_event_t event;

  bus_msg_cpp_diag.type = MCT_BUS_MSG_PP_CHROMATIX_LITE;
  bus_msg_cpp_diag.size = sizeof(ez_pp_params_t);
  bus_msg_cpp_diag.msg = (void *)cpp_params;
  bus_msg_cpp_diag.sessionid = (identity & 0xFFFF0000) >> 16;

  /* CPP being a sub-module inside pproc it cannot directly access mct */
  /* Create an event so that PPROC can post it to MCT */
  event.identity = identity;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.direction = MCT_EVENT_UPSTREAM;
  event.u.module_event.type = MCT_EVENT_MODULE_PP_SUBMOD_POST_TO_BUS;
  event.u.module_event.module_event_data = (void *)&bus_msg_cpp_diag;

  if (cpp_module_send_event_upstream(module, &event) != 0) {
    CPP_ERR("error posting diag to bus\n");
  }
  return 0;
}

int32_t cpp_module_util_update_session_diag_params(mct_module_t *module,
  cpp_hardware_params_t* hw_params)
{
  cpp_module_stream_params_t *stream_params = NULL;
  cpp_module_session_params_t *session_params = NULL;
  cpp_module_ctrl_t *ctrl;

  /* Check whether the current stream type needs update diag params */
   if ((hw_params->stream_type != CAM_STREAM_TYPE_OFFLINE_PROC) &&
     (hw_params->stream_type != CAM_STREAM_TYPE_SNAPSHOT) &&
     (hw_params->stream_type != CAM_STREAM_TYPE_PREVIEW)){
     return 0;
   }

  ctrl = (cpp_module_ctrl_t*) MCT_OBJECT_PRIVATE(module);
  if(!ctrl) {
    CPP_ERR("failed\n");
    return -EFAULT;
  }

  /* Pick up session params and update diag params */
  cpp_module_get_params_for_identity(ctrl, hw_params->identity,
    &session_params, &stream_params);
  if(!session_params) {
    CPP_ERR("failed\n");
    return -EFAULT;
  }

  if ((hw_params->stream_type == CAM_STREAM_TYPE_OFFLINE_PROC) ||
    (hw_params->stream_type == CAM_STREAM_TYPE_SNAPSHOT)){
    memcpy(&session_params->diag_params.snap_asf7x7, &hw_params->asf_diag,
      sizeof(asfsharpness7x7_t));
    memcpy(&session_params->diag_params.snap_asf9x9, &hw_params->asf9x9_diag,
      sizeof(asfsharpness9x9_t));
    memcpy(&session_params->diag_params.snap_wnr, &hw_params->wnr_diag,
      sizeof(wavelet_t));
  } else if (hw_params->stream_type == CAM_STREAM_TYPE_PREVIEW) {
    memcpy(&session_params->diag_params.prev_asf7x7, &hw_params->asf_diag,
      sizeof(asfsharpness7x7_t));
    memcpy(&session_params->diag_params.prev_asf9x9, &hw_params->asf9x9_diag,
      sizeof(asfsharpness9x9_t));
    memcpy(&session_params->diag_params.prev_wnr, &hw_params->wnr_diag,
      sizeof(wavelet_t));
  }

  if (hw_params->diagnostic_enable) {
    if (hw_params->stream_type == CAM_STREAM_TYPE_OFFLINE_PROC) {
      cpp_module_hw_cookie_t *cookie;
      meta_data_container    *meta_datas;
      cookie = (cpp_module_hw_cookie_t *)hw_params->cookie;
      if (cookie->meta_datas) {
        meta_datas = cookie->meta_datas;
        if (meta_datas->mct_meta_data) {
          add_metadata_entry(CAM_INTF_META_CHROMATIX_LITE_PP,
            sizeof(ez_pp_params_t), &session_params->diag_params,
            meta_datas->mct_meta_data);
        } else {
          CPP_ERR("mct meta data is NULL\n");
        }
      } else {
        CPP_DBG("meta_data container is NULL\n");
      }
    } else {
      /* Post the updated diag params to bus if diagnostics is enabled */
      cpp_module_util_post_diag_to_bus(module, &session_params->diag_params,
        hw_params->identity);
    }
  }
  return 0;
}

int32_t cpp_module_util_post_crop_info(
  mct_module_t *module,
  cpp_hardware_params_t* hw_params,
  cpp_module_stream_params_t *stream_params)
{
  cam_stream_crop_info_t   crop_info;

  /* Output CPP crop to metadata for snapshots.
   * Required by dual-camera to calculate FOV changes.
   */
  if(stream_params->stream_type != CAM_STREAM_TYPE_SNAPSHOT &&
     stream_params->stream_type != CAM_STREAM_TYPE_OFFLINE_PROC) {
    return 0;
  }

  memset(&crop_info, 0, sizeof(crop_info));
  crop_info.stream_id       = PPROC_GET_STREAM_ID(stream_params->identity);
  crop_info.roi_map.left    = 0;
  crop_info.roi_map.top     = 0;
  crop_info.roi_map.width   = hw_params->input_info.width;
  crop_info.roi_map.height  = hw_params->input_info.height;
  crop_info.crop.left       = hw_params->crop_info.process_window_first_pixel;
  crop_info.crop.top        = hw_params->crop_info.process_window_first_line;
  crop_info.crop.width      = hw_params->crop_info.process_window_width;
  crop_info.crop.height     = hw_params->crop_info.process_window_height;

  CPP_CROP_LOW("CROP_INFO_CPP: str_id %x crop (%d, %d, %d,%d) ==> (%d,%d,%d,%d)",
    crop_info.stream_id,
    crop_info.roi_map.left, crop_info.roi_map.top,
    crop_info.roi_map.width, crop_info.roi_map.height,
    crop_info.crop.left, crop_info.crop.top,
    crop_info.crop.width, crop_info.crop.height);

  /* Put crop info into the metadata.
   * For reprocess update the metadata directly,
   * for snapshot send the data to MCT for update
   */
  if (hw_params->stream_type == CAM_STREAM_TYPE_OFFLINE_PROC) {
    cpp_module_hw_cookie_t  *cookie = NULL;
    meta_data_container     *meta_datas = NULL;
    metadata_buffer_t       *meta_data_mct = NULL;

    cookie = hw_params->cookie;
    if(cookie != NULL) {
      meta_datas = (meta_data_container *)cookie->meta_datas;
    }
    if(meta_datas != NULL) {
      meta_data_mct = (metadata_buffer_t *)meta_datas->mct_meta_data;
    }

    if (meta_data_mct != NULL) {
      add_metadata_entry(CAM_INTF_META_SNAP_CROP_INFO_CPP,
        sizeof(crop_info), &crop_info, meta_data_mct);
    } else {
      CPP_ERR("meta_data container is NULL\n");
    }
  } else { /* snapshot stream - post to MCT */
    mct_bus_msg_t bus_msg;

    memset(&bus_msg, 0, sizeof(mct_bus_msg_t));
    bus_msg.sessionid = PPROC_GET_SESSION_ID(stream_params->identity);
    bus_msg.type = MCT_BUS_MSG_SNAP_CROP_INFO_PP;
    bus_msg.size = sizeof(crop_info);
    bus_msg.msg = &crop_info;

    if (cpp_module_util_post_to_bus(module, &bus_msg, stream_params->identity) != 0) {
      CPP_ERR("error posting to bus\n");
    }
  }

  return 0;
}

int32_t cpp_module_util_handle_frame_drop(mct_module_t *module,
  cpp_module_stream_params_t* stream_params, uint32_t frame_id,
  cam_hal_version_t hal_version)
{
  int32_t                   rc = 0;
  uint32_t                  stream_frame_valid = 0;
  mct_event_t               mct_event;
  mct_event_frame_request_t frame_req;
  cpp_hardware_cmd_t        cmd;
  cpp_hardware_event_data_t hw_event_data;

  cpp_module_ctrl_t* ctrl = (cpp_module_ctrl_t *)MCT_OBJECT_PRIVATE(module);
  if(!ctrl) {
    CPP_ERR("failed\n");
    return -EINVAL;
  }


  if (!stream_params->hw_params.expect_divert ||
    (stream_params->is_stream_on != TRUE)) {
    return 0;
  }

  /* Check whether frame processing is valid. Only then the buffer
     is available in stream queue */
  if ((hal_version == CAM_HAL_V3) &&
    (stream_params->is_stream_on == TRUE)) {
    stream_frame_valid = cpp_module_get_frame_valid(module,
      stream_params->identity, frame_id,
      PPROC_GET_STREAM_ID(stream_params->identity),
      stream_params->stream_type);
  }

  if (stream_frame_valid) {
    /* Send upstream event to indicate frame drop */
    memset(&mct_event, 0, sizeof(mct_event));
    mct_event.u.module_event.type = MCT_EVENT_MODULE_FRAME_DROP_NOTIFY;
    memset(&frame_req, 0, sizeof(frame_req));
    frame_req.frame_index = frame_id;
    frame_req.stream_ids.num_streams = 1;
    frame_req.stream_ids.streamID[0] = (stream_params->identity & 0x0000FFFF);
    mct_event.u.module_event.module_event_data = (void *)&frame_req;
    mct_event.type = MCT_EVENT_MODULE_EVENT;
    mct_event.identity = stream_params->identity;
    mct_event.direction = MCT_EVENT_UPSTREAM;
    rc = cpp_module_send_event_upstream(module, &mct_event);
    if (rc < 0) {
      CPP_ERR("failed");
      return -EFAULT;
    }

    /* TODO: Flush the queue parameters if any */

    /* Send the IOCTL to kernel to pop the buffer */
    rc = cpp_module_util_pop_buffer(ctrl, stream_params, frame_id);
  }

  return 0;
}

int32_t cpp_module_util_pop_buffer(cpp_module_ctrl_t* ctrl,
  cpp_module_stream_params_t* stream_params, uint32_t frame_id)
{
  int32_t                   rc = 0;
  cpp_hardware_cmd_t        cmd;
  cpp_hardware_event_data_t hw_event_data;

  if(!ctrl) {
    CPP_ERR("failed\n");
    return -EINVAL;
  }
  CPP_ERR("Sending IOCTL to pop \n");
  /* Send the IOCTL to kernel to pop the buffer */
  memset(&hw_event_data, 0, sizeof(hw_event_data));
  cmd.type = CPP_HW_CMD_POP_STREAM_BUFFER;
  cmd.u.event_data = &hw_event_data;
  hw_event_data.frame_id = frame_id;
  hw_event_data.identity = stream_params->identity;
  rc = cpp_hardware_process_command(ctrl->cpphw, cmd);
  if (rc < 0) {
    CPP_ERR("failed stream buffer pop, iden:0x%x frmid:%d\n",
     stream_params->identity, frame_id);
    return rc;
  }

  return 0;
}



/** cpp_module_utill_free_queue_data:
 *
 *  @data: list data
 *
 *  @user_data : user data (NULL)
 *
 *  Free list data
 **/
boolean cpp_module_utill_free_queue_data(void *data, void *user_data __unused)
{
  cpp_frame_ctrl_data_t *frame_ctrl_data = data;
  if (frame_ctrl_data) {
    if (MCT_EVENT_MODULE_EVENT == frame_ctrl_data->mct_type)
      free(frame_ctrl_data->u.module_event.module_event_data);
    else
      free(frame_ctrl_data->u.ctrl_param.parm_data);
    free(frame_ctrl_data);
  }
  return TRUE;
}

int32_t cpp_module_util_post_to_bus(mct_module_t *module,
  mct_bus_msg_t *bus_msg, uint32_t identity)
{
  int32_t rc = 0;
  mct_event_t event;

  /* CPP being a sub-module inside pproc it cannot directly access mct */
  /* Create an event so that PPROC can post it to MCT */
  event.identity = identity;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.direction = MCT_EVENT_UPSTREAM;
  event.u.module_event.type = MCT_EVENT_MODULE_PP_SUBMOD_POST_TO_BUS;
  event.u.module_event.module_event_data = (void *)bus_msg;

  CPP_DBG("type:%d size:%d\n", bus_msg->type, bus_msg->size);
  rc = cpp_module_send_event_upstream(module, &event);
  if (rc < 0) {
    CPP_ERR("error posting msg to bus\n");
  }
  return rc;
}

int32_t cpp_module_util_post_metadata_to_bus(mct_module_t *module,
  cam_intf_parm_type_t type, void *parm_data, uint32_t identity)
{
  int32_t rc = 0;
  mct_bus_msg_t bus_msg;

  if (!module || !parm_data) {
    CPP_ERR("failed: module %p parm_data %p", module, parm_data);
    return -EINVAL;
  }

  memset(&bus_msg, 0x0, sizeof(bus_msg));
  bus_msg.sessionid = (identity >> 16) & 0xFFFF;
  bus_msg.msg = (void *)parm_data;
  bus_msg.size = 0;

  switch (type) {
  case CAM_INTF_PARM_SHARPNESS: {
    bus_msg.type = MCT_BUS_MSG_SET_SHARPNESS;
    bus_msg.size = sizeof(int32_t);
    CPP_ASF_DBG("CAM_INTF_PARM_SHARPNESS size:%d value:%d\n",
      bus_msg.size, *(int32_t *)parm_data);
  }
    break;
  case CAM_INTF_PARM_EFFECT: {
    bus_msg.type = MCT_BUS_MSG_SET_EFFECT;
    bus_msg.size = sizeof(int32_t);
    CPP_ASF_DBG("CAM_INTF_PARM_EFFECT size:%d value:%d\n",
      bus_msg.size, *(int32_t *)parm_data);
  }
    break;
  case CAM_INTF_META_EDGE_MODE: {
    bus_msg.type = MCT_BUS_MSG_SET_EDGE_MODE;
    bus_msg.size = sizeof(cam_edge_application_t);
    CPP_ASF_DBG("CAM_INTF_META_EDGE_MODE size:%d mode:%d strength:%d\n",
      bus_msg.size, ((cam_edge_application_t *)parm_data)->edge_mode,
      ((cam_edge_application_t *)parm_data)->sharpness);
  }
    break;
  case CAM_INTF_META_NOISE_REDUCTION_MODE: {
    bus_msg.type = MCT_BUS_MSG_SET_NOISE_REDUCTION_MODE;
    bus_msg.size = sizeof(int32_t);
    CPP_DENOISE_DBG("CAM_INTF_META_NOISE_REDUCTION_MODE size:%d value:%d\n",
      bus_msg.size, *(int32_t *)parm_data);
  }
    break;
  case CAM_INTF_PARM_WAVELET_DENOISE: {
    bus_msg.type = MCT_BUS_MSG_SET_WAVELET_DENOISE;
    bus_msg.size = sizeof(cam_denoise_param_t);
    CPP_DENOISE_DBG("CAM_INTF_PARM_WAVELET_DENOISE size:%d enable:%d"
      "plates:%d\n", bus_msg.size,
      ((cam_denoise_param_t *)parm_data)->denoise_enable,
      ((cam_denoise_param_t *)parm_data)->process_plates);
  }
    break;
  case CAM_INTF_PARM_TEMPORAL_DENOISE: {
    bus_msg.type = MCT_BUS_MSG_SET_TEMPORAL_DENOISE;
    bus_msg.size = sizeof(cam_denoise_param_t);
    CPP_TNR_DBG("CAM_INTF_PARM_TEMPORAL_DENOISE size:%d enable:%d"
      "plates:%d\n", bus_msg.size,
      ((cam_denoise_param_t *)parm_data)->denoise_enable,
      ((cam_denoise_param_t *)parm_data)->process_plates);
  }
    break;
  case CAM_INTF_PARM_FPS_RANGE: {
    bus_msg.type = MCT_BUS_MSG_SET_FPS_RANGE;
    bus_msg.size = sizeof(cam_fps_range_t);
    CPP_META_DBG("CAM_INTF_PARM_FPS_RANGE size:%d min_fps:%f max_fps:%f\n",
      bus_msg.size, ((cam_fps_range_t *)parm_data)->min_fps,
      ((cam_fps_range_t *)parm_data)->max_fps);
  }
    break;
  case CAM_INTF_PARM_ROTATION: {
    bus_msg.type = MCT_BUS_MSG_SET_ROTATION;
    bus_msg.size = sizeof(cam_rotation_info_t);
    CPP_META_DBG("CAM_INTF_PARM_ROTATION size:%d rotation:%x, dev_rotation:%x\n",
      bus_msg.size,
     ((cam_rotation_info_t *)parm_data)->rotation,
     ((cam_rotation_info_t *)parm_data)->device_rotation);
  }
    break;
  case CAM_INTF_PARM_CDS_MODE: {
    bus_msg.type = MCT_BUS_MSG_SET_CDS;
    bus_msg.size = sizeof(int32_t);
    CPP_DENOISE_DBG("CAM_INTF_PARM_CDS_MODE size:%d value:%d\n",
      bus_msg.size, *(int32_t *)parm_data);
    break;
  }
  case CAM_INTF_META_IMG_DYN_FEAT: {
    bus_msg.type = MCT_BUS_MSG_SET_IMG_DYN_FEAT;
    bus_msg.size = sizeof(cam_dyn_img_data_t);
    CPP_DENOISE_DBG("CAM_INTF_META_IMG_DYN_FEAT size:%d value:%llu\n",
      bus_msg.size,
      ((cam_dyn_img_data_t *)parm_data)->dyn_feature_mask);
    break;
  }
  default:
    bus_msg.type = MCT_BUS_MSG_MAX;
    bus_msg.size = 0;
    break;
  }

  if (bus_msg.size) {
    rc = cpp_module_util_post_to_bus(module, &bus_msg, identity);
    if (rc < 0) {
      CPP_ERR("failed to post meta to bus: type=%d\n", bus_msg.type);
    }
  }
  return rc;
}

/** cpp_module_utils_fetch_native_bufs:
 *
 *  @data - This parameter holds the current member of the list.
 *  @user_data - This parameter holds the user data to be set.
 *
 *  This function allocates and populates buf_holder structure with data for
 *  the current native buffer. It appends the allocated structure to buffer
 *  array.
 *
 *  Return: Returns 0 at success.
 **/
boolean cpp_module_utils_fetch_native_bufs(void *data, void *user_data) {

  pp_frame_buffer_t *img_buf = (pp_frame_buffer_t *)data;
  cpp_hardware_native_buff_array *buffer_array =
    (cpp_hardware_native_buff_array *)user_data;
  uint32_t i;

  if (!img_buf || !buffer_array) {
    CPP_ERR("Fail to fetch native buffers");
    return FALSE;
  }

  memset(img_buf->vaddr, 0x80, img_buf->ion_alloc[0].len);

  for (i = 0; i < CPP_TNR_SCRATCH_BUFF_COUNT; i++) {
    if (buffer_array->buff_array[i].fd == 0) {
      buffer_array->buff_array[i].fd = img_buf->fd;
      buffer_array->buff_array[i].index = img_buf->buffer.index;
      buffer_array->buff_array[i].native_buff = 1;
      buffer_array->buff_array[i].offset = 0;
      buffer_array->buff_array[i].processed_divert = 0;
      buffer_array->buff_array[i].vaddr = img_buf->vaddr;
      buffer_array->buff_array[i].alloc_len = img_buf->ion_alloc[0].len;
      break;
    }
  }

  if (i >= CPP_TNR_SCRATCH_BUFF_COUNT) {
    CPP_ERR("Array is full\n");
    return FALSE;
  }

  return TRUE;
}

int32_t cpp_module_port_mapping(mct_module_t *module, mct_port_direction_t dir,
  mct_port_t *port, uint32_t identity)
{
  uint16_t stream_id;
  uint16_t session_id;
  cpp_module_ctrl_t *ctrl;
  uint16_t dir_idx = 0;

  if (!module) {
    CPP_ERR("Invalid pointers module:%p", module);
    return -EINVAL;
  }

  stream_id = CPP_GET_STREAM_ID(identity);
  session_id = CPP_GET_SESSION_ID(identity);
  ctrl = (cpp_module_ctrl_t *)MCT_OBJECT_PRIVATE(module);

  if (!ctrl) {
    CPP_ERR("Invalid pointers ctrl:%p", ctrl);
    return -EINVAL;
  }

  if ((session_id >= CPP_MODULE_MAX_SESSIONS) ||
    (stream_id >= CPP_MODULE_MAX_STREAMS)) {
    CPP_ERR("Exceeding the port map entries, session:%d, stream:%d",
      session_id, stream_id);
    return FALSE;
  }

  if (dir == MCT_PORT_SINK) {
    dir_idx = 1;
  }

  if ((port != NULL) && (ctrl->port_map[session_id][stream_id][dir_idx] !=
    NULL)) {
    CPP_ERR("Port from old stream, port:%p",
      ctrl->port_map[session_id][stream_id][dir_idx]);
    return -EINVAL;
  }

  ctrl->port_map[session_id][stream_id][dir_idx] = port;

  return 0;
}

boolean cpp_module_check_queue_compatibility(cpp_module_ctrl_t *ctrl,
  cam_stream_type_t process_stream_type, uint32_t process_identity,
  uint32_t queue_identity)
{
  boolean                      ret = TRUE;
  cam_stream_type_t            queue_stream_type;
  cpp_module_stream_params_t  *stream_params = NULL;
  cpp_module_session_params_t *session_params = NULL;

  if (process_identity == queue_identity)
    return ret;

  cpp_module_get_params_for_identity(ctrl, queue_identity, &session_params,
     &stream_params);
  if(!stream_params) {
    CPP_ERR("failed\n");
    return ret;
  }

  if ((stream_params->stream_type != CAM_STREAM_TYPE_OFFLINE_PROC) &&
    (process_stream_type != CAM_STREAM_TYPE_OFFLINE_PROC))
    return ret;

  return FALSE;
}

boolean cpp_module_pop_per_frame_entry(cpp_module_ctrl_t *ctrl,
  cpp_per_frame_params_t *per_frame_params, uint32_t q_idx,
  uint32_t cur_frame_id, cpp_frame_ctrl_data_t **frame_ctrl_data,
  cpp_module_stream_params_t *stream_params)
{
  boolean                queue_compatible = TRUE;
  cpp_frame_ctrl_data_t *local_frame_ctrl_data;

  if (!frame_ctrl_data) {
    CPP_ERR("frame_ctrl_data:%p\n", frame_ctrl_data);
    return FALSE;
  }

  *frame_ctrl_data = NULL;
  if (!ctrl || !per_frame_params) {
    CPP_ERR("ctrl:%p, per_frame_params:%p\n", ctrl,
      per_frame_params);
    return FALSE;
  }

  local_frame_ctrl_data =
    mct_queue_pop_head(per_frame_params->frame_ctrl_q[q_idx]);
  if (!local_frame_ctrl_data)
    return FALSE;

  /* Since we are currently reusing the queue for realtime & offline
     processing the frameid from the queue entry needs to be checked
     considering the fact that some offline entries with different
     frameid may be available. Also for different concurrent offline
     streams the entries of current offline stream should not be
     compared with other offline streams */
  queue_compatible = cpp_module_check_queue_compatibility(ctrl,
    stream_params->stream_type, stream_params->identity,
    local_frame_ctrl_data->identity);
  if (queue_compatible == FALSE) {
    /* Enqueue the entry back to the queue */
    mct_queue_push_tail(per_frame_params->frame_ctrl_q[q_idx],
      (void *)local_frame_ctrl_data);
    return TRUE;
  }

  if (local_frame_ctrl_data->frame_id != cur_frame_id) {
    CPP_PER_FRAME_LOW("failed: wrong queue for mct_type = %d frame %d exp %d",
      local_frame_ctrl_data->mct_type,
      local_frame_ctrl_data->frame_id,
      cur_frame_id);
    if (MCT_EVENT_MODULE_EVENT == local_frame_ctrl_data->mct_type)
      free(local_frame_ctrl_data->u.module_event.module_event_data);
    else
      free(local_frame_ctrl_data->u.ctrl_param.parm_data);
    free(local_frame_ctrl_data);
    return TRUE;
  }

  *frame_ctrl_data = local_frame_ctrl_data;
  return  TRUE;
}

void cpp_module_free_stream_based_entry(cpp_module_ctrl_t *ctrl,
  cam_stream_type_t stream_type, cpp_per_frame_params_t *per_frame_params)
{
  uint32_t                     j, i, queue_len;
  cpp_module_session_params_t *session_params = NULL;
  cpp_module_stream_params_t  *stream_params = NULL;
  cpp_frame_ctrl_data_t       *frame_ctrl_data;

  /* Dont wait for stop session, clear the offline stream per
     frame queue entries after last offline stream is off. */
  for (j = 0; j < FRAME_CTRL_SIZE; j++) {
    if (!per_frame_params->frame_ctrl_q[j]) {
      continue;
    }

    pthread_mutex_lock(&per_frame_params->frame_ctrl_mutex[j]);
    queue_len = per_frame_params->frame_ctrl_q[j]->length;
    for (i = 0; i < queue_len; i++) {
      frame_ctrl_data =
        mct_queue_pop_head(per_frame_params->frame_ctrl_q[j]);
      if (!frame_ctrl_data) {
        CPP_ERR("frame_ctrl_data:%p\n", frame_ctrl_data);
        continue;
      }

      cpp_module_get_params_for_identity(ctrl,
        frame_ctrl_data->identity, &session_params, &stream_params);
      if(!stream_params) {
        CPP_ERR("queue_stream_params:%p\n", stream_params);
        continue;
      }

      if (((stream_type == CAM_STREAM_TYPE_OFFLINE_PROC) &&
        (stream_params->stream_type == CAM_STREAM_TYPE_OFFLINE_PROC)) ||
        ((stream_type != CAM_STREAM_TYPE_OFFLINE_PROC) &&
        (stream_params->stream_type != CAM_STREAM_TYPE_OFFLINE_PROC))) {
        cpp_module_utill_free_queue_data(frame_ctrl_data, NULL);
        continue;
      }

      mct_queue_push_tail(per_frame_params->frame_ctrl_q[j],
        (void *)frame_ctrl_data);
    }
    pthread_mutex_unlock(&per_frame_params->frame_ctrl_mutex[j]);
  }
}

void cpp_module_util_update_asf_params(cpp_hardware_params_t *hw_params, bool asf_mask)
{

  if (asf_mask == TRUE) {
    if (hw_params->sharpness_level == 0.0f) {
      if (hw_params->asf_mode == CPP_PARAM_ASF_DUAL_FILTER) {
        CPP_ASF_LOW("CPP_PARAM_ASF_OFF");
        hw_params->asf_mode = CPP_PARAM_ASF_OFF;
      }
    } else {
      if (hw_params->asf_mode == CPP_PARAM_ASF_OFF) {
        CPP_ASF_LOW("CPP_PARAM_ASF_DUAL_FILTER");
        hw_params->asf_mode = CPP_PARAM_ASF_DUAL_FILTER;
      }
    }
  } else {
    hw_params->asf_mode = CPP_PARAM_ASF_OFF;
    hw_params->sharpness_level = 0.0f;
  }
  CPP_ASF_LOW("stream_type %d, asf_mask %d, asf_mode %d,asf_level %f",
    hw_params->stream_type, asf_mask,
    hw_params->asf_mode, hw_params->sharpness_level);
}

void cpp_module_util_update_asf_region(cpp_module_session_params_t
  *session_params, cpp_module_stream_params_t *stream_params,
  cpp_hardware_params_t *hw_params)
{
  if(!session_params || !stream_params || !hw_params){
    CPP_ERR("Invalid parameters\n");
    return;
  }

  if(stream_params->stream_type == CAM_STREAM_TYPE_PREVIEW) {
    /*Update session param value since it will be consumed in sof_notify which will be in session
       stream  for OSD feature*/
    CPP_LOW("Preview stream .Updating session params\n");
    session_params->hw_params.asf_info.region = hw_params->asf_info.region;
  } else {
    CPP_LOW("Not preview stream. Not updating session params\n");
  }
}

void cpp_module_util_calculate_scale_ratio(cpp_hardware_params_t *hw_params,
  float *isp_scale_ratio, float *cpp_scale_ratio)
{
  if (!hw_params->isp_width_map || !hw_params->isp_height_map ||
    !hw_params->input_info.width || !hw_params->input_info.height) {
    *isp_scale_ratio = 1.0f;
  } else {
    CPP_CROP_LOW("### isp w, h [%d, %d], i/p w,h[%d: %d]",
        hw_params->isp_width_map, hw_params->isp_height_map,
        hw_params->input_info.width, hw_params->input_info.height);
    float width_ratio, height_ratio;
    width_ratio = (float)hw_params->isp_width_map /
    hw_params->input_info.width;
    height_ratio = (float)hw_params->isp_height_map /
    hw_params->input_info.height;
    if (width_ratio <= height_ratio) {
      *isp_scale_ratio = width_ratio;
    } else {
      *isp_scale_ratio = height_ratio;
    }
  }

  if (!hw_params->crop_info.process_window_width ||
    !hw_params->crop_info.process_window_height ||
    !hw_params->input_info.width || !hw_params->input_info.height) {
    *cpp_scale_ratio = 1.0f;
  } else {
    CPP_CROP_LOW("### crop w, h [%d, %d], o/p w,h[%d: %d]",
      hw_params->crop_info.process_window_width,
      hw_params->crop_info.process_window_height,
      hw_params->output_info.width, hw_params->output_info.height);
    float width_ratio, height_ratio;
    width_ratio = (float)hw_params->crop_info.process_window_width /
    hw_params->output_info.width;
    height_ratio = (float)hw_params->crop_info.process_window_height /
    hw_params->output_info.height;
    if (width_ratio <= height_ratio) {
      *cpp_scale_ratio = width_ratio;
    } else {
      *cpp_scale_ratio = height_ratio;
    }
  }
  CPP_CROP_DBG("### Scale ratio [isp: %f, cpp: %f]",
    *isp_scale_ratio, *cpp_scale_ratio);
  return;
}

#ifdef OEM_CHROMATIX
bool cpp_module_util_is_two_pass_reprocess(cpp_module_stream_params_t *stream_params)
{
  cam_pp_feature_config_t *pp_config = NULL;
  mct_stream_info_t* stream_info = stream_params->stream_info;

  if (!stream_params || !stream_params->stream_info) {
    CPP_LOW("Invalid stream info for stream %d", stream_params->stream_type);
  }
  switch (stream_params->stream_type) {
    case CAM_STREAM_TYPE_OFFLINE_PROC:
      pp_config = &stream_info->reprocess_config.pp_feature_config;
    break;
    case CAM_STREAM_TYPE_CALLBACK:
    case CAM_STREAM_TYPE_SNAPSHOT:
    case CAM_STREAM_TYPE_VIDEO:
    default:
      pp_config = &stream_info->pp_config;
    break;
  }

  if (!pp_config) {
    CPP_ERR("pp config null, no two pass");
    goto end;
  }

  CPP_DBG("[REPROCESS] reprocess count %d, total reprocess_count %d",
    pp_config->cur_reproc_count, pp_config->total_reproc_count);

  // Two reprocess counts return true
  if (pp_config->total_reproc_count == 2) {
    return true;
  }
end:
  return false;
}
#endif

int32_t cpp_module_util_update_chromatix_pointer(cpp_module_stream_params_t  *stream_params,
  modulesChromatix_t *chromatix_ptr, float scale_ratio)
{

  chromatix_cpp_type *chromatix_snap_ptr = NULL;
  int32_t ret = 0;
  if (!stream_params) {
     ret = -EFAULT;
     CPP_ERR("Invalid stream params");
     goto end;
  }

  if (!chromatix_ptr) {
     ret = -EFAULT;
     CPP_ERR("Invalid chromatix pointer");
     goto end;
  }
  CPP_DBG("[CHROMATIX] chromatix ptr: stream_type %d, stream_chromatix %p,"
   "cpp_scale_ratio %f\n",
   stream_params->stream_type, chromatix_ptr->chromatixCppPtr,
   scale_ratio);

  switch (stream_params->stream_type) {
    case CAM_STREAM_TYPE_CALLBACK:
    case CAM_STREAM_TYPE_SNAPSHOT:
    case CAM_STREAM_TYPE_OFFLINE_PROC:
      chromatix_snap_ptr =
        (chromatix_cpp_type *)chromatix_ptr->chromatixSnapCppPtr;
      if (!chromatix_snap_ptr) {
        CPP_ERR("Invalid chromatix snapshot pointer, using preview");
        ret = 0;
        goto end;
      }
      #ifdef OEM_CHROMATIX
      if (!cpp_module_util_is_two_pass_reprocess(stream_params)) {
        if(stream_params->hw_params.low_light_capture_enable) {
          chromatix_ptr->chromatixCppPtr =
            (chromatix_ptr->chromatixOisSnapCppPtr != NULL) ?
            chromatix_ptr->chromatixOisSnapCppPtr : chromatix_snap_ptr;
        } else {
          chromatix_ptr->chromatixCppPtr = chromatix_snap_ptr;
        }
        CPP_DBG("### [CHROMATIX] chromatix ptr: Not 2 step reprocess"
          " for stream_type %d stream chromatix %p",
          stream_params->stream_type, chromatix_ptr->chromatixCppPtr);
        ret = 0;
        goto end;
      }
      #endif

      if(scale_ratio < CPP_UPSCALE_THRESHOLD(chromatix_snap_ptr)) {
        /* Low light Capture */
        if(stream_params->hw_params.low_light_capture_enable) {
          chromatix_ptr->chromatixCppPtr =
            (chromatix_ptr->chromatixOisUsCppPtr != NULL) ?
            chromatix_ptr->chromatixOisUsCppPtr : chromatix_snap_ptr;
        } else {
        chromatix_ptr->chromatixCppPtr =
          (chromatix_ptr->chromatixUsCppPtr != NULL) ?
          chromatix_ptr->chromatixUsCppPtr : chromatix_snap_ptr;
        }

      } else if (scale_ratio > CPP_DOWNSCALE_THRESHOLD(chromatix_snap_ptr)) {
        if(stream_params->hw_params.low_light_capture_enable) {
        chromatix_ptr->chromatixCppPtr =
          (chromatix_ptr->chromatixOisDsCppPtr != NULL) ?
          chromatix_ptr->chromatixOisDsCppPtr : chromatix_snap_ptr;
        } else {
          chromatix_ptr->chromatixCppPtr =
            (chromatix_ptr->chromatixDsCppPtr != NULL) ?
            chromatix_ptr->chromatixDsCppPtr : chromatix_snap_ptr;
        }
      } else {
        if(stream_params->hw_params.low_light_capture_enable) {
          chromatix_ptr->chromatixCppPtr = chromatix_ptr->chromatixOisSnapCppPtr;
          CPP_DBG("OIS capture enabled stream_type %d stream chromatix %p",
            stream_params->stream_type, chromatix_ptr->chromatixCppPtr);
        } else {
          chromatix_ptr->chromatixCppPtr = chromatix_snap_ptr;
        }
      }

      break;
    case CAM_STREAM_TYPE_VIDEO:
      if(chromatix_ptr->chromatixVideoCppPtr) {
        chromatix_ptr->chromatixCppPtr =
          (chromatix_cpp_type *)chromatix_ptr->chromatixVideoCppPtr;
      }
      break;
    default:
      CPP_DBG("###default ptr update for stream_type %d",
        stream_params->stream_type);
    break;
  }

  CPP_DBG("### [CHROMATIX]  chromatix ptr after: stream_type %d, stream_chromatix %p\n",
    stream_params->stream_type, chromatix_ptr->chromatixCppPtr);
end:
  return ret;
}

/** cpp_module_util_update_plane_info:
 *
 *  @hw_params - This parameter holds the cpp hardware params data structure.
 *  @dim_info - This parameter holds cpp dimension info data structure.
 *  @plane_info - This paramter is input data structure referring to the
 *  HAL plane info data structure having dimension/fmt/plane parameters.
 *  This data structure is updated in the function.
 *
 *  This function is a utility function used to convert/copy the buffer/plane
 *  dimension related parameters to HAL data structure so clients like
 *  buffer manager that accepts HAL data structure recieves correct params.
 *
 *  Return: Returns 0 at success and error (-ve value) on failure.
 **/
int32_t cpp_module_util_update_plane_info(cpp_hardware_params_t *hw_params,
  cpp_params_dim_info_t *dim_info, cam_frame_len_offset_t *plane_info)
{
    uint8_t i = 0;
    if (!hw_params || !dim_info || !plane_info) {
      CPP_ERR("invalid input parameters, cannot update plane info");
      return -EINVAL;
    }
    memset(plane_info, 0x00, sizeof(cam_frame_len_offset_t));
    plane_info->num_planes =
      (dim_info->plane_fmt == CPP_PARAM_PLANE_CRCB420) ? 3 : 2;
    for (i = 0; i < plane_info->num_planes; i++) {
      plane_info->mp[i].width = dim_info->width;
      plane_info->mp[i].height = dim_info->height;
      plane_info->mp[i].stride = dim_info->stride;
      plane_info->mp[i].scanline = dim_info->scanline;
      plane_info->mp[i].len = dim_info->plane_info[i].plane_len;
      CPP_BUF_DBG("plane:%d,width:%d,height:%d,st:%d,sc:%d,len:%d",
        i, plane_info->mp[i].width, plane_info->mp[i].height,
        plane_info->mp[i].stride, plane_info->mp[i].scanline, plane_info->mp[i].len);
    }
    plane_info->frame_len = dim_info->frame_len;
    CPP_BUF_DBG("frame_len %d", plane_info->frame_len);
    return 0;
}

/** cpp_module_util_configure_clock_rate:
 *
 *  @ctrl - This parameter holds the cpp control pointer
 *  @perf_mode - This parameter holds info if performance mode is set from HAL.
 *  @index - This parameter holds the referenceindex to the clock freq table.
 *  @clk_rate - This parameter points to clock rate calculated from the load.
 *
 *  This function is a utility function to set the clock value calculated
 *  with default load or override with user input based on property or
 *  turbo value based on performance falg set from HAL.
 *
 *  Return: Returns 0 at success and error (-ve value) on failure.
 **/
int32_t cpp_module_util_configure_clock_rate(cpp_module_ctrl_t *ctrl,
  uint32_t perf_mode, uint32_t *index, long unsigned int *clk_rate)
{
  char value[PROPERTY_VALUE_MAX] = "";
  do {
    property_get("cpp.set.clock", value, "0");
    if (atoi(value)) {
      *index = 0;
      *clk_rate = (atoi(value)) * 1000000;
      CPP_DBG("Set clock rate with property clk rate %lu", *clk_rate);
      break;
    } else if (perf_mode == CAM_PERF_HIGH_PERFORMANCE) {
      *index = ctrl->cpphw->hwinfo.freq_tbl_count - 1;
      break;
    } else if (perf_mode == CAM_PERF_HIGH) {
      *index = ctrl->cpphw->hwinfo.freq_tbl_count - 2;
      break;
    } else {
      *index = 0;
      break;
    }
  } while(0);

  return 0;
}

int32_t cpp_module_util_post_error_to_bus(mct_module_t *module, uint32_t identity)
{
  mct_bus_msg_t bus_msg;
  mct_event_t event;

  CPP_DBG(": post error");
  mct_bus_msg_error_message_t err_msg;
  memset(&bus_msg, 0, sizeof(bus_msg));
  bus_msg.sessionid =  CPP_GET_SESSION_ID(identity);
  bus_msg.type = MCT_BUS_MSG_SEND_HW_ERROR;

  /* CPP being a sub-module inside pproc it cannot directly access mct */
  /* Create an event so that PPROC can post it to MCT */
  event.identity = identity;
  event.type = MCT_EVENT_MODULE_EVENT;
  event.direction = MCT_EVENT_UPSTREAM;
  event.u.module_event.type = MCT_EVENT_MODULE_PP_SUBMOD_POST_TO_BUS;
  event.u.module_event.module_event_data = (void *)&bus_msg;

  if (cpp_module_send_event_upstream(module, &event) != 0) {
    CPP_ERR("error posting diag to bus\n");
  }
  return 0;
}

/** cpp_module_util_get_cds_hystersis_info
 *
 *  @chromatix_cpp - This parameter holds the preview chromatix pointer.
 *  @hw_params - This parameter holds the sessions hw params.
 *  @aec_trigger - This parameter holds the aec trigger value.
 *
 *  This function is used to extracts the trigger and calculate and update
 *  the hystersis value.
 *
 *  Return: Returns 0 on success error otherwise
 **/
int32_t cpp_module_util_get_cds_hystersis_info(chromatix_cpp_type *chromatix_cpp,
  cpp_hardware_params_t *hw_params, cpp_params_aec_trigger_info_t *aec_trigger)
{
  float trigger_input;
  float bf_dec_hyst_trigger_strt;
  float bf_dec_hyst_trigger_end;

#if ((defined (CHROMATIX_308) || defined (CHROMATIX_308E) || \
  defined (CHROMATIX_309)) && \
  defined(CAMERA_USE_CHROMATIX_HW_WNR_TYPE))
  Chromatix_hardware_wavelet_type   *wavelet_denoise;
#else
  wavelet_denoise_type *wavelet_denoise;
  goto end;
#endif

  GET_WAVELET_DENOISE_POINTER(chromatix_cpp, hw_params, wavelet_denoise);
  if (wavelet_denoise->control_denoise == 0) {
    CPP_DENOISE_LOW("lux triggered");
    /* Lux index based */
    trigger_input = aec_trigger->lux_idx;
    bf_dec_hyst_trigger_strt = GET_WNR_DEC_LUXIDX_TRIGGER_START(wavelet_denoise);
    bf_dec_hyst_trigger_end = GET_WNR_DEC_LUXIDX_TRIGGER_END(wavelet_denoise);
  } else {
    CPP_DENOISE_LOW("gain triggered");
    /* Gain based */
    trigger_input = aec_trigger->gain;
    bf_dec_hyst_trigger_strt = GET_WNR_DEC_GAIN_TRIGGER_START(wavelet_denoise);
    bf_dec_hyst_trigger_end = GET_WNR_DEC_GAIN_TRIGGER_END(wavelet_denoise);
  }

  CPP_DENOISE_DBG("Previous CDS hystersis state %d", hw_params->hyst_dsdn_status);
  hw_params->hyst_dsdn_status =
    cpp_module_utils_get_hysteresis_trigger(trigger_input,
    bf_dec_hyst_trigger_strt, bf_dec_hyst_trigger_end,
    hw_params->hyst_dsdn_status);

  CPP_DENOISE_DBG("### CDS HYSTERSIS trigger_input %f, trigger start %f,"
  "trigger end %f, prev dsdn hyst_status %d",
   trigger_input, bf_dec_hyst_trigger_strt, bf_dec_hyst_trigger_end,
   hw_params->hyst_dsdn_status);

end:
  return 0;
}
/** cpp_module_util_get_hystersis_info
 *
 *  @module - This parameter holds the mct module type.
 *  @event - This parameter holds the mct event.
 *
 *  This function is used to calculate and update the hystersis
 *  status of module based on hysteresis trigger
 *
 *  Return: Returns 0 on success error otherwise
 **/
int32_t cpp_module_util_get_hystersis_info(mct_module_t* module, mct_event_t *event)
{
  modulesChromatix_t *module_chromatix;
  chromatix_cpp_type *chromatix_cpp;
  cpp_module_ctrl_t           *ctrl;
  cpp_module_session_params_t *session_params;
  cpp_module_stream_params_t *stream_params;
  stats_update_t               *stats_update;
  aec_update_t                 *aec_update;
  int32_t                       rc = 0;
  cpp_params_aec_trigger_info_t aec_trigger;
  cpp_per_frame_params_t      *per_frame_params;
  cam_dyn_img_data_t dyn_img_data;
  mct_event_control_parm_t event_parm;
  mct_event_t l_event;


  ctrl = (cpp_module_ctrl_t *)MCT_OBJECT_PRIVATE(module);
  if(!ctrl) {
    CPP_ERR("failed\n");
    rc = -EFAULT;
    goto end;
  }

  if (ctrl->cpphw->hwinfo.version != CPP_HW_VERSION_6_0_0) {
    goto end;
  }

  cpp_module_get_params_for_identity(ctrl, event->identity,
    &session_params, &stream_params);
  if (!session_params || !stream_params) {
    CPP_ERR("error: Invalid session %p or stream %p params\n",
      session_params, stream_params);
    rc = -EINVAL;
    goto end;
  }

  /* Hystersis updated for offline is already in metadata. Do not re-compute */
  if (stream_params->hw_params.stream_type == CAM_STREAM_TYPE_OFFLINE_PROC) {
    goto end;
  }

  stats_update =
      (stats_update_t *)event->u.module_event.module_event_data;
  aec_update = &stats_update->aec_update;

  if (stats_update->flag & STATS_UPDATE_AEC) {
    aec_trigger.gain = aec_update->real_gain;
    aec_trigger.lux_idx = aec_update->lux_idx;
  } else {
    CPP_DENOISE_LOW("No change in AEC - hyst status dsdn %d tnr %d pbf %d ",
      session_params->hw_params.hyst_dsdn_status,
      session_params->hw_params.hyst_tnr_status,
      session_params->hw_params.hyst_pbf_status);
     goto end;
  }

  module_chromatix = &session_params->module_chromatix;
  if (!module_chromatix) {
    CPP_ERR("error: Invalid module_chromatix\n");
    rc = -EINVAL;
    goto end;
  }

  chromatix_cpp =  (chromatix_cpp_type *)module_chromatix->chromatixCppPtr;
  if (!chromatix_cpp) {
    CPP_ERR("error: Invalid cpp chromatix\n");
    rc = -EINVAL;
    goto end;
  }

  CPP_DENOISE_DBG("Hystersis for frame %d",
    event->u.module_event.current_frame_id);
  cpp_module_util_get_cds_hystersis_info(chromatix_cpp,
    &session_params->hw_params, &aec_trigger);

#if defined (CHROMATIX_308E)
  cpp_hw_params_is_tnr_enabled_ext(chromatix_cpp,
    &session_params->hw_params, &aec_trigger);
  cpp_hw_params_is_pbf_enabled_ext(chromatix_cpp,
    &session_params->hw_params, &aec_trigger);
#else
    session_params->hw_params.hyst_tnr_status = 1;
    session_params->hw_params.hyst_pbf_status = 1;
#endif

   /* generate a set param event and push it to perframe queue */
   memset(&dyn_img_data, 0x0, sizeof(cam_dyn_img_data_t));
   dyn_img_data.dyn_feature_mask =
     (session_params->hw_params.hyst_dsdn_status << DYN_IMG_CDS_HYS_BIT) |
     (session_params->hw_params.hyst_tnr_status << DYN_IMG_TNR_HYS_BIT) |
     (session_params->hw_params.hyst_pbf_status << DYN_IMG_PBF_HYS_BIT);
   /* send Dynamic feature data event */
   l_event.identity =  event->identity;
   l_event.direction = MCT_EVENT_DOWNSTREAM;
   l_event.type = MCT_EVENT_CONTROL_CMD;
   l_event.timestamp = 0;
   l_event.u.ctrl_event.type = MCT_EVENT_CONTROL_SET_PARM;
   l_event.u.ctrl_event.control_event_data = &event_parm;
   l_event.u.ctrl_event.current_frame_id = event->u.module_event.current_frame_id;
   event_parm.type = CAM_INTF_META_IMG_DYN_FEAT;
   event_parm.parm_data =  (void *)&dyn_img_data;
   cpp_module_handle_set_parm_event(module, &l_event);

   /*
    * Post the calculated hystersis to metadata on
    * frame id + max pipeline delay + reporting delay
    */
   per_frame_params = &session_params->per_frame_params;
   rc = cpp_module_post_sticky_meta_entry(ctrl, event->identity,
     per_frame_params, (event->u.module_event.current_frame_id +
     per_frame_params->max_apply_delay +
     per_frame_params->max_report_delay),
     CAM_INTF_META_IMG_DYN_FEAT);

end:
  return rc;
}

/** cpp_module_utils_get_hysteresis_trigger
 *
 *  @trigger_input - This parameter holds the input trigger for interpolation
 *  @trigger_start - This parameter holds the start threshold to keep the module
 *   disabled.
 *  @trigger_end - This parameter holds the end threshold to keep the module
 *   enabled
 *  @prev_status -  This parameter holds previous status of the module.
 *
 *  This function is used to get the state of module based on hysteresis trigger
 *
 *  Return: Returns state of the module based on hysteresis
 *  (true enable / false disable).
 **/
bool cpp_module_utils_get_hysteresis_trigger(float trigger_input,
  float trigger_start, float trigger_end, bool prev_state)
{

  /* No trigger, assume hysteresis trigg returns true (do not disable module) */
  if ((F_EQUAL(trigger_start, 0.0f)) || (F_EQUAL(trigger_end, 0.0f)))
    return true;

  /* trigger i/p < trigger start - disable module based on hystersis trigger */
  if (trigger_input < trigger_start)
    return false;
  /* trigger i/p >= trigger end - enable module based on hystersis trigger */
  else if ((F_EQUAL(trigger_input, trigger_end)) || (trigger_input > trigger_end))
    return true;
  /* trigger between start and end threshold - return previous status */
  else
      return prev_state;
}

int32_t cpp_module_util_check_per_frame_limits(cpp_module_ctrl_t *ctrl,
  uint32_t identity, uint32_t cur_frame_id, cam_stream_ID_t *valid_stream_ids)
{
  uint32_t                      i = 0, j = 0;
  cpp_module_stream_params_t  *stream_params = NULL;
  cpp_module_session_params_t *session_params = NULL;

  /* Scan through the real time streams and handle empty buffer done
     for those streams for which buffer divert has raced compared to
     Super-param (CAM_INTF_META_STREAM_ID) */
  cpp_module_get_params_for_identity(ctrl, identity,
    &session_params, &stream_params);
  if (!session_params || !stream_params || !valid_stream_ids) {
    CPP_ERR("session_params: %p, stream_params: %p valid_stream_ids: %p\n",
      session_params, stream_params, valid_stream_ids);
    return 0;
  }

  for (j = 0; j < valid_stream_ids->num_streams; j++) {
    for (i = 0; i < CPP_MODULE_MAX_STREAMS; i++) {
      if (session_params->stream_params[i] &&
        (valid_stream_ids->streamID[j] ==
        CPP_GET_STREAM_ID(session_params->stream_params[i]->identity)) &&
        (session_params->stream_params[i]->stream_type !=
        CAM_STREAM_TYPE_OFFLINE_PROC)) {
        PTHREAD_MUTEX_LOCK(&(session_params->stream_params[i]->mutex));
        if (session_params->stream_params[i]->cur_frame_id >=
          cur_frame_id) {
          PTHREAD_MUTEX_LOCK(&(ctrl->cpp_mutex));
          if (ctrl->cpp_thread_started) {
            cpp_module_event_t           *isp_buffer_drop_event = NULL;
            cpp_thread_msg_t             msg;
            isp_buffer_drop_event =
              (cpp_module_event_t*)malloc(sizeof(cpp_module_event_t));
            if(!isp_buffer_drop_event) {
              CPP_ERR("malloc failed\n");
              PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
              return -ENOMEM;
            }
            memset(isp_buffer_drop_event, 0x00, sizeof(cpp_module_event_t));
            isp_buffer_drop_event->hw_process_flag = TRUE;
            isp_buffer_drop_event->type = CPP_MODULE_EVENT_ISP_BUFFER_DROP;
            isp_buffer_drop_event->invalid = FALSE;
            isp_buffer_drop_event->u.drop_buffer.frame_id = cur_frame_id;
            isp_buffer_drop_event->u.drop_buffer.stream_params =
              session_params->stream_params[i];

            CPP_ERR("SOF drop for id %x frame %d \n",
              session_params->stream_params[i]->identity,
              isp_buffer_drop_event->u.drop_buffer.frame_id);

            cpp_module_enq_event(ctrl, isp_buffer_drop_event,
              CPP_PRIORITY_REALTIME);

            msg.type = CPP_THREAD_MSG_NEW_EVENT_IN_Q;
            cpp_module_post_msg_to_thread(ctrl, msg);
          } else {
            CPP_ERR("Thread not active");
          }
          PTHREAD_MUTEX_UNLOCK(&(ctrl->cpp_mutex));
          valid_stream_ids->streamID[j] = 0;
        }
        PTHREAD_MUTEX_UNLOCK(&(session_params->stream_params[i]->mutex));
        break;
      }
    }
  }

  return 0;
}
