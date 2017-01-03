/*============================================================================

  Copyright (c) 2015-2016 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#include <sys/mman.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <linux/ion.h>
#include <linux/msm_ion.h>
#include <media/msmb_camera.h>

extern "C" {
#include "mct_controller.h"
#include "mct_pipeline.h"
#include "mct_stream.h"
}

#include "camera_dbg.h"
#include <dlfcn.h>
#include "tm_interface.h"
#include "test_manager.h"

#undef LOG_TAG
#define LOG_TAG "mm-camera-test-manager-interface"

namespace tm_interface {

#define TM_ADD_CASE(field)            \
 case field:                          \
   CLOGH(CAM_NO_MODULE, ""#field"");  \
 break;

#define TM_ADD_CASE_PRINT(field, fmt, args...)   \
 case field:                                     \
 CLOGH(CAM_NO_MODULE, #field ": " #fmt, ##args);  \
 break;

typedef struct {
  cam_format_t  fmt;
  float         pix_size;

} tm_pix_size_lut_t;

typedef struct {
  mct_module_t      *dest_mod;
  mct_port_t        *link_port;
  mct_list_t        *dst_ports;
  mct_stream_info_t *stream_info;
  uint32_t          identity;
  boolean           status;

} tm_intf_check_port_caps_t;


tm_pix_size_lut_t tm_pix_size_lut[] = {
  { CAM_FORMAT_YUV_420_NV12,         1.5 },
  { CAM_FORMAT_YUV_420_NV21,         1.5 },
  { CAM_FORMAT_YUV_420_NV21_ADRENO,  1.5 },
  { CAM_FORMAT_YUV_420_YV12,         1.5 },
  { CAM_FORMAT_YUV_422_NV16,         2   },
  { CAM_FORMAT_YUV_422_NV61,         2   },
  { CAM_FORMAT_YUV_420_NV12_VENUS,   1.5 },
  { CAM_FORMAT_YUV_420_NV12_UBWC,    1.5 },
  { CAM_FORMAT_YUV_RAW_8BIT_YUYV,    2   },
  { CAM_FORMAT_YUV_RAW_8BIT_YVYU,    2   },
  { CAM_FORMAT_YUV_RAW_8BIT_UYVY,    2   },
  { CAM_FORMAT_YUV_RAW_8BIT_VYUY,    2   },
  { CAM_FORMAT_BAYER_MIPI_RAW_10BPP_GBRG, 2 },
  { CAM_FORMAT_BAYER_MIPI_RAW_10BPP_GRBG, 2 },
  { CAM_FORMAT_BAYER_MIPI_RAW_10BPP_RGGB, 2 },
  { CAM_FORMAT_BAYER_MIPI_RAW_10BPP_BGGR, 2 },
  { CAM_FORMAT_MAX,                  0   }
};

typedef struct {
  const char   *name;
  cam_format_t fmt;
  uint32_t     planes;
} tm_fmt_desc_t;

static tm_fmt_desc_t tm_fmt_desc_lut[] = {
  { "NV12", CAM_FORMAT_YUV_420_NV12, 2 },
  { "NV21", CAM_FORMAT_YUV_420_NV21, 2 },
  { "NV21_ADRENO", CAM_FORMAT_YUV_420_NV21_ADRENO, 2 },
  { "YV12", CAM_FORMAT_YUV_420_YV12, 3 },
  { "NV16", CAM_FORMAT_YUV_422_NV16, 2 },
  { "NV61", CAM_FORMAT_YUV_422_NV61, 2 },
  { "NV21_VENUS", CAM_FORMAT_YUV_420_NV12_VENUS, 2},
  { "YUYV", CAM_FORMAT_YUV_RAW_8BIT_YUYV, 1},
  { "YVYU", CAM_FORMAT_YUV_RAW_8BIT_YVYU, 1},
  { "UYVY", CAM_FORMAT_YUV_RAW_8BIT_UYVY, 1},
  { "VYUY", CAM_FORMAT_YUV_RAW_8BIT_VYUY, 1},
  { "MIPI_RAW_110BPP_GBRG", CAM_FORMAT_BAYER_MIPI_RAW_10BPP_GBRG, 1},
  { "MIPI_RAW_110BPP_GRBG", CAM_FORMAT_BAYER_MIPI_RAW_10BPP_GRBG, 1},
  { "MIPI_RAW_110BPP_RGGB", CAM_FORMAT_BAYER_MIPI_RAW_10BPP_RGGB, 1},
  { "MIPI_RAW_110BPP_BGGR", CAM_FORMAT_BAYER_MIPI_RAW_10BPP_BGGR, 1},
  { NULL, CAM_FORMAT_MAX, 0}
};

void *(*open_lib)(void);


boolean tm_intf_check_name(void *mod, void *name)
{
  return ((!strcmp(MCT_OBJECT_NAME(mod), (char *)name)) ? TRUE : FALSE);
}

boolean tm_intf_check_port_unlinked_by_type(void *data1,
  void *data2)
{
  if (!data1 || !data2) {
    TMDBG_ERROR("Invalid input args: data1: %p, data2: %p", data1, data2);
    return FALSE;
  }
  mct_port_t *port = (mct_port_t *)data1;
  mct_port_caps_type_t type = *(mct_port_caps_type_t *)data2;

  if (!MCT_PORT_PEER(port)) {
    if (port->caps.port_caps_type == type)
      return TRUE;
    else
      return FALSE;
  }
  return FALSE;
}

boolean tm_intf_check_port_by_type(void *data1,
  void *data2)
{
  if (!data1 || !data2) {
    TMDBG_ERROR("Invalid input args: data1: %p, data2: %p", data1, data2);
    return FALSE;
  }
  mct_port_t *port = (mct_port_t *)data1;
  mct_port_caps_type_t type = *(mct_port_caps_type_t *)data2;

  if (port->caps.port_caps_type == type)
    return TRUE;
  else
    return FALSE;
}


static boolean tm_intf_compare_port_number(void *data1, void *data2)
{
  unsigned int *ids = (unsigned int *)(data1);
  unsigned int *info = (unsigned int *)(data2);
  if (*ids == *info)
    return TRUE;

  return FALSE;
}

static boolean tm_intf_find_linked_port(void *data1, void *data2)
{
  mct_port_t *port = MCT_PORT_CAST(data1);
  unsigned int *info = (unsigned int *)(data2);
  if (mct_list_find_custom(MCT_PORT_CHILDREN(port), info,
    tm_intf_compare_port_number))
    return TRUE;

  return FALSE;
}

/*========================================================================

                                      TM_INTERFACE

    This interface layer serves as a repository of utility functions that
    unit test suites can conveniently make use of in constructing
    meaningful test case methods.

  ======================================================================*/


/*  Functions for data/buffer exchange with the back-end */

void TM_Interface::bufReceiveInit(void)
{
  pipe(tm_sync_ctx.pipe_frame);

  tm_sync_ctx.poll_fds[0].fd = tm_sync_ctx.pipe_frame[READ_FD];
  tm_sync_ctx.poll_fds[0].events = POLLIN|POLLOUT|POLLPRI|POLLWRNORM|POLLRDNORM;
  tm_sync_ctx.num_subscribed = 1;
}

void TM_Interface::bufReceiveDeinit(void)
{
  close(tm_sync_ctx.pipe_frame[READ_FD]);
  close(tm_sync_ctx.pipe_frame[WRITE_FD]);
}

TM_Interface::TM_Interface(void)
                 :  modules(NULL),
                    p_stream_info(NULL),
                    ref_img_buffer(NULL),
                    img_bufs_idx(1)
{
  input_port.num_ports = 0;
  output_port.num_ports = 0;
  stats_port.num_ports = 0;
  in_buf_desc.map_buf = NULL;
  in_buf_desc.v4l_buf.addr[0] = 0;
  meta_buf_desc.map_buf = NULL;
  meta_buf_desc.v4l_buf.addr[0] = 0;
  out_buf_desc[0].map_buf = NULL;
  out_buf_desc[0].v4l_buf.addr[0] = 0;
  scratch_buf_desc[0].map_buf = NULL;
  memset(img_buf_list, 0, sizeof(img_buf_list) * TM_MAX_NUM_STREAMS);
  memset(isp_out_dims, 0, sizeof(tm_out_dims_t));
  chromatix.chromatixPrv_header = NULL;
  chromatix.chromatixCom_header = NULL;
  chromatix.chromatixCpp_header = NULL;
  chromatix.chromatix3a_header  = NULL;
  cur_fps.max_fps = 0;
  bufReceiveInit();
}

TM_Interface::~TM_Interface(void)
{
  bufReceiveDeinit();
}

tm_intf_input_t TM_Interface::getInput()
{
  return m_input;
}

boolean TM_Interface::putInput(tm_intf_input_t *in_input)
{
  if (!in_input) {
    TMDBG_ERROR("No valid XML input available");
    return FALSE;
  }
  memcpy(&m_input, in_input, sizeof(tm_intf_input_t));
  return TRUE;
}

cam_format_t TM_Interface::getFormat(char *in_format)
{
  uint32_t cnt = 0;

  while (NULL != tm_fmt_desc_lut[cnt].name) {
    if(!strncmp(tm_fmt_desc_lut[cnt].name, in_format, 32)) {
      return tm_fmt_desc_lut[cnt].fmt;
    }
    cnt++;
  }
  return CAM_FORMAT_MAX;
}

uint32_t TM_Interface::getNumPlanes(char *in_format)
{
  uint32_t cnt = 0;
  while (NULL != tm_fmt_desc_lut[cnt].name) {
    if(!strncmp(tm_fmt_desc_lut[cnt].name, in_format, 32)) {
      return tm_fmt_desc_lut[cnt].planes;
    }
    cnt++;
  }
  return 0;
}

float TM_Interface::getPixelSize(cam_format_t format)
{
  uint32_t cnt = 0;

  while (CAM_FORMAT_MAX != tm_pix_size_lut[cnt].fmt) {
    if (format == tm_pix_size_lut[cnt].fmt) {
      return tm_pix_size_lut[cnt].pix_size;
    }
    cnt++;
  }
  return 0;
}

/** Name: bufReceiveMsg
 *
 *  Arguments/Fields:
 *  @buf_desc: pointer to buffer descriptor
 *  Return: Boolean result for success/failure
 *
 *  Description: Waits for synchronization and
 *  reads buffer descriptor data from pipe.
 *
 **/
boolean TM_Interface::bufReceiveMsg(tm_intf_buffer_desc_t *buf_desc)
{
  int32_t                   num_read;
  int32_t                   ready;

  if (!buf_desc) {
    TMDBG_ERROR("Invalid input argument: buf_desc");
    return FALSE;
  }
  ready = poll(tm_sync_ctx.poll_fds, tm_sync_ctx.num_subscribed, 1000);
  if (ready < 0) {
    TMDBG_ERROR("poll() failed");
    return FALSE;
  } else if (ready == 0) {
    TMDBG_ERROR("poll() timed out");
    return FALSE;
  }
  num_read = read(tm_sync_ctx.pipe_frame[READ_FD], buf_desc,
    sizeof(tm_intf_buffer_desc_t));
  if(num_read < 0) {
    TMDBG_ERROR("Read from pipe fails!");
    return FALSE;
  } else if (num_read != sizeof(tm_intf_buffer_desc_t)) {
    TMDBG_ERROR("failed, in read(), num_read=%d, msg_size=%d", num_read,
      sizeof(tm_intf_buffer_desc_t));
    return FALSE;
  }
  return TRUE;
}

/** Name: notifyBufAckEvent
 *
 *  Arguments/Fields:
 *  @buf_desc: pointer to buffer descriptor
 *
 *  Description: Sends synchronization and
 *  writes buffer descriptor to pipe.
 *
 **/
void TM_Interface::notifyBufAckEvent(tm_intf_buffer_desc_t *buf_desc)
{
  if (!buf_desc) {
    TMDBG_ERROR("Invalid input argument: buf_desc");
    return;
  }
  write(tm_sync_ctx.pipe_frame[WRITE_FD], buf_desc, sizeof(tm_intf_buffer_desc_t));
}

/** Name: sendBufDivertAck
 *
 *  Arguments/Fields:
 *  @buf_desc: pointer to buffer descriptor
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends buffer diver acknowledge to ISP module. This
 *  method should be used in cases when there is no pproc after ISP
 *  module. ISP expects acknowledge on buffer divert message.
 *
 **/
boolean TM_Interface::sendBufDivertAck(tm_intf_buffer_desc_t *buf_desc)
{
  isp_buf_divert_ack_t  isp_buf_divert_ack;
  boolean               ret_val;
  mct_list_t            *port_container;
  unsigned int          info;
  mct_port_t            *port;
  if (!buf_desc) {
    TMDBG_ERROR("Invalid input argument: buf_desc");
    return FALSE;
  }
  isp_buf_divert_ack.buf_idx = buf_desc->index;
  isp_buf_divert_ack.is_buf_dirty = TRUE;
  isp_buf_divert_ack.identity = buf_desc->identity;
  isp_buf_divert_ack.frame_id = buf_desc->frame_id;
  isp_buf_divert_ack.timestamp = buf_desc->timestamp;


  info = buf_desc->identity;
  port_container = mct_list_find_custom(MCT_MODULE_SINKPORTS(output_port.module),
    &info, tm_intf_find_linked_port);

    if (!port_container) {
    TMDBG_ERROR("%s: Could not find port\n", __func__);
    return FALSE;
  }
  port = MCT_PORT_CAST(port_container->data);
  if (!port) {
    TMDBG_ERROR("no peer port!");
    return FALSE;
  }
  port = MCT_PORT_PEER(port);
  if (!port) {
    TMDBG_ERROR("no peer port!");
    return FALSE;
  }

  ret_val = sendModuleEventUp(port, MCT_EVENT_MODULE_BUF_DIVERT_ACK,
    &isp_buf_divert_ack, buf_desc->identity);
  return ret_val;
}

boolean TM_Interface::calcBufSize(tm_intf_buffer_desc_t *buf_desc)
{
  mct_pipeline_get_stream_info_t info;
  mct_stream_t *stream = NULL;
  cam_format_t format;
  uint32_t cnt = 0;

  info.check_type   = CHECK_INDEX;
  info.stream_index = buf_desc->identity & 0xFFFF;

  stream = mct_pipeline_get_stream(pipeline, &info);

  if (NULL == stream) {
    TMDBG_ERROR("Cannot find stream identity: %x", buf_desc->identity);
    return FALSE;
  }
  format = stream->streaminfo.fmt;

  while (CAM_FORMAT_MAX != tm_pix_size_lut[cnt].fmt) {
    if (format == tm_pix_size_lut[cnt].fmt) {
      buf_desc->size = buf_desc->dims.width * buf_desc->dims.height *
        tm_pix_size_lut[cnt].pix_size;
      TMDBG("%s: width: [%d], height: [%d], bpp: [%f]. Image size = [%d] bytes", __func__,
        buf_desc->dims.width, buf_desc->dims.height, tm_pix_size_lut[cnt].pix_size, buf_desc->size);
      return TRUE;
    }
    cnt++;
  }
  return FALSE;
}

boolean TM_Interface::calcPlanesOffset(tm_intf_buffer_desc_t *buf_desc,
  tm_buf_planes_t *buf_planes)
{
  mct_pipeline_get_stream_info_t info;
  mct_stream_t *stream = NULL;
  cam_format_t format;
  uint32_t addr;
  uint32_t cnt = 0;

  TMASSERT(!buf_desc, TRUE, "NULL pointer for buf desc\n");
  TMASSERT(!buf_desc->size, TRUE, "buf desc size is 0\n");

  info.check_type   = CHECK_INDEX;
  info.stream_index = buf_desc->identity & 0xFFFF;

  stream = mct_pipeline_get_stream(pipeline, &info);

  if (NULL == stream) {
    TMDBG_ERROR("Cannot find stream identity: %x", buf_desc->identity);
    return FALSE;
  }
  format = stream->streaminfo.fmt;

  memset(buf_planes, 0, sizeof(tm_buf_planes_t));

  while (CAM_FORMAT_MAX != tm_pix_size_lut[cnt].fmt) {
    if (format == tm_pix_size_lut[cnt].fmt) {
      buf_planes->p1 = (uint8_t *)buf_desc->vaddr;
      if (stream->streaminfo.buf_planes.plane_info.num_planes > 1) {
        addr = (uint32_t)buf_desc->vaddr +
        (uint32_t)((float)buf_desc->size / tm_pix_size_lut[cnt].pix_size);
        buf_planes->p2 = (uint8_t *)((addr + 3) & ~3);

        if (3 == stream->streaminfo.buf_planes.plane_info.num_planes) {
          addr = (uint32_t)buf_planes->p2 +
            (uint32_t)((float)buf_desc->size / tm_pix_size_lut[cnt].pix_size / 4);
          buf_planes->p3 = (uint8_t *)((addr + 3) & ~3);
        }
      }
      return TRUE;
    }
    cnt++;
  }
  return FALSE;
}

boolean TM_Interface::getStreamOutDims(tm_out_dims_t *out_dims,
  uint32_t identity)
{
  mct_pipeline_get_stream_info_t info;
  mct_stream_t *stream = NULL;

  info.check_type   = CHECK_INDEX;
  info.stream_index = identity & 0xFFFF;

  stream = mct_pipeline_get_stream(pipeline, &info);

  if (NULL == stream) {
    TMDBG_ERROR("Cannot find stream identity: %x", identity);
    return FALSE;
  }

  out_dims->width  = stream->streaminfo.dim.width;
  out_dims->height = stream->streaminfo.dim.height;
  return TRUE;
}

void TM_Interface::getStreamIspOutDims(tm_out_dims_t *out_dims,
  uint32_t identity)
{
  uint32_t idx = (identity & 0xFFFF) - 1;
  if (idx >= TM_MAX_NUM_STREAMS) {
    TMDBG_ERROR("Invalid stream index %d", idx);
    return;
  }
  memcpy(out_dims, &isp_out_dims[idx], sizeof(tm_out_dims_t));
}

void TM_Interface::saveStreamIspOutDims(cam_dimension_t *dim,
  uint32_t identity)
{
  uint32_t idx = (identity & 0xFFFF) - 1;
  if (idx >= TM_MAX_NUM_STREAMS) {
    TMDBG_ERROR("Invalid stream index %d", idx);
    return;
  }
  isp_out_dims[idx].width  = dim->width;
  isp_out_dims[idx].height = dim->height;
}

void TM_Interface::saveStreamIspOutCrop(mct_bus_msg_stream_crop_t *crop,
  uint32_t identity)
{
  uint32_t idx = (identity & 0xFFFF) - 1;
  if (idx >= TM_MAX_NUM_STREAMS) {
    TMDBG_ERROR("Invalid stream index %d", idx);
    return;
  }
  isp_out_dims[idx].crop_x  = crop->crop_out_x;
  isp_out_dims[idx].crop_y  = crop->crop_out_y;
  isp_out_dims[idx].start_x = crop->x;
  isp_out_dims[idx].start_y = crop->y;
}

void TM_Interface::printBuf(tm_intf_buffer_desc_t *buf_desc)
{
  if (!buf_desc) {
    TMDBG_ERROR("Invalid input argument: buf_desc");
    return;
  }
  tm_buf_planes_t buf_planes;
  uint32_t *out_p;
  uint32_t *out_b_cr;
  char const *cr;

  calcPlanesOffset(buf_desc, &buf_planes);

  if (buf_planes.p1) {
    out_p = (uint32_t *)buf_planes.p1;
    TMDBG_HIGH("------------ buffer --------------");
    TMDBG_HIGH("Y : 0x%x, 0x%x, 0x%x, 0x%x\n", out_p[0], out_p[1],
      out_p[2], out_p[3]);
  }
  if (buf_planes.p3) {
    cr = "U";
  }   else {
    cr = "UV";
  }
  if (buf_planes.p2) {
    out_b_cr = (uint32_t *)buf_planes.p2;
    TMDBG_HIGH("%s: 0x%x, 0x%x, 0x%x, 0x%x", cr, out_b_cr[0], out_b_cr[1],
      out_b_cr[2], out_b_cr[3]);
  }
  if (buf_planes.p3) {
    out_b_cr = (uint32_t *)buf_planes.p3;
    TMDBG_HIGH("V: 0x%x, 0x%x, 0x%x, 0x%x", out_b_cr[0], out_b_cr[1],
      out_b_cr[2], out_b_cr[3]);
  }
  TMDBG_HIGH("----------------------------------\n");
}

boolean TM_Interface::saveOutFile(char *out_fname, void *addr, size_t len)
{
  int                      out_file_fd;
  size_t written;

  TMDBG_HIGH("save output to file: %s\n", out_fname);
  out_file_fd = open(out_fname, O_RDWR | O_CREAT, 0777);
  if (out_file_fd < 0) {
    TMDBG_ERROR("Cannot open file\n");
    return FALSE;
  }
  written = write(out_file_fd, addr, len);
  if (written != len) {
    TMDBG_ERROR("Mismatch! save buf size %d, written: %zu", len, written);
  }
  close(out_file_fd);
  return TRUE;
}

void TM_Interface::publishResults(const char *test_case_name, int frames_cnt,
  tm_intf_buffer_desc_t *buf_desc)
{
  uint32_t idx = (buf_desc->identity & 0xFFFF) - 1;
  uint32_t frames_save_rate = m_input.frames_skip + 1;

  if (FALSE == m_input.save_file) {
    return;
  }
  if (!test_case_name || !buf_desc) {
    TMDBG_ERROR("Invalid input args! test_case_name: %p, buf_desc: %p",
        test_case_name, buf_desc);
    return;
  }
  TMDBG("frames_cnt: %d, frames_skip: %d", frames_cnt, m_input.frames_skip);
  if (frames_cnt % frames_save_rate) {
    return;
  }

  if (idx >= m_input.num_streams) {
    TMDBG_ERROR("unexpected identity: %x, idx: %d", buf_desc->identity, idx);
    return;
  }
  printBuf(buf_desc);

  snprintf(out_fname, sizeof(out_fname), "%s%s_%dx%d_%s_%d.yuv",
    m_input.outfile_path, m_input.uc_name,
    buf_desc->dims.width, buf_desc->dims.height,
    m_input.out_format[idx], frames_cnt);
  saveOutFile(out_fname, buf_desc->vaddr, buf_desc->size);
}

/** Name: setModulesList
 *
 *  Arguments/Fields:
 *  @modules_list: modules list
 *  Return: Boolean result for success/failure
 *
 *  Description: This method should be called after modules list
 *  manipulation (add or remove module) to update class member
 *  modules list. This is because this class is working with
 *  modules list.
 *
 **/
void TM_Interface::setModulesList(mct_list_t *modules_list)
{
  CDBG("%s: set modules list: %p", __func__, modules_list);
  modules = modules_list;
}

/** getModule:
 *  @name: module name to retreive from modules list
 *  Return: module descriptor
 *
 *  Description: Returns corresponding module or NULL
 *  if not found in the list.
 **/
mct_module_t *TM_Interface::getModule(const char *name)
{
  mct_list_t *module;

  if (!modules) {
    TMDBG_ERROR("No modules list!");
    return NULL;
  }
  if (name) {
    module = mct_list_find_custom(modules, (void *)name, tm_intf_check_name);
    if (module)
      return (mct_module_t *)(module->data);
  } else {
    TMDBG_ERROR("Invalid argument: module name");
  }

  return NULL;
}

/** getFirstModule:
 *  Return: module descriptor
 *
 *  Description: Returns first module in modules list
 *   or NULL if no list is avalable.
 **/
mct_module_t *TM_Interface::getFirstModule(void)
{
  if (modules)
    return (mct_module_t *)(modules->data);
  return NULL;
}

/** getListFirstModule:
 *  @lst:  modules list
 *
 *  Description: Returns first module in given modules list or
 *  NULL if no list available.
 **/
mct_module_t *TM_Interface::getListFirstModule(mct_list_t *lst)
{
  if (lst)
    return (mct_module_t *)(lst->data);
  return NULL;
}

/** getSinkPort:
 *  @module: modules descriptor
 *  Return: port descriptor
 *
 *  Description: Returns sink port descriptor of given module.
 **/
mct_port_t *TM_Interface::getSinkPort(mct_module_t *module)
{
  mct_list_t *sinkports;
  if (module) {
  sinkports = MCT_MODULE_SINKPORTS(module);

  if(sinkports)
    return (mct_port_t *)sinkports->data;
  else
    TMDBG_ERROR("No ports found!");
  } else {
    TMDBG_ERROR("Null module pointer");
  }
  return NULL;
}

/** getSrcPort:
 *  @module: modules descriptor
 *  Return: port descriptor
 *
 *  Description: Returns source port descriptor of given module.
 **/
mct_port_t *TM_Interface::getSrcPort(mct_module_t *module)
{
  mct_list_t *srcports;
  if (module) {
    srcports = MCT_MODULE_SRCPORTS(module);

    if(srcports)
      return (mct_port_t *)srcports->data;
    else
      TMDBG_ERROR("No ports found!");
  } else {
    TMDBG_ERROR("Null module pointer");
  }
  return NULL;
}


/*************************************************************/
/***** Interface functions and support functions for module linking *****/

boolean TM_Interface::unlinkTwoPorts(mct_port_t *src_port,
  mct_port_t *sink_port, uint32_t identity)
{
  boolean           rc;

  if (!src_port || !sink_port) {
    return FALSE;
  }

  mct_port_remove_child(identity, sink_port);
  mct_port_remove_child(identity, src_port);
  /* Unlink on module port */
  src_port->un_link(identity, src_port, sink_port);
  /* Unlink on module port */
  sink_port->un_link(identity, sink_port, src_port);
  /* Caps unreserve on module port */
  rc = sink_port->check_caps_unreserve(sink_port, identity);
  /* Caps unreserve on module port */
  rc |= src_port->check_caps_unreserve(src_port, identity);

  return rc;
}

mct_port_t *TM_Interface::getUnlinkedSrcPortType(
  mct_module_t *module, mct_port_caps_type_t type)
{
  mct_list_t *srcports;

  if (!module) {
    return FALSE;
  }

  srcports = MCT_MODULE_SRCPORTS(module);

  if(srcports) {
    srcports = mct_list_find_custom(srcports, &type,
      tm_intf_check_port_unlinked_by_type);
    if (srcports)
      return (mct_port_t *)srcports->data;
    else
      return NULL;
  } else {
    return NULL;
  }
}

mct_port_t *TM_Interface::getUnlinkedSinkPortType(
  mct_module_t *module, mct_port_caps_type_t type)
{
  mct_list_t *ports;

  if (!module) {
    return FALSE;
  }
  ports = MCT_MODULE_SINKPORTS(module);

  if(ports) {
    ports = mct_list_find_custom(ports, &type,
      tm_intf_check_port_unlinked_by_type);
    if (ports)
      return (mct_port_t *)ports->data;
    else
      return NULL;
  } else {
    return NULL;
  }
}

static boolean tm_intf_get_compatible_dest_port(void *data1, void *data2)
{
  mct_port_t *destport = (mct_port_t *)data1;
  tm_intf_check_port_caps_t *sent_custom = (tm_intf_check_port_caps_t *)data2;
  mct_port_t *srcport = sent_custom->link_port;
  boolean ret = FALSE;
  void *temp_caps;

  if (!data1 || !data2) {
    TMDBG_ERROR("Invalid input args: data1: %p, data2: %p", data1, data2);
    return FALSE;
  }
  ret = tm_intf_check_port_by_type(destport,
    &sent_custom->link_port->caps.port_caps_type);
  if (FALSE == ret) {
    CDBG_HIGH("%s: port <%s> type: %d, expected: %d",
      __func__, destport->object.name,
      destport->caps.port_caps_type, sent_custom->link_port->caps.port_caps_type);
    return FALSE;
  }
  if (!mct_port_check_link(srcport, destport)) {
    CDBG_ERROR("%s: Check link failed on ports <%s> <%s>\n", __func__,
      srcport->object.name, destport->object.name);
    return FALSE;
  }

  if (destport->caps.port_caps_type == MCT_PORT_CAPS_OPAQUE) {
    temp_caps = srcport->caps.u.data;
  } else {
    temp_caps = &srcport->caps;
  }
  CDBG_HIGH("%s: dest port: %s", __func__, destport->object.name);

  /* DO COMPATIBILITY CHECK between destport and srcport */
  if (destport->check_caps_reserve) {
    if ((ret = destport->check_caps_reserve(destport, temp_caps,
      sent_custom->stream_info)) == TRUE) {

      if (destport->caps.port_caps_type == MCT_PORT_CAPS_STATS) {
        if (srcport->caps.u.stats.flag & destport->caps.u.stats.flag)
          ret = TRUE;
      } else if (destport->caps.port_caps_type == MCT_PORT_CAPS_FRAME) {
        if ((srcport->caps.u.frame.format_flag
          & destport->caps.u.frame.format_flag)
          && (srcport->caps.u.frame.size_flag
            <= destport->caps.u.frame.size_flag))
          ret = TRUE;
      } else {
        ret = TRUE;
      }
    }/*check_caps*/
  }

  return ret;
}

static boolean tm_intf_get_compatible_src_port(void *data1, void *data2)
{
  mct_port_t *srcport = (mct_port_t *)data1;
  tm_intf_check_port_caps_t *custom = (tm_intf_check_port_caps_t *)data2;
  tm_intf_check_port_caps_t local_custom;
  mct_list_t *destports;
  mct_list_t *request_dest_holder;
  mct_port_t *request_dest = NULL;
  boolean rc;

  if (!data1 || !data2) {
    TMDBG_ERROR("Invalid input args: data1: %p, data2: %p", data1, data2);
    return FALSE;
  }

  CDBG_HIGH("%s: src port: %s", __func__, srcport->object.name);

  rc = tm_intf_check_port_by_type(srcport,
    &custom->link_port->caps.port_caps_type);
  // it is expected to return FALSE until find compatible port
  if (FALSE == rc) {
    CDBG_HIGH("%s: port <%s> type: %d, expected: %d",
      __func__, srcport->object.name,
      srcport->caps.port_caps_type, custom->link_port->caps.port_caps_type);
    return FALSE;
  }

  rc = srcport->check_caps_reserve(srcport, &custom->link_port->caps,
    custom->stream_info);
  // it is expected to return FALSE until find compatible port
  if (FALSE == rc)
    return FALSE;

  CDBG_HIGH("%s: reserved src port: %s", __func__, srcport->object.name);

  custom->status = TRUE;
  destports = custom->dst_ports;
  /* Now, loop through destports and try to find a port that
   * 1) Can meet stream needs
   * 2) Is compatible with this srcport
   */
  local_custom = *custom;
  local_custom.link_port = srcport;

  request_dest_holder = mct_list_find_custom(destports, &local_custom,
    tm_intf_get_compatible_dest_port);

  if (!request_dest_holder) {
    if ((MCT_PORT_DIRECTION(srcport) == MCT_PORT_SRC) &&
      (MCT_PORT_PEER(srcport) == NULL)) {
      /* it has to be SRC port to link to SINK port,
       * reverse link might be supported in future but not now */
      if ((custom->dest_mod)->request_new_port) {
        request_dest = (custom->dest_mod)->request_new_port(custom->stream_info,
          MCT_PORT_SINK, custom->dest_mod, &srcport->caps);
      }
    }
  } else {
    request_dest = MCT_PORT_CAST(request_dest_holder->data);
  }

  if (!request_dest) {
    CDBG_HIGH("%s: going to unreserve src port: %s",
      __func__, srcport->object.name);
    srcport->check_caps_unreserve(srcport, custom->identity);
    return FALSE;
  }

  custom->link_port = request_dest;

  return TRUE;
}

/** Name: linkTwoPorts
 *
 *  Arguments/Fields:
 *  @src_port: source port to be linked
 *  @sink_port: sink port to be linked
 *  @stream_info: stream info for stream that ports will be linked
 *  Return: Boolean result for success/failure
 *
 *  Description: This function links two compatible ports for given
 *  stream info.
 **/
boolean TM_Interface::linkTwoPorts(mct_port_t *src_port, mct_port_t *sink_port,
  mct_stream_info_t *stream_info)
{
  mct_list_t *srcports, *destports;
  mct_port_t *srcport = NULL, *destport = NULL;
  mct_list_t *srcport_holder, *destport_holder;
  unsigned int identity;
  mct_list_t *lst;
  mct_module_t *src;
  mct_module_t *dest;
  tm_intf_check_port_caps_t check_port;


  if (!src_port || !sink_port)
    return FALSE;

  CDBG("%s: enter linking ports <%s> <%s>", __func__,
    src_port->object.name, sink_port->object.name);

  identity = ((mct_stream_info_t *)stream_info)->identity;

  lst = MCT_PORT_PARENT(sink_port);
  if (!lst) {
    TMDBG_ERROR("no module for port: %s", sink_port->object.name);
    return FALSE;
  }
  dest = getListFirstModule(lst);
  if (!dest) {
    return FALSE;
  }
  CDBG_HIGH("module %s has %d sink and %d src ports", dest->object.name,
    dest->numsinkports, dest->numsrcports);
  destports = MCT_MODULE_SINKPORTS(dest);

  lst = MCT_PORT_PARENT(src_port);
  if (!lst) {
    TMDBG_ERROR("no module for port: %s", src_port->object.name);
    return FALSE;
  }
  src = getListFirstModule(lst);
  if (!src) {
    return FALSE;
  }
  CDBG_HIGH("module %s has %d sink and %d src ports", src->object.name,
    src->numsinkports, src->numsrcports);
  srcports = MCT_MODULE_SRCPORTS(src);
  if (!srcports) {
    TMDBG_ERROR("no src ports");
  }


  check_port.link_port = sink_port;
  check_port.stream_info = stream_info;
  check_port.identity = identity;
  check_port.dst_ports = destports;
  check_port.dest_mod = dest;

  /* traverse through the allowed ports in the source, trying to find a
   * compatible destination port
   */
  TMDBG("%s: enter linking ports for modules <%s> <%s>", __func__,
    src->object.name, dest->object.name);

  srcport_holder = mct_list_find_custom(srcports, &check_port,
    tm_intf_get_compatible_src_port);

  CDBG_HIGH("%s: end search comp src port for modules <%s> <%s>", __func__,
    src->object.name, dest->object.name);

  if (!srcport_holder) {
    CDBG_HIGH("%s: no srcport_holder", __func__);
    if (check_port.status == TRUE) {
      /* not able to find a valid destination port */
      goto fail;
    } else if (check_port.status == FALSE) {
      if (src->request_new_port) {
        srcport = src->request_new_port(stream_info, MCT_PORT_SRC, src, NULL);
        if (!srcport)
          goto fail;
        CDBG_HIGH("%s: requested new port src port: %s",
          __func__, srcport->object.name);
        memset(&check_port, 0, sizeof(check_port));
        check_port.identity = identity;
        check_port.stream_info = stream_info;
        check_port.link_port = srcport;

        destport_holder = mct_list_find_custom(destports, &check_port,
          tm_intf_get_compatible_dest_port);
        if (!destport_holder) {
          if (dest->request_new_port)
            destport = dest->request_new_port(
              stream_info, MCT_PORT_SINK, dest, NULL);
        } else {
          destport = MCT_PORT_CAST(destport_holder->data);
        }
        if (!destport)
          /*un-request src port*/
          goto fail;
      } else { /*src module doesnt support request port*/
        goto fail;
      }
    }
  } else { /*already found src port*/
    srcport = (mct_port_t *)srcport_holder->data;
    destport = check_port.link_port;
    CDBG_HIGH("%s: found srcport_holder for ports <%s> <%s>", __func__,
      srcport->object.name, destport->object.name);
  }

  if (srcport && destport) {
    TMDBG("%s: linking ports <%s> <%s>, caps type: %d, %d", __func__,
      srcport->object.name, destport->object.name,
      srcport->caps.port_caps_type, destport->caps.port_caps_type);
    if (mct_port_establish_link(identity, srcport, destport))
      return TRUE;

    /*if linking fails, unreserve the ports*/
    srcport->check_caps_unreserve(srcport, identity);
    destport->check_caps_unreserve(destport, identity);
  }

fail:
  TMDBG_ERROR("%s: fail for ports <%s> <%s>", __func__,
    src_port->object.name, sink_port->object.name);
  return FALSE;
}

/** Name: linkTwoModules
 *
 *  Arguments/Fields:
 *  @mod1: first module to be linked
 *  @mod2: second module to be linked
 *  @stream_info: stream info for stream that ports will be linked
 *  Return: Boolean result for success/failure
 *
 *  Description: This function links two modules for given stream info.
 **/
boolean TM_Interface::linkTwoModules(mct_module_t *mod1, mct_module_t *mod2,
  mct_stream_info_t *stream_info)
{
  if (!mod1 || !mod2 || !stream_info) {
    TMDBG_ERROR("Invalid input args: mod1: %p, mod2: %p, stream_info: %p",
        mod1, mod2, stream_info);
    return FALSE;
  }
  mct_stream_t *stream = stream_info->stream;
  boolean rc;

  if (!stream) {
    TMDBG_ERROR("Failed ! ");
    return FALSE;
  }
  CDBG_HIGH("%s: Enter ", __func__);

  TMDBG("%s: Linking modules %s and %s for stream identity: %x\n", __func__,
    MCT_MODULE_NAME(mod1), MCT_MODULE_NAME(mod2), stream_info->identity);

  if (mct_module_link((void *)(&(stream->streaminfo)), mod1, mod2) == TRUE) {
    CDBG_HIGH("%s: Module = %s ", __func__, MCT_OBJECT_NAME(mod1));
    CDBG_HIGH("%s: Module = %s ", __func__, MCT_OBJECT_NAME(mod2));
    rc = mct_object_set_parent(MCT_OBJECT_CAST(mod1),
      MCT_OBJECT_CAST(stream_info->stream));
    rc |= mct_object_set_parent(MCT_OBJECT_CAST(mod2),
      MCT_OBJECT_CAST(stream_info->stream));
    if (rc == FALSE) {
      TMDBG_ERROR("in set parent\n");
      goto error;
    }
  } else {
    TMDBG_ERROR("%s: mct_module_link failed for mod1 (%s) mod2(%s) ",
      __func__, MCT_OBJECT_NAME(mod1), MCT_OBJECT_NAME(mod2));
    goto error;
  }

  return TRUE;

error:
  /*unlinking is done during stream destroy*/
  return FALSE;
}

boolean TM_Interface::unlinkTwoModules(mct_module_t *mod1, mct_module_t *mod2,
  mct_stream_info_t *stream_info)
{
  if (!mod1 || !mod2 || !stream_info) {
    TMDBG_ERROR("Invalid input args: mod1: %p, mod2: %p, stream_info: %p",
        mod1, mod2, stream_info);
    return FALSE;
  }
  mct_stream_t *stream = stream_info->stream;
  boolean rc;

  if (!stream) {
    TMDBG_ERROR("%s [%d]: Failed ! ",__func__, __LINE__);
    return FALSE;
  }
  CDBG_HIGH("%s: Enter ", __func__);

  TMDBG("%s: Unlinking modules %s and %s\n", __func__,
    MCT_MODULE_NAME(mod1), MCT_MODULE_NAME(mod2));
  mct_module_unlink(stream_info->identity, mod1, mod2);
  rc = mct_object_unparent(MCT_OBJECT_CAST(mod1),
    MCT_OBJECT_CAST(stream_info->stream));
  rc |= mct_object_unparent(MCT_OBJECT_CAST(mod2),
    MCT_OBJECT_CAST(stream_info->stream));
  TMASSERT(rc == FALSE, TRUE, "unparent modules\n");

  return TRUE;

//error:
//  /*unlinking is done during stream destroy*/
//  return FALSE;
}

/** Name: linkTestPorts
 *
 *  Arguments/Fields:
 *  @ld: link data structure filled by method caller
 *  Return: Boolean result for success/failure
 *
 *  Description: This method links dummy test ports to first and last
 *  modules and stats port in case ISP module is present.
 **/
boolean TM_Interface::linkTestPorts(tm_intf_link_data_t *ld)
{
  boolean rc = TRUE;
  mct_module_t *p_mod;
  mct_module_t *p_isp_mod;
  mct_module_t *p_imglib_mod;
  mct_port_t   *port;
  if (!ld) {
    TMDBG_ERROR("Invalid link data");
    return FALSE;
  }

  // link stats port
  p_isp_mod = getModule("isp");
  if (p_isp_mod) {
    if (0 == stats_port.num_ports) {
      TMDBG_ERROR("No stats port created!");
      return FALSE;
    }
    port = stats_port.first_port;
    //link stats port to isp src port
    ld->prev_port = getSrcPort(p_isp_mod);
    if (NULL == ld->prev_port) {
      TMDBG_ERROR("no src port available to link stats port");
    } else {
      rc = linkTwoPorts(ld->prev_port, port, &ld->stream->streaminfo);
      TMASSERT(rc == FALSE, TRUE, "link isp with stats port!");
//        if (FALSE == rc) {
//          TMDBG_ERROR("link isp with stats port!");
//          return FALSE;
//        }
    }
  }

  // link img port
  p_mod = getModule("pproc");
  if (p_mod) {
    // if imagelib is present do not link image port
    p_imglib_mod = getModule("imglib");
    if (p_imglib_mod) {
      rc = TRUE;
    } else {
    if (0 == output_port.num_ports) {
      TMDBG_ERROR("No out port created!");
      return FALSE;
    }
      port = output_port.first_port;
      // link image port to pproc src port
      ld->prev_port = getSrcPort(p_mod);
      if (NULL == ld->prev_port) {
        TMDBG_ERROR("no src port available to link img port");
      } else {
        rc = linkTwoPorts(ld->prev_port, port, &ld->stream->streaminfo);
        TMASSERT(rc == FALSE, FALSE, "link isp with stats port!");
//        if (FALSE == rc)
//          TMDBG_ERROR("link pproc with output port!");
      }
    }
  } else if (p_isp_mod) {
    // if no pproc will need to link image port to isp to use native buffers
    ld->prev_port = getSrcPort(p_isp_mod);
    if (NULL == ld->prev_port) {
      TMDBG_ERROR("no src port available to link img port");
    } else {
      port = output_port.first_port;
      rc = linkTwoPorts(ld->prev_port, port, &ld->stream->streaminfo);
    }
  }
  if (rc == FALSE) {
    TMDBG_ERROR("Error on link last module with dummy sink port!");
  }
  return rc;
}

/************ End of interface functions for module linking ***********/
/************************************************************/

/** Name: createPort
 *
 *  Arguments/Fields:
 *  @name: port name
 *  @direction: port direction (source/sink)
 *  @port_data: port capabilities.
 *  Return: Boolean result for success/failure
 *
 *  Description: This method creates port and fills port capabilities.
 *  It is used to create dummy ports.
 **/
mct_port_t *TM_Interface::createPort(const char *name,
  mct_port_direction_t direction, tm_uc_port_caps_t *port_data)
{
  mct_port_t *port;
  if (!name || !port_data) {
    TMDBG_ERROR("Invalid input args: name: %p, port_data: %p", name,port_data);
    return NULL;
  }
  port = mct_port_create(name);
  if (!port) {
    return NULL;
  }
  mct_port_set_event_func(port, port_data->event_func);
  port->caps.port_caps_type = port_data->caps.port_caps_type;
  port->port_private = port_data->priv_data;
  port->direction = direction;

  switch (port_data->caps.port_caps_type) {
  case MCT_PORT_CAPS_OPAQUE:
    port->caps.u.data = port_data->caps.u.data;
    break;
  case MCT_PORT_CAPS_STATS:
    memcpy(&port->caps.u.stats, &port_data->caps.u.stats,
      sizeof(port_data->caps.u.stats));
    break;
  case MCT_PORT_CAPS_FRAME:
    memcpy(&port->caps.u.frame, &port_data->caps.u.frame,
      sizeof(port_data->caps.u.frame));
    break;
  default:
    break;
  }

  return port;
}

static boolean destroy_port(void *data, void *user_data)
{
  boolean ret;
  mct_port_t *port = (mct_port_t *)data;
  if (!port) {
    TMDBG_ERROR("Invalid port ptr");
    return FALSE;
  }
  mct_module_t *module = (mct_module_t *)user_data;

  TMASSERT(!module, TRUE, "No module provided!");

  ret = mct_object_unparent(MCT_OBJECT_CAST(port), MCT_OBJECT_CAST(module));
  if (FALSE == ret) {
    TMDBG_ERROR("%s: Can not unparent port %s from module %s \n",
      __func__, MCT_OBJECT_NAME(port), MCT_OBJECT_NAME(module));
  }
  if (port->caps.port_caps_type == MCT_PORT_CAPS_OPAQUE)
    free(port->caps.u.data);
  mct_port_destroy(port);
  return ret;
}

/** createInputPort:
 *  @port_data: port capabilities
 *  @num_ports: required ports number
 *  Return: Boolean result for success/failure
 *
 *  Description: Creates dummy input module and source ports.
 **/
boolean TM_Interface::createInputPort(tm_uc_port_caps_t *port_data,
  uint32_t num_ports)
{
  mct_port_t *port;
  char port_name[32];
  uint32_t cnt;

  if (0 == input_port.num_ports) {
    input_port.module = mct_module_create("input_module");
    TMASSERT(!input_port.module, TRUE, "on input module create");
  }
  for (cnt = 0 ; cnt < num_ports ; cnt++) {
    snprintf(port_name, sizeof(port_name), "input_port%d", cnt);
    port = createPort(port_name, MCT_PORT_SRC, port_data);
    if (!port) {
      TMDBG_ERROR("creating input port");
      return FALSE;
    }
    if (FALSE == mct_module_add_port(input_port.module, port)) {
      TMDBG_ERROR("%s: Failed to add port <%s> to list", __func__, port_name);
      destroy_port(port, input_port.module);
      mct_module_destroy(input_port.module);
      return FALSE;
    }
    input_port.num_ports++;
    if (cnt == 0) {
      input_port.first_port = port;
    }
  }

  return TRUE;
}

/** createStatsPort:
 *  @port_data: port capabilities
 *  @num_ports: required ports number
 *  Return: Boolean result for success/failure
 *
 *  Description: Creates dummy stats module and sink ports.
 **/
boolean TM_Interface::createStatsPort(tm_uc_port_caps_t *port_data,
  uint32_t num_ports)
{
  mct_port_t *port;
  char port_name[32];
  uint32_t cnt;

  stats_port.module = mct_module_create("stats_module");
  TMASSERT(!stats_port.module, TRUE, "on stats module create");

  for (cnt = 0 ; cnt < num_ports ; cnt++) {
    snprintf(port_name, sizeof(port_name), "stats_port%d", cnt);
    port = createPort(port_name, MCT_PORT_SINK, port_data);
    if (!port) {
      TMDBG_ERROR("creating input port");
      return FALSE;
    }
    if (FALSE == mct_module_add_port(stats_port.module, port)) {
      TMDBG_ERROR("%s: Failed to add port <%s> to list", __func__, port_name);
      destroy_port(port, stats_port.module);
      mct_module_destroy(stats_port.module);
      return FALSE;
    }
    stats_port.num_ports++;
    if (cnt == 0) {
      stats_port.first_port = port;
    }
  }
  return TRUE;
}

/** createOutputPort:
 *  @port_data: port capabilities
 *  @num_ports: required ports number
 *  Return: Boolean result for success/failure
 *
 *  Description: Creates dummy output module and sink ports.
 *  This is needed when last tested module expects to have other module.
 **/
boolean TM_Interface::createOutputPort(tm_uc_port_caps_t *port_data,
  uint32_t num_ports)
{
  mct_port_t *port;
  char port_name[32];
  uint32_t cnt;

  output_port.module = mct_module_create("output_module");
  TMASSERT(!output_port.module, TRUE, "on output module create");

  for (cnt = 0 ; cnt < num_ports ; cnt++) {
    snprintf(port_name, sizeof(port_name), "output_port%d", cnt);
    port = createPort(port_name, MCT_PORT_SINK, port_data);
    if (!port) {
      TMDBG_ERROR("creating input port");
      return FALSE;
    }
    if (FALSE == mct_module_add_port(output_port.module, port)) {
      TMDBG_ERROR("%s: Failed to add port <%s> to list", __func__, port_name);
      destroy_port(port, output_port.module);
      mct_module_destroy(output_port.module);
      return FALSE;
    }
    output_port.num_ports++;
    if (cnt == 0) {
      output_port.first_port = port;
    }
  }
  return TRUE;
}

void TM_Interface::destroyInputPort(void)
{
  mct_list_t      *port_container;

  if (input_port.num_ports) {
    port_container = MCT_MODULE_SINKPORTS(input_port.module);
    if (port_container) {
      mct_list_traverse(port_container, destroy_port, input_port.module);
      mct_list_free_list(port_container);
    }
    port_container = MCT_MODULE_SRCPORTS(input_port.module);
    if (port_container) {
      mct_list_traverse(port_container, destroy_port, input_port.module);
      mct_list_free_list(port_container);
    }
    mct_module_destroy(input_port.module);

    input_port.num_ports = 0;
  }
}

void TM_Interface::destroyOutputPort(void)
{
  mct_list_t                *port_container;

  if (output_port.num_ports) {
    port_container = MCT_MODULE_SINKPORTS(output_port.module);
    if (port_container) {
      mct_list_traverse(port_container, destroy_port, output_port.module);
      mct_list_free_list(port_container);
    }
    port_container = MCT_MODULE_SRCPORTS(output_port.module);
    if (port_container) {
      mct_list_traverse(port_container, destroy_port, output_port.module);
      mct_list_free_list(port_container);
    }
    mct_module_destroy(output_port.module);

    output_port.num_ports = 0;
  }
}

void TM_Interface::destroyStatsPort(void)
{
  mct_list_t                *port_container;

  if (stats_port.num_ports) {
    port_container = MCT_MODULE_SINKPORTS(stats_port.module);
    if (port_container) {
      mct_list_traverse(port_container, destroy_port, stats_port.module);
      mct_list_free_list(port_container);
    }
    port_container = MCT_MODULE_SRCPORTS(stats_port.module);
    if (port_container) {
      mct_list_traverse(port_container, destroy_port, stats_port.module);
      mct_list_free_list(port_container);
    }

    mct_module_destroy(stats_port.module);

    stats_port.num_ports = 0;
  }
}

mct_port_t *TM_Interface::getFirstInputPort()
{
  return input_port.first_port;
}

/*  Interface functions used by test suites for
 *   session handling & regular frame processing
*/

/** Name: sendChromatixPointers
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends module event with chromatix pointers.
 **/
boolean TM_Interface::sendChromatixPointers(uint32_t identity)
{
  boolean rc;
  modulesChromatix_t module_chromatix;
  mct_port_t         *port = NULL;
  mct_module_t       *module;

  module = getFirstModule();
  port = getSinkPort(module);

  module_chromatix.chromatixPostProcPtr =
    module_chromatix.chromatixPtr = chromatix.chromatixPtr;
  module_chromatix.chromatixComPtr = chromatix.chromatixComPtr;
  module_chromatix.chromatixVideoCppPtr =
    module_chromatix.chromatixSnapCppPtr =
    module_chromatix.chromatixPostProcPtr =
    module_chromatix.chromatixCppPtr = chromatix.chromatixCppPtr;
  module_chromatix.chromatix3APtr = chromatix.chromatix3aPtr;
  module_chromatix.use_stripped = 0;

  rc = sendModuleEventDown(MCT_EVENT_MODULE_SET_CHROMATIX_PTR,
    &module_chromatix, identity);
  if (rc == FALSE) {
    TMDBG_ERROR("error in set chromatix pointer!\n");
  }
  return rc;
}

/** Name: fillMetadataChromatixPtrs
 *
 *  Arguments/Fields:
 *  @sen_meta: sensor metadata bus message
 *  Return: Boolean result for success/failure
 *
 *  Description: Fills chromatix pointers in sensor metadata bus message.
 **/
void TM_Interface::fillMetadataChromatixPtrs(
  mct_bus_msg_sensor_metadata_t *sen_meta)
{
  if(sen_meta) {
    sen_meta->chromatix_ptr = chromatix.chromatixPtr;
    sen_meta->cpp_snapchromatix_ptr =
      sen_meta->cpp_videochromatix_ptr =
      sen_meta->cpp_chromatix_ptr = chromatix.chromatixCppPtr;
    sen_meta->common_chromatix_ptr = chromatix.chromatixComPtr;
    sen_meta->a3_chromatix_ptr = chromatix.chromatix3aPtr;
  } else {
    TMDBG_ERROR("No sensor metadata present");
  }
}

/** Name: loadChromatixLibraries
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Loads chromatix libraries and fills chromatix pointers.
 *  This method should be used in case when there is no sensor module.
 *  Otherwise sensor module loads libraries and sends chromatix pointers.
 **/
boolean TM_Interface::loadChromatixLibraries()
{
  snprintf(chromatix.chromatix_com_file, MAX_FILENAME_LEN, "%s_common.so",
    m_input.chromatix_file);
  snprintf(chromatix.chromatix_cpp_file, MAX_FILENAME_LEN, "%s_cpp_preview.so",
    m_input.chromatix_file);
  snprintf(chromatix.chromatix_file, MAX_FILENAME_LEN, "%s_preview.so",
    m_input.chromatix_file);
  snprintf(chromatix.chromatix_3a_file, MAX_FILENAME_LEN,
    "%s_zsl_video_lc898122.so",
    m_input.chromatix_file);

  chromatix.chromatixPrv_header = dlopen((const char *)chromatix.chromatix_file,
    RTLD_NOW);
  if(!chromatix.chromatixPrv_header) {
    TMDBG_ERROR("Error opening chromatix file %s \n",
      chromatix.chromatix_file);
  } else {
    *(void **)&open_lib = dlsym(chromatix.chromatixPrv_header, "load_chromatix");
    if (!open_lib) {
      const char *perr = dlerror();

      if (NULL != perr)
      {
        TMDBG_ERROR("Fail to find symbol %s", perr);
      }
    } else {
      chromatix.chromatixPtr = open_lib();
    }
  }
  chromatix.chromatixCom_header = dlopen(
    (const char *)chromatix.chromatix_com_file, RTLD_NOW);
  if(!chromatix.chromatixCom_header) {
    TMDBG_ERROR("Error opening chromatix file %s \n",
      chromatix.chromatix_com_file);
  } else {
    *(void **)&open_lib = dlsym(chromatix.chromatixCom_header, "load_chromatix");
    if (!open_lib) {
      const char *perr = dlerror();

      if (NULL != perr) {
        TMDBG_ERROR("Fail to find symbol %s", perr);
      }
    } else {
      chromatix.chromatixComPtr = open_lib();
    }
  }
  chromatix.chromatixCpp_header = dlopen(
    (const char *)chromatix.chromatix_cpp_file, RTLD_NOW);
  if(!chromatix.chromatixCpp_header) {
    TMDBG_ERROR("Error opening chromatix file %s \n",
      chromatix.chromatix_cpp_file);
  } else {
    *(void **)&open_lib = dlsym(chromatix.chromatixCpp_header, "load_chromatix");
    if (!open_lib) {
      const char *perr = dlerror();

      if (NULL != perr) {
         TMDBG_ERROR("Fail to find symbol %s", perr);
      }
    } else {
      chromatix.chromatixCppPtr = open_lib();
    }
  }
  chromatix.chromatix3a_header = dlopen(
    (const char *)chromatix.chromatix_3a_file, RTLD_NOW);
  if(!chromatix.chromatix3a_header) {
    TMDBG_ERROR("Error opening chromatix file %s \n",
      chromatix.chromatix_3a_file);
  } else {
    *(void **)&open_lib = dlsym(chromatix.chromatix3a_header, "load_chromatix");
    if (!open_lib) {
      const char *perr = dlerror();

      if (NULL != perr) {
        TMDBG_ERROR("Fail to find symbol %s", perr);
      }
    } else {
      chromatix.chromatix3aPtr = open_lib();
    }
  }
  return TRUE;
}

void TM_Interface::closeChromatixLibraries()
{
  if (chromatix.chromatixPrv_header) {
    dlclose(chromatix.chromatixPrv_header);
  }
  if (chromatix.chromatixCom_header) {
    dlclose(chromatix.chromatixCom_header);
  }
  if (chromatix.chromatixCpp_header) {
    dlclose(chromatix.chromatixCpp_header);
  }
  if (chromatix.chromatix3a_header) {
    dlclose(chromatix.chromatix3a_header);
  }
}

/** configureStream:
 *  @stream_dims: stream dimensions
 *  Return: Boolean result for success/failure
 *
 *  Description: Sets stream dimensions and sends stream info downstream
 *  event.
 **/
boolean TM_Interface::configureStream(uint32_t feature_mask)
{
  mct_event_control_parm_t  mct_event_control_parm;
  mct_list_t                *port_container;
  mct_port_t                *port;
  mct_event_t               event;
  boolean                   rc;
  uint32_t                  cnt;
  mct_stream_t              *stream;
  mct_pipeline_get_stream_info_t info;

  memset(&cam_stream_info, 0, sizeof(cam_stream_info));

  cam_stream_info.num_streams = m_input.num_streams;
  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {

    info.check_type  = CHECK_TYPE;
    info.stream_type = m_input.stream_types[cnt];

    stream = mct_pipeline_get_stream(pipeline, &info);
    if (!stream) {
      TMDBG_ERROR("%s: Couldn't find stream type: %d\n", __func__, info.stream_type);
      return FALSE;
    }
    cam_stream_info.type[cnt] = m_input.stream_types[cnt];
    cam_stream_info.stream_sizes[cnt].width  = stream->streaminfo.dim.width;
    cam_stream_info.stream_sizes[cnt].height = stream->streaminfo.dim.height;
    cam_stream_info.postprocess_mask[cnt] = feature_mask;
  }
  cam_stream_info.buffer_info.max_buffers = 2;
  cam_stream_info.buffer_info.min_buffers = 1;

  mct_event_control_parm.type = CAM_INTF_META_STREAM_INFO;
  mct_event_control_parm.parm_data = (void *)&cam_stream_info;
  if (input_port.num_ports) {
    port_container = mct_list_find_custom(MCT_MODULE_SRCPORTS(input_port.module),
       &parm_stream_identity, tm_intf_find_linked_port);
    if (!port_container) {
      TMDBG_ERROR("%s: Could not find port\n", __func__);
      return FALSE;
    }
    port = MCT_PORT_CAST(port_container->data);
    memset(&event, 0, sizeof(event));
    event.identity = parm_stream_identity;
    event.direction = MCT_EVENT_DOWNSTREAM;
    event.type = MCT_EVENT_CONTROL_CMD;
    event.u.ctrl_event.type = MCT_EVENT_CONTROL_SET_PARM;
    event.u.ctrl_event.control_event_data = &mct_event_control_parm;

    if (port) {
      rc = port->event_func(port, &event);
      if (rc == FALSE)
        TMDBG_ERROR("failed to send event on input port!");
    }
  }
  CDBG_HIGH("going to send stream info");
  return sendControlEventDown(MCT_EVENT_CONTROL_SET_PARM,
    &mct_event_control_parm, parm_stream_identity);
}

boolean TM_Interface::sendParam(cam_intf_parm_type_t type, void *data)
{
  mct_event_control_parm_t  mct_event_control_parm;
  mct_list_t                *port_container;
  mct_port_t                *port;
  mct_event_t               event;
  boolean                   rc;

  mct_event_control_parm.type = type;
  mct_event_control_parm.parm_data = data;

  if (input_port.num_ports) {
    port_container = mct_list_find_custom(MCT_MODULE_SRCPORTS(input_port.module),
       &parm_stream_identity, tm_intf_find_linked_port);
    if (!port_container) {
      TMDBG_ERROR("%s: Could not find port\n", __func__);
      return FALSE;
    }
    port = MCT_PORT_CAST(port_container->data);
    memset(&event, 0, sizeof(event));
    event.identity = parm_stream_identity;
    event.direction = MCT_EVENT_DOWNSTREAM;
    event.type = MCT_EVENT_CONTROL_CMD;
    event.u.ctrl_event.type = MCT_EVENT_CONTROL_SET_PARM;
    event.u.ctrl_event.control_event_data = &mct_event_control_parm;

    if (port) {
      rc = port->event_func(port, &event);
      if (rc == FALSE)
        TMDBG_ERROR("failed to send event on input port!");
    }
  }
  CDBG_HIGH("going to send %d", type);
  return sendControlEventDown(MCT_EVENT_CONTROL_SET_PARM,
    &mct_event_control_parm, parm_stream_identity);
}

/** Name: setOutputBuffer
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends set output buffer message to set output buffer info
 *  in pproc module.
 *
 **/
boolean TM_Interface::setOutputBuffer(cam_stream_type_t stream_type)
{
  mct_module_t              *module;
  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;

  if (!modules) {
    CDBG_ERROR("%s: NULL modules list", __func__);
    return FALSE;
  }
  module = getModule("pproc");
  if (!module) {
    TMDBG_ERROR("%s: No module present in TM ctx", __func__);
    return FALSE;
  }
  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  if (!stream) {
    TMDBG_ERROR("%s: Couldn't find stream type: %d\n", __func__, stream_type);
    return FALSE;
  }
  TMDBG_HIGH("stream type: %d, identity: %x", stream->streaminfo.stream_type,
    stream->streaminfo.identity);
  TMDBG_HIGH("set out buf idx: %d", out_buf_desc[stream->streamid - 1].map_buf->buf_index);
  return sendModuleEvent(module, MCT_EVENT_MODULE_PPROC_SET_OUTPUT_BUFF,
    MCT_EVENT_DOWNSTREAM, out_buf_desc[stream->streamid - 1].map_buf,
    stream->streaminfo.identity);
}

/** Name: doReprocess
 *
 *  Arguments/Fields:
 *  @frame_idx: frame number
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends message do reprocess to pproc module. Some
 *  pproc parameters (crop, rotation, etc.) are configured by this
 *  message
 **/
boolean TM_Interface::doReprocess(uint32_t frame_idx,
  cam_stream_type_t stream_type, tm_img_buf_desc_t *p_in_buf_desc)
{
  mct_event_t               event;
  mct_module_t              *module;
  cam_stream_parm_buffer_t  parm_buf;
  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;
  tm_img_buf_desc_t       *p_buf_desc;

  if (!modules) {
    CDBG_ERROR("%s: NULL modules list", __func__);
    return FALSE;
  }
  module = getModule("pproc");
  if (!module) {
    CDBG_ERROR("%s: No module present in TM ctx", __func__);
    return FALSE;
  }

  if (p_in_buf_desc) {
    p_buf_desc = p_in_buf_desc;
  } else {
    p_buf_desc = &in_buf_desc;
  }

  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  if (!stream) {
    TMDBG_ERROR("%s: Couldn't find stream type: %d\n", __func__, stream_type);
    return FALSE;
  }

  memset(&event, 0, sizeof(mct_event_t));
  memset(&parm_buf, 0, sizeof(cam_stream_parm_buffer_t));
  event.identity = stream->streaminfo.identity;
  event.direction = MCT_EVENT_DOWNSTREAM;
  event.type = MCT_EVENT_CONTROL_CMD;
  event.u.ctrl_event.type = MCT_EVENT_CONTROL_PARM_STREAM_BUF;
  event.u.ctrl_event.control_event_data = (void *)&parm_buf;

  parm_buf.type = CAM_STREAM_PARAM_TYPE_DO_REPROCESS;
  parm_buf.reprocess.frame_idx = frame_idx; /* Frame id */
  parm_buf.reprocess.buf_index = p_buf_desc->map_buf->buf_index;

  if (meta_buf_desc.map_buf) {
    parm_buf.reprocess.meta_present = 1;
    parm_buf.reprocess.meta_buf_index = meta_buf_desc.map_buf->buf_index;
    parm_buf.reprocess.meta_stream_handle = stream->streamid;
  }
  return module->process_event(module, &event);
}

boolean TM_Interface::streamOn(cam_stream_type_t stream_type)
{
  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;

  CDBG_HIGH("sending stream ON");

  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  if (!stream) {
    TMDBG_ERROR("%s: Couldn't find stream type: %d\n", __func__, stream_type);
    return FALSE;
  }
  return sendControlEventDown(MCT_EVENT_CONTROL_STREAMON, &stream->streaminfo,
    stream->streaminfo.identity);
}


boolean TM_Interface::streamOff(cam_stream_type_t stream_type)
{
  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;

  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  if (!stream) {
    TMDBG_ERROR("%s: Couldn't find stream type: %d\n", __func__, stream_type);
    return FALSE;
  }

  return sendControlEventDown(MCT_EVENT_CONTROL_STREAMOFF, &stream->streaminfo,
    stream->streaminfo.identity);
}

void TM_Interface::sendCtrlSof(mct_bus_msg_isp_sof_t *isp_sof_bus_msg)
{
  boolean rc = FALSE;

  if (!modules) {
    CDBG_ERROR("%s: NULL modules list", __func__);
    return;
  }

  rc = sendControlEventDown(MCT_EVENT_CONTROL_SOF, isp_sof_bus_msg,
    parm_stream_identity);

  if(FALSE == rc) {
    TMDBG_ERROR("%s: FAILED", __func__);
  } else {
    CDBG("%s: Sending SOF ID %d", __func__, isp_sof_bus_msg->frame_id);
  }
}

void TM_Interface::prepSendCtrlSof(uint32_t identity, uint32_t frame_id)
{
  boolean rc = FALSE;
  mct_bus_msg_isp_sof_t sof_event;
  mct_bus_msg_t bus_msg;

  if (!modules) {
    CDBG_ERROR("%s: NULL modules list", __func__);
    return;
  }
  sof_event.frame_id = frame_id;
  sof_event.num_streams = 1;
  sof_event.streamids[0] = identity;
  sof_event.skip_meta = FALSE;

  sendCtrlSof(&sof_event);

  bus_msg.msg = &sof_event;
  bus_msg.sessionid = (identity & 0xFFFF0000) >> 16;
  bus_msg.type = MCT_BUS_MSG_ISP_SOF;
  bus_msg.size = sizeof(mct_bus_msg_isp_sof_t);

  rc = postBusMsg(&bus_msg);
  if (FALSE == rc) {
    TMDBG_ERROR("%s: bus msg fail", __func__);
  }
}

boolean TM_Interface::sendIspOutDims(cam_stream_type_t stream_type)
{
  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;

  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  if (!stream) {
    TMDBG_ERROR("%s: Couldn't find stream type: %d\n", __func__, stream_type);
    return FALSE;
  }
  saveStreamIspOutDims(&stream->streaminfo.dim, stream->streaminfo.identity);

  return sendModuleEventDown(MCT_EVENT_MODULE_ISP_OUTPUT_DIM,
    &stream->streaminfo, stream->streaminfo.identity);
}

boolean TM_Interface::sendStreamCropDims(cam_stream_type_t stream_type)
{
  mct_bus_msg_stream_crop_t stream_crop;

  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;

  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  TMASSERT(!stream, TRUE, "%s: Couldn't find stream type: %d\n", __func__,
    stream_type);

  stream_crop.crop_out_x = stream->streaminfo.dim.width;
  stream_crop.crop_out_y = stream->streaminfo.dim.height;
  stream_crop.x = 0;
  stream_crop.y = 0;
  stream_crop.x_map = 0;
  stream_crop.y_map = 0;
  stream_crop.width_map  = stream->streaminfo.dim.width;
  stream_crop.height_map = stream->streaminfo.dim.height;
  stream_crop.stream_id = stream->streamid;
  stream_crop.session_id = session_id;

  return sendModuleEventDown(MCT_EVENT_MODULE_STREAM_CROP, &stream_crop,
    stream->streaminfo.identity);
}

boolean TM_Interface::sendBufDivert(cam_stream_type_t stream_type, uint32_t cnt,
  uint32_t *ack_flag, tm_img_buf_desc_t *p_in_buf_desc)
{
  boolean ret;
  isp_buf_divert_t buf_divert;
  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;
  void *addr[2];
  tm_img_buf_desc_t       *p_buf_desc;

  if (p_in_buf_desc) {
    p_buf_desc = p_in_buf_desc;
  } else {
    p_buf_desc = &in_buf_desc;
  }

  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  TMASSERT(!stream, TRUE, "%s: Couldn't find stream type: %d\n", __func__,
    stream_type);

  memset(&buf_divert, 0, sizeof(buf_divert));

  addr[0] = p_buf_desc->map_buf->buf_planes[0].buf;

  buf_divert.vaddr = addr;
  buf_divert.fd = p_buf_desc->map_buf->buf_planes[0].fd;
  buf_divert.identity = stream->streaminfo.identity;
  buf_divert.stats_valid = FALSE;
  buf_divert.is_uv_subsampled = FALSE;
  buf_divert.native_buf = TRUE;
  buf_divert.ack_flag = TRUE;

  buf_divert.buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  buf_divert.buffer.memory = V4L2_MEMORY_USERPTR;
  buf_divert.buffer.index = p_buf_desc->map_buf->buf_index;
  buf_divert.buffer.length = p_buf_desc->map_buf->num_planes;
  buf_divert.buffer.sequence = cnt;
  gettimeofday(&buf_divert.buffer.timestamp, NULL);


  UCDBG(" send buf seq %d, addr: 0x%x, index: %d for processing\n",
    buf_divert.buffer.sequence,
    *(uint32_t *)buf_divert.vaddr, buf_divert.buffer.index);

  ret = sendModuleEventDown(MCT_EVENT_MODULE_BUF_DIVERT, &buf_divert,
    stream->streaminfo.identity);

  if (ack_flag) {
    TMDBG("ack flag: %d", buf_divert.ack_flag);
    *ack_flag = buf_divert.ack_flag;
  } else {
    TMDBG_ERROR("NULL ack_flag");
    ret = FALSE;
  }

  return ret;
}

boolean TM_Interface::triggerRestart(void)
{
  /* Interface to house a mechanism to trigger camera
  *  restart, typically with new settings such as changed
  *  image resolution/format, fps, etc.
  */
  return TRUE;
}

/** tmSendSuperparam:
 *  @param: parameter id
 *  @data: parameter payload
 *  @frame_id: frame number
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends parameters read from xml file into array.
 *  There could be more than one parameters read this way.
 **/
boolean TM_Interface::tmSendSuperparam(cam_intf_parm_type_t param, void *data,
  uint32_t frame_id)
{
  boolean rc = FALSE;
  mct_event_super_control_parm_t  super_param;
  mct_event_control_parm_t param_events;

  super_param.parm_events = &param_events;

  super_param.identity = parm_stream_identity;
  super_param.frame_number = frame_id;
  super_param.num_of_parm_events = 1;
  super_param.parm_events->type = param;
  super_param.parm_events->parm_data = data;

  rc = sendControlEventDown(MCT_EVENT_CONTROL_SET_SUPER_PARM, &super_param,
    parm_stream_identity);

  if (rc == FALSE) {
    TMDBG_ERROR("in superparam \n");
  }
  return rc;
}

/** tmSendParams:
 *  @params_lut: parameters array
 *  @parm_iter: current iteration of parameters
 *  @frame_id: frame number
 *  Return: Boolean result for success/failure
 *
 *  Description: Takes current parameters iteration from parameter
 *  array read from xml file and sends it as super parameter.
 **/
boolean TM_Interface::tmSendParams(tm_params_type_lut_t *params_lut,
  uint32_t param_iter, uint32_t frame_id)
{
  boolean rc, ret = TRUE;
  uint32_t cnt;
  tm_params_t *params;
  tm_params_t *cur_param;
  if (!params_lut) {
    TMDBG_ERROR("Invalid params LUT");
    return FALSE;
  }
  uint32_t params_cnt = params_lut->params_group[param_iter].cnt;

  if(param_iter >= params_lut->groups) {
    TMDBG_ERROR("No more parameter groups");
    return FALSE;
  }
  TMDBG("\n\tGoing to send %d superparams from xml \n", params_cnt);

  params = &params_lut->params_group[param_iter].tbl[0];

  for (cnt = 0 ; cnt < params_cnt ; cnt++) {
    cur_param = &params[cnt];
    rc = tmSendSuperparam(cur_param->val, cur_param->payload, frame_id);
    TMDBG("send superparam id [%d]", cur_param->val);

    if (rc == FALSE) {
      TMDBG_ERROR("in superparam [%d]\n",
        cur_param->val);
      ret = FALSE;
    }
  }
  return ret;
}

/** sendSuperParamsXmlStyle:
 *  @parm_iter: current iteration of parameters
 *  @frame_id: frame number
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends parameters read from xml file into array.
 *  There could be more than one parameters read this way.
 **/
boolean TM_Interface::sendSuperParamsXmlStyle(uint32_t parm_iter,
  uint32_t frame_id)
{
  boolean rc;

  rc = tmSendParams(&m_input.params_lut, parm_iter, frame_id);
  return rc;
}

/** sendPortModuleEvent:
 *  @port: port descriptor
 *  @event_type: event id
 *  @direction: event direction
 *  @event_data: event payload
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends module event on given module port with image
 *  stream identity.
 **/
boolean TM_Interface::sendPortModuleEvent(mct_port_t *port,
  mct_event_module_type_t event_type, mct_event_direction direction,
  void *event_data, uint32_t identity)
{
  boolean                   rc;
  mct_event_t               event;

  if (!port) {
    TMDBG_ERROR("port is NULL");
    return FALSE;
  }

  memset(&event, 0, sizeof(event));
  event.type = MCT_EVENT_MODULE_EVENT;
  event.identity = identity;
  event.direction = direction;
  event.u.module_event.type = event_type;
  event.u.module_event.module_event_data = event_data;

  if (!port->event_func) {
    TMDBG_ERROR("no port event function");
    return FALSE;
  }
  rc = port->event_func(port, &event);
  if (rc == FALSE) {
    TMDBG_ERROR("%s:%d] module event: %d\n", __func__, __LINE__, event_type);
  }
  return rc;
}

/** sendModuleEvent:
 *  @module: module descriptor
 *  @event_type: event id
 *  @direction: event direction
 *  @event_data: event payload
 *  @identity: session plus stream identity
 *  Return: Boolean result for success/failure
 *
 *  Description: Finds given module sink port linked for given identity and
 *  sends module event.
 **/
boolean TM_Interface::sendModuleEvent(mct_module_t *module,
  mct_event_module_type_t event_type, mct_event_direction direction,
  void *event_data, uint32_t identity)
{
  boolean                   rc;
  mct_port_t                *port = NULL;
  mct_list_t                *port_container;
  unsigned int              info;

  if (!module) {
    TMDBG_ERROR("module is NULL");
    return FALSE;
  }
  info = identity;
  port_container = mct_list_find_custom(MCT_MODULE_SINKPORTS(module), &info,
    tm_intf_find_linked_port);
  if (!port_container) {
    TMDBG_ERROR("%s: Could not find port\n", __func__);
    return FALSE;
  }
  port = MCT_PORT_CAST(port_container->data);
  if (!port || !port->event_func) {
    TMDBG_ERROR("Port cannot handle event\n");
    return FALSE;
  }
  TMDBG("%s: going to send event %d on port %s with identity: 0x%x",
    __func__, event_type, port->object.name, identity);

  rc = sendPortModuleEvent(port, event_type, direction, event_data, identity);
  if (rc == FALSE) {
    TMDBG_ERROR("on send module event: %d\n", event_type);
  }
  return rc;
}

/** sendModuleEventDown:
 *  @event_type: event id
 *  @event_data: event payload
 *  @identity: stream identity
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends downstream module event on first linked module
 *  with image stream identity.
 **/
boolean TM_Interface::sendModuleEventDown(mct_event_module_type_t event_type,
  void *event_data, uint32_t identity)
{
  mct_module_t *module;

  module = getFirstModule();
  return sendModuleEvent(module, event_type, MCT_EVENT_DOWNSTREAM, event_data,
    identity);
}

/** sendModuleEventUp:
 *  @port: port descriptor
 *  @event_type: event id
 *  @event_data: event payload
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends upstream module event on given port.
 **/
boolean TM_Interface::sendModuleEventUp(mct_port_t *port,
  mct_event_module_type_t event_type, void *event_data, uint32_t identity)
{
  return sendPortModuleEvent(port, event_type, MCT_EVENT_UPSTREAM, event_data,
    identity);
}

/** sendModuleEvent:
 *  @event_type: event id
 *  @direction: event direction
 *  @event_data: event payload
 *  @identity: session plus stream identity
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends control event with given identity on first linked
 *  module or on first linked module port if dummy input port is used. In
 *  input parameter "is_first_module_src" is set then first module can be
 *  source and dummy input port is not used.
 **/
boolean TM_Interface::send_control_event(mct_event_control_type_t event_type,
  mct_event_direction direction, void *event_data, uint32_t identity)
{
  boolean                   rc;
  mct_event_t               event;
  mct_module_t              *module;
  mct_list_t                *port_container;
  mct_port_t                *port = NULL;

  module = getFirstModule();
  if (!module) {
    TMDBG_ERROR("module is NULL");
    return FALSE;
  }
  if (FALSE == m_input.is_first_module_src) {
    port_container = mct_list_find_custom(
      MCT_MODULE_SINKPORTS(module), &identity, tm_intf_find_linked_port);
    if (!port_container) {
      TMDBG_ERROR("Could not find port\n");
      return FALSE;
    }

    port = MCT_PORT_CAST(port_container->data);
    if (!port) {
      TMDBG_ERROR("Port cannot handle event\n");
      return FALSE;
    }
    CDBG_HIGH("%s: will send event %d on port <%s>", __func__, event_type,
      port->object.name);
  } else {
    CDBG_HIGH("%s: will send event %d on module <%s>", __func__, event_type,
      module->object.name);
  }

  memset(&event, 0, sizeof(event));
  event.identity = identity;
  event.direction = direction;
  event.type = MCT_EVENT_CONTROL_CMD;
  event.u.ctrl_event.type = event_type;
  event.u.ctrl_event.control_event_data = event_data;

  if (port)
    rc = port->event_func(port, &event);
  else
    rc = module->process_event(module, &event);

  if (rc == FALSE) {
    TMDBG_ERROR("on send control event: %d\n", event_type);
  }
  return rc;
}

/** sendControlEventDown:
 *  @event_type: event id
 *  @event_data: event payload
 *  @identity: session plus stream identity
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends downstream control event with given identity.
 **/
boolean TM_Interface::sendControlEventDown(mct_event_control_type_t event_type,
  void *event_data, uint32_t identity)
{
  return send_control_event(event_type, MCT_EVENT_DOWNSTREAM, event_data, identity);
}

/** sendControlEventUp:
 *  @event_type: event id
 *  @event_data: event payload
 *  @identity: session plus stream identity
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends upstream control event with given identity.
 **/
boolean TM_Interface::sendControlEventUp(mct_event_control_type_t event_type,
  void *event_data, uint32_t identity)
{
  return send_control_event(event_type, MCT_EVENT_UPSTREAM, event_data, identity);
}

boolean TM_Interface::checkKeyPress()
{
  int ch;
  int oldf;

  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  ch = getchar();

  fcntl(STDIN_FILENO, F_SETFL, oldf);

  if(ch != EOF)
  {
    return TRUE;
  }
  return FALSE;
}

/*
*  Name: checkForTermination()
*  Result: boolean result based on testing conditions
*  Description:
*  A flexible interface that can be used to check for different
*  termination conditions for exiting a test case.
*  TRUE: Terminate; FALSE: Continue
*/
boolean TM_Interface::checkForTermination(tm_intf_term_cond_type_t cond_type,
  void* data, cam_stream_type_t stream_type)
{
  boolean rc = TRUE;
  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;

  if (!data) {
    TMDBG_ERROR("NULL ptr detected: data [%p]", data);
    return TRUE;
  }
  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  if (!stream) {
    TMDBG_ERROR("%s: Couldn't find stream type: %d\n", __func__, stream_type);
    return FALSE;
  }

  switch (cond_type)
  {
    case TM_CHECK_KEY:
      rc = checkKeyPress();
      break;
    case TM_CHECK_ITERATION: {
      /* Loop termination based on loop count */
      uint32_t count = *(uint32_t *)data;
      uint32_t num_iterations;

      rc = checkKeyPress();
      if (TRUE == rc) {
        break;
      }

      if (CAM_STREAMING_MODE_CONTINUOUS == stream->streaminfo.streaming_mode) {
        num_iterations = m_input.frame_count;
      } else {
        num_iterations = m_input.num_burst[stream->streamid - 1];
      }

      TMDBG("max_num_iterations: %d, counter: %d", num_iterations,
        count);
      if ( count >= num_iterations) {
        return TRUE;
        }
      else {
        return FALSE;
        }
      }
      break;

    case TM_CHECK_VALUE:
      break;
    default:
      break;
  }
  return rc;
}

/** printBusMsg:
 *  @bus_msg: bus message structure
 *
 *  Description: Helper function that prints bus message ID.
 **/
void TM_Interface::printBusMsg(mct_bus_msg_t *bus_msg)
{
  if (!bus_msg) {
    return;
  }
  switch (bus_msg->type) {
  TM_ADD_CASE(MCT_BUS_MSG_ISP_SOF)
  TM_ADD_CASE(MCT_BUS_MSG_ISP_STATS_AF) /* mct_bus_msg_isp_stats_af_t */
  TM_ADD_CASE(MCT_BUS_MSG_ISP_SESSION_CROP) /* mct_bus_msg_session_crop_info_t */
  TM_ADD_CASE(MCT_BUS_MSG_Q3A_AF_STATUS)/* mct_bus_msg_isp_stats_af_t */
  TM_ADD_CASE(MCT_BUS_MSG_PREPARE_HW_DONE) /* cam_prep_snapshot_state_t */
  TM_ADD_CASE(MCT_BUS_MSG_ZSL_TAKE_PICT_DONE)  /* cam_frame_idx_range_t */
  TM_ADD_CASE(MCT_BUS_MSG_HIST_STATS_INFO) /* cam_hist_stats_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_STATS_AEC_INFO) /*mct_bus_msg_stats_aec_metadata_t*/
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_STATS_AWB_INFO) /*mct_bus_msg_isp_stats_awb_metadata_t*/
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_GAMMA_INFO) /*mct_bus_msg_isp_gamma_t*/
  TM_ADD_CASE(MCT_BUS_MSG_ERROR_MESSAGE) /*mct_bus_msg_error_message_t*/
  TM_ADD_CASE(MCT_BUS_MSG_AE_INFO) /* cam_ae_params_t */
  TM_ADD_CASE(MCT_BUS_MSG_SENSOR_STARTING)/*NULL*/
  TM_ADD_CASE(MCT_BUS_MSG_SENSOR_STOPPING)/*NULL*/
  TM_ADD_CASE(MCT_BUS_MSG_SEND_HW_ERROR)  /*NULL*/
  TM_ADD_CASE(MCT_BUS_MSG_SENSOR_AF_STATUS)
  TM_ADD_CASE(MCT_BUS_MSG_AUTO_SCENE_INFO) /* mct_bus_msg_asd_decision_t */
  TM_ADD_CASE(MCT_BUS_MSG_SCENE_MODE) /* int32_t*/
  TM_ADD_CASE(MCT_BUS_MSG_AE_EZTUNING_INFO) /* ae_eztuning_params_t */
  TM_ADD_CASE(MCT_BUS_MSG_AWB_EZTUNING_INFO)/* awb_eztuning_params_t */
  TM_ADD_CASE(MCT_BUS_MSG_AF_EZTUNING_INFO) /* af_eztuning_params_t */
  TM_ADD_CASE(MCT_BUS_MSG_ASD_EZTUNING_INFO)
  TM_ADD_CASE(MCT_BUS_MSG_AF_MOBICAT_INFO)
  TM_ADD_CASE(MCT_BUS_MSG_AE_EXIF_DEBUG_INFO)
  TM_ADD_CASE(MCT_BUS_MSG_AWB_EXIF_DEBUG_INFO)
  TM_ADD_CASE(MCT_BUS_MSG_AF_EXIF_DEBUG_INFO)
  TM_ADD_CASE(MCT_BUS_MSG_ASD_EXIF_DEBUG_INFO)
  TM_ADD_CASE(MCT_BUS_MSG_STATS_EXIF_DEBUG_INFO)
  TM_ADD_CASE(MCT_BUS_MSG_ISP_CHROMATIX_LITE)
  TM_ADD_CASE(MCT_BUS_MSG_PP_CHROMATIX_LITE)
  TM_ADD_CASE(MCT_BUS_MSG_ISP_META)
  TM_ADD_CASE(MCT_BUS_MSG_SENSOR_BET_META)
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_LENS_SHADING_INFO) /* cam_lens_shading_map_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_LENS_SHADING_MODE) /* cam_lens_shading_mode_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_LENS_SHADING_MAP_MODE) /* cam_lens_shading_map_mode_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_TONE_MAP) /* cam_tonemap_curve_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_TONE_MAP_MODE) /* cam_tonemap_mode_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_CC_MODE) /* cam_color_correct_mode_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_CC_TRANSFORM) /* cam_color_correct_matrix_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_PRED_CC_TRANSFORM) /* cam_color_correct_matrix_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_PRED_CC_GAIN) /* cam_color_correct_gains_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_CC_GAIN) /* cam_color_correct_gains_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_BLACK_LEVEL_LOCK) /* uint8_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_CONTROL_MODE) /* uint8_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_ABF_MODE) /* uint8_t */
  TM_ADD_CASE(MCT_BUS_MSG_AEC_IMMEDIATE)   /* mct_bus_msg_aec_immediate_t */
  TM_ADD_CASE(MCT_BUS_MSG_AEC)           /* TODO */
  TM_ADD_CASE(MCT_BUS_MSG_AF_IMMEDIATE) /* mct_bus_msg_af_immediate_t */
  TM_ADD_CASE(MCT_BUS_MSG_AF)          /* mct_bus_msg_af_t */
  TM_ADD_CASE(MCT_BUS_MSG_AWB)          /* mct_bus_msg_awb_immediate_t */
  TM_ADD_CASE(MCT_BUS_MSG_AWB_IMMEDIATE)/* mct_bus_msg_awb_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ISP_CAPTURE_INTENT) /* uint8_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_AEC_STATE) /*uint8_t*/
  TM_ADD_CASE(MCT_BUS_MSG_SET_AEC_RESET) /* NULL */
  TM_ADD_CASE(MCT_BUS_MSG_SET_SENSOR_SENSITIVITY) /*int32_t*/
  TM_ADD_CASE(MCT_BUS_MSG_SET_SENSOR_EXPOSURE_TIME) /* int64_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_SENSOR_FRAME_DURATION) /* int64_t */
  TM_ADD_CASE(MCT_BUS_MSG_ISP_CROP_REGION) /* cam_crop_region_t */
  TM_ADD_CASE(MCT_BUS_MSG_AFD) /* mct_bus_msg_afd_t */
  TM_ADD_CASE(MCT_BUS_MSG_SENSOR_FLASH_MODE) /* cam_flash_ctrl_t */
  TM_ADD_CASE(MCT_BUS_MSG_SENSOR_FLASH_STATE) /* cam_flash_state_t */
  TM_ADD_CASE(MCT_BUS_MSG_FRAME_DROP) /*uint32_t: stream type*/
  TM_ADD_CASE(MCT_BUS_MSG_SET_SHARPNESS) /* int32_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_EFFECT) /* int32_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_EDGE_MODE) /* cam_edge_application_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_NOISE_REDUCTION_MODE) /* int32_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_WAVELET_DENOISE) /* cam_denoise_param_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_TEMPORAL_DENOISE) /* cam_denoise_param_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_FPS_RANGE) /* cam_fps_range_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_ROTATION) /* cam_rotation_t */
  TM_ADD_CASE(MCT_BUS_MSG_REPROCESS_STAGE_DONE) /* NULL */
  TM_ADD_CASE(MCT_BUS_MSG_PREPARE_HDR_ZSL_DONE) /* cam_prep_snapshot_state_t */
  TM_ADD_CASE(MCT_BUS_MSG_CLOSE_CAM)        /* Simulated message for ending process */
  TM_ADD_CASE(MCT_BUS_MSG_DELAY_SUPER_PARAM)       /* mct_bus_msg_delay_dequeue_t */
  TM_ADD_CASE(MCT_BUS_MSG_FRAME_SKIP)       /* mct_bus_msg_delay_dequeue_t */
  TM_ADD_CASE(MCT_BUS_MSG_SENSOR_EXPOSURE_TIMESTAMP) /* int64_t */
  TM_ADD_CASE(MCT_BUS_MSG_SENSOR_ROLLING_SHUTTER_SKEW) /* int64_t */
  TM_ADD_CASE(MCT_BUS_MSG_EZTUNE_JPEG)
  TM_ADD_CASE(MCT_BUS_MSG_EZTUNE_RAW)
  TM_ADD_CASE(MCT_BUS_MSG_PROFILE_TONE_CURVE) /*cam_profile_tone_curve */
  TM_ADD_CASE(MCT_BUS_MSG_NEUTRAL_COL_POINT)   /* cam_neutral_col_point_t */
  TM_ADD_CASE(MCT_BUS_MSG_SET_CAC_MODE) /*cam_aberration_mode_t*/
  TM_ADD_CASE(MCT_BUG_MSG_OTP_WB_GRGB) /* float */
  TM_ADD_CASE(MCT_BUS_MSG_LED_REDEYE_REDUCTION_MODE)
  TM_ADD_CASE(MCT_BUS_MSG_TEST_PATTERN_DATA)
  TM_ADD_CASE(MCT_BUS_MSG_LED_MODE_OVERRIDE)
  TM_ADD_CASE_PRINT(MCT_BUS_MSG_SET_FACEDETECT_MODE, "face detect mode: %d",
    *(uint8_t *)bus_msg->msg);
  TM_ADD_CASE_PRINT(MCT_BUS_MSG_SENSOR_APERTURE, ": %f",
    *(float *)bus_msg->msg);
  TM_ADD_CASE_PRINT(MCT_BUS_MSG_SENSOR_FOCAL_LENGTH, ": %f",
    *(float *)bus_msg->msg);
  TM_ADD_CASE_PRINT(MCT_BUS_MSG_SENSOR_OPT_STAB_MODE, ": %d",
    *(uint8_t *)bus_msg->msg);
  TM_ADD_CASE_PRINT(MCT_BUS_MSG_SENSOR_FILTERDENSITY, ": %f",
    *(float *)bus_msg->msg);

  case MCT_BUS_MSG_FACE_INFO: {
    uint8_t cnt;
    cam_face_detection_data_t *faces_data =
      (cam_face_detection_data_t *)bus_msg->msg;
    CDBG_HIGH("%s: MCT_BUS_MSG_FACE_INFO, frame id: %d, faces detected: %d",
      __func__, faces_data->frame_id, faces_data->num_faces_detected);
    CDBG_HIGH("%s: fd type: %d", __func__, faces_data->fd_type);
    for (cnt = 0 ; cnt < faces_data->num_faces_detected ; cnt++) {
      CDBG_HIGH("%s: face boundary - start: %dx%d, size: %dx%d", __func__,
        faces_data->faces[cnt].face_boundary.top,
        faces_data->faces[cnt].face_boundary.left,
        faces_data->faces[cnt].face_boundary.width,
        faces_data->faces[cnt].face_boundary.height);
    }
  }
  break;
  case MCT_BUS_MSG_META_CURRENT_SCENE: {
    cam_scene_mode_type *scene = (cam_scene_mode_type *)bus_msg->msg;
    CDBG_HIGH("%s: MCT_BUS_MSG_META_CURRENT_SCENE: %d", __func__, *scene);
  }
    break;
  case MCT_BUS_MSG_SENSOR_INFO: {
    cam_sensor_params_t *sen_params = (cam_sensor_params_t *)bus_msg->msg;
    CDBG_HIGH("%s: MCT_BUS_MSG_SENSOR_INFO: flash mode: %d, flash state: %d,"
      "focal length: %f, aperture value: %f",
      __func__, sen_params->flash_mode, sen_params->flash_state,
      sen_params->focal_length, sen_params->aperture_value);
  }
    break;
  case MCT_BUS_MSG_SET_SENSOR_INFO: {
    mct_bus_msg_sensor_metadata_t *sen_meta =
      (mct_bus_msg_sensor_metadata_t *)bus_msg->msg;
    CDBG_HIGH("%s: MCT_BUS_MSG_SET_SENSOR_INFO: chromatix ptr: %p"
      "common chromatix ptr: %p, cpp chromatix ptr: %p",
      __func__, sen_meta->chromatix_ptr, sen_meta->common_chromatix_ptr,
      sen_meta->cpp_chromatix_ptr);
  }
    break;
  default:
    CDBG_HIGH("%s: received bus message: %d", __func__, bus_msg->type);
  }
}

/** parseMetadata:
 *  @bus_msg: bus message structure
 *
 *  Description: Helper function that parses metadata to output buffer.
 **/
boolean TM_Interface::parseMetadata(mct_bus_msg_t *bus_msg,
  tm_intf_buffer_desc_t *buf)
{
  TMASSERT(!bus_msg || !buf, TRUE);

  switch (bus_msg->type) {
  case MCT_BUS_MSG_FACE_INFO: {
    cam_face_detection_data_t *faces_data;
  }
  break;
  case MCT_BUS_MSG_SET_FACEDETECT_MODE: {
    cam_face_detect_mode_t *facedetect_mode;
  }
    break;
  case MCT_BUS_MSG_META_CURRENT_SCENE: {
    cam_scene_mode_type *scene = (cam_scene_mode_type *)bus_msg->msg;
    CDBG_HIGH("%s: MCT_BUS_MSG_META_CURRENT_SCENE: %d", __func__, *scene);
  }
    break;
  case MCT_BUS_MSG_SENSOR_INFO: {
    cam_sensor_params_t *sen_params = (cam_sensor_params_t *)bus_msg->msg;
    CDBG_HIGH("%s: MCT_BUS_MSG_SENSOR_INFO: flash mode: %d, flash state: %d,"
      "focal length: %f, aperture value: %f",
      __func__, sen_params->flash_mode, sen_params->flash_state,
      sen_params->focal_length, sen_params->aperture_value);
  }
    break;
  case MCT_BUS_MSG_SET_SENSOR_INFO: {
    mct_bus_msg_sensor_metadata_t *sen_meta =
      (mct_bus_msg_sensor_metadata_t *)bus_msg->msg;
    CDBG_HIGH("%s: MCT_BUS_MSG_SET_SENSOR_INFO: chromatix ptr: %p"
      "common chromatix ptr: %p, cpp chromatix ptr: %p",
      __func__, sen_meta->chromatix_ptr, sen_meta->common_chromatix_ptr,
      sen_meta->cpp_chromatix_ptr);
  }
    break;
  default:
    CDBG_HIGH("%s: received bus message: %d", __func__, bus_msg->type);
  }
  return TRUE;
}

/** Name: saveBusHandle
 *
 *  Arguments/Fields:
 *  @bus: bus descriptor
 *
 *  Description: Saves bus descriptor handle for bus created outside
 *  of this class. Bus handles will be used in some methods of this
 *  class. Should be called after bus creation.
 **/
void TM_Interface::saveBusHandle(mct_bus_t *bus)
{
  p_mct_bus = bus;
}

boolean TM_Interface::postBusMsg(mct_bus_msg_t *bus_msg)
{
  mct_bus_t *bus;

  bus = pipeline->bus;
  TMASSERT ((!bus || !bus->post_msg_to_bus), TRUE, "Couldn't find pipeline\n");

  bus->post_msg_to_bus(bus, bus_msg);

  return TRUE;
}

boolean TM_Interface::saveBusMsg(mct_bus_msg_t *bus_msg,
  mct_bus_msg_t **out_bus_msg)
{
  mct_bus_msg_t *ret_bus_msg = *out_bus_msg;
  if (!bus_msg || !ret_bus_msg) {
    TMDBG_ERROR("Invalid input args: bus_msg: %p, ret_bus_msg: %p",
      bus_msg, ret_bus_msg);
    return FALSE;
  }
  memcpy(ret_bus_msg, bus_msg, sizeof(mct_bus_msg_t));

  if (bus_msg->size) {
    ret_bus_msg->msg = malloc(bus_msg->size);
    if (ret_bus_msg->msg) {
      memcpy(ret_bus_msg->msg, bus_msg->msg, bus_msg->size);
    } else {
      TMDBG_ERROR("Failed to allocate bus msg size <%d> to save message!",
        bus_msg->size);
      return FALSE;
    }
  } else {
    switch (bus_msg->type) {
    case MCT_BUS_MSG_ISP_SOF:
      bus_msg->size = sizeof(mct_bus_msg_isp_sof_t);
      ret_bus_msg->msg = malloc(bus_msg->size);
      if (ret_bus_msg->msg) {
        memcpy(ret_bus_msg->msg, bus_msg->msg, bus_msg->size);
      } else {
        TMDBG_ERROR("Failed to allocate bus msg size <%d> to save message!",
          bus_msg->size);
        return FALSE;
      }
      break;
    default:
      TMDBG_ERROR("Bus msg size is ZERO!");
      return FALSE;
    }
  }
  return TRUE;
}

boolean TM_Interface::processMetadata(mct_bus_msg_type_t type,
  mct_bus_msg_t *ret_bus_msg)
{
  boolean rc = FALSE;
  mct_bus_msg_t *bus_msg = NULL;
  mct_bus_msg_t *new_bus_msg = NULL;

  if (!ret_bus_msg) {
    TMDBG_ERROR("%s: NULL ptr detected: bus_msg [%p]",
      __func__, ret_bus_msg);
    return FALSE;
  }
  if (!p_mct_bus) {
    TMDBG_ERROR("%s: NULL mct bus ptr", __func__);
    return FALSE;
  }

  /* Read messages from priority queue */
  while (1) {
    pthread_mutex_lock(&p_mct_bus->priority_q_lock);
    bus_msg = (mct_bus_msg_t *)mct_queue_pop_head
      (p_mct_bus->priority_queue);
    pthread_mutex_unlock(&p_mct_bus->priority_q_lock);

    if (!bus_msg) {
      break;
    }
    if (bus_msg->type == MCT_BUS_MSG_CLOSE_CAM) {
      break;
    }
    printBusMsg(bus_msg);

    if (bus_msg->type == type) {
      rc = saveBusMsg(bus_msg, &ret_bus_msg);
    }
    if (bus_msg->msg)
      free(bus_msg->msg);

    if (bus_msg)
      free(bus_msg);
  }
  /* Now read other bus messages */
  while (1) {
    pthread_mutex_lock(&p_mct_bus->bus_msg_q_lock);
    new_bus_msg = (mct_bus_msg_t *)
      mct_queue_pop_head(p_mct_bus->bus_queue);
    pthread_mutex_unlock(&p_mct_bus->bus_msg_q_lock);

    if (!new_bus_msg) {
      break;
    }
    printBusMsg(new_bus_msg);

    if (new_bus_msg->type == type) {
      rc = saveBusMsg(new_bus_msg, &ret_bus_msg);
    }
    if (new_bus_msg->msg)
      free(new_bus_msg->msg);
    if (new_bus_msg)
      free(new_bus_msg);
  }
  return rc;
}

void* TM_Interface::busHandlerThread(void *data)
{
  mct_process_ret_t  proc_ret;
  mct_bus_msg_t     *bus_msg;
  mct_bus_msg_t *new_bus_msg = NULL;
  TM_Interface* tm_ctx = ((tm_bus_thread_data_t *)data)->tmi;
  mct_pipeline_t  *pipeline;

  memset(&proc_ret, 0x00, sizeof(mct_process_ret_t));
  if(!tm_ctx) {
    TMDBG_ERROR("%s: Invalid pointer for context", __func__);
    free(data);
    return NULL;
  }
  pipeline = ((tm_bus_thread_data_t *)data)->pipeline;
  free(data);

  do {
    pthread_mutex_lock(pipeline->bus->mct_mutex);
    if(!pipeline->bus->priority_queue->length) {
      CDBG("%s: Waiting on mctl_bus_handle_cond", __func__);
      pthread_cond_wait(pipeline->bus->mct_cond, pipeline->bus->mct_mutex);
    }
    pthread_mutex_unlock(pipeline->bus->mct_mutex);
    CDBG("%s: Received notification on bus_handler thread", __func__);
    /* Received Signal from Pipeline Bus */
    while (1) {
      pthread_mutex_lock(&pipeline->bus->priority_q_lock);
      bus_msg = (mct_bus_msg_t *)
        mct_queue_pop_head(pipeline->bus->priority_queue);
      pthread_mutex_unlock(&pipeline->bus->priority_q_lock);
      if (!bus_msg) {
        break;
      }
      if (bus_msg->type == MCT_BUS_MSG_CLOSE_CAM) {
        goto thread_exit;
      }
      tm_ctx->printBusMsg(bus_msg);

      if (MCT_BUS_MSG_ISP_SOF == bus_msg->type) {
        mct_bus_msg_isp_sof_t *sof_event;
        sof_event =(mct_bus_msg_isp_sof_t *)(bus_msg->msg);
        CDBG_HIGH("%s: MCT_EVENT_MODULE_SOF_NOTIFY: frame id: %d",
            __func__, sof_event->frame_id);
        tm_ctx->sendCtrlSof(sof_event);
      }
      if (bus_msg->msg)
        free(bus_msg->msg);
      if (bus_msg)
        free(bus_msg);
      if (proc_ret.type == MCT_PROCESS_RET_ERROR_MSG) {
        CDBG_HIGH("return message status: %d", proc_ret.u.bus_msg_ret.error);
      }
    }
  } while(1);
thread_exit:
  TMDBG_HIGH("%s: Force Exit", __func__);
  return NULL;
 }

boolean TM_Interface::compareBuffers(void* buf1, uint32_t size1,
  void* buf2, uint32_t size2)
{
  uint32_t index = 0;
  uint8_t *buffer_1, *buffer_2;
  if(!buf1 || !buf2 || (size1 == 0) || (size2 == 0))
  {
    TMDBG_ERROR("%s: Invalid input params. buf1 = [%p], buf2 = [%p],\
      size1 = [%d], size2 = [%d]",
      __func__, buf1, buf2, size1, size2);
    return FALSE;
  }
  if (size1 != size2)
  {
    /* Comparison allowed for equi-sized buffers for now */
    TMDBG_ERROR("%s: Buffer sizes don't match. size1 = [%d], size2 = [%d]",
    __func__, size1, size2);
    return FALSE;
  }
  buffer_1 = (uint8_t *)buf1;
  buffer_2 = (uint8_t *)buf2;
  for (index = 0; index < size1; index++) {
    if(buffer_1[index] != buffer_2[index]) {
      break;
    }
  }
  if(index == size2) {
    TMDBG("Buffers match!");
    return TRUE;
  }
  else {

    TMDBG_ERROR("%s: Buffers mismatch after byte %d", __func__, index);
    return FALSE;
  }
}

/** Name: getOutBufDesc
 *
 *  Arguments/Fields:
 *  @buf_desc: structure to be used for output buffer info.
 *  Return: Boolean result for success/failure
 *
 *  Description: Fills in buffer descriptor structure output buffer
 *  address and size.
 **/
boolean TM_Interface::getOutBufDesc(tm_intf_buffer_desc_t *buf_desc,
  uint32_t identity)
{
  uint32_t streamid = identity & 0xFFFF;
  if (streamid >= TM_MAX_NUM_STREAMS) {
    TMDBG_ERROR("Invalid stream index %d", streamid);
    return FALSE;
  }
  if ((NULL == out_buf_desc[streamid - 1].map_buf) || (NULL == buf_desc))
    return FALSE;

  buf_desc->vaddr     = out_buf_desc[streamid - 1].map_buf->buf_planes[0].buf;
  buf_desc->size      = out_buf_desc[streamid - 1].map_buf->buf_size;
  buf_desc->identity  = identity;
  return TRUE;
}

/** Name: getInBufDesc
 *
 *  Arguments/Fields:
 *  @buf_desc: structure to be used for output buffer info.
 *  Return: Boolean result for success/failure
 *
 *  Description: Fills in buffer descriptor structure output buffer
 *  address and size.
 **/
boolean TM_Interface::getInBufDesc(tm_intf_buffer_desc_t *buf_desc,
  uint32_t identity, tm_img_buf_desc_t *p_in_buf_desc)
{
  tm_img_buf_desc_t       *p_buf_desc;

  if (p_in_buf_desc) {
    p_buf_desc = p_in_buf_desc;
  } else {
    p_buf_desc = &in_buf_desc;
  }
  TMASSERT(((NULL == p_buf_desc->map_buf) || (NULL == buf_desc)), TRUE);

  buf_desc->vaddr     = p_buf_desc->map_buf->buf_planes[0].buf;
  buf_desc->size      = p_buf_desc->map_buf->buf_size;
  buf_desc->identity  = identity;
  return TRUE;
}

boolean TM_Interface::validateResults(tm_intf_validation_type_t v_type,
  void* data)
{
  boolean ret = FALSE;
  uint32_t len_output = 0, len_reference = 0;
  void *output_buf = NULL, *ref_img_buf = NULL;

  if (!data) {
    TMDBG_ERROR("NULL ptr detected: data");
    return FALSE;
  }
  switch (v_type)
  {
    case VALIDATE_METADATA: {
      /* Validate using expected results */
      mct_bus_msg_t *bus_msg = (mct_bus_msg_t *)data;
      if (MCT_BUS_MSG_FACE_INFO == bus_msg->type) {
        uint8_t cnt;
        cam_face_detection_data_t *faces_data =
          (cam_face_detection_data_t *)bus_msg->msg;
        TMASSERT(!faces_data, TRUE);
        TMDBG_HIGH("%s: MCT_BUS_MSG_FACE_INFO, frame id: %d, faces detected: %d",
          __func__, faces_data->frame_id, faces_data->num_faces_detected);
        TMDBG_HIGH("%s: fd type: %d", __func__, faces_data->fd_type);
        for (cnt = 0 ; cnt < faces_data->num_faces_detected ; cnt++) {
          TMDBG_HIGH("face boundary - start: %dx%d, size: %dx%d",
            faces_data->faces[cnt].face_boundary.top,
            faces_data->faces[cnt].face_boundary.left,
            faces_data->faces[cnt].face_boundary.width,
            faces_data->faces[cnt].face_boundary.height);
        }
    }
      ret = TRUE;
    }
      break;
    case VALIDATE_OUTPUT_FRAME: {
      tm_intf_buffer_desc_t *buf_desc = (tm_intf_buffer_desc_t *)data;

      /* Validate using reference output  */
      if(m_input.have_ref_image) {
        if(ref_img_buffer) {
          len_output = buf_desc->size;
          len_reference = m_input.sizes.ref_img_width *
            m_input.sizes.ref_img_height * 3 / 2;
          len_reference = (len_reference +
            (TEST_MANAGER_ALIGN_4K - 1)) & ~(TEST_MANAGER_ALIGN_4K - 1);
          output_buf = (void*)(buf_desc->vaddr);
          ref_img_buf = (void*)(ref_img_buffer);
          ret = compareBuffers(output_buf, len_output,
            ref_img_buf, len_reference);
        }
        else {
          TMDBG_ERROR("Missing reference image buffer. Abort comparison\n");
          ret = FALSE;
        }
      }
      else {
        ret = TRUE;
      }
    }
      break;
    default:
      break;
  }
  return ret;
}

void TM_Interface::fillSensorInfo(sensor_out_info_t *sen_out,
  sensor_set_dim_t *sen_dims)
{
  if (!sen_out || !sen_dims) {
    TMDBG_ERROR("Invalid input args: sen_out: %p, sen_dims %p",
        sen_out, sen_dims);
    return;
  }
  sen_dims->output_format = CAM_FORMAT_BAYER_MIPI_RAW_10BPP_GBRG;
  sen_dims->op_pixel_clk = 320000000;
  sen_dims->dim_output.width  = m_input.sizes.input_width;
  sen_dims->dim_output.height = m_input.sizes.input_height;

  memset(sen_out, 0, sizeof(sensor_out_info_t));
  sen_out->mode = CAMERA_MODE_2D_B;
  sen_out->dim_output.width = sen_dims->dim_output.width;
  sen_out->dim_output.height = sen_dims->dim_output.height;
  sen_out->request_crop.first_line = 0;
  sen_out->request_crop.last_line = sen_dims->dim_output.height - 1;
  sen_out->request_crop.first_pixel = 0;
  sen_out->request_crop.last_pixel = sen_dims->dim_output.width - 1;
  sen_out->op_pixel_clk = sen_dims->op_pixel_clk; /* HZ */
  sen_out->num_frames_skip = 1;
  sen_out->max_gain = 16;
  sen_out->max_linecount = 65535;
  sen_out->vt_pixel_clk = sen_out->op_pixel_clk;
  sen_out->ll_pck = 4572;
  sen_out->fl_lines = sen_dims->dim_output.height + 0x4000;
  sen_out->pixel_sum_factor = 1;
  sen_out->fmt = (cam_format_t)sen_dims->output_format;
  sen_out->is_dummy = TRUE; //will use pattern generator

}

void TM_Interface::fillSensorCap(sensor_src_port_cap_t *sensor_cap,
  uint32_t session_id)
{
  if (!sensor_cap) {
    TMDBG_ERROR("Invalid input arg: sensor_cap");
    return;
  }

  sensor_cap->num_cid_ch = 1;
  sensor_cap->session_id = session_id;
  sensor_cap->sensor_cid_ch[0].cid = 0;
  sensor_cap->sensor_cid_ch[0].csid = 0;
  sensor_cap->sensor_cid_ch[0].csid_version = 0x30010000;
  sensor_cap->sensor_cid_ch[0].fmt = CAM_FORMAT_BAYER_MIPI_RAW_10BPP_GBRG;
}

/** prepareSensorCaps:
 *  @caps: capabilities structure
 *  Return: Boolean result for success/failure
 *
 *  Prepares sensor type capabilities
 **/
boolean TM_Interface::prepareSensorCaps(tm_uc_port_caps_t *caps)
{
  if (!caps) {
    return FALSE;
  }
  memset(&caps->caps, 0, sizeof(mct_port_caps_t));
  // input port should be sensor type - fill with sensor capabilities
  TMDBG("----- fill sen caps -----");
  caps->caps.u.data = malloc(sizeof(sensor_src_port_cap_t));
  if (caps->caps.u.data) {
    fillSensorCap((sensor_src_port_cap_t *)caps->caps.u.data, session_id);
  } else {
    return FALSE;
  }
  caps->caps.port_caps_type = MCT_PORT_CAPS_OPAQUE;
  return TRUE;
}

/** prepareFrameCaps:
 *  @caps: capabilities structure
 *  Return: Boolean result for success/failure
 *
 *  Prepares frame type capabilities
 **/
boolean TM_Interface::prepareFrameCaps(tm_uc_port_caps_t *caps)
{
  if (!caps) {
    return FALSE;
  }
  memset(&caps->caps.u.frame, 0, sizeof(caps->caps.u.frame));
  caps->caps.port_caps_type = MCT_PORT_CAPS_FRAME;
  caps->caps.u.frame.format_flag = MCT_PORT_CAP_FORMAT_YCBCR;

  return TRUE;
}

/** prepareStatsCaps:
 *  @caps: capabilities structure
 *  Return: Boolean result for success/failure
 *
 *  Prepares statistics type capabilities
 **/
boolean TM_Interface::prepareStatsCaps(tm_uc_port_caps_t *caps)
{
  if (!caps) {
    return FALSE;
  }
  memset(&caps->caps.u.frame, 0, sizeof(caps->caps.u.frame));
  caps->caps.port_caps_type = MCT_PORT_CAPS_STATS;

  return TRUE;
}

/** Name: statsGetData
 *
 *  Arguments/Fields:
 *  @event: event pointer
 *
 *  Description: Fills exposure data read from xml.
 **/
void TM_Interface::statsGetData(mct_event_t *event)
{
  stats_get_data_t *stats_get_data;

  if (!event) {
    return;
  }
  stats_get_data =
    (stats_get_data_t *)event->u.module_event.module_event_data;

  stats_get_data->aec_get.real_gain[0] = m_input.stats.real_gain;
  stats_get_data->aec_get.linecount[0] = m_input.stats.line_count;
  stats_get_data->aec_get.exp_time = m_input.stats.exp_time;

  stats_get_data->aec_get.lux_idx = m_input.stats.lux_idx;
  stats_get_data->aec_get.valid_entries = 1;
  stats_get_data->flag = STATS_UPDATE_AEC;

  stats_get_data->flag = STATS_UPDATE_AWB;
  stats_get_data->awb_get.g_gain = 1.0;
  stats_get_data->awb_get.r_gain = 1.0;
  stats_get_data->awb_get.b_gain = 1.0;
}

/** Name: sendStatsUpdate
 *
 *  Arguments/Fields:
 *  @port: port to send update
 *  @sof_id: SOF index
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends exposure update to given port. Exposure data
 *  is read from xml file.
 **/
boolean TM_Interface::sendStatsUpdate(mct_port_t *port, uint32_t sof_id)
{
  if (!port) {
    TMDBG_ERROR("Invalid port ptr");
    return FALSE;
  }
  stats_update_t stats_update;
  uint32_t identity = getStreamIdentity(m_input.stream_types[0]);

  if (cur_fps.max_fps)
    m_input.stats.exp_time = 1 / (float)cur_fps.max_fps;

  stats_update.flag = STATS_UPDATE_AEC;
  stats_update.aec_update.real_gain = m_input.stats.real_gain;
  stats_update.aec_update.linecount = m_input.stats.line_count;
  stats_update.aec_update.exp_time = m_input.stats.exp_time;
  stats_update.aec_update.lux_idx = m_input.stats.lux_idx;
  stats_update.aec_update.sof_id   = sof_id;

  return sendModuleEventUp(port,
    MCT_EVENT_MODULE_STATS_AEC_UPDATE, &stats_update, identity);
}

/** statsSetSuperparam:
 *  @event: control event structure
 *
 *  Description: Sets some stats parameters on super parameter event.
 **/
void TM_Interface::statsSetSuperparam(mct_event_t *event)
{
  if (!event) {
    TMDBG_ERROR("Invalid event ptr");
    return;
  }
  mct_event_super_control_parm_t *ctrl_param =
    (mct_event_super_control_parm_t *)event->u.ctrl_event.control_event_data;
  uint32_t cnt;

  for ( cnt = 0 ; cnt < ctrl_param->num_of_parm_events ; cnt++) {
    TMDBG("%s: set type: %d", __func__, ctrl_param->parm_events[cnt].type);

    switch (ctrl_param->parm_events[cnt].type) {
    case CAM_INTF_PARM_HFR: {
      cam_hfr_mode_t hfr_mode =
        *(cam_hfr_mode_t *)ctrl_param->parm_events[cnt].parm_data;
      TMDBG("set HFR mode: %d", hfr_mode);
      if(hfr_mode == CAM_HFR_MODE_60FPS) {
        TMDBG("set 60 fps");
        m_input.stats.exp_time = (float)1/60;
        TMDBG("new exp time = %f", m_input.stats.exp_time);
      } else if(hfr_mode == CAM_HFR_MODE_90FPS) {
        TMDBG("set 90 fps");
        m_input.stats.exp_time = (float)1/90;
        TMDBG("new exp time = %f", m_input.stats.exp_time);
      } else if(hfr_mode == CAM_HFR_MODE_120FPS) {
        TMDBG("set 120 fps");
        m_input.stats.exp_time = (float)1/120;
        TMDBG("new exp time = %f", m_input.stats.exp_time);
      }
    }
    case CAM_INTF_PARM_FPS_RANGE: {
        cam_fps_range_t *fps =
          (cam_fps_range_t *)ctrl_param->parm_events[cnt].parm_data;
        TMDBG("set FPS min: %f, max: %f", fps->min_fps, fps->max_fps);
        cur_fps = *fps;
      }
      break;
    default:
      break;
    }
  }
}

/** Name: sendAWBStatsUpdate
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Sends awb update on port linked with stats dummy port (ISP).
 **/
boolean TM_Interface::sendAWBStatsUpdate(void)
{
  boolean               rc;
  stats_update_t        stats_update;
  awb_update_t          *awb_update;
  mct_port_t            *port;

  mct_list_t                *port_container;
  unsigned int              info;

  if (!stats_port.num_ports) {
    return FALSE;
  }

  info = getStreamIdentity(m_input.stream_types[0]);
  port_container = mct_list_find_custom(MCT_MODULE_SINKPORTS(stats_port.module),
    &info, tm_intf_find_linked_port);
  if (!port_container) {
    TMDBG_ERROR("%s: Could not find port\n", __func__);
    return FALSE;
  }
  port = MCT_PORT_CAST(port_container->data);
  if (!port) {
    TMDBG_ERROR("no peer port!");
    return FALSE;
  }

  port = MCT_PORT_PEER(port);
  if (!port) {
    TMDBG_ERROR("no peer port!");
    return FALSE;
  }

  CDBG_HIGH("%s: send awb update to port name: %s", __func__, port->object.name);

  stats_update.flag = STATS_UPDATE_AWB;
  awb_update = &stats_update.awb_update;

  memset(awb_update, 0, sizeof(awb_update_t));

  awb_update->color_temp = 5000;
  awb_update->gain.b_gain = 1.0;
  awb_update->gain.g_gain = 0.7;
  awb_update->gain.r_gain = 1.0;

  rc = sendModuleEventUp(port, MCT_EVENT_MODULE_STATS_AWB_UPDATE, &stats_update,
    info);
  if (rc == FALSE) {
    TMDBG_ERROR("setting awb update\n");
    return FALSE;
  }
  return TRUE;
}

boolean TM_Interface::appendToImgBufsList(mct_stream_map_buf_t *buf)
{
  mct_list_t                        *list;
  uint32_t                          cnt;

  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    list = mct_list_append(img_buf_list[cnt], buf, NULL, NULL);
    if (!list) {
      return FALSE;
    }
    img_buf_list[cnt] = list;
  }
  return TRUE;
}

boolean TM_Interface::removeFromImgBufsList(mct_stream_map_buf_t *buf)
{
  mct_list_t                        *list;
  uint32_t                          cnt;

  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    list = mct_list_remove(img_buf_list[cnt], buf);
    if (!list) {
      return FALSE;
    }
    img_buf_list[cnt] = list;
  }
  return TRUE;
}

/** Name: allocateBuffer
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Allocates image buffer.
 *
 **/
boolean TM_Interface::allocateBuffer(tm_img_buf_desc_t *buf_desc,
  cam_mapping_buf_type buf_type, uint32_t num_planes, uint32_t buf_size)
{
  int                   frame_fd = 0;
  uint32_t              cnt;
  mct_stream_map_buf_t  *stream_buf;
  v4l2_frame_buffer_t   *p_v4l_buf;

  TMASSERT(!buf_desc, TRUE);

  p_v4l_buf = &buf_desc->v4l_buf;

  memset(p_v4l_buf, 0, sizeof(v4l2_frame_buffer_t));
  p_v4l_buf->ion_alloc[0].len = buf_size;
  p_v4l_buf->ion_alloc[0].heap_id_mask = (0x1 << ION_IOMMU_HEAP_ID);
  p_v4l_buf->ion_alloc[0].align = TEST_MANAGER_ALIGN_4K;
  p_v4l_buf->addr[0] = (unsigned long)tm_do_mmap_ion(ionfd,
    &(p_v4l_buf->ion_alloc[0]),
    &(p_v4l_buf->fd_data[0]),
    &frame_fd);
  if (!p_v4l_buf->addr[0]) {
    TMDBG_ERROR("%s:%d] error mapping output ion fd\n", __func__, __LINE__);
    goto EXIT7;
  }

  stream_buf =
    (mct_stream_map_buf_t *)malloc(sizeof(mct_stream_map_buf_t));
  if (!stream_buf) {
    TMDBG_ERROR("Cannot allocate output stream buffer descriptor!");
    goto EXIT8;
  }

  memset(stream_buf, 0, sizeof(mct_stream_map_buf_t));
  stream_buf->buf_planes[0].buf =
    (void *)p_v4l_buf->addr[0];
  for (cnt = 0 ; cnt < num_planes ; cnt++) {
    stream_buf->buf_planes[cnt].fd = frame_fd;
  }
  stream_buf->num_planes = num_planes;
  stream_buf->buf_index = img_bufs_idx++;
  stream_buf->buf_size = p_v4l_buf->ion_alloc[0].len;
  stream_buf->buf_type = buf_type;
  stream_buf->common_fd = TRUE;

  buf_desc->map_buf = stream_buf;

  return TRUE;

EXIT8:
  tm_do_munmap_ion(ionfd, &(p_v4l_buf->fd_data[0]),
    (void *)p_v4l_buf->addr[0],
    p_v4l_buf->ion_alloc[0].len);
EXIT7:
  return FALSE;
}

/** Name: allocateFillMetadata
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Allocates metadata buffer. Adds metadata buffer in
 *  image buffers list. Fills metadata.
 *
 **/
boolean TM_Interface::allocateFillMetadata(streams_t *p_streams)
{
  boolean                           ret;
  stats_get_data_t                  stats_get;
  mct_stream_session_metadata_info  *priv_metadata;
  cam_crop_data_t                   crop_data;
  int                               meta_frame_fd = 0;
  int                               rotation;
  metadata_buffer_t                 *metadata_buf;
  uint32_t                          cnt;
  cam_mapping_buf_type              buf_type;

  TMDBG("-------------------fill metadata ------------------------\n");

  if (0 == meta_buf_desc.v4l_buf.addr[0]) {

    if (CAM_STREAM_TYPE_OFFLINE_PROC == m_input.stream_types[0]) {
      buf_type = CAM_MAPPING_BUF_TYPE_OFFLINE_META_BUF;
    } else {
      buf_type = CAM_MAPPING_BUF_TYPE_STREAM_BUF;
    }
    ret = allocateBuffer(&meta_buf_desc, buf_type, 1, sizeof(metadata_buffer_t));
    TMASSERT(FALSE == ret, TRUE, "failed to allocate output buffer");

    ret = appendToImgBufsList(meta_buf_desc.map_buf);
    TMASSERT(FALSE == ret, TRUE, "Failed to append meta buf in image bufs list");
  }

  metadata_buf = (metadata_buffer_t *)meta_buf_desc.v4l_buf.addr[0];

  memset(&stats_get, 0, sizeof(stats_get_data_t));
  priv_metadata = (mct_stream_session_metadata_info *)
    POINTER_OF_META(CAM_INTF_META_PRIVATE_DATA, metadata_buf);
  if (priv_metadata) {
    fillMetadataChromatixPtrs(&priv_metadata->sensor_data);
    stats_get.aec_get.lux_idx = m_input.stats.lux_idx;
    stats_get.aec_get.real_gain[0] = m_input.stats.real_gain;
    memcpy(&priv_metadata->stats_aec_data.private_data, &stats_get,
      sizeof(stats_get_data_t));
  }
  add_metadata_entry(CAM_INTF_META_NOISE_REDUCTION_MODE,
    sizeof(m_input.cpp.denoise_params.denoise_enable),
    &m_input.cpp.denoise_params.denoise_enable, metadata_buf);

  add_metadata_entry(CAM_INTF_META_EDGE_MODE,
    sizeof(m_input.cpp.sharpness_ratio),
    &m_input.cpp.sharpness_ratio, metadata_buf);

  if (m_input.cpp.use_crop) {
    crop_data.num_of_streams = p_streams->num;
    for (cnt = 0 ; cnt < p_streams->num ; cnt++) {
       crop_data.crop_info[cnt].stream_id = p_streams->ids[cnt].id;
      crop_data.crop_info[cnt].crop.left =
        m_input.sizes.win_first_pixel;
      crop_data.crop_info[cnt].crop.top =
        m_input.sizes.win_first_line;
      crop_data.crop_info[cnt].crop.width =
        m_input.sizes.win_width;
      crop_data.crop_info[cnt].crop.height =
        m_input.sizes.win_height;

      TMDBG("in crop data stream id: %d", crop_data.crop_info[cnt].stream_id);
    }
    add_metadata_entry(CAM_INTF_META_CROP_DATA, sizeof(cam_crop_data_t),
      &crop_data, metadata_buf);
  }

  IF_META_AVAILABLE(cam_rotation_info_t, rotation_info,
    CAM_INTF_PARM_ROTATION, metadata_buf) {
    if (m_input.sizes.rotation == 90) {
      rotation_info->rotation = ROTATE_90;
      rotation_info->device_rotation = ROTATE_90;
    } else if (m_input.sizes.rotation == 180) {
      rotation_info->rotation = ROTATE_180;
      rotation_info->device_rotation = ROTATE_180;
    } else if (m_input.sizes.rotation == 270) {
      rotation_info->rotation = ROTATE_270;
      rotation_info->device_rotation = ROTATE_270;
    } else {
      rotation_info->rotation = ROTATE_0;
      rotation_info->device_rotation = ROTATE_0;
    }
  }
  IF_META_AVAILABLE(int32_t, flip, CAM_INTF_PARM_FLIP, metadata_buf) {
    *flip = (int32_t)m_input.sizes.flip;
  }

  return TRUE;
}

/** Name: allocateAndReadInputBufXml
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Allocates and maps in ION input buffer with described in
 *  XML input sizes. Adds input buffer in buffers list.
 *  Reads input data from the described in xml input file name.
 *
 **/
boolean TM_Interface::allocateAndReadInputBufXml(void)
{
  if (TRUE == m_input.read_input_y_only) {
    return allocateAndReadInputBuf(m_input.input_filename[0], NULL, FALSE);
  } else {
    return allocateAndReadInputBuf(m_input.input_filename[0], NULL, TRUE);
  }
}

/** Name: allocateAndReadInputBuf
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Allocates and maps in ION input buffer with given
 *  sizes. Adds input buffer in buffers list. Reads input data from
 *  the given input file name.
 *
 **/
boolean TM_Interface::allocateAndReadInputBuf(const char *fname,
  tm_img_buf_desc_t *p_in_buf_desc, boolean read)
{
  boolean                 ret;
  tm_img_buf_desc_t       *p_buf_desc;
  int                     data_len;
  uint32_t                num_planes;
  cam_format_t            format;

  format = getFormat(m_input.input_format);
  TMASSERT(CAM_FORMAT_MAX == format, TRUE, "wrong format");
  data_len = m_input.sizes.input_width *
    m_input.sizes.input_height * getPixelSize(format);
  TMASSERT(!data_len, TRUE, "wrong buffer size");
  num_planes = getNumPlanes(m_input.input_format);
  TMASSERT(!num_planes, TRUE, "wrong number planes");

  if (p_in_buf_desc) {
    p_buf_desc = p_in_buf_desc;
  } else {
    p_buf_desc = &in_buf_desc;
  }
  TMEXIT(!p_buf_desc, TRUE, EXIT1, "No input buffer descriptor!");

  TMEXIT(!data_len, TRUE, EXIT1, "wrong buffer size");

  TMEXIT(!num_planes, TRUE, EXIT1, "wrong number planes");

  TMDBG("going to allocate out buf: size: %d, num_planes: %d", data_len,
    num_planes);
  ret = allocateBuffer(p_buf_desc, CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF,
    num_planes, data_len);
  TMEXIT(FALSE == ret, TRUE, EXIT1, "failed to allocate input buffer");

  ret = appendToImgBufsList(p_buf_desc->map_buf);
  TMEXIT(FALSE == ret, TRUE, EXIT2, "Failed to append in buf in image buf list");

  if (TRUE == read) {
    ret = readInputFile(fname, data_len, p_buf_desc);
    TMEXIT(FALSE == ret, TRUE, EXIT3, "Failed to read input file name: %s", fname);
  }
  return TRUE;

EXIT3:
  removeFromImgBufsList(p_buf_desc->map_buf);

EXIT2:
  free(p_buf_desc->map_buf);
  p_buf_desc->map_buf = NULL;
  if (p_buf_desc->v4l_buf.addr[0])
    tm_do_munmap_ion(ionfd, &(p_buf_desc->v4l_buf.fd_data[0]),
      (void *)p_buf_desc->v4l_buf.addr[0],
      p_buf_desc->v4l_buf.ion_alloc[0].len);

EXIT1:
  return FALSE;
}

/** Name: readInputFile
 *
 *  Arguments/Fields:
 *  @fname: input image name
 *  @data_len: request data length to be read
 *  @p_in_buf_desc: input buffer descriptor - in case when test suite
 *  allocates own buffer for input image it proivides own buffer descriptor
 *  Return: Boolean result for success/failure
 *
 *  Description: Reads input data in input buffer from the given input file
 *  with given size.
 *
 **/
boolean TM_Interface::readInputFile(const char *fname,
  size_t data_len, tm_img_buf_desc_t *p_in_buf_desc)
{
  boolean   ret = TRUE;
  int       in_file_fd = 0;
  size_t    read_len = 0;
  tm_img_buf_desc_t       *p_buf_desc;

  if (p_in_buf_desc) {
    p_buf_desc = p_in_buf_desc;
  } else {
    p_buf_desc = &in_buf_desc;
  }
  TMASSERT(!p_buf_desc, TRUE, "No input buffer descriptor!");

  TMASSERT(data_len > p_buf_desc->v4l_buf.ion_alloc[0].len, TRUE,
    "Wrong input size");

  TMDBG("read size: %zu from file: %s", data_len, fname);

  in_file_fd = open(fname, O_RDONLY);
  if (in_file_fd < 0) {
    TMDBG_ERROR("%s:%d] Cannot open input file\n", __func__, __LINE__);
    return FALSE;
  }
  /* Read from input file */
  read_len = read(in_file_fd, (void *)p_buf_desc->v4l_buf.addr[0], data_len);
  if (read_len != data_len) {
    TMDBG_ERROR("%s:%d] Read input image failed read len: %d, expected: %d\n",
      __func__, __LINE__, read_len, data_len);
    ret = FALSE;
  }
  close(in_file_fd);
  return ret;
}

/** Name: allocateOutputBufs
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Allocates and maps in ION output buffer. Adds output
 *  buffer in buffers list.
 *
 **/
boolean TM_Interface::allocateOutputBufs(uint32_t idx)
{
  boolean                 ret;
  mct_list_t              *list;
  uint32_t                num_planes;
  uint32_t                buf_size;
  cam_format_t            format;
  if (idx >= TM_MAX_NUM_STREAMS) {
    TMDBG_ERROR("Invalid stream index %d", idx);
    return FALSE;
  }
  format = getFormat(m_input.out_format[idx]);
  TMASSERT(CAM_FORMAT_MAX == format, TRUE, "wrong format");
  buf_size = m_input.sizes.output_width[idx] *
    m_input.sizes.output_height[idx] * getPixelSize(format);
  TMASSERT(!buf_size, TRUE, "wrong buffer size");
  num_planes = getNumPlanes(m_input.out_format[idx]);
  TMASSERT(!num_planes, TRUE, "wrong number planes");

  TMDBG("going to allocate out buf: size: %d, num_planes: %d", buf_size, num_planes);
  ret = allocateBuffer(&out_buf_desc[idx], CAM_MAPPING_BUF_TYPE_STREAM_BUF,
    num_planes, buf_size);
  TMASSERT(FALSE == ret, TRUE, "failed to allocate output buffer");

  TMDBG("alloc out buf addr: %p, index: %d, fd: %d",
    out_buf_desc[idx].map_buf->buf_planes[0].buf, out_buf_desc[idx].map_buf->buf_index,
    out_buf_desc[idx].map_buf->common_fd);

  memset((void *) out_buf_desc[idx].v4l_buf.addr[0], 0xCA,
    out_buf_desc[idx].v4l_buf.ion_alloc[0].len);

  list = mct_list_append(img_buf_list[idx], out_buf_desc[idx].map_buf,
    NULL, NULL);
  if (!list) {
    TMDBG_ERROR("Failed to append output buf in image bufs list");
  }
  img_buf_list[idx] = list;

  return TRUE;
}

/** Name: allocateScratchBufs
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Allocates and maps in ION scratch buffer,
 *  needed by pproc module. Adds scratch buffer in buffers list.
 **/
boolean TM_Interface::allocateScratchBufs(uint32_t idx)
{
  mct_list_t              *list;
  boolean                 ret;
  uint32_t                num_planes;
  uint32_t                buf_size;
  cam_format_t            format;
  if (idx >= TM_MAX_NUM_STREAMS) {
    TMDBG_ERROR("Invalid stream index %d", idx);
    return FALSE;
  }

  format = getFormat(m_input.out_format[idx]);
  TMASSERT(CAM_FORMAT_MAX == format, TRUE, "wrong format");
  buf_size = m_input.sizes.output_width[idx] *
    m_input.sizes.output_height[idx] * getPixelSize(format);
  TMASSERT(!buf_size, TRUE, "wrong buffer size");
  num_planes = getNumPlanes(m_input.out_format[idx]);
  TMASSERT(!num_planes, TRUE, "wrong number planes");

  TMDBG("going to allocate out buf: size: %d, num_planes: %d", buf_size, num_planes);
  ret = allocateBuffer(&scratch_buf_desc[idx], CAM_MAPPING_BUF_TYPE_STREAM_BUF,
    num_planes, buf_size);
  TMASSERT(FALSE == ret, TRUE, "failed to allocate scratch buffer");

  list = mct_list_append(img_buf_list[idx],
    scratch_buf_desc[idx].map_buf, NULL, NULL);
  if (!list) {
    TMDBG_ERROR("Failed to append scratch buf in image bufs list");
  }
  img_buf_list[idx] = list;

  return TRUE;
}

/** Name: allocateAndReadRefImage
 *
 *  Arguments/Fields:
 *  Return: Boolean result for success/failure
 *
 *  Description: Allocates and maps in ION reference buffer. Adds reference
 *  buffer in buffers list. This buffer is to be compared with
 *  output buffer
 **/
boolean TM_Interface::allocateAndReadRefImage(void)
{
  int ref_file_fd = 0;
  int data_len = 0, read_len = 0;

  if(FALSE == m_input.have_ref_image)
    return TRUE;
  if(m_input.ref_imgname[0] != '\0') {
    ref_file_fd = open(m_input.ref_imgname, O_RDONLY);
    if (ref_file_fd < 0) {
      TMDBG_ERROR("%s:%d] Cannot open ref_input file\n", __func__, __LINE__);
      return FALSE;
    }
    data_len = m_input.sizes.ref_img_width *
      m_input.sizes.ref_img_height * 3 / 2;
    data_len = (data_len +
      (TEST_MANAGER_ALIGN_4K - 1)) & ~(TEST_MANAGER_ALIGN_4K - 1);
    ref_img_buffer = (uint8_t*)malloc(data_len);
    if(NULL == ref_img_buffer) {
      CDBG_ERROR("%s: malloc (%d bytes) failed", __func__, data_len);
      goto EXIT_FAIL;
    }
    /* Read into ref_img_buffer */
    read_len = read(ref_file_fd, (void *)ref_img_buffer,
    data_len);
    if (read_len != data_len) {
      TMDBG_ERROR("%s:%d] Copy reference image failed read_len: %d, \
        expected len: %d\n",
        __func__, __LINE__, read_len, data_len);
      goto EXIT_FAIL;
    }
    close(ref_file_fd);
    return TRUE;
  }

EXIT_FAIL:
  close(ref_file_fd);
  return FALSE;
}

void TM_Interface::freeOutputBuffer(void)
{
  uint32_t                          cnt;

  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    if (!out_buf_desc[cnt].map_buf) {
      break;
    }
    img_buf_list[cnt] = mct_list_remove(img_buf_list[cnt], out_buf_desc[cnt].map_buf);
    free(out_buf_desc[cnt].map_buf);
    tm_do_munmap_ion(ionfd, &(out_buf_desc[cnt].v4l_buf.fd_data[0]),
      (void *)out_buf_desc[cnt].v4l_buf.addr[0], out_buf_desc[cnt].v4l_buf.ion_alloc[0].len);
  }
}

void TM_Interface::freeInputBuffer(tm_img_buf_desc_t *p_in_buf_desc)
{
  tm_img_buf_desc_t       *p_buf_desc;

  if (p_in_buf_desc) {
    p_buf_desc = p_in_buf_desc;
  } else {
    p_buf_desc = &in_buf_desc;
  }
  TMRETURN(!p_buf_desc, "No input buffer descriptor!");

  if (p_buf_desc->map_buf) {
    removeFromImgBufsList(p_buf_desc->map_buf);
    free(p_buf_desc->map_buf);
    p_buf_desc->map_buf = NULL;
    if (p_buf_desc->v4l_buf.addr[0])
      tm_do_munmap_ion(ionfd, &(p_buf_desc->v4l_buf.fd_data[0]),
        (void *)p_buf_desc->v4l_buf.addr[0],
        p_buf_desc->v4l_buf.ion_alloc[0].len);
  }
}

void TM_Interface::freeMetadataBuffer(void)
{
  if (meta_buf_desc.map_buf) {
    // TODO: free meta buf list element
    removeFromImgBufsList(meta_buf_desc.map_buf);
    meta_buf_desc.map_buf = NULL;
    if (meta_buf_desc.v4l_buf.addr[0]) {
      tm_do_munmap_ion(ionfd, &(meta_buf_desc.v4l_buf.fd_data[0]),
        (void *)meta_buf_desc.v4l_buf.addr[0],
        meta_buf_desc.v4l_buf.ion_alloc[0].len);
    }
  }
}

void TM_Interface::freeReferenceBuffer(void)
{
  if(ref_img_buffer) {
    free(ref_img_buffer);
    ref_img_buffer = NULL;
  }
}

void TM_Interface::freeScratchBuffer()
{
  uint32_t                          cnt;

  for (cnt = 0 ; cnt < m_input.num_streams ; cnt++) {
    if (!scratch_buf_desc[cnt].map_buf) {
      break;
    }
    img_buf_list[cnt] = mct_list_remove(img_buf_list[cnt],
      scratch_buf_desc[cnt].map_buf);
    free(scratch_buf_desc[cnt].map_buf);
    if (scratch_buf_desc[cnt].v4l_buf.addr[0])
      tm_do_munmap_ion(ionfd, &(scratch_buf_desc[cnt].v4l_buf.fd_data[0]),
        (void *)scratch_buf_desc[cnt].v4l_buf.addr[0],
        scratch_buf_desc[cnt].v4l_buf.ion_alloc[0].len);
  }
}

mct_list_t *TM_Interface::getImgBufsList(uint32_t stream_id)
{
  TMDBG_HIGH("getting buffers for stream id: %d", stream_id);
  return img_buf_list[stream_id - 1];
}

void *TM_Interface::tm_do_mmap_ion(int ion_fd, struct ion_allocation_data *alloc,
  struct ion_fd_data *ion_info_fd, int *mapFd)
{
  void                  *ret; /* returned virtual address */
  int                    rc = 0;
  struct ion_handle_data handle_data;
  if (!alloc || !ion_info_fd || !mapFd) {
    TMDBG_ERROR("Invalid input args: alloc[%p], ion_info_fd[%p], mapFd[%p]",
        alloc, ion_info_fd, mapFd);
    return NULL;
  }

  /* to make it page size aligned */
  alloc->len = (alloc->len +
    (TEST_MANAGER_ALIGN_4K - 1)) & ~(TEST_MANAGER_ALIGN_4K - 1);
  rc = ioctl(ion_fd, ION_IOC_ALLOC, alloc);
  if (rc < 0) {
    TMDBG_ERROR("[%s:%d]ION allocation failed ionfd: %d\n", __func__, __LINE__,
      ion_fd);
    goto ION_ALLOC_FAILED;
  }

  ion_info_fd->handle = alloc->handle;
  rc = ioctl(ion_fd, ION_IOC_SHARE, ion_info_fd);
  if (rc < 0) {
    TMDBG_ERROR("ION map failed %s\n", strerror(errno));
    goto ION_MAP_FAILED;
  }
  *mapFd = ion_info_fd->fd;
  ret = mmap(NULL, alloc->len, PROT_READ | PROT_WRITE, MAP_SHARED, *mapFd, 0);
  if (ret == MAP_FAILED) {
    TMDBG_ERROR("ION_MMAP_FAILED: %s (%d)\n", strerror(errno), errno);
    goto ION_MAP_FAILED;
  }

  return ret;

ION_MAP_FAILED:
  handle_data.handle = ion_info_fd->handle;
  ioctl(ion_fd, ION_IOC_FREE, &handle_data);
ION_ALLOC_FAILED:
  return NULL;
}

int TM_Interface::tm_do_munmap_ion(int ion_fd, struct ion_fd_data *ion_info_fd,
  void *addr, size_t size)
{
  int                    rc = 0;
  struct ion_handle_data handle_data;
  if (!ion_info_fd || !addr) {
    TMDBG_ERROR("Invalid input args: ion_info_fd[%p], addr[%p]",
        ion_info_fd, addr);
    return -1;
  }

  rc = munmap(addr, size);
  close(ion_info_fd->fd);
  handle_data.handle = ion_info_fd->handle;
  ioctl(ion_fd, ION_IOC_FREE, &handle_data);
  return rc;
}

boolean TM_Interface::openIonFD(void)
{
  ionfd = open("/dev/ion", O_RDONLY | O_SYNC);
  if (ionfd < 0) {
    TMDBG_ERROR(" Ion device open failed\n");
    return FALSE;
  }
  return TRUE;
}

void TM_Interface::closeIonFD(void)
{
  if (ionfd)
    close(ionfd);
}

uint32_t TM_Interface::getStreamIdentity(cam_stream_type_t stream_type)
{
  mct_stream_t *stream;
  mct_pipeline_get_stream_info_t info;
  info.check_type  = CHECK_TYPE;
  info.stream_type = stream_type;

  stream = mct_pipeline_get_stream(pipeline, &info);
  if (!stream) {
    TMDBG_ERROR("%s: Couldn't find stream type: %d\n", __func__, stream_type);
    return FALSE;
  }

  return stream->streaminfo.identity;
}

uint32_t TM_Interface::getParmStreamIdentity(void)
{
  return parm_stream_identity;
}

void TM_Interface::setParmStreamIdentity(uint32_t identity)
{
  parm_stream_identity = identity;
  session_id = (identity & 0xFFFF0000);
}

void TM_Interface::setPipelineInstance(mct_pipeline_t *mct_pipeline)
{
  pipeline = mct_pipeline;
}

superParam::superParam(boolean is_src_first_module)
  : num_super_params(0),
    is_first_module_src(is_src_first_module)
{
  p_table = (parm_buffer_t*)malloc(sizeof(parm_buffer_t));
  if (!p_table) {
    CDBG_ERROR("%s: malloc (%d bytes) failed", __func__, sizeof(parm_buffer_t));
  }
}

superParam::~superParam()
{
  if (p_table) {
    free(p_table);
    p_table = NULL;
  }
}

void superParam::addParmToTable(cam_intf_parm_type_t parm_type,
  void *data, parm_buffer_t *p_table, size_t parm_size)
{
  void* src;
  if (!p_table) {
    return;
  }
  src = get_pointer_of(parm_type, p_table);
  if(src){
    memcpy(src, data, parm_size);
    p_table->is_valid[parm_type] = 1;
  }
  return;
}

void superParam::getParmFromTable(cam_intf_parm_type_t parm_type,
  void *data, parm_buffer_t *p_table, size_t parm_size)
{
  void* src;
  if (!p_table) {
    return;
  }
  src = get_pointer_of(parm_type, p_table);
  if(src){
    memcpy(src, data, parm_size);
    p_table->is_valid[parm_type] = 1;
  }
  return;
}

/*
*  Name: addParamToBatch()
*  Result: boolean result based on testing conditions
*  Description:
*  Wrapper function around MCT call addParmToTable
*  to compose batch of super-params to be sent
*  to modules.
*/

boolean superParam::addParamToBatch(cam_intf_parm_type_t paramType,
  size_t paramLength, void *paramValue)
{
  if ((NULL == paramValue) || (paramType >= CAM_INTF_PARM_MAX)) {
      CDBG_ERROR("%s: Invalid arguments: paramValue: %p, param type: %d",
          __func__, paramValue, paramType);
      return FALSE;
  }
  if (paramLength > get_size_of(paramType)) {
    CDBG_ERROR("%s: input larger than max entry size, type=%d, length =%d",
            __func__, paramType, paramLength);
    return FALSE;
  }
  if(p_table) {
    addParmToTable(paramType, paramValue, p_table, paramLength);
    num_super_params++;
    CDBG_HIGH("add param to batch. Num params: %d", num_super_params);
    return TRUE;
  }

  return FALSE;
}

boolean superParam::getParamFromBatch(cam_intf_parm_type_t paramType,
  size_t paramLength, void *paramValue)
{
  if ((NULL == paramValue) || (paramType >= CAM_INTF_PARM_MAX)) {
      CDBG_ERROR("%s: Invalid arguments: paramValue: %p, param type: %d",
          __func__, paramValue, paramType);
      return FALSE;
  }
  if (paramLength > get_size_of(paramType)) {
    CDBG_ERROR("%s: input larger than max entry size, type=%d, length =%d",
            __func__, paramType, paramLength);
    return FALSE;
  }
  if(p_table) {
    getParamFromBatch(paramType, paramLength, paramValue);
    return TRUE;
  }
  return FALSE;
}

boolean superParam::sendSuperParams(TM_Interface *tm,
  uint32_t frame_id, uint32_t stream_identity)
{
  boolean rc = FALSE;
  uint32_t current = 0;
  mct_event_control_parm_t event_parm;
  mct_event_super_control_parm_t *super_event = NULL;

  if (!tm) {
    TMDBG_ERROR("Invalid TM_Interface ptr");
    return FALSE;
  }
  if(!p_table) {
    TMDBG_ERROR("p_table absent");
    return FALSE;
  }
  super_event = (mct_event_super_control_parm_t *)malloc(
    sizeof(mct_event_super_control_parm_t));
  if (super_event == NULL) {
    TMDBG_ERROR("%s: super event malloc failed\n", __func__);
    return FALSE;
  }
  super_event->identity = stream_identity;
  super_event->frame_number = frame_id;
  super_event->num_of_parm_events = 0;
  super_event->parm_events = (mct_event_control_parm_t *)malloc(
    num_super_params * sizeof(mct_event_control_parm_t));
    if (super_event->parm_events == NULL) {
    TMDBG_ERROR("%s: Allocation of %d bytes failed\n",
      __func__, num_super_params *sizeof(mct_event_control_parm_t));
    goto EXIT1;
    return FALSE;
  }

  /* Parse through valid entries in param
  *  table and populate super-event */
  for(current = 0; current < CAM_INTF_PARM_MAX; current++) {
    if(!p_table->is_valid[current])
      continue;
    event_parm.type = (cam_intf_parm_type_t)current;
    event_parm.parm_data = get_pointer_of((cam_intf_parm_type_t)current,
      p_table);
    if (NULL == event_parm.parm_data) {
      TMDBG_ERROR("%s: event_parm.parm_data = NULL for %d", __func__, current);
    goto EXIT2;
      return FALSE;
    }
    TMDBG("send superparam id [%d]", current);
    if(super_event->num_of_parm_events < num_super_params) {
      memcpy(&super_event->parm_events[super_event->num_of_parm_events],
        &event_parm, sizeof(mct_event_control_parm_t));
      super_event->num_of_parm_events++;
    } else {
      CDBG("Completed parsing %d p_table entries", num_super_params);
      break;
    }
  }
  // No superparams to send - exit
  if (!super_event->num_of_parm_events) {
    goto EXIT2;
    return TRUE;
  }
  TMDBG("\n\tGoing to send %d superparams \n", super_event->num_of_parm_events);

  /* Now send super-event downstream */
  rc = tm->sendControlEventDown(MCT_EVENT_CONTROL_SET_SUPER_PARM, super_event,
    tm->getParmStreamIdentity());
  if (rc == FALSE) {
    TMDBG_ERROR("in superparam \n");
  }
  /* Clear all entries in p_table */
  memset(p_table, 0, sizeof(parm_buffer_t));
  num_super_params = 0;
EXIT2:
  /* Free super-event structure */
  if (super_event->parm_events) {
    free(super_event->parm_events);
    super_event->parm_events = NULL;
  }

EXIT1:
  free(super_event);
  super_event = NULL;

  return rc;
}

/** Name: loadDefaultParams
 *
 *  Arguments/Fields:
 *  Return: TRUE on successfully sending default
 *  suepr-params downstream; FALSE on failure
 *  Description:
 *  This is a wrapper method to bundle required params
 *  into one batch and invoking send_super_param
 *  to set params to downstream module(s).
 *
 **/
boolean superParam::loadDefaultParams(tm_intf_input_t *input)
{
  boolean rc = FALSE;
  TMDBG("%s", __func__);
  /* HAL Version */
  uint32_t hal_version = CAM_HAL_V1;
  rc = addParamToBatch(CAM_INTF_PARM_HAL_VERSION,
    sizeof(hal_version), &hal_version);

  /* Best Shot */
  uint8_t sceneMode = CAM_SCENE_MODE_AUTO;
  rc &= addParamToBatch(CAM_INTF_PARM_BESTSHOT_MODE,
    sizeof(sceneMode), &sceneMode);

  /* Effect mode */
  uint8_t effectMode = CAM_EFFECT_MODE_OFF;
  rc &= addParamToBatch(CAM_INTF_PARM_EFFECT,
    sizeof(effectMode), &effectMode);
  /* Saturation */
  int32_t saturation = 5;
  rc &= addParamToBatch(CAM_INTF_PARM_SATURATION,
    sizeof(saturation), &saturation);

  /* Contrast */
  int32_t contrast = 5;
  rc &= addParamToBatch(CAM_INTF_PARM_CONTRAST,
    sizeof(contrast), &contrast);

  /* Wavelet Denoise */
  if (input->cpp.denoise_params.denoise_enable) {
      cam_denoise_param_t denoise_param;
      denoise_param.denoise_enable = 1;
      denoise_param.process_plates = CAM_WAVELET_DENOISE_YCBCR_PLANE;

    if (TM_DENOISE_TNR == input->cpp.denoise_type) {
      rc &= addParamToBatch(CAM_INTF_PARM_TEMPORAL_DENOISE,
        sizeof(cam_denoise_param_t), &denoise_param);
    } else if (input->cpp.denoise_params.denoise_enable) {
      rc &= addParamToBatch(CAM_INTF_PARM_WAVELET_DENOISE,
        sizeof(cam_denoise_param_t), &denoise_param);
    }
  }
  /* Sharpness */
  uint32_t sharpness = 0;
  rc &= addParamToBatch(CAM_INTF_PARM_SHARPNESS,
    sizeof(sharpness), &sharpness);

  cam_fps_range_t fps;
  fps.min_fps = fps.video_min_fps = 30;
  fps.max_fps = fps.video_max_fps = 30;
  rc &= addParamToBatch(CAM_INTF_PARM_FPS_RANGE,
    sizeof(fps), &fps);

  return rc;
}

void* tm_intf_wrapper_timer_thread_run(void *data)
{
  SOF_Timer *tmr = (SOF_Timer *)data;

  if (!tmr)
    return NULL;

  return tmr->timerThreadRun();
}

int SOF_Timer::timerCountdown(void)
{
  uint32_t timer_interval;  // SOF interval in ms

  /* Setup thread timeout interval */
  if (fps <= 0) {
    CDBG_ERROR("%s: Invalid fps value %f", __func__, fps);
    return 0;
  }
  timer_interval = (uint32_t) ((1/fps) * 1000.0);
  CDBG("%s: Timer interval = %d ms", __func__, timer_interval);
  /* Timer implementation using usleep() */
  usleep(timer_interval * 1000);
  CDBG("%s: SOF interval elapsed. Waking up.", __func__);
  return 1;
}

void* SOF_Timer::timerThreadRun(void)
{
  int ret = 0;

  thread_run = 1;

  while(thread_run) {
    ret = timerCountdown();
    if (ret == 1) {
      /* Wake up main thread with SOF notify */
      CDBG("%s: Sending timer_notify signal", __func__);
      pthread_mutex_lock(&timer_lock);
      pthread_cond_signal(&timer_cond);
      pthread_mutex_unlock(&timer_lock);
    } else {
      CDBG_ERROR("%s: Breaking out. thread_run = %d",
        __func__, thread_run);
    }
  }
  if (thread_run == 1) {
    CDBG_ERROR("%s: Bad state: thread_run = 1", __func__);
    /* TODO: Error Handling */
  }
  return NULL;
}

boolean SOF_Timer::startAsyncTimer(float in_fps)
{
  int rc = 0;

  if (thread_run == 1)
    return TRUE;
  fps = in_fps;
  /* Spawn a thread to generate SOF notifications */
  TMDBG_HIGH("%s: Starting timer thread with fps: %f\n", __func__, in_fps);
  pthread_mutex_init(&timer_lock, NULL);
  pthread_cond_init(&timer_cond, NULL);
  pthread_mutex_lock(&timer_lock);
  rc = pthread_create(&timer_tid, NULL,
    tm_intf_wrapper_timer_thread_run, this);
  pthread_mutex_unlock(&timer_lock);
  if (rc)
    return FALSE;

  return TRUE;
}

int SOF_Timer::waitForTimer(long long timeout)
{
  signed long long end_time;
  struct timeval r;
  struct timespec ts;
  int ret;

  /* Blocking call waiting for SOF notification from thread */
  pthread_mutex_lock(&timer_lock);
  CDBG("%s: Waiting on sof_notify", __func__);

  gettimeofday(&r, NULL);

  end_time = (((((signed long long)r.tv_sec) * 1000000) + r.tv_usec) +
    (timeout));
  ts.tv_sec  = (end_time / 1000000);
  ts.tv_nsec = ((end_time % 1000000) * 1000);
  ret = pthread_cond_timedwait(&timer_cond, &timer_lock, &ts);
//  pthread_cond_wait(&timer_cond, &timer_lock);
  pthread_mutex_unlock(&timer_lock);
  if (ETIMEDOUT != ret) {
    UCDBG("Received SOF notification from thread");
  }
  return ret;
}


boolean SOF_Timer::stopAsyncTimer(void)
{
  if (thread_run == 0)
    return FALSE;
  CDBG_HIGH("%s: Stopping SOF timeout thread\n", __func__);
  thread_run = 0;
  pthread_mutex_lock(&timer_lock);
  pthread_cond_signal(&timer_cond);
  pthread_mutex_unlock(&timer_lock);
  pthread_join(timer_tid, NULL);
  pthread_cond_destroy(&timer_cond);
  pthread_mutex_destroy(&timer_lock);
  return TRUE;
}

boolean IEvent::moduleProcessEvent(mct_module_t *module, mct_event_t *event)
{
  TMASSERT(!module || !event, TRUE);
  return TRUE;
}

boolean IEvent::portEvent(mct_port_t *port, mct_event_t *event)
{
  TMASSERT(!port || !event, TRUE);

  switch(event->type) {
  case MCT_EVENT_MODULE_EVENT: {
    switch(event->u.module_event.type) {
    case MCT_EVENT_MODULE_BUF_DIVERT:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_BUF_DIVERT", __func__);
      break;
    case MCT_EVENT_MODULE_BUF_DIVERT_ACK:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_BUF_DIVERT_ACK", __func__);
      break;
    case MCT_EVENT_MODULE_SOF_NOTIFY:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_SOF_NOTIFY", __func__);
      break;
    case MCT_EVENT_MODULE_ISP_FRAMESKIP:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_ISP_FRAMESKIP - %d", __func__,
        *(uint32_t*)event->u.module_event.module_event_data);
      break;
    case MCT_EVENT_MODULE_QUERY_DIVERT_TYPE: {
      uint32_t *divert_mask = (uint32_t *)event->u.module_event.module_event_data;
      *divert_mask = 0;
      CDBG_HIGH("%s: MCT_EVENT_MODULE_QUERY_DIVERT_TYPE: divert_mask = %x",
        __func__, *divert_mask);
      }
      break;
    case MCT_EVENT_MODULE_SET_STREAM_CONFIG:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_SET_STREAM_CONFIG", __func__);
      break;
    case MCT_EVENT_MODULE_ISP_OUTPUT_DIM: {
      mct_stream_info_t *stream_info =
        (mct_stream_info_t *)(event->u.module_event.module_event_data);
      CDBG_HIGH("%s: MCT_EVENT_MODULE_ISP_OUTPUT_DIM %dx%d",
        __func__, stream_info->dim.width, stream_info->dim.height);

    }
      break;
    case MCT_EVENT_MODULE_PPROC_SET_OUTPUT_BUFF: {
      mct_stream_map_buf_t *img_buf;

      CDBG_HIGH("%s: MCT_EVENT_MODULE_PPROC_SET_OUTPUT_BUFF", __func__);
      img_buf = (mct_stream_map_buf_t *)
        (event->u.module_event.module_event_data);
      if (!img_buf) {
        CDBG_ERROR("%s:%d, failed\n", __func__, __LINE__);
        break;
      }
      CDBG_HIGH("fd: %d, idx: %d", img_buf->buf_planes[0].fd, img_buf->buf_index);
    }
      break;
    case MCT_EVENT_MODULE_SET_CHROMATIX_PTR:
      CDBG_HIGH("%s: MCT_EVENT_MODULE_SET_CHROMATIX_PTR", __func__);
      break;
    case MCT_EVENT_MODULE_IFACE_REQUEST_PP_DIVERT: {
      pp_buf_divert_request_t *divert_request;

      CDBG_HIGH("%s: MCT_EVENT_MODULE_IFACE_REQUEST_PP_DIVERT", __func__);

      divert_request =
        (pp_buf_divert_request_t *)(event->u.module_event.module_event_data);
      divert_request->need_divert = TRUE;
      divert_request->force_streaming_mode = CAM_STREAMING_MODE_MAX;
    }
      break;
    default:
      CDBG_HIGH("%s: module event type: %d",
        __func__, event->u.module_event.type);
      break;
    }
    break;
  }
  case MCT_EVENT_CONTROL_CMD: {
    switch(event->u.ctrl_event.type) {
    case MCT_EVENT_CONTROL_STREAMON:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_STREAMON", __func__);
      break;
    case MCT_EVENT_CONTROL_STREAMOFF:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_STREAMOFF", __func__);
      break;
    case MCT_EVENT_CONTROL_SET_PARM:
      CDBG_HIGH("%s: MCT_EVENT_CONTROL_SET_PARM", __func__);
      break;
    default:
      CDBG_ERROR("%s: control cmd type: %d",
        __func__, event->u.module_event.type);
      break;
    }
    break;
  }
  default:
    break;
  }
  return TRUE;
}

}
