/**********************************************************************
*  Copyright (c) 2014-2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/

#include "faceproc_hw_comp.h"
#include "facial_parts_wrapper.h"
#include <media/msm_fd.h>
#include <math.h>
#include <poll.h>

/* Face detection device node max name length. */
#define FD_DRV_MAX_NAME_SIZE 128
/* Face detection driver max number of faces which can be detected. */
#define FD_DRV_MAX_FACE_NUM 35
/* Max number of searched device nodes for detecting face detection driver. */
#define FD_DRV_MAX_DEVICES 32
/* Max number of buffers mapped in driver in one session. */
#define FD_DRV_MAX_BUF_NUM 16
/* Max number of failed calls allowed in worker thread. */
#define FD_MAX_FAIL_CNT 10
/* Timeout for message ack .*/
#define FD_MSG_ACK_TIMEOUT_MS 150
/* Timeout for start in progress*/
#define FD_START_TIMEOUT_MS 300
/* Max Number of continous frames skipped due to busy HW to trigger reset*/
#define MAX_TIMEOUT_FRAME_COUNT 5
/* Detected face in front pose angle in degrees */
#define FD_POSE_FRONT_ANGLE 0
/* Detected face in right diagonal pose angle in degrees */
#define FD_POSE_RIGHT_DIAGONAL_ANGLE 45
/* Detected face in right pose angle in degrees */
#define FD_POSE_RIGHT_ANGLE 90
/* Detected face in left diagonal pose angle in degrees */
#define FD_POSE_LEFT_DIAGONAL_ANGLE -45
/* Detected face in left pose angle in degrees */
#define FD_POSE_LEFT_ANGLE -90

/* Half profile angle in degrees */
#define FD_HALF_PROFILE_ANGLE 45
/* Full profile angle in degrees */
#define FD_FULL_PROFILE_ANGLE 90

/* Number of frames of which tracking id will be hold when face is lost */
#define FD_TRACKING_LOST_CNT 3

/* Minimum face detect count for false positive filtering */
#define FD_TRACKING_MIN_DETECT_CNT 2

/* Minimum frame delta for false positive peak filtering */
#define FD_TRACKING_MIN_FRAME_DELTA 1

/* Maximum size variation allowed for false positive filter */
#define FD_FPF_MAX_SIZE_VARIATION 2.0f

/* Convert frame buffer index to driver buffer index */
#define FD_FRAME_TO_DRV_BUF_IDX(a) ((a) % FD_DRV_MAX_BUF_NUM)

/* Face Detection driver name. */
#define FD_DRV_NAME "msm_fd"
/* Sysfs directory containing V4L2 devices. */
#define FD_V4L2_SYS_PATH "/sys/class/video4linux/"

/* Macro - enable if SW filtering needs to be used for every frame */
#define FD_USE_SW_FD_ALWAYS 1

/* Macro - enable if you want HW to insert fake result */
#define FD_HW_USE_FAKE_RESULT

/* Min HW confidence beyond which SW FD needs to be executed */
#define FD_HW_MIN_HW_CONFIDENCE /*300*/ 0

/* Max HW confidence beyond which SW FD needs to be executed */
#define FD_HW_MAX_HW_CONFIDENCE 900

/* overall confidence adjustment for HW/SW failure */
#define FD_HW_SW_FAIL_OVERALL_CONF_ADJUST_THRES 0.5f

/* SW threshold adjustment for tracked faces */
#define FD_HW_TRACK_THRES_ADJUSTMENT 0.9f

/* Confidence adjustment for half profile faces */
#define FD_HW_CONFIDENCE_ADJ_FOR_POSE 0.75

/* Macro to recheck teh lost faces with SW FD */
// #define RECHECK_LOST_FACE

/** fdhw_tracking_grid_mode_t
 *   @FDHW_TRACKING_GRID_MODE_ALL_TOUCHED: in this mode during
 *     tracking all rectangles in grid where at least some part
 *     of face is present will be marked with face id.
 *     NOTE: FLOOR used when calculating left/top
 *       CEIL used when calculating width/height
 *
 *   @FDHW_TRACKING_GRID_MODE_MAX_ACCURACY: in this mode during
 *     tracking all rectangles in grid where both edges of face
 *     cover 50% of rectangle at least.
 *     //NOTE: ROUND used when calculating left/top/width/height
 *
 *   HW Faceproc tracking grid mode.
 */
typedef enum {
  FDHW_TRACKING_GRID_MODE_ALL_TOUCHED,
  FDHW_TRACKING_GRID_MODE_MAX_ACCURACY,
} fdhw_tracking_grid_mode_t;

static fdhw_tracking_grid_mode_t current_fdhw_tracking_grid_mode =
  FDHW_TRACKING_GRID_MODE_ALL_TOUCHED;

/** fdhw_frame_dump_count
 *
 *   number of frames that have been dumped from fd hw debugging.
 */
static int32_t fdhw_frame_dump_count = 0;

/*static functions */
static void faceproc_hw_comp_reset_batchcounters(faceproc_hw_comp_t *p_comp);

/**
 * Function: faceproc_hw_comp_get_fd_device_name
 *
 * Description: Go thru each "/dev/videoN" and search for Face Detection
 *   device name.
 *
 * Arguments:
 *   @p_fd_name: Char array where Face Detection device name should be stored.
 *   @size: Size of "fd_name" array.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_get_fd_device_name(char *p_fd_name, size_t size)
{
  char temp_name[FD_DRV_MAX_NAME_SIZE];
  int temp_fd;
  int num_device;
  int cnt = FD_DRV_MAX_DEVICES;
  int ret;

  if (size > sizeof(temp_name)) {
    size = sizeof(temp_name);
  }

  num_device = -1;
  while (cnt-- > 0) {
    num_device++;

    ret = snprintf(temp_name, sizeof(temp_name),
      FD_V4L2_SYS_PATH"video%d/name", num_device);
    if (ret <= 0) {
      IDBG_ERROR("Snprintf fail %s", strerror(errno));
      return IMG_ERR_GENERAL;
    }

    temp_fd = open(temp_name, O_RDONLY | O_NONBLOCK);
    if (temp_fd < 0) {
      if (errno == ENOENT) {
        IDBG_ERROR("Can not discover face-detect device");
        return IMG_ERR_NOT_FOUND;
      } else {
        IDBG_MED("%s %s Skip", strerror(errno), temp_name);
        continue;
      }
    }

    ret = read(temp_fd, temp_name, sizeof(FD_DRV_NAME));
    close(temp_fd);
    if (ret <= 0) {
      temp_name[0] = 0;
      IDBG_MED("Can not read name for the device %s skip", strerror(errno));
      continue;
    }

    if ((size_t)ret < size) {
      temp_name[ret - 1] = 0;
    } else {
      temp_name[size - 1] = 0;
    }

    IDBG_MED("Check video device %s", temp_name);

    ret = strncmp((char *)temp_name, FD_DRV_NAME, sizeof(FD_DRV_NAME));
    if (!ret) {
      snprintf(temp_name, sizeof(temp_name), "/dev/video%d", num_device);
      IDBG_MED("Found Face-Detect device %s", temp_name);
      strlcpy(p_fd_name, temp_name, size);
      return IMG_SUCCESS;
    }
  }

  IDBG_MED("Exceed max dev number %d FD not found", FD_DRV_MAX_DEVICES);

  return IMG_ERR_NOT_FOUND;
}

/**
 * Function: faceproc_hw_comp_round_size
 *
 * Description: Check if frame size is supported by Face Detection driver.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @p_frame_cfg: Pointer to frame configuration contain max sizes.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_round_size(int fh,
  faceproc_frame_cfg_t *p_frame_cfg)
{
  struct v4l2_format fmt;
  int ret;

  memset(&fmt, 0x00, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  fmt.fmt.pix.width = p_frame_cfg->max_width;
  fmt.fmt.pix.height = p_frame_cfg->max_height;
  fmt.fmt.pix.bytesperline = p_frame_cfg->min_stride;
  ret = ioctl(fh, VIDIOC_TRY_FMT, &fmt);
  if (ret < 0) {
    IDBG_ERROR("Try format fail %s", strerror(errno));
    return IMG_ERR_GENERAL;
  }
  p_frame_cfg->max_width = fmt.fmt.pix.width;
  p_frame_cfg->max_height = fmt.fmt.pix.height;
  p_frame_cfg->min_stride = fmt.fmt.pix.bytesperline;
  p_frame_cfg->min_scanline = fmt.fmt.pix.height;

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_check_size
 *
 * Description: Check if frame size is already set in Face Detection driver.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @p_frame_cfg: Pointer to frame configuration contain max sizes.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_check_size(int fh,
  faceproc_frame_cfg_t *p_frame_cfg)
{
  struct v4l2_format fmt;
  int ret;

  memset(&fmt, 0x00, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  ret = ioctl(fh, VIDIOC_G_FMT, &fmt);
  if (ret < 0) {
    IDBG_ERROR("Get format fail %s", strerror(errno));
    return IMG_ERR_GENERAL;
  }

  if (fmt.fmt.pix.width != p_frame_cfg->max_width ||
    fmt.fmt.pix.height != p_frame_cfg->max_height) {
    IDBG_MED("Sizes do not mach drv %dx%d cfg %dx%d",
      fmt.fmt.pix.width, fmt.fmt.pix.height,
      p_frame_cfg->max_width, p_frame_cfg->max_height);
    return IMG_ERR_GENERAL;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_apply_crop
 *
 * Description: Apply crop if frame size is different then current crop.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @p_frame: Pointer to frame buffer structure.
 *   @p_curr_crop: Pointer to current applied crop.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_apply_crop(int fh,
  img_frame_t *p_frame, fd_rect_t *p_curr_crop)
{
  if (p_frame->frame[0].plane[0].width != p_curr_crop->dx ||
    p_frame->frame[0].plane[0].height != p_curr_crop->dy) {
    struct v4l2_crop crop;
    int ret;

    memset(&crop, 0x00, sizeof(crop));
    crop.c.width = (int32_t)p_frame->frame[0].plane[0].width;
    crop.c.height = (int32_t)p_frame->frame[0].plane[0].height;
    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = ioctl(fh, VIDIOC_S_CROP, &crop);
    if (ret < 0) {
      IDBG_ERROR("Set crop fail %s", strerror(errno));
      return IMG_ERR_INVALID_INPUT;
    }
    p_curr_crop->x = (uint32_t)crop.c.left;
    p_curr_crop->y = (uint32_t)crop.c.top;
    p_curr_crop->dx = (uint32_t)crop.c.width;
    p_curr_crop->dy = (uint32_t)crop.c.height;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_get_direction
 *
 * Description: Get face direction based on faceproc configuration.
 *
 * Arguments:
 *   @p_face_cfg: Pointer to faceproc configuration.
 *
 * Return values:
 *   Direction in degrees 0, 90, 180, 270.
 **/
static int faceproc_hw_comp_get_direction(faceproc_face_cfg_t *p_face_cfg)
{
  int direction;

  switch (p_face_cfg->face_orientation_hint) {
  case FD_FACE_ORIENTATION_0:
    direction = 0;
    break;
  case FD_FACE_ORIENTATION_90:
    direction = 90;
    break;
  case FD_FACE_ORIENTATION_180:
    direction = 180;
    break;
  case FD_FACE_ORIENTATION_270:
    direction = 270;
    break;
  default:
    direction = 0;
    IDBG_HIGH("Invalid orientatation hint set direction to 0");
    break;
  }
  return direction;
}

/**
 * Function: faceproc_hw_comp_config
 *
 * Description: Configure Face Detection driver.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @p_face_cfg: Pointer to faceproc configuration.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_config(int fh, faceproc_face_cfg_t *p_face_cfg)
{
  struct v4l2_control control;
  int ret;

  IDBG_MED("%s:%d]sending to HW XXXmin_rotation_range %d",
    __func__, __LINE__,
    p_face_cfg->rotation_range);
  control.id = V4L2_CID_FD_FACE_ANGLE;
  control.value = (int32_t)p_face_cfg->rotation_range;
  ret = ioctl(fh, VIDIOC_S_CTRL, &control);
  if (ret < 0) {
    IDBG_ERROR("Set ctrl face angle fail %s ", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }

  control.id = V4L2_CID_FD_FACE_DIRECTION;
  control.value = faceproc_hw_comp_get_direction(p_face_cfg);
  ret = ioctl(fh, VIDIOC_S_CTRL, &control);
  if (ret < 0) {
    IDBG_ERROR("Set ctrl face direction fail %s ", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }

  IDBG_MED("%s:%d]sending to HW XXXmin_face_size %d",
    __func__, __LINE__,
    p_face_cfg->min_face_size);
  control.id = V4L2_CID_FD_MIN_FACE_SIZE;
  control.value = (int32_t)p_face_cfg->min_face_size;
  ret = ioctl(fh, VIDIOC_S_CTRL, &control);
  if (ret < 0) {
    IDBG_ERROR("Set ctrl min face size fail %s ", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }

  control.id = V4L2_CID_FD_DETECTION_THRESHOLD;
  control.value = (int32_t)p_face_cfg->detection_threshold;
  ret = ioctl(fh, VIDIOC_S_CTRL, &control);
  if (ret < 0) {
    IDBG_ERROR("Set ctrl detection threshold fail %s ", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }

  /* Zero speed is invalid value, range is from 1..3 */
  if (p_face_cfg->speed) {
    control.id = V4L2_CID_FD_SPEED;
    control.value = (int32_t)p_face_cfg->speed - 1;
    ret = ioctl(fh, VIDIOC_S_CTRL, &control);
    if (ret < 0) {
      IDBG_ERROR("Set ctrl fd speed fail %s ", strerror(errno));
      return IMG_ERR_INVALID_INPUT;
    }
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_queue_drvbuff
 *
 * Description: Queue buffer to Face Detection driver.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @p_frame: Pointer to img frame which should be enqueued.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_queue_drvbuff(int fh, img_frame_t *p_frame)
{
  struct v4l2_buffer buff;
  int ret;

  memset(&buff, 0x00, sizeof(buff));

  buff.index = FD_FRAME_TO_DRV_BUF_IDX(p_frame->idx);
  buff.sequence = p_frame->frame_id;
  buff.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  buff.memory = V4L2_MEMORY_USERPTR;
  buff.m.userptr = (unsigned long)p_frame->frame[0].plane[0].fd;
  buff.length = p_frame->frame[0].plane[0].length;
  ret = ioctl(fh, VIDIOC_QBUF, &buff);
  if (ret < 0) {
    IDBG_ERROR("QBUF drv fail %s", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_dequeue_drvbuff
 *
 * Description: Dequeue buffer from Face Detection driver.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @p_frame_id: Pointer to store frame id of dequeued buffer.
 *   @p_buff_idx: Pointer to store buffer index of dequeued buffer.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_dequeue_drvbuff(int fh,
  int *p_frame_id, uint32_t *p_buff_idx)
{
  struct v4l2_buffer buff;
  int ret;

  memset(&buff, 0x00, sizeof(buff));
  buff.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  ret = ioctl(fh, VIDIOC_DQBUF, &buff);
  if (ret < 0) {
    IDBG_ERROR("DQBUF drv fail %s ", strerror(errno));

    return IMG_ERR_INVALID_INPUT;
  }

  *p_frame_id = (int32_t)buff.sequence;
  *p_buff_idx = (uint32_t)buff.index;

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_dequeue_drvevent
 *
 * Description: Dequeue event from Face Detection driver.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @p_face_cnt: Pointer to store number of faces detected of processed frame.
 *   @p_frame_id: Pointer to store frame id of processed frame.
 *   @p_buff_idx: Pointer to store buffer index of processed frame.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_dequeue_drvevent(int fh, uint32_t *p_face_cnt,
  uint32_t *p_frame_id, uint32_t *p_buff_idx)
{
  struct msm_fd_event *p_fd_event;
  struct v4l2_event event;
  int ret;

  memset(&event, 0x00, sizeof(event));
  event.type = MSM_EVENT_FD;
  ret = ioctl(fh, VIDIOC_DQEVENT, &event);
  if (ret < 0) {
    IDBG_ERROR("DQEVENT drv fail %s ", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }
  p_fd_event = (struct msm_fd_event *)event.u.data;

  *p_face_cnt = p_fd_event->face_cnt;
  *p_frame_id = p_fd_event->frame_id;
  *p_buff_idx = p_fd_event->buf_index;

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_get_drvresult
 *
 * Description: Get Face detection result from FD driver for given frame id.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @frame_id: Frame id for requested result.
 *   @p_face_cnt: Pointer to face count (number of faces) requested
 *     in the result, this can be modified to smaller value
 *     if (face_cnt > actual_detected_faces).
 *   @p_face_data: Pointer to store Face detection result.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_get_drvresult(int fh, uint32_t frame_id,
  uint32_t *p_face_cnt, struct msm_fd_face_data *p_face_data)
{
  struct msm_fd_result fd_result;
  int ret;

  memset(&fd_result, 0x00, sizeof(fd_result));
  fd_result.frame_id  = frame_id;
  fd_result.face_cnt  = *p_face_cnt;
  fd_result.face_data = p_face_data;
  ret = ioctl(fh, VIDIOC_MSM_FD_GET_RESULT, &fd_result);
  if (ret < 0) {
    IDBG_ERROR("GET_RESULT fail drv fail %s", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }
  *p_face_cnt = fd_result.face_cnt;

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_start_streaming
 *
 * Description: Face detection driver start streaming, function will:
 *   Request buffers, set working buffer and Trigger
 *   "VIDIOC_STREAMON" command to FD driver.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance
 *   @fh: Face Detection driver file handle.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_start_streaming(faceproc_hw_comp_t *p_comp, int fh)
{
  struct v4l2_requestbuffers req_bufs;
  struct v4l2_control control;
  enum v4l2_buf_type buf_type;
  int ret;

  memset(&req_bufs, 0x00, sizeof(req_bufs));

  req_bufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  req_bufs.memory = V4L2_MEMORY_USERPTR;
  req_bufs.count = FD_DRV_MAX_BUF_NUM;
  ret = ioctl(fh, VIDIOC_REQBUFS, &req_bufs);
  if (ret < 0) {
    IDBG_ERROR("Req buffers fail %s", strerror(errno));
    return IMG_ERR_INVALID_OPERATION;
  }

  control.id = V4L2_CID_FD_WORK_MEMORY_FD;
  control.value = p_comp->workbuf_handle.fd;
  ret = ioctl(fh, VIDIOC_S_CTRL, &control);
  if (ret < 0) {
    IDBG_ERROR("Set working buffer fail %s", strerror(errno));
    return IMG_ERR_INVALID_OPERATION;
  }

  buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  ret = ioctl(fh, VIDIOC_STREAMON, &buf_type);
  if (ret < 0) {
    IDBG_ERROR("Can not start streaming %s", strerror(errno));
    return IMG_ERR_INVALID_OPERATION;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_start_streaming
 *
 * Description: Face detection driver stop streaming, function will:
 *   Trigger "VIDIOC_STREAMOFF" command to FD driver, release
 *   frame and working buffers.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_stop_streaming(int fh)
{
  enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  struct v4l2_requestbuffers req_bufs;
  struct v4l2_control control;
  int ret;

  memset(&req_bufs, 0x00, sizeof(req_bufs));

  ret = ioctl(fh, VIDIOC_STREAMOFF, &buf_type);
  if (ret < 0) {
    IDBG_ERROR("Can not stop streaming %s", strerror(errno));
    return IMG_ERR_INVALID_OPERATION;
  }

  /* Release buffers from the driver */
  req_bufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  req_bufs.memory = V4L2_MEMORY_USERPTR;
  req_bufs.count = 0;
  ret = ioctl(fh, VIDIOC_REQBUFS, &req_bufs);
  if (ret < 0) {
    IDBG_ERROR("Req buffers fail %s", strerror(errno));
    return IMG_ERR_INVALID_OPERATION;
  }

  /* Release working buffer */
  control.id = V4L2_CID_FD_WORK_MEMORY_FD;
  control.value = -1;
  ret = ioctl(fh, VIDIOC_S_CTRL, &control);
  if (ret < 0) {
    IDBG_ERROR("Clean work buffer fail %s", strerror(errno));
    return IMG_ERR_INVALID_OPERATION;
  }


  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_prepare
 *
 * Description: Prepare and allocate necessary resources,
 *  for face detection driver to be capable of streaming.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @fh: Face Detection driver file handle.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_prepare(faceproc_hw_comp_t *p_comp, int fh)
{
  struct v4l2_format fmt;
  struct v4l2_event_subscription event;
  struct v4l2_control control;
  struct v4l2_crop crop;
  uint32_t wbuf_len;
  int ret;

  memset(&fmt, 0x00, sizeof(fmt));
  memset(&crop, 0x00, sizeof(crop));
  memset(&event, 0x00, sizeof(event));

  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  fmt.fmt.pix.width = p_comp->config.frame_cfg.max_width;
  fmt.fmt.pix.height = p_comp->config.frame_cfg.max_height;
  fmt.fmt.pix.bytesperline = p_comp->config.frame_cfg.min_stride;
  ret = ioctl(fh, VIDIOC_S_FMT, &fmt);
  if (ret < 0) {
    IDBG_ERROR("Set format fail %s", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }

  crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  ret = ioctl(fh, VIDIOC_G_CROP, &crop);
  if (ret < 0) {
    IDBG_ERROR("Get crop fail %s", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }
  p_comp->crop.x = (uint32_t)crop.c.left;
  p_comp->crop.y = (uint32_t)crop.c.top;
  p_comp->crop.dx = (uint32_t)crop.c.width;
  p_comp->crop.dy = (uint32_t)crop.c.height;

  event.type = MSM_EVENT_FD;
  ret = ioctl(fh, VIDIOC_SUBSCRIBE_EVENT, &event);
  if (ret < 0) {
    IDBG_ERROR("Subscribe event fail %s", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }

  control.id = V4L2_CID_FD_WORK_MEMORY_SIZE;
  ret = ioctl(fh, VIDIOC_G_CTRL, &control);
  if (ret < 0) {
    IDBG_ERROR("Get work buffer size fail %s ", strerror(errno));
    return IMG_ERR_INVALID_INPUT;
  }

  wbuf_len = p_comp->workbuf_handle.handle ? p_comp->workbuf_handle.length : 0;
  if ((uint32_t)control.value > wbuf_len) {

    /* Release working buffer first, we will need more memory */
    if (p_comp->workbuf_handle.handle) {
      ret = p_comp->b.mem_ops.release_buffer(&p_comp->workbuf_handle);
      if (IMG_ERROR(ret)) {
        IDBG_ERROR("Can not release ION memory");
        return IMG_ERR_GENERAL;
      }
    }

    ret = p_comp->b.mem_ops.get_buffer(IMG_BUFFER_ION_IOMMU,
      ION_HEAP(ION_IOMMU_HEAP_ID), 0, control.value, &p_comp->workbuf_handle);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Can not allocate ION memory");
      return IMG_ERR_GENERAL;
    }
  }

  /* Set lookup table width and height based on actual processing area */
  p_comp->tracking_info.div_x = 1;
  if (p_comp->crop.dx > HWFD_TRACKING_GRID) {
    p_comp->tracking_info.div_x =
      (p_comp->crop.dx + HWFD_TRACKING_GRID - 1) / HWFD_TRACKING_GRID;
  }

  p_comp->tracking_info.div_y = 1;
  if (p_comp->crop.dx > HWFD_TRACKING_GRID) {
    p_comp->tracking_info.div_y =
      (p_comp->crop.dy + HWFD_TRACKING_GRID - 1) / HWFD_TRACKING_GRID;
  }
  p_comp->tracking_info.id_cnt = 0;

  IDBG_LOW("%s%d] crop.dx %d, div_x %d, crop.dy %d, div_y %d",
    __func__, __LINE__,
    p_comp->crop.dx,
    p_comp->tracking_info.div_x,
    p_comp->crop.dy,
    p_comp->tracking_info.div_y
    );

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_unprepare
 *
 * Description: Unprepare and release resources allocated in prepare function.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @fh: Face Detection driver file handle.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_unprepare(faceproc_hw_comp_t *p_comp, int fh)
{
  struct v4l2_format fmt;
  struct v4l2_requestbuffers req_bufs;
  struct v4l2_event_subscription event;
  int ret_val;
  int ret = IMG_SUCCESS;

  memset(&fmt, 0x00, sizeof(fmt));
  memset(&req_bufs, 0x00, sizeof(req_bufs));
  memset(&event, 0x00, sizeof(event));

  req_bufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  req_bufs.memory = V4L2_MEMORY_USERPTR;
  req_bufs.count = 0;
  ret_val = ioctl(fh, VIDIOC_REQBUFS, &req_bufs);
  if (ret_val) {
    IDBG_ERROR("Req buffers fail %s", strerror(errno));
    ret = IMG_ERR_GENERAL;
  }

  event.type = MSM_EVENT_FD;
  ret_val = ioctl(fh, VIDIOC_UNSUBSCRIBE_EVENT, &event);
  if (ret_val < 0) {
    fprintf(stderr, "Error %d %s %d", __LINE__, strerror(errno), ret);
    ret = IMG_ERR_GENERAL;
  }

  /* Release working buffer */
  if (p_comp->workbuf_handle.handle) {
    ret_val = p_comp->b.mem_ops.release_buffer(&p_comp->workbuf_handle);
    if (IMG_ERROR(ret_val)) {
      IDBG_ERROR("Can not release ION memory");
      ret = IMG_ERR_GENERAL;
    }
  }

  return ret;
}

/**
 * Function: faceproc_hw_comp_check_state
 *
 * Description: Check if requested state transition is allowed or not.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @state: State need to be checked.
 *
 * Return values:
 *   IMG_SUCCESS - Transition allowed / IMG_ERR - Transition not allowed.
 **/
int faceproc_hw_comp_check_state(faceproc_hw_comp_t *p_comp, comp_state_t state)
{
  int status;

  pthread_mutex_lock(&p_comp->b.mutex);

  switch (p_comp->b.state) {
  case IMG_STATE_IDLE:
    if (IMG_STATE_INIT == state) {
      status = IMG_SUCCESS;
    } else {
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  case IMG_STATE_INIT:
    if (IMG_STATE_STARTED == state || IMG_STATE_STOPPED == state) {
      status = IMG_SUCCESS;
    } else {
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  case IMG_STATE_STARTED:
    if (IMG_STATE_STOP_REQUESTED == state || IMG_STATE_STOPPED == state) {
      status = IMG_SUCCESS;
    } else {
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  case IMG_STATE_STOP_REQUESTED:
    if (IMG_STATE_STOPPED == state) {
      status = IMG_SUCCESS;
    } else {
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  case IMG_STATE_STOPPED:
    if (IMG_STATE_STARTED == state || IMG_STATE_INIT == state) {
      status = IMG_SUCCESS;
    } else {
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  default:
    IDBG_ERROR("Invalid component state %d", p_comp->b.state);
    status = IMG_ERR_INVALID_INPUT;
    break;
  }

  pthread_mutex_unlock(&p_comp->b.mutex);

  return status;
}

/**
 * Function: faceproc_hw_comp_set_state
 *
 * Description: Set component state, state will be set only if state transition
 *   is allowed.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @state: State need to be set.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_set_state(faceproc_hw_comp_t *p_comp, comp_state_t state)
{
  int ret;

  ret = faceproc_hw_comp_check_state(p_comp, state);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Wrong state transition %d->%d", p_comp->b.state, state);
    return ret;
  }

  pthread_mutex_lock(&p_comp->b.mutex);
  p_comp->b.state = state;
  pthread_mutex_unlock(&p_comp->b.mutex);

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_is_state
 *
 * Description: Check if component is in given state.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @state: State need to be checked.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_is_state(faceproc_hw_comp_t *p_comp, comp_state_t state)
{
  int state_equal;

  pthread_mutex_lock(&p_comp->b.mutex);
  state_equal = (p_comp->b.state == state);
  pthread_mutex_unlock(&p_comp->b.mutex);

  return state_equal;
}

/**
 * Function: faceproc_hw_comp_adjust_size
 *
 * Description: Validate that configuration is supported by
 *   Face Detection driver.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @p_conf: Pointer to Faceproc configuration.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_adjust_size(int fh, faceproc_config_t *p_conf)
{
  int ret;

  ret = faceproc_hw_comp_round_size(fh, &p_conf->frame_cfg);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Max size %dx%d not supported",
      p_conf->frame_cfg.max_width, p_conf->frame_cfg.max_height);
  }

  return ret;
}

/**
 * Function: faceproc_hw_send_cmd_msg
 *
 * Description: Send cmd message.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @thread_msg: Pointer to message need to be sent.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_send_cmd_msg(faceproc_hw_comp_t *p_comp,
  faceproc_hw_thread_msg_t *thread_msg)
{
  size_t write_size;

  write_size = (size_t)write(p_comp->cmd_pipefd[1],
    thread_msg, sizeof(*thread_msg));
  if (write_size != sizeof(*thread_msg)) {
    IDBG_ERROR("Can not write cmd message to pipe");
    return IMG_ERR_GENERAL;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_read_cmd_msg
 *
 * Description: REad cmd message.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @thread_msg: Pointer to message need to be sent.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_read_cmd_msg(faceproc_hw_comp_t *p_comp,
  faceproc_hw_thread_msg_t *thread_msg)
{
  size_t read_size;

  read_size = (size_t)read(p_comp->cmd_pipefd[0],
    thread_msg, sizeof(*thread_msg));
  if (read_size != sizeof(*thread_msg)) {
    IDBG_ERROR("Can not read cmd message to pipe");
    return IMG_ERR_GENERAL;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_send_ack_msg
 *
 * Description: Send ack message.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @thread_msg: Pointer to message need to be sent.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_send_ack_msg(faceproc_hw_comp_t *p_comp,
  faceproc_hw_thread_msg_t *thread_msg)
{
  size_t write_size;

  write_size = (size_t)write(p_comp->ack_pipefd[1],
    thread_msg, sizeof(*thread_msg));
  if (write_size != sizeof(*thread_msg)) {
    IDBG_ERROR("Can not write ack message to pipe");
    return IMG_ERR_GENERAL;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_wait_ack_msg
 *
 * Description: Wait for ack message.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @thread_msg: Pointer to message need to be sent.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_wait_ack_msg(faceproc_hw_comp_t *p_comp,
  faceproc_hw_thread_msg_t *thread_msg)
{
  faceproc_hw_thread_msg_t tmp_thread_msg;
  size_t read_size;
  struct pollfd poll_fd;
  int ret;

  poll_fd.fd = p_comp->ack_pipefd[0];
  poll_fd.events = POLLIN | POLLPRI;
  ret = poll(&poll_fd, 1, FD_MSG_ACK_TIMEOUT_MS);
  if (ret <= 0) {
    /* Do not return error on timeout we want stream off to finish correctly */
    IDBG_ERROR("Poll timeout return ack");
    return IMG_SUCCESS;
  }

  read_size = (size_t)read(p_comp->ack_pipefd[0],
    &tmp_thread_msg, sizeof(tmp_thread_msg));
  if (read_size != sizeof(tmp_thread_msg) ||
    tmp_thread_msg.mode != thread_msg->mode) {
    IDBG_ERROR("Can not read ack message to pipe");
    return IMG_ERR_GENERAL;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_send_msg
 *
 * Description: Send message to HW Faceproc worker thread.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @type: Message type need to be set.
 *   @mode: Message mode blocking/non-blocking.
 *   @p_data: Pointer to message data (It should match with message type).
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_send_msg(faceproc_hw_comp_t *p_comp,
  faceproc_hw_msg_type_t type, faceproc_hw_msg_mode_t mode, void *p_data)
{
  faceproc_hw_thread_msg_t thread_msg;
  int status = IMG_SUCCESS;

  memset(&thread_msg, 0x0, sizeof(thread_msg));

  thread_msg.type = type;
  thread_msg.mode = mode;

  pthread_mutex_lock(&p_comp->b.mutex);

  if (p_comp->fd_drv < 0) {
    IDBG_ERROR("Driver closed can not process the message");
    status = IMG_ERR_GENERAL;
    goto exit_unlock;
  }

  switch (thread_msg.type) {
  case FDHW_MSG_SET_MODE:
    if (p_data) {
      thread_msg.u.mode = *(faceproc_mode_t *)p_data;
    } else {
      IDBG_ERROR("Error FDHW_MSG_SET_MODE %p", p_data);
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  case FDHW_MSG_CFG_UPDATE:
    if (p_data) {
      /* Check that size is supported by Face Detection driver */
      status = faceproc_hw_comp_adjust_size(p_comp->fd_drv, p_data);
      if (IMG_SUCCEEDED(status)) {
        p_comp->config = *(faceproc_config_t *)p_data;
      }
    } else {
      IDBG_ERROR("Error FDHW_MSG_CFG_UPDATE %p", p_data);
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  case FDHW_MSG_CHROMATIX_UPDATE:
    IDBG_INFO("%s %d] FDHW_MSG_CHROMATIX_UPDATE ", __func__,  __LINE__);
    if (p_data) {
      p_comp->fd_chromatix = *(fd_chromatix_t *)p_data;
      p_comp->valid_chromatix = TRUE;
    } else {
      IDBG_ERROR("Error FDHW_MSG_CHROMATIX_UPDATE %p", p_data);
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  case FDHW_MSG_DEBUG_UPDATE:
    if (p_data) {
      thread_msg.u.debug_mode = *(faceproc_dump_mode_t *)p_data;
    } else {
      IDBG_ERROR("Error FDHW_MSG_DEBUG_UPDATE %p", p_data);
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  case FDHW_MSG_QUEUE_FRAME:
    if (p_data) {
      thread_msg.u.frame = (img_frame_t *)p_data;
    } else {
      IDBG_ERROR("Error FDHW_MSG_QUEUE_FRAME %p", p_data);
      status = IMG_ERR_INVALID_INPUT;
    }
    break;
  case FDHW_MSG_START:
    IDBG_MED("FDHW_MSG_START %p", p_data);
    break;
  case FDHW_MSG_STOP:
    IDBG_MED("FDHW_MSG_STOP %p", p_data);
    break;
  case FDHW_MSG_EXIT:
    IDBG_MED("FDHW_MSG_EXIT %p", p_data);
    break;
  case FDHW_MSG_RECOVER:
    IDBG_MED("FDHW_MSG_RECOVER");
    break;
  default:
    IDBG_ERROR("Unknown message %p", p_data);
    status = IMG_ERR_INVALID_INPUT;
    break;
  }
  if (IMG_ERROR(status)) {
    IDBG_ERROR("Wrong message");
    goto exit_unlock;
  }

  status = faceproc_hw_send_cmd_msg(p_comp, &thread_msg);
  if (IMG_ERROR(status)) {
    IDBG_ERROR("Can not write message to pipe");
    goto exit_unlock;
  }

  pthread_mutex_unlock(&p_comp->b.mutex);

  if (mode == FDHW_MSG_MODE_BLOCK) {
    status = faceproc_hw_wait_ack_msg(p_comp, &thread_msg);
    if (IMG_ERROR(status)) {
      IDBG_ERROR("Error wait for ack message");
      goto exit;
    }
  }

exit:
  return status;

exit_unlock:
  pthread_mutex_unlock(&p_comp->b.mutex);
  return status;
}

/**
 * Function: faceproc_hw_comp_set_param
 *
 * Description: Set HW Faceproc component parameter.
 *
 * Arguments:
 *   @handle: Pointer to HW Faceproc component handle.
 *   @parm: Param type need to be set.
 *   @p_data: Pointer to param data (It should match with param type),
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_set_param(void *handle, img_param_type parm, void *p_data)
{
  faceproc_hw_comp_t *p_comp = (faceproc_hw_comp_t *)handle;
  faceproc_hw_msg_mode_t mode = FDHW_MSG_MODE_NON_BLOCK;
  int ret;

  if (!handle || !p_data) {
    IDBG_ERROR("invalid input");
    return IMG_ERR_INVALID_INPUT;
  }

  ret = p_comp->b.ops.set_parm(&p_comp->b, parm, p_data);
  if (ret < 0) {
    IDBG_ERROR("Error set param");
    return ret;
  }

  switch (parm) {
  case QWD_FACEPROC_TRY_SIZE:
    ret = faceproc_hw_comp_round_size(p_comp->fd_drv, p_data);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Try size fail");
    }
    break;
  case QWD_FACEPROC_CFG:
    ret = faceproc_hw_comp_send_msg(p_comp, FDHW_MSG_CFG_UPDATE, mode, p_data);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Can not send set cfg update message");
    }
    break;
  case QWD_FACEPROC_MODE:
    ret = faceproc_hw_comp_send_msg(p_comp, FDHW_MSG_SET_MODE, mode, p_data);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Can not send set mode message");
    }
    break;
  case QWD_FACEPROC_CHROMATIX:
    ret = faceproc_hw_comp_send_msg(p_comp,
      FDHW_MSG_CHROMATIX_UPDATE, mode, p_data);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Can not send set chromatix update message");
    }
    break;
  case QWD_FACEPROC_DUMP_DATA:
    ret = faceproc_hw_comp_send_msg(p_comp,
      FDHW_MSG_DEBUG_UPDATE, mode, p_data);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Can not send debug update message");
    }
    break;
  case QWD_FACEPROC_BATCH_MODE_DATA:
    /* skip  batch update */
    break;
  default:
    IDBG_ERROR("Error, parm=0x%x", parm);
    return IMG_ERR_INVALID_INPUT;
  }

  return ret;
}

/**
 * Function: faceproc_hw_comp_get_param
 *
 * Description: Get HW Faceproc component param.
 *
 * Arguments:
 *   @handle: Pointer to HW Faceproc component handle.
 *   @parm: Param type need to be get.
 *   @p_data: Pointer to store requested param.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_get_param(void *handle, img_param_type param, void *p_data)
{
  faceproc_hw_comp_t *p_comp = (faceproc_hw_comp_t *)handle;
  int status;

  status = p_comp->b.ops.get_parm(&p_comp->b, param, p_data);
  if (status < 0) {
    return status;
  }

  switch (param) {
  case QWD_FACEPROC_RESULT: {
    faceproc_result_t *p_result = (faceproc_result_t *)p_data;

    pthread_mutex_lock(&p_comp->result_mutex);
    *p_result = p_comp->last_result;
    pthread_mutex_unlock(&p_comp->result_mutex);

    break;
  }
  case QWD_FACEPROC_USE_INT_BUFF: {
    uint32_t *p_use_int_buff = (uint32_t *)p_data;

    if (NULL == p_use_int_buff) {
      IDBG_ERROR("%s:%d] invalid input", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    *p_use_int_buff = FALSE;
    break;
  }
  case QWD_FACEPROC_BUFF_TYPE: {
    img_buf_type_t *p_type = (img_buf_type_t *)p_data;

    if (NULL == p_type) {
      IDBG_ERROR("%s:%d] invalid input", __func__, __LINE__);
      return IMG_ERR_INVALID_INPUT;
    }
    *p_type = IMG_BUFFER_ION_IOMMU;
    break;
  }
  default:
    IDBG_ERROR("%s:%d] Error, param=0x%x", __func__, __LINE__, param);
    return IMG_ERR_INVALID_INPUT;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_flush_buffers
 *
 * Description: Flush buffers from compnent queue.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *
 * Return values:
 *   none.
 **/
void faceproc_hw_comp_flush_buffers(faceproc_hw_comp_t *p_comp)
{
  int i;
  int count;
  int status;
  img_frame_t *p_frame;

  count = img_q_count(&p_comp->b.inputQ);

  for (i = 0; i < count; i++) {
    p_frame = img_q_dequeue(&p_comp->b.inputQ);
    if (NULL == p_frame) {
      IDBG_ERROR("Fail to dequeue input buffer");
      continue;
    }
    status = img_q_enqueue(&p_comp->b.outputQ, p_frame);
    if (IMG_ERROR(status)) {
      IDBG_ERROR("Fail to enqueue input buffer, status=%d", status);
      continue;
    }
    IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_BUF_DONE);
  }

}

/**
 * Function: faceproc_hw_comp_reset_batchcounters
 *
 * Description: Reset batchmode counters
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *
 * Return values:
 *   none
 *
 * Note:
 *   none
 **/
static void faceproc_hw_comp_reset_batchcounters(faceproc_hw_comp_t *p_comp)
{
  pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);
  p_comp->batch_result_count = 0;
  p_comp->batch_frame_count = 0;
  pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
}

/**
 * Function: faceproc_hw_comp_set_mode
 *
 * Description: Set HW Faceproc component operating mode,
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @fh: Face Detection driver file handle.
 *   @mode: Mode need to be set.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 *
 * Note:
 *   Supported modes: FACE_DETECT, FACE_DETECT_OFF.
 **/
static int faceproc_hw_comp_set_mode(faceproc_hw_comp_t *p_comp,
  int fh, faceproc_mode_t mode)
{
  int ret;

  if (faceproc_hw_comp_is_state(p_comp, IMG_STATE_INIT)) {
    ret = IMG_SUCCESS;
    goto out;
  }

  switch (mode) {
  case FACE_DETECT_BSGC:
  case FACE_DETECT:
  case FACE_DETECT_LITE:
    ret = faceproc_hw_comp_check_state(p_comp, IMG_STATE_STARTED);
    if (IMG_SUCCEEDED(ret)) {
      faceproc_hw_comp_reset_batchcounters(p_comp);

      ret = faceproc_hw_comp_start_streaming(p_comp, fh);
      if (IMG_SUCCEEDED(ret)) {
        faceproc_hw_comp_set_state(p_comp, IMG_STATE_STARTED);
      } else {
        IDBG_ERROR("Can not start fd engine, ret=%d", ret);
      }
    }
    /* Verify that Component is in started state */
    ret = IMG_SUCCESS;
    if (!faceproc_hw_comp_is_state(p_comp, IMG_STATE_STARTED)) {
      IDBG_ERROR("Can not change fd state to started, ret=%d", ret);
      ret = IMG_ERR_INVALID_OPERATION;
    }
    break;
  case FACE_DETECT_OFF:
    ret = faceproc_hw_comp_check_state(p_comp, IMG_STATE_STOPPED);
    if (IMG_SUCCEEDED(ret)) {
      pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);
      p_comp->skip_on_busy_cnt = 0;
      pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
      ret = faceproc_hw_comp_stop_streaming(fh);
      if (IMG_SUCCEEDED(ret)) {
        faceproc_hw_comp_set_state(p_comp, IMG_STATE_STOPPED);
        /* Flush buffers after stream off driver queue is empty */
        faceproc_hw_comp_flush_buffers(p_comp);
      } else {
        IDBG_ERROR("Can not stop fd engine, ret=%d", ret);
      }
    }
    /* Verify that Component is in stopped state */
    ret = IMG_SUCCESS;
    if (!faceproc_hw_comp_is_state(p_comp, IMG_STATE_STOPPED)) {
      IDBG_ERROR("Can not change fd state to stopped");
      ret = IMG_ERR_INVALID_OPERATION;
    }
    break;
  default:
    IDBG_ERROR("Mode %d not supported", mode);
    ret = IMG_ERR_NOT_SUPPORTED;
    break;
  }

out:
  if (IMG_SUCCEEDED(ret)) {
    p_comp->mode = mode;
  } else {
    IDBG_ERROR("Set mode fail %d", mode);
  }

  return ret;
}

/**
 * Function: faceproc_hw_comp_get_chromatix_cfg
 *
 * Description: Fill face configuration based on chromatix.
 *
 * Arguments:
 *   @p_chromatix: Pointer to Faceproc chromatix.
 *   @p_face_cfg: Pointer to store chromatix config parameters.
 *   @p_tracking: Pointer to store chromatix tracking info.
 *   @p_facial_parts_config: pointer to facial parts config structure.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 *
 * Note:
 *   When chromatix is set component will be using always chromatix
 *   based parameters.
 **/
static int faceproc_hw_comp_get_chromatix_cfg(fd_chromatix_t *p_chromatix,
  faceproc_face_cfg_t *p_face_cfg, faceproc_hw_detect_tracking_t *p_tracking,
  facial_parts_wrap_config_t *p_facial_parts_config)
{
  if (!p_chromatix || !p_face_cfg) {
    IDBG_MED("chromatix or cfg missing skip %p %p", p_face_cfg, p_chromatix);
    return IMG_ERR_INVALID_INPUT;
  }

  /* Max faces to detect need to be included in chromatix */
  p_face_cfg->max_num_face_to_detect =
    faceproc_get_max_number_to_detect(p_chromatix);
  p_face_cfg->detection_mode = p_chromatix->detection_mode;
  p_face_cfg->detection_threshold = (p_chromatix->threshold + 50) / 100;
  p_face_cfg->min_face_size = p_chromatix->min_face_size;
  IDBG_MED("%s:%d]getting in comp from chromatix XXXmin_face_size %d",
    __func__, __LINE__,
    p_face_cfg->min_face_size);

  p_face_cfg->speed = p_chromatix->speed;
  p_face_cfg->pending_buffs_number = p_chromatix->input_pending_buf;

  switch (p_chromatix->angle_front) {
  case FD_ANGLE_15_ALL:
    p_face_cfg->rotation_range = 15;
    break;
  case FD_ANGLE_45_ALL:
    p_face_cfg->rotation_range = 45;
    break;
   case FD_ANGLE_135_ALL:
    p_face_cfg->rotation_range = 135;
    break;
  case FD_ANGLE_ALL:
    p_face_cfg->rotation_range = 360;
    break;
  default:
    IDBG_ERROR("Invalid angle %d", p_chromatix->angle_front);
    return IMG_ERR_INVALID_INPUT;
  }
  IDBG_MED("%s:%d]getting in comp from chromatix XXXmin_rotation_range %d",
    __func__, __LINE__,
    p_face_cfg->rotation_range);

  /* If lock faces is set enable tracking */
  p_tracking->enable = p_chromatix->lock_faces;
  memset(&p_tracking->batch_faces_detected, 0x0,
    sizeof(p_tracking->batch_faces_detected));
  memset(&p_tracking->batch_index, 0x0,
    sizeof(p_tracking->batch_index));
  p_tracking->pt_idx = 0;
  memset(&p_tracking->prev_table, 0x0, sizeof(p_tracking->prev_table));

  /* Set allowed profile angle for new detected face */
  p_tracking->profiles_angle = 0;
  if (p_chromatix->angle_half_profile != FD_ANGLE_NONE) {
    p_tracking->profiles_angle = FD_HALF_PROFILE_ANGLE;
  }
  if (p_chromatix->angle_full_profile != FD_ANGLE_NONE) {
    p_tracking->profiles_angle = FD_FULL_PROFILE_ANGLE;
  }

  if (p_chromatix->non_tracking_threshold) {
    p_tracking->first_detect_threshold =
      p_chromatix->non_tracking_threshold / 100;
  }

  /* Fill facial parts detection tuning */
  p_facial_parts_config->enable_blink =
    p_chromatix->enable_blink_detection;
  p_facial_parts_config->enable_smile =
    p_chromatix->enable_smile_detection;
  p_facial_parts_config->enable_gaze =
    p_chromatix->enable_gaze_detection;
  p_facial_parts_config->detection_threshold =
    p_chromatix->facial_parts_threshold;
  p_facial_parts_config->closed_eye_ratio =
    p_chromatix->closed_eye_ratio_threshold;
  p_facial_parts_config->discard_threshold =
    p_chromatix->assist_facial_discard_threshold;
  p_facial_parts_config->weight_eyes =
    p_chromatix->assist_facial_weight_eyes;
  p_facial_parts_config->weight_mouth =
    p_chromatix->assist_facial_weight_mouth;
  p_facial_parts_config->weight_nose =
    p_chromatix->assist_facial_weight_nose;
  p_facial_parts_config->weight_face =
    p_chromatix->assist_facial_weight_face;
  p_facial_parts_config->discard_face_below =
    p_chromatix->assist_below_threshold;
  p_facial_parts_config->weight_eyes =
    p_chromatix->assist_facial_weight_eyes;
  p_facial_parts_config->eyes_use_max_filter =
    (p_chromatix->assist_facial_eyes_filter_type == FD_FILTER_TYPE_MAX);
  p_facial_parts_config->nose_use_max_filter =
    (p_chromatix->assist_facial_nose_filter_type == FD_FILTER_TYPE_MAX);
  p_facial_parts_config->sw_face_threshold =
    p_chromatix->assist_sw_detect_threshold;
  p_facial_parts_config->sw_face_box_border_per =
    p_chromatix->assist_sw_detect_box_border_perc;
  p_facial_parts_config->sw_face_search_dens =
    p_chromatix->assist_sw_detect_search_dens;
  p_facial_parts_config->sw_face_discard_border =
    p_chromatix->assist_sw_discard_frame_border;
  p_facial_parts_config->sw_face_discard_out =
    p_chromatix->assist_sw_discard_out_of_border;
  p_facial_parts_config->min_threshold =
    p_chromatix->assist_facial_min_face_threshold;
  p_facial_parts_config->enable_contour =
    p_chromatix->enable_contour_detection;
  p_facial_parts_config->contour_detection_mode =
    p_chromatix->ct_detection_mode;
  p_facial_parts_config->enable_fp_false_pos_filtering =
    p_chromatix->enable_facial_parts_assisted_face_filtering;
  p_facial_parts_config->enable_sw_false_pos_filtering =
    p_chromatix->enable_sw_assisted_face_filtering;

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_set_cfg
 *
 * Description: Set HW Faceproc configuration to Face Detection driver.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @fh: Face Detection driver file handle.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 *
 * Note:
 *   When driver is streaming and configuration size changes,
 *   error will be returned.
 **/
static int faceproc_hw_comp_set_cfg(faceproc_hw_comp_t *p_comp, int fh)
{
  int component_started;
  facial_parts_wrap_config_t fp_config;
  faceproc_face_cfg_t *p_face_cfg;
  faceproc_frame_cfg_t *p_frame_cfg;
  int ret;

  component_started = faceproc_hw_comp_is_state(p_comp, IMG_STATE_STARTED);

  /* Protect component private data  */
  pthread_mutex_lock(&p_comp->b.mutex);

  p_face_cfg = &p_comp->config.face_cfg;
  p_frame_cfg = &p_comp->config.frame_cfg;

  if (!component_started) {
    ret = faceproc_hw_comp_prepare(p_comp, fh);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Can not prepare fd, ret=%d", ret);
      goto out;
    }
  } else {
    ret = faceproc_hw_comp_check_size(fh, p_frame_cfg);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Sizes not match, ret=%d", ret);
      goto out;
    }
  }

  /* Get chromatix configuration if available */
  if (p_comp->valid_chromatix) {
    /* update batch mode */
    p_comp->frame_batch_info.batch_mode =
      p_comp->fd_chromatix.enable_frame_batch_mode;
    if (FACE_FRAME_BATCH_MODE_ON == p_comp->frame_batch_info.batch_mode) {
      p_comp->frame_batch_info.frame_batch_size =
        p_comp->fd_chromatix.frame_batch_size;
    } else {
      p_comp->frame_batch_info.frame_batch_size = 1;
    }

    faceproc_hw_comp_get_chromatix_cfg(&p_comp->fd_chromatix,
      p_face_cfg, &p_comp->tracking_info, &fp_config);
  } else {
    memset(&fp_config, 0x00, sizeof(fp_config));
  }
  memset(&p_comp->face_tracker, 0x0, sizeof(p_comp->face_tracker));

  /* If orientation hint is unknown search in all directions with max speed */
  if (p_face_cfg->face_orientation_hint == FD_FACE_ORIENTATION_UNKNOWN) {
    p_face_cfg->rotation_range = 360;
    /*p_face_cfg->speed = ~0;
    p_face_cfg->min_face_size = 0;*/
  }

  /* If use se assisted fd is used set defaults */
  if (p_face_cfg->use_sw_assisted_fd) {
    if (!fp_config.sw_face_box_border_per) {
      fp_config.sw_face_box_border_per = 25;
    }
    if (!fp_config.sw_face_search_dens) {
      fp_config.sw_face_search_dens = 33;
    }
    if (!fp_config.sw_face_threshold) {
      fp_config.sw_face_threshold = 560;
    }
    if (!fp_config.sw_face_discard_border) {
      fp_config.sw_face_discard_border= 5;
    }
    if (!fp_config.sw_face_discard_out) {
      fp_config.sw_face_discard_out = 1;
    }
  }

  ret = faceproc_hw_comp_config(fh, p_face_cfg);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Fail to config fd, ret=%d", ret);
    goto out;
  }

  if (p_comp->facial_parts_hndl) {
    ret = facial_parts_wrap_config(p_comp->facial_parts_hndl, &fp_config);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Can not config face parts, ret=%d", ret);
      goto out;
    }
  }

out:
  pthread_mutex_unlock(&p_comp->b.mutex);
  return ret;
}

/**
 * Function: faceproc_hw_comp_queue_frame
 *
 * Description: Queue frame to driver and input component queue.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @fh: Face Detection driver file handle.
 *   @p_frame: pointer to frame need to be queued.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 *
 * Note:
 *   We are always queuing the frame in component and driver,
 *   in this way we are keeping the frame struct as it is.
 **/
static int faceproc_hw_comp_queue_frame(faceproc_hw_comp_t *p_comp,
  int fh, img_frame_t *p_frame)
{
  int ret;

  ret = faceproc_hw_comp_apply_crop(fh, p_frame, &p_comp->crop);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Fail to apply crop to drv, ret=%d", ret);
    goto error;
  }

  ret = faceproc_hw_comp_queue_drvbuff(fh, p_frame);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Fail to queue buffer to drv, ret=%d", ret);
    goto error;
  }

  ret = img_q_enqueue(&p_comp->b.inputQ, p_frame);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Fail to queue buffer to internal queue, ret=%d", ret);
    goto error;
  }

  return IMG_SUCCESS;

error:
  img_q_enqueue(&p_comp->b.outputQ, p_frame);
  IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_BUF_DONE);
  IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_ERROR);
  return ret;
}

/**
 * Function: faceproc_hw_comp_proc_msg
 *
 * Description: Helper function executed from worker thread context used to
 *   process message sent from HW Faceproc.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @fh: Face Detection driver file handle.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_proc_msg(faceproc_hw_comp_t *p_comp, int fh)
{
  faceproc_hw_thread_msg_t recieved_msg;
  int ack_ret;
  int ret;

  ret = faceproc_hw_read_cmd_msg(p_comp, &recieved_msg);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not read cmd message, ret=%d", ret);
    return IMG_ERR_GENERAL;
  }

  switch (recieved_msg.type) {
  case FDHW_MSG_CHROMATIX_UPDATE:
  case FDHW_MSG_CFG_UPDATE:
    IDBG_MED("%s %d] FDHW_MSG_CHROMATIX_UPDATE ", __func__,  __LINE__);
    /* Reset the counter since chromatix is being changed */
    pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);
    p_comp->skip_on_busy_cnt = 0;
    pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
    ret = faceproc_hw_comp_set_cfg(p_comp, fh);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Fail to set comp cfg");
    }
    break;
  case FDHW_MSG_SET_MODE:
    ret = faceproc_hw_comp_set_mode(p_comp, fh, recieved_msg.u.mode);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Fail to set comp mode");
    }
    break;
  case FDHW_MSG_DEBUG_UPDATE:
    p_comp->dump_mode = recieved_msg.u.debug_mode;
    break;
  case FDHW_MSG_QUEUE_FRAME:
    ret = faceproc_hw_comp_queue_frame(p_comp, fh, recieved_msg.u.frame);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Fail to queue frame");
    }
    /*Reset the counter since frame has been queued*/
    pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);
    p_comp->skip_on_busy_cnt = 0;
    pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
    break;
  case FDHW_MSG_START:
    /* Put component to stopped state and let set mode to start the engine */
    if (faceproc_hw_comp_is_state(p_comp, IMG_STATE_INIT)) {
      faceproc_hw_comp_set_state(p_comp, IMG_STATE_STOPPED);
    }
    ret = faceproc_hw_comp_set_mode(p_comp, fh, p_comp->mode);
    if (IMG_ERROR(ret)) {
      IDBG_ERROR("Fail to start the engine");
    }

    pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);
    p_comp->start_in_progress = FALSE;
    pthread_cond_signal(&p_comp->sync_threadstart_proccount_cond);
    pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
    break;
  case FDHW_MSG_STOP:
    /* During stop we can send ack message here and
     * do stream off in background */
    if (recieved_msg.mode == FDHW_MSG_MODE_BLOCK) {
      /* Flush the buffers before unblocking stop call */
      if (faceproc_hw_comp_is_state(p_comp, IMG_STATE_STARTED)) {
        faceproc_hw_comp_flush_buffers(p_comp);
      }
      ack_ret = faceproc_hw_send_ack_msg(p_comp, &recieved_msg);
      if (IMG_SUCCEEDED(ack_ret)) {
        recieved_msg.mode = FDHW_MSG_MODE_NON_BLOCK;
      }
    }

    /* Stop driver streaming only if component is in started state */
    if (faceproc_hw_comp_is_state(p_comp, IMG_STATE_STARTED)) {
      /* Reset the counter since there is a stream off */
      pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);
      p_comp->skip_on_busy_cnt = 0;
      pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
      ret = faceproc_hw_comp_stop_streaming(fh);
      if (IMG_SUCCEEDED(ret)) {
        faceproc_hw_comp_set_state(p_comp, IMG_STATE_STOPPED);
      } else {
        IDBG_ERROR("Stop streaming failed, ret=%d", ret);
      }
    }

    if (faceproc_hw_comp_is_state(p_comp, IMG_STATE_STOPPED)) {
      /* Flush buffers here, after stream off driver queue is empty */
      ret = faceproc_hw_comp_unprepare(p_comp, fh);
      if (IMG_SUCCEEDED(ret)) {
        /* Flush buffers after unprepare driver queue is empty */
        faceproc_hw_comp_set_state(p_comp, IMG_STATE_INIT);
      } else {
        IDBG_ERROR("Fail to unprepare the engine, ret=%d", ret);
      }
    }
    break;
  case FDHW_MSG_EXIT:
    IDBG_MED("FDHW_MSG_EXIT received");
    ret = 1;
    break;
  case FDHW_MSG_RECOVER:
    IDBG_MED("FDHW_MSG_RECOVER received");
    ret = faceproc_hw_comp_stop_streaming(fh);
      if (IMG_SUCCEEDED(ret)) {
        faceproc_hw_comp_set_state(p_comp, IMG_STATE_STOPPED);
        faceproc_hw_comp_flush_buffers(p_comp);
      } else {
        IDBG_ERROR("Stop streaming failed");
        return IMG_ERR_GENERAL;
      }
      ret = faceproc_hw_comp_start_streaming(p_comp, fh);
      if (IMG_SUCCEEDED(ret)) {
        faceproc_hw_comp_set_state(p_comp, IMG_STATE_STARTED);
      } else {
        IDBG_ERROR("Can not start fd engine");
        return IMG_ERR_GENERAL;
      }
    break;
  default:
    IDBG_ERROR("Unknown message type %d", recieved_msg.type);
    ret = IMG_ERR_NOT_FOUND;
    break;
  }

  if (recieved_msg.mode == FDHW_MSG_MODE_BLOCK) {
    ack_ret = faceproc_hw_send_ack_msg(p_comp, &recieved_msg);
    if (IMG_ERROR(ack_ret)) {
      IDBG_ERROR("Can not send ack message, ack_ret=%d", ack_ret);
      ret = IMG_ERR_GENERAL;
    }
  }

  return ret;
}

/**
 * Function: faceproc_hw_comp_frame_dump
 *
 * Description: Dump frames based on dump configuration
 *
 * Arguments:
 *   @p_frame: Pointer to frame which need to be dumped.
 *   @dump_mode: Frame dump mode.
 *   @tracked: Set if face is tracked.
 *   @faces_detected: Number of detected faces.
 *
 * Return values:
 *   None
 **/
static void faceproc_hw_comp_frame_dump(img_frame_t *p_frame,
  faceproc_dump_mode_t dump_mode, uint32_t tracked, uint32_t faces_detected)
{
  int32_t fdhw_frame_dump_set_count = 0;
  if (!p_frame) {
    IDBG_ERROR("Invalid frame dump input");
    return;
  }

  if (dump_mode != FACE_FRAME_DUMP_OFF) {
    switch (dump_mode) {
    case FACE_FRAME_DUMP_NON_TRACKED:
      if (faces_detected && !tracked) {
        img_dump_frame(p_frame, FACE_DEBUG_PATH, 0, NULL);
      }
      break;
    case FACE_FRAME_DUMP_TRACKED:
      if (tracked) {
        img_dump_frame(p_frame, FACE_DEBUG_PATH, 0, NULL);
      }
      break;
    case FACE_FRAME_DUMP_NOT_DETECTED:
      if (faces_detected == 0) {
        img_dump_frame(p_frame, FACE_DEBUG_PATH, 0, NULL);
      }
      break;
    case FACE_FRAME_DUMP_ALL:
      img_dump_frame(p_frame, FACE_DEBUG_PATH, 0, NULL);
      break;
    case FACE_FRAME_DUMP_NUMBERED_SET:
      fdhw_frame_dump_set_count = img_common_get_prop(
        "persist.camera.fd.frdump.ct", "0");
      IDBG_MED("%s %d, set_count %d dump_count %d",
        __func__, __LINE__,
        fdhw_frame_dump_set_count,
        fdhw_frame_dump_count );
        if (0 == fdhw_frame_dump_set_count) {
          fdhw_frame_dump_count = 0;
        }
        else if (fdhw_frame_dump_count < fdhw_frame_dump_set_count) {
          img_dump_frame(p_frame, FACE_DEBUG_PATH, ++fdhw_frame_dump_count,
            NULL);
        }
      break;
    default:
      return;
    }
  }
}

/**
 * Function: faceproc_hw_reset_track_region
 *
 * Description: Function to reset the track region
 *
 * Arguments:
 *   @p_comp: Pointer to the component
 *   @unique_id: Unique ID of the face
 *   @fill_val: value to be filled
 *   @p_face_region: Face region
 *
 * Return values:
 *   None
 **/
static void faceproc_hw_reset_track_region(faceproc_hw_comp_t *p_comp,
  int32_t unique_id,
  uint8_t fill_val,
  img_rect_t *p_face_region)
{
  int32_t i;
  IDBG_MED("%s:%d] Reset track info for face %d val %d (%d %d %d %d)",
    __func__, __LINE__,
    unique_id, fill_val,
    p_face_region->pos.x,
    p_face_region->pos.y,
    p_face_region->size.width,
    p_face_region->size.height);
  for (i = p_face_region->pos.x;
    i < (p_face_region->pos.x + p_face_region->size.width); i++) {
    memset(&p_comp->tracking_info.table[
      p_comp->tracking_info.cur_tbl_idx][i][p_face_region->pos.y],
      fill_val,
      (p_face_region->size.height) *
      sizeof(p_comp->tracking_info.table[
      p_comp->tracking_info.cur_tbl_idx][i][p_face_region->pos.y]));
  }
}

/**
 * Function: faceproc_hw_add_fake_result
 *
 * Description: Add fake result incase of lost faces
 *
 * Arguments:
 *   @p_frame: Pointer to frame which need to be dumped.
 *   @dump_mode: Frame dump mode.
 *   @tracked: Set if face is tracked.
 *   @faces_detected: Number of detected faces.
 *
 * Return values:
 *   None
 **/
static void faceproc_hw_add_fake_result(faceproc_hw_comp_t *p_comp,
  faceproc_result_t *p_result,
  uint32_t frame_id)
{
#ifdef FD_HW_USE_FAKE_RESULT
  uint32_t i, max_size = 0;
  int32_t p_idx = -1;
  faceproc_hw_result_t *p_hw_result = NULL;
  faceproc_info_t *p_roi = &p_result->roi[0];

  /* find the prominent ID */
  for (i = 0; i < HWFD_MAX_HIST; i++) {
    p_hw_result = &p_comp->face_tracker.hw_result[i];
    if (p_hw_result->roi.unique_id &&
      (p_hw_result->final_detect_count > FD_TRACKING_MIN_DETECT_CNT) &&
      (frame_id - p_hw_result->hw_last_frame_id) < HWFD_LOST_FACE_HOLD) {
      if (p_roi->face_boundary.dx > max_size) {
        max_size = p_roi->face_boundary.dx;
        p_idx = (int32_t)i;
      }
    }
  }

  if (p_idx >= 0) {
    p_hw_result = &p_comp->face_tracker.hw_result[p_idx];
    faceproc_hw_reset_track_region(p_comp, p_hw_result->roi.unique_id,
      p_hw_result->roi.unique_id, &p_hw_result->face_region);
    p_result->num_faces_detected = 1;
    *p_roi = p_hw_result->roi;
    IDBG_MED("%s:%d] Add fake face frame id %d %d face_id %d face ROI (%d %d %d %d)",
      __func__, __LINE__, p_hw_result->hw_last_frame_id, frame_id,
      p_roi->unique_id,
      p_roi->face_boundary.x,
      p_roi->face_boundary.y,
      p_roi->face_boundary.dx,
      p_roi->face_boundary.dy);
  } else {
    IDBG_MED("%s:%d] cannot add face frame_id %d", __func__, __LINE__, frame_id);
  }
#endif
}

/**
 * Function: faceproc_hw_find_last_result
 *
 * Description: Add faces to the face tracker
 *
 * Arguments:
 *   @p_comp: Pointer to the component
 *   @unique_id: unique ID of the face
 *
 * Return values:
 *   None
 **/
static int32_t __unused faceproc_hw_find_last_result(
  faceproc_hw_comp_t *p_comp,
  int32_t unique_id,
  faceproc_hw_result_t **p_old_result)
{
  uint32_t i;
  int32_t valid_index = -1;
  faceproc_hw_result_t *p_hw_result = NULL;

  *p_old_result = NULL;

  if (!unique_id)
   return -1;

  /* find the unique ID */
  for (i = 0; i < HWFD_MAX_HIST; i++) {
    p_hw_result = &p_comp->face_tracker.hw_result[i];
    if (p_hw_result->roi.unique_id &&
      (unique_id == p_hw_result->roi.unique_id)) {

    /* found the entry */
    valid_index = i;
    IDBG_MED("%s:%d] Found last_frame id %d face_id %d detect_cnt %d"
      "valid_idx %d",
      __func__, __LINE__, p_hw_result->hw_last_frame_id, unique_id,
      p_hw_result->sw_detect_count, valid_index);
      *p_old_result = p_hw_result;
      break;
    }
  }
  return valid_index;
}

/**
 * Function: faceproc_hw_find_add_to_facetracker
 *
 * Description: Add faces to the face tracker
 *
 * Arguments:
 *   @p_comp: Pointer to the component
 *   @p_roi: Pointer to the current face result
 *   @frame_id: Current frame ID
 *   @index: if index is present bypass unique ID search
 *   @p_reset_tracker: Pointer to track faceinfo variable
 *
 * Return values:
 *   None
 **/
static bool faceproc_hw_find_add_to_facetracker(faceproc_hw_comp_t *p_comp,
  faceproc_info_t *p_roi,
  uint32_t frame_id,
  int32_t *p_index,
  faceproc_hw_result_t **p_tracked_hw_result)
{
  uint32_t oldest_frame_id = 0xffffff;
  int32_t valid_idx = -1;
  int32_t i;
  faceproc_hw_result_t *p_hw_result = NULL;
  uint32_t frame_id_delta = 0;
  double size_inc, min_d, max_d;

  *p_tracked_hw_result = NULL;
  *p_index = -1;
  /* find the unique ID */
  for (i = 0; i < HWFD_MAX_HIST; i++) {
    p_hw_result = &p_comp->face_tracker.hw_result[i];
    if (p_hw_result->roi.unique_id &&
      (p_roi->unique_id == p_hw_result->roi.unique_id)) {
      /* Filter duplicate result */
      if (p_hw_result->hw_last_frame_id == frame_id) {
        IDBG_ERROR("%s:%d] [FD_DBG_HW_DUP] Duplicate frame id %d face_id %d",
          __func__, __LINE__, frame_id, p_roi->unique_id);
        return false;
      }

      /* Filter incorrect ID */
      min_d = MIN(p_roi->face_boundary.dx, p_hw_result->roi.face_boundary.dx);
      max_d = MAX(p_roi->face_boundary.dx, p_hw_result->roi.face_boundary.dx);
      size_inc = max_d/min_d;
      if (size_inc > FD_FPF_MAX_SIZE_VARIATION) {
        faceproc_hw_reset_track_region(p_comp, p_hw_result->roi.unique_id,
          p_hw_result->roi.unique_id, &p_hw_result->face_region);
        IDBG_ERROR("%s:%d] [FD_DBG_HW_DUP] Filter incorrect frame id %d"
          "face_id %d size_inc %f",
          __func__, __LINE__, frame_id, p_roi->unique_id, size_inc);
        return false;
      }

      valid_idx = i;
      /* found the entry */
      IDBG_MED("%s:%d] Found last_frame id %d %d face_id %d detect_cnt %d"
        "valid_idx %d size_inc %f",
        __func__, __LINE__, p_hw_result->hw_last_frame_id,
        frame_id, p_roi->unique_id,
        p_hw_result->sw_detect_count, valid_idx, size_inc);
      frame_id_delta = frame_id - p_hw_result->hw_last_frame_id;
      break;
    }
  }

  if (valid_idx < 0) {
    valid_idx = 0; /* init */
    /* new ID. replace the oldest entry */
    for (i = 0; i < HWFD_MAX_HIST; i++) {
      p_hw_result = &p_comp->face_tracker.hw_result[i];
      if (!p_hw_result->sw_detect_count) {
        valid_idx = i;
        break;
      }
      if (p_hw_result->hw_last_frame_id < oldest_frame_id) {
        valid_idx = i;
        oldest_frame_id = p_hw_result->hw_last_frame_id;
      }
    }

    p_hw_result = &p_comp->face_tracker.hw_result[valid_idx];

    IDBG_MED("%s:%d] Add last_frame id %d %d face_id %d detect_cnt %d"
      " valid_index %d",
      __func__, __LINE__, p_hw_result->hw_last_frame_id,
      frame_id, p_roi->unique_id, p_hw_result->sw_detect_count, valid_idx);
    p_hw_result->hw_detect_count = 0;
    p_hw_result->hw_last_frame_delta = 0;
    p_hw_result->hw_first_frame_id = frame_id;
    p_hw_result->hw_total_conf = 0;
    p_hw_result->sw_detect_count = 0;
    p_hw_result->sw_trial_count = 0;
    p_hw_result->sw_first_frame_id = 0;
    p_hw_result->sw_last_frame_id = 0;
    p_hw_result->sw_last_frame_delta = 0;
    p_hw_result->sw_total_conf = 0;
    p_hw_result->final_detect_count = 0;
    p_hw_result->final_first_frame_id = 0;
    p_hw_result->final_last_frame_id = 0;
    p_hw_result->final_last_frame_delta = 0;
    p_hw_result->overall_total_conf = 0;
    p_hw_result->overall_detect_status = 0;
  }

  p_hw_result->prev_roi = p_hw_result->roi;
  p_hw_result->roi = *p_roi;
  p_hw_result->hw_last_frame_id = frame_id;
  p_hw_result->hw_detect_count++;
  p_hw_result->hw_last_frame_delta = frame_id_delta;
  p_hw_result->hw_total_conf += p_hw_result->roi.fd_confidence;

  p_hw_result->hw_deviation =
    (double)(p_hw_result->hw_last_frame_id - p_hw_result->hw_first_frame_id) /
    (double)p_hw_result->hw_detect_count;
  *p_tracked_hw_result = p_hw_result;
  *p_index = valid_idx;

  return true;
}

/**
 * Function: faceproc_hw_apply_false_pos_peak_filter
 *
 * Description: Apply false positive peak filter
 *
 * Arguments:
 *   @p_comp: Pointer to the component
 *   @p_reset_tracker: Pointer to track faceinfo variable
 *   @p_hw_result: Current HW result
 *
 * Return values:
 *   None
 **/
static bool faceproc_hw_apply_false_pos_peak_filter(faceproc_hw_comp_t *p_comp,
  uint32_t frame_id,
  bool *p_reset_tracker,
  faceproc_hw_result_t *p_hw_result)
{
  *p_reset_tracker = false;
  /* Filter the result if detection count is not good enough */
  if (p_hw_result->sw_detect_count < FD_TRACKING_MIN_DETECT_CNT) {
    IDBG_MED("%s:%d] [FD_FALSE] Filter FPP frame id %d face_id %d"
      " detect cnt %d %d",
      __func__, __LINE__, p_hw_result->hw_last_frame_id,
      p_hw_result->roi.unique_id,
      p_hw_result->sw_detect_count, FD_TRACKING_MIN_DETECT_CNT);
    return false;
  } else if ((p_hw_result->sw_detect_count == FD_TRACKING_MIN_DETECT_CNT) &&
    (p_hw_result->sw_last_frame_delta > FD_TRACKING_MIN_FRAME_DELTA)) {
    /* Filter the result if detection count is not good enough and
       reset the face from tracker */
    IDBG_MED("%s:%d] [FD_DBG_HW_SW] Filter frame id %d face_id %d"
      " detect cnt %d frame_id_delta %d",
      __func__, __LINE__, frame_id, p_hw_result->roi.unique_id,
      p_hw_result->sw_detect_count, p_hw_result->sw_last_frame_delta);
    p_hw_result->sw_detect_count = 0;
    *p_reset_tracker = true;
    return false;
  }

  return true;
}

/**
 * Function: faceproc_hw_prepare_tracking_info
 *
 * Description: Prepare tracking info based on detected faces.
 *
 * Arguments:
 *   @p_comp: pointer to HW comp
 *   @p_track_info: Pointer to tracking info.
 *   @face_cnt: Number of detected faces.
 *   @p_reset_tracker: Pointer to the flag to indicate whether the
 *             tracker is reset
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_prepare_tracking_info(faceproc_hw_comp_t *p_comp,
  faceproc_hw_detect_tracking_t *p_track_info,
  uint32_t face_cnt,
  uint32_t *p_reset_tracker)
{
  int reset_tracking_info = 0;
  uint8_t b_idx, cur, prev;

  if (!p_track_info) {
    IDBG_ERROR("Invalid input tracking info");
    return IMG_ERR_INVALID_INPUT;
  }

  if (!p_track_info->enable) {
    IDBG_MED("Tracking not enabled skip");
    return IMG_SUCCESS;
  }

  if (!p_comp->frame_batch_info.batch_mode) {
    if (face_cnt && (face_cnt >= p_track_info->faces_detected)) {
      reset_tracking_info = 1;
    }
    IDBG_MED("%s:%d] face_cnt %d hist %d tracked %d reset %d old %d",
      __func__, __LINE__, face_cnt,
      p_track_info->clear_hist_cnt,
      p_track_info->faces_tracked,
      reset_tracking_info,
      p_track_info->faces_detected);
  } else {
    b_idx = p_track_info->frame_id & 0x1;
    cur = p_track_info->batch_index[b_idx];
    p_track_info->batch_faces_detected[b_idx][cur] = face_cnt;
    prev = (cur - 1 + HWFD_MAX_HIST) % HWFD_MAX_HIST;

    if (p_track_info->batch_faces_detected[b_idx][cur] &&
      (p_track_info->batch_faces_detected[b_idx][cur] >=
      p_track_info->batch_faces_detected[b_idx][prev])) {
      reset_tracking_info = 1;
    }
    IDBG_MED("%s:%d] [%d] face_cnt %d hist %d tracked %d reset %d"
      " batch (%d %d)",
      __func__, __LINE__, b_idx, face_cnt,
      p_track_info->clear_hist_cnt,
      p_track_info->faces_tracked,
      reset_tracking_info,
      p_track_info->batch_faces_detected[b_idx][cur],
      p_track_info->batch_faces_detected[b_idx][prev]);

    p_track_info->batch_index[b_idx] = (p_track_info->batch_index[b_idx] + 1)
      % HWFD_MAX_HIST;
  }

  if (!reset_tracking_info &&
    p_track_info->clear_hist_cnt && p_track_info->faces_tracked) {
    p_track_info->clear_hist_cnt--;
    reset_tracking_info = !p_track_info->clear_hist_cnt;
  }

  if (reset_tracking_info && p_track_info->faces_tracked) {
    IDBG_MED("%s:%d] Reset tracker %d hist %d tracked %d", __func__, __LINE__,
      p_track_info->cur_tbl_idx,
      p_track_info->clear_hist_cnt,
      p_track_info->faces_tracked);
    memcpy(p_track_info->old_table, p_track_info->table[
      p_track_info->cur_tbl_idx],
      sizeof(p_track_info->table[0]));
    memset(p_track_info->table[p_track_info->cur_tbl_idx], 0x0,
      sizeof(p_track_info->table[0]));
    p_track_info->faces_tracked = 0;
    p_track_info->clear_hist_cnt = FD_TRACKING_LOST_CNT;
    *p_reset_tracker = true;
  }
  p_track_info->faces_detected = face_cnt;

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_update_tracking_id
 *
 * Description: Update tracking id based on tracking info.
 *
 * Arguments:
 *   @p_holder: Pointer to face holder.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_update_tracking_id(faceproc_hw_face_holder_t *p_holder)
{
  uint32_t center_x, center_y, det_x, det_y;
  struct msm_fd_face_data *p_face;
  uint32_t i, orig_id = 0, cnt = 0, orig_idx = 0;

  if (!p_holder || !p_holder->p_face_data) {
    IDBG_ERROR("Invalid input arguments %p", p_holder);
    return IMG_ERR_INVALID_INPUT;
  }

  if (!p_holder->p_tracking_info || !p_holder->p_tracking_info->enable) {
    IDBG_MED("Tracking info disabled set id to 0");
    p_holder->tracking_id = 0;
    return IMG_SUCCESS;
  }
  p_face = (struct msm_fd_face_data *)p_holder->p_face_data;

  center_x = p_face->face.left + (p_face->face.width >> 1);
  center_y = p_face->face.top + (p_face->face.height >> 1);

  det_x = center_x / p_holder->p_tracking_info->div_x;
  det_y = center_y / p_holder->p_tracking_info->div_y;
  p_holder->tracking_id = 0;
  if (p_holder->p_comp->frame_batch_info.batch_mode) {
    for (i = 0; i < FRAME_BATCH_SIZE_MAX; i++) {
      if (p_holder->p_tracking_info->table[i][det_x][det_y]) {
        p_holder->tracking_id =
          p_holder->p_tracking_info->table[i][det_x][det_y];
        break;
      }
    }
  } else {
    p_holder->tracking_id = orig_id =
      p_holder->p_tracking_info->table[0][det_x][det_y];
    /* if tracking id is not found */
    cnt = (p_holder->p_tracking_info->pt_idx - 1 + HWFD_LOST_FACE_HIST)
      % HWFD_LOST_FACE_HIST;
    for (i = 0; i < HWFD_LOST_FACE_HIST; i++) {
      if (p_holder->p_tracking_info->prev_table[cnt][det_x][det_y]) {
        p_holder->tracking_id =
          p_holder->p_tracking_info->prev_table[cnt][det_x][det_y];
        orig_idx = cnt;
        break;
      }
      cnt = (cnt + 1) % HWFD_LOST_FACE_HIST;
    }
  }
  IDBG_MED("%s:%d] tracking id %d det (%d %d) orig_id %d cnt %d orig_idx %d",
    __func__, __LINE__,
    p_holder->tracking_id, det_x, det_y, orig_id, cnt, orig_idx);

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_sort_faces
 *
 * Description: Callback function passed to qsort for sorting the faces
 *   based on bigger face.
 *
 * Arguments:
 *   @arg1: Pointer to first faceproc_hw_face_holder_t struct.
 *   @arg2: Pointer to second faceproc_hw_face_holder_t struct.
 *
 * Return values:
 *   0 - faces are equal / 1 - face is bigger / 2 - face is smaller.
 **/
static int faceproc_hw_sort_faces(const void *arg1, const void *arg2)
{
  faceproc_hw_face_holder_t *fir_holder = (faceproc_hw_face_holder_t *) arg1;
  faceproc_hw_face_holder_t *sec_holder = (faceproc_hw_face_holder_t *) arg2;
  struct msm_fd_face_data *first_face;
  struct msm_fd_face_data *second_face;
  int diff;

  if (!fir_holder->p_face_data || !sec_holder->p_face_data) {
    return 0;
  }

  first_face = (struct msm_fd_face_data *)fir_holder->p_face_data;
  second_face = (struct msm_fd_face_data *)sec_holder->p_face_data;

  faceproc_hw_update_tracking_id(fir_holder);
  faceproc_hw_update_tracking_id(sec_holder);
#if 1//def __FD_HW_SORT_TRACK_PRIO__
  /* Report tracked faces with priority */
  if (fir_holder->tracking_id && !sec_holder->tracking_id) {
    return -1;
  } else if (!fir_holder->tracking_id && sec_holder->tracking_id) {
    return 1;
  }

  /* Compare confidence as second reference for sorting */
  if (first_face->confidence > second_face->confidence) {
    return -1;
  } else if (first_face->confidence < second_face->confidence) {
    return 1;
  }
  /* Compare face size as third reference for sorting */
  diff = abs((int)first_face->face.width - (int)second_face->face.width);
  if (diff > (int)((first_face->face.width + second_face->face.width) / 8)) {
    if (first_face->face.width > second_face->face.width) {
      return -1;
    } else if (first_face->face.width < second_face->face.width){
      return 1;
    }
  }
#else
    if (first_face->face.width > second_face->face.width) {
      return -1;
    } else if (first_face->face.width < second_face->face.width){
      return 1;
    }
#endif

  return 0;
}

/**
 * Function: faceproc_hw_fill_tracking_info
 *
 * Description: Fill tracking info of detected face.
 *   Mark fields in tracking table based on face position and size.
 *
 * Arguments:
 *   @p_face_holder: Pointer to face detection holder.
 *
 * Return values:
 *   0 - faces are equal / 1 - face is bigger / 2 - face is smaller.
 **/
static int faceproc_hw_fill_tracking_info(
  faceproc_hw_face_holder_t *p_face_holder,
  img_rect_t *p_rect)
{
  struct msm_fd_face_data *p_face_data;
  uint32_t left, top;
  uint32_t size_x, size_y;
  uint32_t i;
  uint32_t orig_tracking_id;

  if (!p_face_holder || !p_face_holder->p_face_data) {
    IDBG_ERROR("Invalid input arguments %p", p_face_holder);
    return IMG_ERR_INVALID_INPUT;
  }

  p_face_data = (struct msm_fd_face_data *)p_face_holder->p_face_data;

  if (current_fdhw_tracking_grid_mode == FDHW_TRACKING_GRID_MODE_ALL_TOUCHED) {
    left = p_face_data->face.left / p_face_holder->p_tracking_info->div_x;
    top = p_face_data->face.top / p_face_holder->p_tracking_info->div_y;
    //add left,width to avoid increasing approximation error.
    size_x = ceil((float)(p_face_data->face.left + p_face_data->face.width)
    / p_face_holder->p_tracking_info->div_x) - left ;
    size_y = ceil((float)(p_face_data->face.top + p_face_data->face.height)
    / p_face_holder->p_tracking_info->div_y) - top;
  } else {
    //GRID_MODE_ALL_MAX_ACCURACY - left,top,width,height rounded - to mark well
    // covered (more than 50% each side) faces
    left = round((float)p_face_data->face.left /
      p_face_holder->p_tracking_info->div_x);
    top = round((float)p_face_data->face.top /
      p_face_holder->p_tracking_info->div_y);
    //add left,width to avoid increasing approximation error.
    size_x = round((float)(p_face_data->face.left + p_face_data->face.width)
    / p_face_holder->p_tracking_info->div_x) - left ;
    size_y = round((float)(p_face_data->face.top + p_face_data->face.height)
    / p_face_holder->p_tracking_info->div_y) - top;
  }

  if (left + size_x > HWFD_TRACKING_GRID) {
    size_x = HWFD_TRACKING_GRID - left;
  }
  if (top + size_y > HWFD_TRACKING_GRID) {
    size_y = HWFD_TRACKING_GRID - top;
  }

  orig_tracking_id = p_face_holder->tracking_id;
  /* Assign new unique id if new face is detected */
  if (!p_face_holder->tracking_id) {
    p_face_holder->p_tracking_info->id_cnt++;
    if (!p_face_holder->p_tracking_info->id_cnt) {
      p_face_holder->p_tracking_info->id_cnt = 1;
    }
    p_face_holder->tracking_id = p_face_holder->p_tracking_info->id_cnt;
  }

  for (i = left; i < (left + size_x); i++) {
    memset(&p_face_holder->p_tracking_info->table[
      p_face_holder->p_tracking_info->cur_tbl_idx][i][top],
      p_face_holder->tracking_id,
      (size_y) * sizeof(p_face_holder->p_tracking_info->table[
      p_face_holder->p_tracking_info->cur_tbl_idx][i][top]));
  }

  IDBG_LOW("%s:%d] LL %d, size_x %d, TT %d, size_y %d, tracking id"
    " %d tracked %d orig %u",
    __func__, __LINE__,
    left,
    size_x,
    top,
    size_y,
    p_face_holder->tracking_id,
    p_face_holder->p_tracking_info->faces_tracked,
    orig_tracking_id);

  p_rect->pos.x = left;
  p_rect->pos.y = top;
  p_rect->size.width = size_x;
  p_rect->size.height = size_y;
  p_face_holder->p_tracking_info->faces_tracked++;

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_fill_result
 *
 * Description: Fill Result from Face holder structure.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @face_cnt: Number of detected faces.
 *   @p_face_holder: Pointer to face holder Array contain detected faces.
 *   @p_frame: Pointer to img frame.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_fill_result(faceproc_hw_comp_t *p_comp,
  uint32_t face_cnt,
  faceproc_hw_face_holder_t *p_face_holder,
  img_frame_t *p_frame)
{
  struct msm_fd_face_data *p_face_data;
  faceproc_result_t *p_result;
  faceproc_hw_face_holder_t face_holder[FD_DRV_MAX_FACE_NUM];
  uint32_t num_faces;
  uint32_t max_face_cnt;
  uint32_t i, j;
  uint8_t face_tracked;
  uint8_t tracked_faces;
  int8_t face_pose;
  int tracking_enable;
  int ret;
  uint32_t reset_tracker = false;
  uint32_t max_conf = 0;
  bool reset_track_region = false;
  img_rect_t face_region;
  faceproc_result_t old_result;
  int32_t last_idx = 0;
  faceproc_hw_result_t *p_stored_result = NULL;
  faceproc_info_t * last_result;

  if (!p_comp || !p_face_holder) {
    IDBG_ERROR("%s:%d] Invalid input arguments %p %p", __func__, __LINE__,
      p_comp, p_face_holder);
    return IMG_ERR_INVALID_INPUT;
  }

  old_result = p_comp->last_result;

  /* Return failure in case of overflow */
  if (p_comp->batch_result_count >= FRAME_BATCH_SIZE_MAX) {
    IDBG_ERROR("%s:%d] Batch Result idx overflow %d ", __func__, __LINE__,
      p_comp->batch_result_count);
    return IMG_ERR_OUT_OF_BOUNDS;
  }

  if (p_comp->frame_batch_info.batch_mode) {
    p_comp->tracking_info.cur_tbl_idx = p_frame->frame_id & 0x1;
    p_result = &p_comp->intermediate_result[p_comp->tracking_info.cur_tbl_idx];
    p_comp->batch_result_count++;
  } else {
    p_comp->tracking_info.cur_tbl_idx = 0;
    p_result = &p_comp->last_result;
  }

  p_comp->tracking_info.frame_id =
    p_result->frame_id = p_frame->frame_id;

  if (!face_cnt) {
    /* Prepare tracking info here as well to handle face lost count */
    faceproc_hw_prepare_tracking_info(p_comp,
      p_face_holder[0].p_tracking_info, face_cnt, &reset_tracker);
    p_result->num_faces_detected = 0;
    IDBG_MED("%s:%d] [FD_FALSE_POS_DBG] No face detected frame_id %d",
      __func__, __LINE__, p_frame->frame_id);
    faceproc_hw_comp_frame_dump(p_frame, p_comp->dump_mode,
      0, 0);
    faceproc_hw_add_fake_result(p_comp, p_result, p_frame->frame_id);
    return IMG_SUCCESS;
  }

  /* Use copy of face holder for data sorting */
  memcpy(face_holder, p_face_holder, face_cnt * sizeof(face_holder[0]));

  /* Sort the result first */
  if (face_cnt > 1) {
    qsort(face_holder, (size_t)face_cnt, sizeof(face_holder[0]),
      faceproc_hw_sort_faces);
  } else {
    faceproc_hw_update_tracking_id(&face_holder[0]);
  }
  faceproc_hw_prepare_tracking_info(p_comp, face_holder[0].p_tracking_info,
    face_cnt, &reset_tracker);
  tracking_enable = face_holder[0].p_tracking_info->enable;

  /* Clip faces to max face count */
  max_face_cnt = p_comp->config.face_cfg.max_num_face_to_detect;
  if (max_face_cnt > MAX_FACE_ROI) {
    max_face_cnt = MAX_FACE_ROI;
  }

  if (face_cnt > max_face_cnt) {
    face_cnt = max_face_cnt;
  }

  num_faces = 0;
  face_tracked = 1;
  tracked_faces = 0;

  IDBG_MED("%s:%d] [FD_DBG_SW] ========== [ frame %d ] ============ ",
    __func__, __LINE__, p_frame->frame_id);

  for (i = 0; i < face_cnt; i++) {
    p_face_data = (struct msm_fd_face_data *)face_holder[i].p_face_data;
    switch (p_face_data->pose) {
    case MSM_FD_POSE_FRONT:
      face_pose = FD_POSE_FRONT_ANGLE;
      break;
    case MSM_FD_POSE_RIGHT_DIAGONAL:
      face_pose = FD_POSE_RIGHT_DIAGONAL_ANGLE;
      break;
    case MSM_FD_POSE_RIGHT:
      face_pose = FD_POSE_RIGHT_ANGLE;
      break;
    case MSM_FD_POSE_LEFT_DIAGONAL:
      face_pose = FD_POSE_LEFT_DIAGONAL_ANGLE;
      break;
    case MSM_FD_POSE_LEFT:
      face_pose = FD_POSE_LEFT_ANGLE;
      break;
    default:
      IDBG_ERROR("%s:%d] Invalid pose in driver result", __func__, __LINE__);
      face_pose = FD_POSE_FRONT_ANGLE;
      break;
    }

    uint8_t prof_angle = (uint8_t)abs(face_pose);
    reset_track_region = false;
    if (tracking_enable) {
      uint32_t conf = (uint32_t)p_face_data->confidence;

      face_tracked = face_holder[i].tracking_id;

      /* If face is not tracked and profile angle is not allowed skip */
      if (!face_tracked) {
        if (prof_angle > face_holder[i].p_tracking_info->profiles_angle) {
          IDBG_MED("%s:%d] [FD_FALSE_POS_DBG] Filter face pose %d %d"
            "Position %dx%d %dx%d "
            "conf %d angle %d frame_id %d",
            __func__, __LINE__, face_pose, p_face_data->pose,
            p_face_data->face.top, p_face_data->face.left,
            p_face_data->face.width, p_face_data->face.height,
            p_face_data->confidence, p_face_data->angle, p_frame->frame_id);
            /* continue; */ //  profile face should be acceptable with
                            //  average conf check
        }
        if (conf < face_holder[i].p_tracking_info->first_detect_threshold) {
          IDBG_MED("%s:%d] [FD_FALSE_POS_DBG] Filter face confidence %d"
            "pose %d Position %dx%d %dx%d "
            "conf %d angle %d frame_id %d",
            __func__, __LINE__, p_face_data->confidence, face_pose,
            p_face_data->face.top, p_face_data->face.left,
            p_face_data->face.width, p_face_data->face.height,
            p_face_data->confidence, p_face_data->angle, p_frame->frame_id);
            /* continue; */ // confidence should be checked later
        }
      }
    }

    p_result->roi[num_faces].face_boundary.dx =
      (uint32_t)p_face_data->face.width;
    p_result->roi[num_faces].face_boundary.dy =
      (uint32_t)p_face_data->face.height;
    p_result->roi[num_faces].face_boundary.x =
      (uint32_t)p_face_data->face.left;
    p_result->roi[num_faces].face_boundary.y =
      (uint32_t)p_face_data->face.top;

    p_result->roi[num_faces].face_angle_roll =
      (int32_t)p_face_data->angle;

    p_result->roi[num_faces].gaze_angle =
      (int32_t)face_pose;

    p_result->roi[num_faces].fd_confidence =
      (int32_t)p_face_data->confidence * 100;

    /* when pose is -90 / 90, make conf value smaller */
    if (prof_angle >= FD_FULL_PROFILE_ANGLE) {
      p_result->roi[num_faces].fd_confidence =
        (int32_t)(p_face_data->confidence * 100.0 *
        FD_HW_CONFIDENCE_ADJ_FOR_POSE);
    }

    max_conf = MAX(p_result->roi[num_faces].fd_confidence, (int32_t)max_conf);

    /* Assign unique id if tracking is enabled */
    faceproc_hw_fill_tracking_info(&face_holder[i], &face_region);
    p_result->roi[num_faces].unique_id = (int)face_holder[i].tracking_id;

    /* Add to tracker. Filter duplications */
    if (!faceproc_hw_find_add_to_facetracker(p_comp, &p_result->roi[num_faces],
      p_frame->frame_id, &last_idx, &p_stored_result)) {
      IDBG_MED("%s:%d] [FD_DBG_SW] Duplicate Filter face %d face_pose %d"
        " Position %dx%d %dx%d "
        " conf %d angle %d frame_id %d",
        __func__, __LINE__, i, face_pose,
        p_face_data->face.top, p_face_data->face.left,
        p_face_data->face.width, p_face_data->face.height,
        p_face_data->confidence, p_face_data->angle, p_frame->frame_id);
      continue;
    }

    if (!p_stored_result) {
      IDBG_ERROR("%s:%d] Error cannot find stored result %d",
        __func__, __LINE__,
        p_frame->frame_id);
      continue;
    }
    /* store face region */
    p_stored_result->face_region = face_region;
    IDBG_MED("%s:%d] [FD_DBG_SW] ID [%d:%d] "
      "pose %d pos %dx%d Siz %d "
      "conf %d angle %d, hw_cnt %d, sw_cnt %d",
      __func__, __LINE__,
      i, (int)face_holder[i].tracking_id, face_pose,
      p_face_data->face.top, p_face_data->face.left,
      p_face_data->face.width,
      p_face_data->confidence, p_face_data->angle,
      p_stored_result->hw_detect_count,
      p_stored_result->sw_detect_count);

    last_result = NULL;
#ifdef RECHECK_LOST_FACE
    /* Find and check the identical face that is also detected in the
       previous frame. */
    // this is to find facesthat is detected in the previous frame but
    //  is't in the current frame
    // there should be a better way to implement it.
    for( j = 0; j < old_result.num_faces_detected; j++) {
      if (old_result.roi[j].unique_id == p_result->roi[num_faces].unique_id) {
        old_result.roi[j].unique_id = -1;
        last_result = &p_result->roi[num_faces];
      }
    }
#else
    for( j = 0; j < old_result.num_faces_detected; j++) {
      if (old_result.roi[j].unique_id == p_result->roi[num_faces].unique_id) {
        last_result = & old_result.roi[j];
      }
    }
#endif

    /* Detect facial parts and perform false positives filtering */
    // if (p_comp->facial_parts_hndl && (FD_USE_SW_FD_ALWAYS ||
    //  !face_tracked)) {
    // conditions to execute SW filter
    //  1.  first latch
    //  2. when h/w conf is low
    //  3. every ?? frames  (might not need  to execute sw every frame...)
    p_result->roi[num_faces].sw_fd_result = 0;

    if (p_comp->facial_parts_hndl && (FD_USE_SW_FD_ALWAYS ||
      !face_tracked ||
      ((p_result->roi[num_faces].fd_confidence >= FD_HW_MIN_HW_CONFIDENCE) &&
      (p_result->roi[num_faces].fd_confidence <= FD_HW_MAX_HW_CONFIDENCE)))) {
      /* check HW detect count if applicable */
      p_stored_result->sw_trial_count++;
      ret = facial_parts_wrap_process_result(p_comp->facial_parts_hndl, p_frame,
        &p_result->roi[num_faces], 1, face_tracked, last_result);

      if (ret == IMG_ERR_NOT_FOUND) {
        IDBG_MED("%s:%d] [FD_FALSE_POS_DBG] SW Filter face %d face_pose %d"
          "Position %dx%d %dx%d "
          "conf %d angle %d frame_id %d last idx %d",
          __func__, __LINE__, i, face_pose,
          p_face_data->face.top, p_face_data->face.left,
          p_face_data->face.width, p_face_data->face.height,
          p_face_data->confidence, p_face_data->angle, p_frame->frame_id,
            last_idx);

        // if sw fd fails, rescaled hw conf to 0 - 500(0 to sw thresh) is
        //  added to overall conf
        p_stored_result->overall_total_conf +=
          (p_result->roi[num_faces].fd_confidence *
          FD_HW_SW_FAIL_OVERALL_CONF_ADJUST_THRES);
      } else {
        /* update sw statistics */
        p_result->roi[num_faces].sw_fd_result = 1;
        p_stored_result->sw_detect_count++;
        if (abs(p_result->roi[num_faces].gaze_angle) >=
          FD_FULL_PROFILE_ANGLE) {
          p_result->roi[num_faces].fd_confidence *=
            FD_HW_CONFIDENCE_ADJ_FOR_POSE;
        }
        p_stored_result->sw_total_conf +=
          p_result->roi[num_faces].fd_confidence;
        p_stored_result->overall_total_conf +=
          p_result->roi[num_faces].fd_confidence;
        p_stored_result->sw_last_frame_delta =
          p_frame->frame_id - p_stored_result->sw_last_frame_id;
        p_stored_result->sw_last_frame_id = p_frame->frame_id;
        if (!p_stored_result->sw_first_frame_id)
          p_stored_result->sw_first_frame_id = p_frame->frame_id;
      }
    } else {
      p_stored_result->overall_total_conf +=
        p_result->roi[num_faces].fd_confidence;
    }

    // check just average of overall confidence
    if (p_stored_result) {
      uint32_t sw_ave_conf = 0;
      uint32_t hw_ave_conf = 0;
      uint32_t oa_ave_conf = 0;

      if (p_stored_result->sw_trial_count > 0) {
        sw_ave_conf = p_stored_result->sw_total_conf /
          p_stored_result->sw_trial_count;
      }

      if (p_stored_result->hw_detect_count > 0) {
        hw_ave_conf = p_stored_result->hw_total_conf /
          p_stored_result->hw_detect_count;
      }
      oa_ave_conf = p_stored_result->overall_total_conf /
        p_stored_result->hw_detect_count;

      // once the target is detected, get thresh lower for a better consistancy
      // current setting .. 520 for 1st latch , 450 for tracking
      uint32_t thresh = p_comp->fd_chromatix.assist_sw_detect_threshold;
      if (!p_stored_result->final_detect_count)
        thresh *= FD_HW_TRACK_THRES_ADJUSTMENT;

      if (oa_ave_conf < thresh) {
        IDBG_MED("%s:%d] [FD_DBG_SW_TRK] [%d] Filtered, cur_conf = %d,"
          " oa_ave_conf = %d,"
          "sw_ave_conf = %d (%d/%d), hw_ave_conf = %d(%d) ",
          __func__, __LINE__,
          p_result->roi[num_faces].unique_id,
          p_result->roi[num_faces].fd_confidence,
          oa_ave_conf, sw_ave_conf, p_stored_result->sw_detect_count,
          p_stored_result->sw_trial_count, hw_ave_conf,
          p_stored_result->hw_detect_count);
        continue;
      } else {
        IDBG_MED("%s:%d] [FD_DBG_SW_TRK] [%d] Accepted, cur_conf = %d,"
          " oa_ave_conf = %d,"
          "sw_ave_conf = %d (%d/%d), hw_ave_conf = %d(%d) ",
          __func__, __LINE__,
          p_result->roi[num_faces].unique_id,
          p_result->roi[num_faces].fd_confidence,
          oa_ave_conf, sw_ave_conf, p_stored_result->sw_detect_count,
          p_stored_result->sw_trial_count, hw_ave_conf,
          p_stored_result->hw_detect_count);
      }
    }

    /* average confidence check might work as a peak filter */
    if (!faceproc_hw_apply_false_pos_peak_filter(p_comp, p_frame->frame_id,
      &reset_track_region, p_stored_result)) {
      /* Fill tracking info */
      IDBG_MED("%s:%d] [FD_FALSE_POS_DBG] Filter FPF %d Unique id %d"
        "face_pose %d Position %dx%d %dx%d "
        "conf %d angle %d frame_id %d reset_track_region %d",
        __func__, __LINE__,
        i, (int)face_holder[i].tracking_id, face_pose,
        p_face_data->face.top, p_face_data->face.left,
        p_face_data->face.width, p_face_data->face.height,
        p_face_data->confidence, p_face_data->angle, p_frame->frame_id,
        reset_track_region);
      if (reset_track_region)
        faceproc_hw_reset_track_region(p_comp, (int)face_holder[i].tracking_id,
          0, &face_region);
      continue;
    }

    /* update final statistics */
    p_stored_result->final_detect_count++;
    p_stored_result->final_last_frame_delta = p_frame->frame_id -
      p_stored_result->final_last_frame_id;
    p_stored_result->final_last_frame_id = p_frame->frame_id;
    p_stored_result->overall_detect_status = 1;

    if (!p_stored_result->final_first_frame_id)
      p_stored_result->final_first_frame_id = p_frame->frame_id;

    IDBG_MED("%s:%d] [FD_FALSE_POS_DBG] [FD_DBG_SUCCESS] Success Face %d"
      "Unique id %d "
      "face_pose %d Position %dx%d %dx%d "
      "conf %d angle %d frame_id %d last_idx %d (cnt f l del conf)"
      "HW (%d %d %d %d %d) "
      "SW (%d %d %d %d %d) Final (%d %d %d %d)",
      __func__, __LINE__,
      i, (int)face_holder[i].tracking_id, face_pose,
      p_face_data->face.top, p_face_data->face.left,
      p_face_data->face.width, p_face_data->face.height,
      p_face_data->confidence, p_face_data->angle,
      p_frame->frame_id, last_idx,
      p_stored_result->hw_detect_count,
      p_stored_result->hw_first_frame_id,
      p_stored_result->hw_last_frame_id,
      p_stored_result->hw_last_frame_delta,
      p_stored_result->hw_total_conf,
      p_stored_result->sw_detect_count,
      p_stored_result->sw_first_frame_id,
      p_stored_result->sw_last_frame_id,
      p_stored_result->sw_last_frame_delta,
      p_stored_result->sw_total_conf,
      p_stored_result->final_detect_count,
      p_stored_result->final_first_frame_id,
      p_stored_result->final_last_frame_id,
      p_stored_result->final_last_frame_delta);

    IDBG_MED("%s:%d] [FD_DBG_SW] IDX %d ID %d "
      "pose %d pos %dx%d Siz %d "
      "conf %d angle %d, hw_cnt %d, sw_cnt %d",
      __func__, __LINE__,
      i, (int)face_holder[i].tracking_id, face_pose,
      p_face_data->face.top, p_face_data->face.left,
      p_face_data->face.width,
      p_face_data->confidence, p_face_data->angle,
      p_stored_result->hw_detect_count,
      p_stored_result->sw_detect_count);

    tracked_faces += face_tracked;
    num_faces++;
  }

#ifdef RECHECK_LOST_FACE
  // if there is a face that is detected by s/w in the previous frame
  //  but is not detected even by hw, execute sw fd.
  for (j = 0; j < old_result.num_faces_detected && num_faces < max_face_cnt;
    j++) {
    // if the target is processed, skip it
    if ((old_result.roi[j].unique_id == -1) || !old_result.roi[j].sw_fd_result)
      continue;

    // execute sw fd
    IDBG_MED("%s:%d] [FD_DBG_SW_TRK] Face ID=%d is not processed",
      __func__, __LINE__, old_result.roi[j].unique_id);
    ret = facial_parts_wrap_process_result(p_comp->facial_parts_hndl, p_frame,
      &old_result.roi[j], 1, face_tracked, NULL);

    // if sw fd doesn't detect any faces
    if (ret == IMG_ERR_NOT_FOUND) {
      IDBG_MED("%s:%d] [FD_FALSE_POS_DBG] SW Filter face %d"
        "face_pose %d Position %dx%d %dx%d "
        "conf %d angle %d frame_id %d last idx %d",
        __func__, __LINE__, i, face_pose,
        p_face_data->face.top, p_face_data->face.left,
        p_face_data->face.width, p_face_data->face.height,
        p_face_data->confidence, p_face_data->angle, p_frame->frame_id,
          last_idx);
        continue;
    }

    // in case sw detects a face, it should be added to tracker.
    IDBG_MED("%s:%d] [FD_DBG_SW_TRK] Face ID=%d is recovered",
      __func__, __LINE__, old_result.roi[j].unique_id);
    memcpy(&p_result->roi[num_faces], &old_result.roi[j],
      sizeof(faceproc_info_t));

    /* Add to tracker regardless hw fd result */
    // the tracking id musts exist alreay...
    if (!faceproc_hw_find_add_to_facetracker(p_comp, &p_result->roi[num_faces],
      p_frame->frame_id, &last_idx, &p_stored_result)) {
      IDBG_MED("[FD_DBG_SW] Duplicate Filter face %d face_pose %d"
        "Position %dx%d %dx%d "
        "conf %d angle %d frame_id %d",
        i, face_pose,
        p_face_data->face.top, p_face_data->face.left,
        p_face_data->face.width, p_face_data->face.height,
        p_face_data->confidence, p_face_data->angle, p_frame->frame_id);
        continue;
    }

    if (!p_stored_result) {
      IDBG_ERROR("%s:%d] Error cannot find stored result frame_id %d",
        __func__, __LINE__,
        p_frame->frame_id);
      continue;
    }
    /* update sw statistics */
    p_stored_result->sw_detect_count++;
    p_stored_result->sw_trial_count++;
    if (abs(p_result->roi[num_faces].gaze_angle) >= FD_FULL_PROFILE_ANGLE) {
      p_result->roi[num_faces].fd_confidence *= FD_HW_CONFIDENCE_ADJ_FOR_POSE;
    }
    p_stored_result->sw_total_conf += p_result->roi[num_faces].fd_confidence;
    p_stored_result->sw_last_frame_delta = p_frame->frame_id -
      p_stored_result->sw_last_frame_id;
    p_stored_result->sw_last_frame_id = p_frame->frame_id;
    if (!p_stored_result->sw_first_frame_id)
      p_stored_result->sw_first_frame_id = p_frame->frame_id;
    /* update final statistics */
    p_stored_result->final_detect_count++;
    p_stored_result->final_last_frame_delta = p_frame->frame_id -
      p_stored_result->final_last_frame_id;
    p_stored_result->final_last_frame_id = p_frame->frame_id;
    if (!p_stored_result->final_first_frame_id)
      p_stored_result->final_first_frame_id = p_frame->frame_id;
    tracked_faces += face_tracked;
    num_faces++;
  }
#endif


  p_result->num_faces_detected = num_faces;
#ifdef _USE_FINAL_RES_BASED_TRACKER_
  if (!num_faces && reset_tracker) {
    memcpy(p_comp->tracking_info.table[
      p_face_holder->p_tracking_info->cur_tbl_idx],
      p_comp->tracking_info.old_table,
      sizeof(p_comp->tracking_info.table[0]));
    p_comp->tracking_info.faces_tracked = 1;
  }
#endif

  if (!num_faces)
    faceproc_hw_add_fake_result(p_comp, p_result, p_frame->frame_id);

  if (!p_comp->frame_batch_info.batch_mode && (HWFD_LOST_FACE_HIST > 1)) {
    memcpy(p_comp->tracking_info.prev_table[p_comp->tracking_info.pt_idx],
      p_comp->tracking_info.table[0],
      sizeof(p_comp->tracking_info.prev_table[0]));
    p_comp->tracking_info.pt_idx = (p_comp->tracking_info.pt_idx + 1) %
      HWFD_LOST_FACE_HIST;
  }

  faceproc_hw_comp_frame_dump(p_frame, p_comp->dump_mode,
    tracked_faces, num_faces);

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_proc_stats
 *
 * Description: Helper function executed from worker thread context, used to
 *   Process Face Detection result event.
 *
 * Arguments:
 *   @fh: Face Detection driver file handle.
 *   @p_face_cnt: Pointer to detected face count need to be filled.
 *   @p_frame_id: Pointer to driver result frame id need to be filled.
 *   @p_face_data: Pointer to face holder Array contain detected faces.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_proc_stats(int fh, int *p_face_cnt,
  int *buf_idx, struct msm_fd_face_data *p_face_data)
{
  uint32_t face_cnt;
  uint32_t frame_id;
  uint32_t buff_idx;
  int ret;

  ret = faceproc_hw_comp_dequeue_drvevent(fh, &face_cnt, &frame_id, &buff_idx);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not dequeue drv event");
    return IMG_ERR_INVALID_OPERATION;
  }

  if (face_cnt > FD_DRV_MAX_FACE_NUM) {
    face_cnt = FD_DRV_MAX_FACE_NUM;
  }

  ret = faceproc_hw_comp_get_drvresult(fh, frame_id, &face_cnt, p_face_data);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not get drv result");
    return IMG_ERR_INVALID_OPERATION;
  }

  *p_face_cnt = face_cnt;
  *buf_idx = buff_idx;

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_merge_results_from_sources
 *
 * Description: merges fd results from two frames.
 *    right now supports only two frames,
 *    need to add support for more than two frames,
 *    may use recursion.
 *
 * Arguments:
 *   @p_comp: Pointer to component .
 *
 * Return values:
 *   None.
**/
void faceproc_hw_merge_results_from_sources(faceproc_hw_comp_t *p_comp)
{
  faceproc_result_t * result0 = &p_comp->intermediate_result[0];
  faceproc_result_t * result1 = &p_comp->intermediate_result[1];
  uint32_t uniqueid_rst_cnt = 0;

  uint32_t idx0 = 0;
  uint32_t idx1 = 0;

  p_comp->last_result.num_faces_detected = 0;
  p_comp->last_result.frame_id = MAX(result0->frame_id, result1->frame_id);

  for (idx0 = 0; idx0 < result0->num_faces_detected; idx0++) {
    p_comp->last_result.roi[p_comp->last_result.num_faces_detected++] =
      result0->roi[idx0];
    //find if same tracking id present in both results.
    int32_t result0_id = result0->roi[idx0].unique_id;
    for (idx1 = 0; idx1 < result1->num_faces_detected; idx1++) {
      if (result0_id == result1->roi[idx1].unique_id) {
        result1->roi[idx1].unique_id = 0;
        uniqueid_rst_cnt++;
        break;
      }
    }
  }

  for (idx1 = 0; idx1 < result1->num_faces_detected &&
    (p_comp->last_result.num_faces_detected < MAX_FACE_ROI); idx1++) {
    if (0 != result1->roi[idx1].unique_id) {
      p_comp->last_result.roi[p_comp->last_result.num_faces_detected++] =
        result1->roi[idx1];
    }
  }

  IDBG_MED("%s:%d] rst %d num faces %d %d final %d frame_idx %u %u",
    __func__, __LINE__,
    uniqueid_rst_cnt, result0->num_faces_detected,
    result1->num_faces_detected,
    p_comp->last_result.num_faces_detected,
    result0->frame_id, result1->frame_id);

}

/**
 * Function: faceproc_hw_comp_proc_buff
 *
 * Description: Helper function executed from worker thread context used to
 *   Process buffer received from Face Detection driver.
 *
 * Arguments:
 *   @p_comp: Pointer to HW Faceproc component instance.
 *   @fh: Face Detection driver file handle.
 *   @result_idx: Last result buffer idx.
 *   @face_cnt: Number of faces detected in last result.
 *   @p_face_hold: Pointer to face holder Array contain detected faces.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
static int faceproc_hw_comp_proc_buff(faceproc_hw_comp_t *p_comp, int fh,
  int result_idx, int face_cnt, faceproc_hw_face_holder_t *p_face_hold)
{
  img_frame_t *p_frame;
  int drv_frame_id;
  uint32_t buff_idx;
  int ret;

  ret = faceproc_hw_comp_dequeue_drvbuff(fh, &drv_frame_id, &buff_idx);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("%s:%d] Can not dequeue buffer from the driver",
      __func__, __LINE__);
    return ret;
  }

  p_frame = img_q_dequeue(&p_comp->b.inputQ);
  if (!p_frame) {
    IDBG_ERROR("%s:%d] Input queue is empty", __func__, __LINE__);
    return IMG_ERR_GENERAL;
  }

  if (buff_idx != FD_FRAME_TO_DRV_BUF_IDX(p_frame->idx)) {
    IDBG_ERROR("%s:%d] Buff index mismatch %d %d ", __func__, __LINE__,
      buff_idx, p_frame->idx);
    ret = IMG_ERR_INVALID_OPERATION;
    goto error;
  }

  if (buff_idx != (uint32_t)result_idx) {
    IDBG_ERROR("%s:%d] Result idx mismatch %d %d ", __func__, __LINE__,
      buff_idx, result_idx);
    ret = IMG_ERR_INVALID_OPERATION;
    goto error;
  }

  IDBG_MED("%s:%d] Result idx buff_idx %d result_idx %d "
    "curr_result_index %d bachmode %d",
    __func__, __LINE__, buff_idx, result_idx,
    p_comp->batch_result_count,
    p_comp->frame_batch_info.batch_mode);

  ret = faceproc_hw_fill_result(p_comp, face_cnt, p_face_hold, p_frame);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("%s:%d] Can not fill result", __func__, __LINE__);
    goto error;
  }

  if ((p_comp->frame_batch_info.batch_mode == FACE_FRAME_BATCH_MODE_ON) &&
    (p_comp->batch_result_count >=
     p_comp->frame_batch_info.frame_batch_size)) {

    faceproc_hw_comp_reset_batchcounters(p_comp);

    pthread_mutex_lock(&p_comp->result_mutex);
    faceproc_hw_merge_results_from_sources(p_comp);

    p_comp->last_result.client_id = p_frame->info.client_id;
    p_comp->last_result.frame_id = p_frame->frame_id;

    pthread_mutex_unlock(&p_comp->result_mutex);

    /* Result is ready send event */
    IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_FACE_PROC);

  } else if (p_comp->frame_batch_info.batch_mode ==
    FACE_FRAME_BATCH_MODE_OFF) {
    /* results are filled in faceproc_hw_fill_result */
    pthread_mutex_lock(&p_comp->result_mutex);
    p_comp->last_result.client_id = p_frame->info.client_id;
    p_comp->last_result.frame_id = p_frame->frame_id;
    pthread_mutex_unlock(&p_comp->result_mutex);

    /* Result is ready send event */
    IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_FACE_PROC);
  }

  /* When result is ready buffer done event will be sent to client */
  img_q_enqueue(&p_comp->b.outputQ, p_frame);
  IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_BUF_DONE);

  return IMG_SUCCESS;

error:
  img_q_enqueue(&p_comp->b.outputQ, p_frame);
  IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_ERROR);
  IMG_SEND_EVENT(&p_comp->b, QIMG_EVT_BUF_DONE);
  return ret;
}

/**
 * Function: faceproc_hw_comp_proc_stats
 *
 * Description: Worker thread used for Face Detection driver
 *   communication.
 *
 * Arguments:
 *   @handle: Pointer to HW Faceproc base component.
 *
 * Return values:
 *   none.
 **/
void *hw_face_proc_thread_loop(void *handle)
{
  img_component_t *p_base = (img_component_t *)handle;
  faceproc_hw_comp_t *p_comp = (faceproc_hw_comp_t *)p_base->p_core;
  faceproc_hw_face_holder_t face_hold[FD_DRV_MAX_FACE_NUM];
  struct msm_fd_face_data face_data[FD_DRV_MAX_FACE_NUM];
  int process_succeeded;
  int face_cnt;
  int buf_idx;
  struct pollfd poll_fd[2];
  nfds_t wait_on_fds;
  int fail_count;
  int drv_fd;
  int ret;
  int i;

  if (!p_comp) {
    IDBG_ERROR("Null component");
    return NULL;
  }
  drv_fd = p_comp->fd_drv;

  /* Initialize face holder array */
  for (i = 0; i < FD_DRV_MAX_FACE_NUM; i++) {
    face_hold[i].p_tracking_info = &p_comp->tracking_info;
    face_hold[i].p_face_data = &face_data[i];
    face_hold[i].p_comp = p_comp;
  }

  poll_fd[0].fd = p_comp->cmd_pipefd[0];
  poll_fd[0].events = POLLIN | POLLPRI | POLLOUT;

  poll_fd[1].fd = drv_fd;
  poll_fd[1].events = POLLPRI;

  fail_count = 0;
  while (fail_count < FD_MAX_FAIL_CNT) {
    poll_fd[0].revents = 0;
    poll_fd[1].revents = 0;

    /* Wait on face detect events only if there are buffers in the queue */
    wait_on_fds = 1;
    if (img_q_count(&p_comp->b.inputQ)) {
      wait_on_fds = 2;
    }

    ret = poll(poll_fd, wait_on_fds, -1);
    if (ret < 0) {
      IDBG_ERROR("Poll error exiting from hw faceproc");
      goto out;
    }
    process_succeeded = 1;

    /* Process the message */
    if (poll_fd[0].revents & (POLLIN | POLLPRI)) {
      ret = faceproc_hw_comp_proc_msg(p_comp, drv_fd);
      if (ret < 0) {
        IDBG_ERROR("Error process message fail cnt %d", fail_count);
        fail_count++;
        process_succeeded = 0;
      }
      /* ret == 1 - means exit thread message received */
      if (ret == 1) {
        goto out;
      }
    }

    /* Get face detection driver result */
    if (poll_fd[1].revents & POLLPRI) {
      ret = faceproc_hw_comp_proc_stats(drv_fd, &face_cnt, &buf_idx, face_data);
      if (ret < 0) {
        IDBG_ERROR("Can not process drv statistics fail cnt %d", fail_count);
        process_succeeded = 0;
        fail_count++;
      }

      /* Schedule next buffer and process and fill facial parts in result */
      ret = faceproc_hw_comp_proc_buff(p_comp, drv_fd, buf_idx,
        face_cnt, face_hold);
      if (ret < 0) {
        IDBG_ERROR("Can not process driver buffer fail cnt %d", fail_count);
        process_succeeded = 0;
        fail_count++;
      }
    }

    if (process_succeeded) {
      fail_count = 0;
    }
  }

out:
  IDBG_MED("Thread exit");

  /* Stop streaming first */
  if (faceproc_hw_comp_is_state(p_comp, IMG_STATE_STARTED)) {
    faceproc_hw_comp_stop_streaming(drv_fd);
    faceproc_hw_comp_set_state(p_comp, IMG_STATE_STOPPED);
  }
  faceproc_hw_comp_flush_buffers(p_comp);

  /* Unprepare the hw just in case */
  faceproc_hw_comp_unprepare(p_comp, drv_fd);

  /* Release driver resources */
  if (!faceproc_hw_comp_is_state(p_comp, IMG_STATE_INIT)) {
    faceproc_hw_comp_set_state(p_comp, IMG_STATE_INIT);
  }

  pthread_mutex_lock(&p_comp->b.mutex);
  p_comp->b.thread_exit = 1;
  pthread_mutex_unlock(&p_comp->b.mutex);

  return NULL;
}

/**
 * Function: faceproc_hw_comp_start
 *
 * Description: Start HW Faceproc component,
 *   this function will sent only start message to worker thread.
 *
 * Arguments:
 *   @handle: Pointer to HW Faceproc handler.
 *   @p_data: Data passed to send
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_start(void *handle, void *p_data)
{
  faceproc_hw_comp_t *p_comp = (faceproc_hw_comp_t *)handle;
  faceproc_hw_msg_mode_t mode = FDHW_MSG_MODE_NON_BLOCK;
  int ret;

  if (!handle) {
    IDBG_ERROR("invalid input");
    return IMG_ERR_INVALID_INPUT;
  }

  pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);

  p_comp->start_in_progress = TRUE;
  ret = faceproc_hw_comp_send_msg(p_comp, FDHW_MSG_START, mode, p_data);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not start engine ret=%d", ret);
    p_comp->start_in_progress = FALSE;
    pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
    return ret;
  }

  pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);


  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_abort
 *
 * Description: Abort HW Faceproc component,
 *   this function will stop working thread and put component to init state.
 *
 * Arguments:
 *   @handle: Pointer to HW Faceproc handler.
 *   @p_data: Data passed to send
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_abort(void *handle, void *p_data)
{
  faceproc_hw_comp_t *p_comp = (faceproc_hw_comp_t *)handle;
  faceproc_hw_msg_mode_t mode = FDHW_MSG_MODE_BLOCK;
  int ret;

  if (!handle) {
    IDBG_ERROR("invalid input");
    return IMG_ERR_INVALID_INPUT;
  }

  /* First check if engine can be stopped */
  ret = faceproc_hw_comp_check_state(p_comp, IMG_STATE_STOPPED);
  if (IMG_ERROR(ret)) {
    IDBG_MED("Face detection is in stopped state do nothing");
    return IMG_SUCCESS;;
  }

  ret = faceproc_hw_comp_send_msg(p_comp, FDHW_MSG_STOP, mode, p_data);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not request stop, ret=%d", ret);
    return ret;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_queue_buffer
 *
 * Description: Queue HW Faceproc component buffer,
 *   this function will only sent queue buffer message to worker thread.
 *
 * Arguments:
 *   @handle: Pointer to HW Faceproc handler.
 *   @p_frame: Pointer to img frame need to be queued.
 *   @type: Only IN buffers supported
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_queue_buffer(void *handle, img_frame_t *p_frame,
  img_type_t type)
{
  faceproc_hw_comp_t *p_comp = (faceproc_hw_comp_t *)handle;
  faceproc_hw_msg_mode_t mode = FDHW_MSG_MODE_NON_BLOCK;
  unsigned int count;
  int ret;

  if (!handle || !p_frame || type != IMG_IN) {
    IDBG_ERROR("invalid input, handle=%p, p_frame=%p, type=%d",
      handle, p_frame, type);
    return IMG_ERR_INVALID_INPUT;
  }

  pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);

  while (p_comp->start_in_progress == TRUE)
    img_wait_for_completion(&p_comp->sync_threadstart_proccount_cond,
      &p_comp->sync_threadstart_proccount_mutex, FD_START_TIMEOUT_MS);

  pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);

  if (!faceproc_hw_comp_is_state(p_comp, IMG_STATE_STARTED)) {
    IDBG_ERROR("Component not started can not queue frame");
    return IMG_ERR_NOT_SUPPORTED;
  }

  /* Component supports only frames with fixed stride */
  if (p_comp->config.frame_cfg.min_stride <
    (uint32_t)p_frame->frame[0].plane[0].stride) {
    IDBG_ERROR("Invalid stride Required %d Actual frame stride %d",
      p_comp->config.frame_cfg.min_stride, p_frame->frame[0].plane[0].stride);
    return IMG_ERR_INVALID_INPUT;
  }

  count = img_q_count(&p_comp->b.inputQ);
  if ((count > p_comp->config.face_cfg.pending_buffs_number) &&
    (p_comp->frame_batch_info.batch_mode == FACE_FRAME_BATCH_MODE_OFF)) {
    IDBG_MED("%s:%d] Skip buffer component busy, pending_buffs_number %d , count %d ",
      __func__, __LINE__, p_comp->config.face_cfg.pending_buffs_number, count);
    pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);
    p_comp->skip_on_busy_cnt++;

    if (p_comp->skip_on_busy_cnt == MAX_TIMEOUT_FRAME_COUNT) {
      IDBG_HIGH("No response for %d frames, Trigger reset",
        p_comp->skip_on_busy_cnt);
      /*Timed out waiting for HW. Reset*/
      p_comp->skip_on_busy_cnt = 0;
      ret = faceproc_hw_comp_send_msg(p_comp, FDHW_MSG_RECOVER, mode, NULL);
      if (IMG_ERROR(ret)) {
        IDBG_ERROR("Cannot do FD HW recovery");
      }
    } else {
      pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
      return IMG_ERR_BUSY;
    }
   pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
  }

  IDBG_MED("%s:%d] pending_buffs_number %d, count %d mode %d batch_count %d %d",
    __func__, __LINE__, p_comp->config.face_cfg.pending_buffs_number, count,
    p_comp->frame_batch_info.batch_mode,
    p_comp->batch_frame_count,
    p_comp->frame_batch_info.frame_batch_size);

  pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);
  if ((p_comp->frame_batch_info.batch_mode == FACE_FRAME_BATCH_MODE_ON) &&
    (p_comp->batch_frame_count >= p_comp->frame_batch_info.frame_batch_size)) {
    IDBG_MED("%s:%d] Skip buffer component busy", __func__, __LINE__);
    pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);
    return IMG_ERR_BUSY;
  }
  pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);

  ret = faceproc_hw_comp_send_msg(p_comp, FDHW_MSG_QUEUE_FRAME, mode, p_frame);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("%s:%d] Can not queue the buffer, ret=%d",
      __func__, __LINE__, ret);
    return ret;
  }
  pthread_mutex_lock(&p_comp->sync_threadstart_proccount_mutex);
  if (p_comp->frame_batch_info.batch_mode == FACE_FRAME_BATCH_MODE_ON) {
    p_comp->batch_frame_count++;
  }
  pthread_mutex_unlock(&p_comp->sync_threadstart_proccount_mutex);


  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_init.
 *
 * Description: Init component and start worker thread.
 *
 * Arguments:
 *   @handle: Pointer to HW Faceproc handler.
 *   @p_userdata: Pointer to user data.
 *   @p_data: pointer to private data.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_init(void *handle, void *p_userdata, void *p_data)
{
  faceproc_hw_comp_t *p_comp = (faceproc_hw_comp_t *)handle;
  int drv_fd;
  int ret;

  if (!handle) {
    IDBG_ERROR("invalid input");
    return IMG_ERR_INVALID_INPUT;
  }

  drv_fd = open(p_comp->p_drv_name, O_RDWR | O_NONBLOCK);
  if (drv_fd < 0) {
    IDBG_ERROR("Can not open hw fd");
    return IMG_ERR_GENERAL;
  }
  p_comp->fd_drv = drv_fd;

  ret = p_comp->b.ops.init(&p_comp->b, p_userdata, p_data);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not open hw fd, ret=%d", ret);
    return ret;
  }

  ret = p_comp->b.ops.start(&p_comp->b, p_data);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not start thread, ret=%d", ret);
    return ret;
  }

  /* Put component back to init state */
  faceproc_hw_comp_set_state(p_comp, IMG_STATE_STOPPED);
  ret = faceproc_hw_comp_set_state(p_comp, IMG_STATE_INIT);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not set fd comp to init state, ret=%d", ret);
    return ret;
  }

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_deinit
 *
 * Description: Deinit component and destroy worker thread.
 *
 * Arguments:
 *   @handle: Pointer to HW Faceproc handler.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_deinit(void *handle)
{
  faceproc_hw_comp_t *p_comp = (faceproc_hw_comp_t *)handle;
  faceproc_hw_msg_mode_t mode = FDHW_MSG_MODE_NON_BLOCK;
  int ret;

  if (!handle) {
    IDBG_ERROR("invalid input");
    return IMG_ERR_INVALID_INPUT;
  }

  /* If state is IDLE thread is not yet created */
  if (!faceproc_hw_comp_is_state(p_comp, IMG_STATE_IDLE)) {
    ret = faceproc_hw_comp_send_msg(p_comp, FDHW_MSG_EXIT, mode, NULL);
    if (IMG_ERROR(ret)) {
      IDBG_MED("Thread is already dead");
    }

    if (!pthread_equal(pthread_self(), p_comp->b.threadid)) {
      IDBG_MED("thread id 0x%x 0x%x",(uint32_t)pthread_self(),
        (uint32_t)p_comp->b.threadid);
      pthread_join(p_comp->b.threadid, NULL);
    }
  }

  ret = p_comp->b.mem_ops.close();
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Img mem close fail, ret=%d", ret);
    return ret;
  }

  ret = p_comp->b.ops.deinit(&p_comp->b);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not deinit base component, ret=%d", ret);
    return ret;
  }

  if (p_comp->facial_parts_hndl) {
    facial_parts_wrap_destroy(p_comp->facial_parts_hndl);
  }

  pthread_mutex_destroy(&p_comp->result_mutex);
  pthread_mutex_destroy(&p_comp->sync_threadstart_proccount_mutex);
  pthread_cond_destroy(&p_comp->sync_threadstart_proccount_cond);

  close(p_comp->fd_drv);

  close(p_comp->ack_pipefd[0]);
  close(p_comp->ack_pipefd[1]);

  close(p_comp->cmd_pipefd[0]);
  close(p_comp->cmd_pipefd[1]);

  free(p_comp);

  return IMG_SUCCESS;
}

/**
 * Function: faceproc_hw_comp_create
 *
 * Description: Create component, create pipe for worker thread communication
 *   and open component memory manager used for internal allocations.
 *
 * Arguments:
 *   @handle: Pointer to HW Faceproc handler.
 *   @p_ops: Pointer to component operation for storing component
 *     function table.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_create(void* handle, img_component_ops_t *p_ops)
{
  faceproc_hw_comp_t *p_comp = NULL;
  int ret;

  if (!handle|| !p_ops) {
    IDBG_ERROR("Library not loaded, handle=%p, p_ops=%p", handle, p_ops);
    return IMG_ERR_INVALID_OPERATION;
  }

  p_comp = (faceproc_hw_comp_t *)calloc(1, sizeof(faceproc_hw_comp_t));
  if (NULL == p_comp) {
    IDBG_ERROR("Not enough memory");
    return IMG_ERR_NO_MEMORY;
  }

  ret = pipe(p_comp->cmd_pipefd);
  if (ret < 0) {
    IDBG_ERROR("Can not create command pipe");
    goto error_cmd_pipe;
  }

  ret = pipe(p_comp->ack_pipefd);
  if (ret < 0) {
    IDBG_ERROR("Can not create acknowledge pipe");
    goto error_ack_pipe;
  }

  ret = img_comp_create(&p_comp->b);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Img component create fail, ret=%d", ret);
    goto error_create;
  }

  ret = p_comp->b.mem_ops.open();
  if (ret < 0) {
    ret = IMG_ERR_GENERAL;
    IDBG_ERROR("Img component mem open fail, ret=%d", ret);
    goto error;
  }

  p_comp->facial_parts_hndl = facial_parts_wrap_create();
  if (!p_comp->facial_parts_hndl) {
    IDBG_WARN("Facial create failed. Working without them");
  }

  /* Set the main thread */
  p_comp->b.thread_loop = hw_face_proc_thread_loop;
  p_comp->b.p_core = p_comp;

  /* Store pointer of face detection node name */
  p_comp->p_drv_name = (char *)handle;

  /* Copy the ops table from the base component */
  *p_ops = p_comp->b.ops;
  p_ops->init = faceproc_hw_comp_init;
  p_ops->deinit = faceproc_hw_comp_deinit;
  p_ops->set_parm = faceproc_hw_comp_set_param;
  p_ops->get_parm = faceproc_hw_comp_get_param;
  p_ops->start = faceproc_hw_comp_start;
  p_ops->abort = faceproc_hw_comp_abort;
  p_ops->queue_buffer = faceproc_hw_comp_queue_buffer;

  p_ops->handle = (void *)p_comp;

  /* Init component default values */
  p_comp->fd_drv = -1;
  pthread_mutex_init(&p_comp->result_mutex, NULL);
  pthread_mutex_init(&p_comp->sync_threadstart_proccount_mutex, NULL);
  pthread_cond_init(&p_comp->sync_threadstart_proccount_cond, NULL);
  p_comp->tracking_info.div_x = 1;
  p_comp->tracking_info.div_y = 1;
  p_comp->tracking_info.first_detect_threshold = 9;
  p_comp->dump_mode = FACE_FRAME_DUMP_OFF;

  return IMG_SUCCESS;

error:
  p_comp->b.ops.deinit(&p_comp->b);
error_create:
  close(p_comp->ack_pipefd[0]);
  close(p_comp->ack_pipefd[1]);
error_ack_pipe:
  close(p_comp->cmd_pipefd[0]);
  close(p_comp->cmd_pipefd[1]);
error_cmd_pipe:
  free(p_comp);

  IDBG_ERROR("%s:%d] failed %d", __func__, __LINE__, ret);
  return ret;
}

/**
 * Function: faceproc_hw_comp_load
 *
 * Description: Try to discover Face Detection device node, if device
 *   node is not present load will fail.
 *
 * Arguments:
 *   @name: HW Faceproc name - not used this is checked by base module.
 *   @handle: Result pointer to char array contain driver name.
 *
 * Return values:
 *   IMG_SUCCESS/IMG_ERR.
 **/
int faceproc_hw_comp_load(const char* name, void** handle)
{
  IMG_UNUSED(name);

  int ret;
  char *p_drv_name;

  p_drv_name = calloc(1, sizeof(*p_drv_name) * FD_DRV_MAX_NAME_SIZE);
  if (!p_drv_name) {
    IDBG_ERROR("Can not allocate memory for device node name");
    return IMG_ERR_NO_MEMORY;
  }

  ret = faceproc_hw_comp_get_fd_device_name(p_drv_name, FD_DRV_MAX_NAME_SIZE);
  if (IMG_ERROR(ret)) {
    IDBG_ERROR("Can not find device name");
    free(p_drv_name);
  } else {
    IDBG_MED("Component load successfully device name %s", p_drv_name);
    *(char **)handle = p_drv_name;
  }

  return ret;
}

/**
 * Function: faceproc_hw_comp_unload
 *
 * Description: Unload Face component, currently not needed.
 *
 * Arguments:
 *   @handle: Pointer to component handle.
 *
 * Return values:
 *   None.
 **/
void faceproc_hw_comp_unload(void* handle)
{
  if (!handle) {
    IDBG_ERROR("Wrong handler");
    return;
  }

  free(handle);

  IDBG_MED("Component unloaded");
}
