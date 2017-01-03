/**********************************************************************
*  Copyright (c) 2013-2016 Qualcomm Technologies, Inc.
*  All Rights Reserved.
*  Confidential and Proprietary - Qualcomm Technologies, Inc.
**********************************************************************/

#ifndef __IMG_COMMON_H__
#define __IMG_COMMON_H__

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <linux/msm_ion.h>
#include <stdbool.h>
#include "img_mem_ops.h"
#include "img_thread_ops.h"

/**
 * CONSTANTS and MACROS
 **/
#define MAX_PLANE_CNT 3
#define MAX_FRAME_CNT 2
#define GAMMA_TABLE_ENTRIES 64
#define RNR_LUT_SIZE 164
#define SKINR_LUT_SIZE 255
// Below should be dependent on FD setting.
#define MAX_FD_ROI 10
#define IMG_MAX_INPUT_FRAME 8
#define IMG_MAX_OUTPUT_FRAME 1
#define IMG_MAX_META_FRAME 8

#undef TRUE
#undef FALSE
#undef MIN
#undef MAX

#define TRUE 1
#define FALSE 0

#define IMG_LIKELY(x) __builtin_expect((x),1)
#define IMG_UNLIKELY(x) __builtin_expect((x),0)

#define MIN(a,b) ((a)>(b)?(b):(a))
#define MAX(a,b) ((a)<(b)?(b):(a))

#define MIN2(a,b)      ((a<b)?a:b)
#define MIN4(a,b,c,d)  (MIN2(MIN2(a,b),MIN2(c,d)))
#define MAX2(a,b)      ((a>b)?a:b)
#define MAX4(a,b,c,d)  (MAX2(MAX2(a,b),MAX2(c,d)))
#define CLIP(x, lower, upper)  {x = ((x < lower) ? lower : \
                               ((x > upper) ? upper : x)); }
#define CEILING8(X)  (((X) + 0x0007) & 0xFFF8)

/** BILINEAR_INTERPOLATION
 *
 *   Bilinear interpolation
 **/
#ifndef BILINEAR_INTERPOLATION
#define BILINEAR_INTERPOLATION(v1, v2, ratio) ((v1) + ((ratio) * ((v2) - (v1))))
#endif

/** FLOAT_TO_Q:
 *
 *   convert from float to integer
 **/
#define FLOAT_TO_Q(exp, f) \
  ((int32_t)((f*(1<<(exp))) + ((f<0) ? -0.5 : 0.5)))

/** Sign:
 *
 *   Sign of number
 **/
#ifndef sign
#define sign(x) ((x < 0) ?(-1) : (1))
#endif

/** Round:
 *
 *   Round the value
 **/
#ifndef Round
#define Round(x) (int)(x + sign(x)*0.5)
#endif

/**
 *   feature enable/disable
 **/
#define IMG_ON  1
#define IMG_OFF 0

/**
 *   indices for semiplanar frame
 **/
#define IY 0
#define IC 1

/**
 *   chroma indices for planar frame
 **/
#define IC1 1
#define IC2 2


/** IMG_PAD_TO_X
 *   @v: value to be padded
 *   @x: alignment
 *
 *   Returns padded value w.r.t x
 **/
#define IMG_PAD_TO_X(v, x) (((v)+(x-1))&~(x-1))

/** IMG_MIN
 *
 *   returns minimum of the two values
 **/
#define IMG_MIN(a,b) ((a)>(b)?(b):(a))

/** IMG_MAX
 *
 *   returns maximum among the two values
 **/
#define IMG_MAX(a,b) ((a)<(b)?(b):(a))

/** IMG_AVG
 *
 *   returns average of the two values
 **/
#define IMG_AVG(a,b) (((a)+(b))/2)

/* utility functions to get frame info */
/** IMG_ADDR
 *   @p: pointer to the frame
 *
 *   Returns the Y address from the frame
 **/
#define IMG_ADDR(p) ((p)->frame[0].plane[0].addr)

/** IMG_WIDTH
 *   @p: pointer to the frame
 *
 *   Returns the Y plane width
 **/
#define IMG_WIDTH(p) ((p)->frame[0].plane[0].width)

/** FD_WIDTH
 *   @p: pointer to the frame
 *
 *   Returns the Y plane width.
 *   In SW Face detection stride is not supported, return width as stride.
 **/
#define IMG_FD_WIDTH(p) \
  ((p)->frame[0].plane[0].stride > (p)->frame[0].plane[0].width ? \
  (p)->frame[0].plane[0].stride : (p)->frame[0].plane[0].width)

/** IMG_HEIGHT
 *   @p: pointer to the frame
 *
 *   Returns the Y plane height
 **/
#define IMG_HEIGHT(p) ((p)->frame[0].plane[0].height)

/** IMG_Y_LEN
 *   @p: pointer to the frame
 *
 *   Returns the length of Y plane
 **/
#define IMG_Y_LEN(p) ((p)->frame[0].plane[0].length)

/** IMG_FD
 *   @p: pointer to the frame
 *
 *   Returns the fd of the frame
 **/
#define IMG_FD(p) ((p)->frame[0].plane[0].fd)

/** IMG_2_MASK
 *
 *   converts integer to 2^x
 **/
#define IMG_2_MASK(x) (1 << (x))

/** QIMG_CEILINGN
 *   @X: input 32-bit data
 *   @N: value to which input should be aligned
 *
 *   Align input X w.r.t N
 **/
#define QIMG_CEILINGN(X, N) (((X) + ((N)-1)) & ~((N)-1))

/** QIMG_WIDTH
 *   @p: pointer to the frame
 *
 *   Returns the ith plane width
 **/
#define QIMG_WIDTH(p, i) ((p)->frame[0].plane[i].width)

/** QIMG_HEIGHT
 *   @p: pointer to the frame
 *
 *   Returns the ith plane height
 **/
#define QIMG_HEIGHT(p, i) ((p)->frame[0].plane[i].height)

/** QIMG_STRIDE
 *   @p: pointer to the frame
 *
 *   Returns the ith plane stride
 **/
#define QIMG_STRIDE(p, i) ((p)->frame[0].plane[i].stride)

/** QIMG_SCANLINE
 *   @p: pointer to the frame
 *
 *   Returns the ith plane scanline
 **/
#define QIMG_SCANLINE(p, i) ((p)->frame[0].plane[i].scanline)

/** QIMG_LEN
 *   @p: pointer to the frame
 *
 *   Returns the ith plane length
 **/
#define QIMG_LEN(p, i) ((p)->frame[0].plane[i].length)

/** QIMG_FD
 *   @p: pointer to the frame
 *
 *   Returns the fd of the ith frame
 **/
#define QIMG_FD(p, i) ((p)->frame[0].plane[i].fd)

/** QIMG_ADDR
 *   @p: pointer to the frame
 *
 *   Returns the addr of the ith frame
 **/
#define QIMG_ADDR(p, i) ((p)->frame[0].plane[i].addr)

/** QIMG_LOCK
 *   @p: pointer to the mutex
 *
 *   macro for thread lock
 **/
#define QIMG_LOCK(p) pthread_mutex_lock(p)

/** QIMG_UNLOCK
 *   @p: pointer to the mutex
 *
 *   macro for thread unlock
 **/
#define QIMG_UNLOCK(p) pthread_mutex_unlock(p)

/** QIMG_PL_TYPE
 *   @p: pointer to the frame
 *
 *   Returns the plane type of the ith frame
 **/
#define QIMG_PL_TYPE(p, i) ((p)->frame[0].plane[i].plane_type)

/** IMG_FRAME_LEN
 *   @p: pointer to the frame
 *
 *   Returns the fd of the frame
 **/
#define IMG_FRAME_LEN(p) ({ \
  uint32_t i = 0, len = 0; \
  for (i = 0; i < (p)->frame[0].plane_cnt; i++) { \
    len += (p)->frame[0].plane[i].length; \
  } \
  len; \
})

/** Imaging values error values
*    IMG_SUCCESS - success
*    IMG_ERR_GENERAL - any generic errors which cannot be defined
*    IMG_ERR_NO_MEMORY - memory failure ION or heap
*    IMG_ERR_NOT_SUPPORTED -  mode or operation not supported
*    IMG_ERR_INVALID_INPUT - input passed by the user is invalid
*    IMG_ERR_INVALID_OPERATION - operation sequence is invalid
*    IMG_ERR_TIMEOUT - operation timed out
*    IMG_ERR_NOT_FOUND - object is not found
*    IMG_GET_FRAME_FAILED - get frame failed
*    IMG_ERR_OUT_OF_BOUNDS - input to function is out of bounds
*    IMG_ERR_SSR - DSP sub system restart error
*    IMG_ERR_EAGAIN - Execution not complete
*
**/
#define IMG_SUCCESS                   0
#define IMG_ERR_GENERAL              -1
#define IMG_ERR_NO_MEMORY            -2
#define IMG_ERR_NOT_SUPPORTED        -3
#define IMG_ERR_INVALID_INPUT        -4
#define IMG_ERR_INVALID_OPERATION    -5
#define IMG_ERR_TIMEOUT              -6
#define IMG_ERR_NOT_FOUND            -7
#define IMG_GET_FRAME_FAILED         -8
#define IMG_ERR_OUT_OF_BOUNDS        -9
#define IMG_ERR_BUSY                 -10
#define IMG_ERR_CONNECTION_FAILED    -11
#define IMG_ERR_SSR                  -12
#define IMG_ERR_EAGAIN               -13

/** SUBSAMPLE_TABLE
*    @in: input table
*    @in_size: input table size
*    @out: output table
*    @out_size: output table size
*    @QN: number of bits to shift while generating output tables
*
*    Macro to subsample the tables
**/
#define SUBSAMPLE_TABLE(in, in_size, out, out_size, QN) ({ \
  int i, j = 0, inc = (in_size)/(out_size); \
  for (i = 0, j = 0; j < (out_size) && i < (in_size); j++, i += inc) \
    out[j] = ((int32_t)in[i] << QN); \
})

/** IMG_ERROR
*    @v: status value
*
*    Returns true if the status is error
**/
#define IMG_ERROR(v) ((v) != IMG_SUCCESS)

/** IMG_SUCCEEDED
*    @v: status value
*
*    Returns true if the status is success
**/
#define IMG_SUCCEEDED(v) ((v) == IMG_SUCCESS)

/** IMG_LENGTH
*    @size: image size structure
*
*    Returns the length of the frame
**/
#define IMG_LENGTH(size) (size.width * size.height)

/** IMG_CEIL_FL1
*    @x: image to be converted
*
*    Ceil the image to one decimal point.
*    For eq:- 1.12 will be converted to 1.2
**/
#define IMG_CEIL_FL1(x) ((((int)((x) * 10 + .9))/10))

/** IMG_TRANSLATE2
*    @v: value to be converted
*    @s: scale factor
*    @o: offset
*
*    Translate the coordinates w.r.t scale factors and offset
*    Use this if final coordinates need to be the reverse of
*    crop + downscale
**/
#define IMG_TRANSLATE2(v, s, o) ((float)(v) * (s) + (float)(o))

/** IMG_TRANSLATE
*    @v: value to be converted
*    @s: scale factor
*    @o: offset
*
*    Translate the coordinates w.r.t scale factors and offset
*    Use this if final coordinates need to be the reverse of
*    downscale + crop
**/
#define IMG_TRANSLATE(v, s, o) (((float)(v) - (float)(o)) * (s))

/** IMG_OFFSET_FLIP
*    @x: frame width/height
*    @v: value to be converted
*    @o: offset
*
*    Translate the scale factors w.r.t coordinates and offset
**/
#define IMG_OFFSET_FLIP(x, v, o) ((uint32_t)(x) - (uint32_t)(v) - (uint32_t)(o))

/** IMG_FLIP
*    @x: frame width/height
*    @v: value to be converted
*
*    Flip the factors w.r.t org width/height
**/
#define IMG_FLIP(x, v) ((uint32_t)(x) - (uint32_t)(v))

/** IMG_DUMP_TO_FILE:
 *  @filename: file name
 *  @p_addr: address of the buffer
 *  @len: buffer length
 *
 *  dump the image to the file
 **/
#define IMG_DUMP_TO_FILE(filename, p_addr, len) ({ \
  size_t rc = 0; \
  FILE *fp = fopen(filename, "w+"); \
  if (fp) { \
    rc = fwrite(p_addr, 1, len, fp); \
    IDBG_INFO("%s:%d] written size %zu", __func__, __LINE__, len); \
    fclose(fp); \
  } else { \
    IDBG_ERROR("%s:%d] open %s failed", __func__, __LINE__, filename); \
  } \
})


/** IMG_PRINT_RECT:
   *  @p: img rect
   *
   *  prints the crop region
   **/
#define IMG_PRINT_RECT(p) ({ \
  IDBG_MED("%s:%d] crop info (%d %d %d %d)", __func__, __LINE__, \
    (p)->pos.x, \
    (p)->pos.y, \
    (p)->size.width, \
    (p)->size.height); \
})

/** IMG_RECT_IS_VALID:
   *  @p: img rect
   *  @w: width of the main image
   *  @h: height of the main image
   *
   *  check if the region is valid
   **/
#define IMG_RECT_IS_VALID(p, w, h) (((p)->pos.x >= 0) && ((p)->pos.y >= 0) && \
  ((p)->size.width > 0) && ((p)->size.height > 0) && \
  (((p)->pos.x + (p)->size.width) < w) && \
  (((p)->pos.y + (p)->size.height) < h))

/** IMG_F_EQUAL:
 *  @a: floating point input
 *  @b: floating point input
 *
 *  checks if the floating point numbers are equal
 **/
#define IMG_F_EQUAL(a, b) (fabs(a-b) < 1e-4)

/** IMG_SWAP
 *  @a: input a
 *  @b: input b
 *
 *  Swaps the input values
 **/
#define IMG_SWAP(a, b) ({typeof(a) c; c=a; a=b; b=c;})

/** IMG_UNUSED
 *  @x: parameter to be supressed
 *
 *  Supress build warning for unused parameter
 **/
 #define IMG_UNUSED(x) (void)(x)

/**sigma_lut_in
  * Default sigma table for nornal lighting conditions
**/

/** IMG_CLEAR_BIT
 *  Macro to clear a bit at a given position
**/
#define IMG_CLEAR_BIT(mask, bit_pos) (mask &= ~(1 << bit_pos))

/** IMGLIB_ARRAY_SIZE:
 *    @a: array to be processed
 *
 * Returns number of elements in array
 **/
#define IMGLIB_ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

/** IMG_LINEAR_INTERPOLATE:
 *    @v1: start point
 *    @v2: end point
 *    @ratio: interpolation ratio
 *
 * interpolates b/w v1 and v2 based on ratio
 **/
#define IMG_LINEAR_INTERPOLATE(v1, v2, ratio) \
  ((v2) + ((ratio) * ((v1) - (v2))))

/** IMG_GET_INTERPOLATION_RATIO:
 *    @ct: current values
 *    @s: start
 *    @e: end
 *
 * Returns interpolation ratio
 **/
#define IMG_GET_INTERPOLATION_RATIO(ct, s, e) \
  (1.0 - ((ct) - (s))/((e) - (s)))

/** IMG_CONVERT_TO_Q:
 *    @v: start point
 *    @q: qfactor to be converted to
 *
 * convert the value to qfactor
 **/
#define IMG_CONVERT_TO_Q(v, q) \
  ((v) * (1 << (q)))

/** IMG_HYSTERESIS:
 *    @cur: current value
 *    @trig_a: point A trigger
 *    @trig_b: point B trigger
 *    @prev_st: previous state
 *    @a_st: state for region A
 *    @b_st: state for region B
 *
 *  Hysterisis for 3 region, 2 point (A, B)
 **/
#define IMG_HYSTERESIS(cur, trig_a, trig_b, prev_st, a_st, b_st) ({ \
  typeof(prev_st) _cur_st = prev_st; \
  if (cur < trig_a) \
    _cur_st = a_st; \
  else if (cur >= trig_b) \
    _cur_st = b_st; \
  _cur_st; \
})

/** IMG_RETURN_IF_NULL
 *   @p: pointer to be checked
 *
 *   Returns if pointer is null
 **/
#define IMG_RETURN_IF_NULL(ret, p) {if (!p) {\
  IDBG_ERROR("%s:%d Null pointer detected %s %p\n",\
    __func__, __LINE__, #p, p);\
  ret;\
}}

/** img_timer_granularity
 *  IMG_TIMER_MODE_MS: millisecond granularity
 *  IMG_TIMER_MODE_US: microsecond granularity
 *
 *  granularity of timer
 **/
typedef enum {
  IMG_TIMER_MODE_MS,
  IMG_TIMER_MODE_US,
} img_timer_granularity;

/** GET_TIME_IN_MICROS
 *   @time: struct timeval time
 *
 *   API to calculate the time in micro sec
 **/
#define GET_TIME_IN_MICROS(time) \
  ((1000000L * time.tv_sec) + time.tv_usec) \

/** GET_TIME_IN_MILLIS
 *   @time: struct timeval time
 *
 *   API to calculate the time in milli sec
 **/
#define GET_TIME_IN_MILLIS(time) \
  (((1000000L * time.tv_sec) + time.tv_usec) / 1000) \

/** IMG_TIMER_START
 *   @start: struct timeval start time
 *
 *   API to start timer
 **/
#define IMG_TIMER_START(start) ({ \
  if (g_imgloglevel > 3) { \
    gettimeofday(&start, NULL); \
  } \
})

/** IMG_TIMER_END
 *   @start: start time
 *   @end: used to store end time
 *   @str: string identifier to print in log
 *   @gran: granularity of time (ms or micros)
 *
 *   API to end timer and return delta
 **/
#define IMG_TIMER_END(start, end, str, gran) ({ \
  uint32_t delta = 0; \
  if (g_imgloglevel > 3) { \
    gettimeofday(&end, NULL); \
    switch (gran) { \
    case IMG_TIMER_MODE_US: \
      delta = GET_TIME_IN_MICROS(end) - GET_TIME_IN_MICROS(start); \
      IDBG_HIGH("%s:%d] %s time in micros: %d", __func__, __LINE__, \
        str, delta); \
      break; \
    case IMG_TIMER_MODE_MS: \
    default: \
      delta = GET_TIME_IN_MILLIS(end) - GET_TIME_IN_MILLIS(start); \
      IDBG_HIGH("%s:%d] %s time in ms: %d", __func__, __LINE__, str, delta); \
      break; \
    } \
  } \
  delta; \
})

extern float sigma_lut_in[RNR_LUT_SIZE];

/** img_plane_type_t
*    @PLANE_Y: Y plane
*    @PLANE_CB_CR: C plane for pseudo planar formats
*    @PLANE_CR_CB: C plane for interleaved CbCr components
*    @PLANE_CB: Cb plane for planar format
*    @PLANE_CR: Cr plane for planar format
*    @PLANE_Y_CB_Y_CR: Y Cb Y Cr interleaved format
*    @PLANE_Y_CR_Y_CB: Y Cr Y Cb interleaved format
*    @PLANE_CB_Y_CR_Y: Cb Y Cb Y interleaved format
*    @PLANE_CR_Y_CB_Y: Cr Y Cr Y interleaved format
*    @PLANE_ARGB: ARGB plane
*
*    Plane type. Sequence of the color components in each plane
**/
typedef enum {
  PLANE_Y,
  PLANE_CB_CR,
  PLANE_CR_CB,
  PLANE_CB,
  PLANE_CR,
  PLANE_Y_CB_Y_CR,
  PLANE_Y_CR_Y_CB,
  PLANE_CB_Y_CR_Y,
  PLANE_CR_Y_CB_Y,
  PLANE_ARGB,
} img_plane_type_t;

/** QIMG_SINGLE_PLN_INTLVD
 *   @p: pointer to the frame
 *
 *   Checks and returns true if the image format is interleaved
 *   YUV
 **/
#define QIMG_SINGLE_PLN_INTLVD(p) ({ \
  int8_t ret; \
  switch(QIMG_PL_TYPE(p, 0)) {\
    case PLANE_Y_CB_Y_CR: \
    case PLANE_Y_CR_Y_CB: \
    case PLANE_CB_Y_CR_Y: \
    case PLANE_CR_Y_CB_Y: \
      ret = TRUE; \
    break; \
    default: \
      ret = FALSE; \
  } \
  ret; \
})

/** img_subsampling_t
*    IMG_H2V2 - h2v2 subsampling (4:2:0)
*    IMG_H2V1 - h2v1 subsampling (4:2:2)
*    IMG_H1V2 - h1v2 subsampling (4:2:2)
*    IMG_H1V1 - h1v1 subsampling (4:4:4)
*
*    Image subsampling type
**/
typedef enum {
  IMG_H2V2,
  IMG_H2V1,
  IMG_H1V2,
  IMG_H1V1,
} img_subsampling_t;

/** img_frame_info_t
*    @width: width of the frame
*    @height: height of the frame
*    @ss: subsampling for the frame
*    @analysis: flag to indicate if this is a analysis frame
*    @client_id: id provided by the client
*    @num_planes: number of planes
*
*    Returns true if the status is success
**/
typedef struct {
  uint32_t width;
  uint32_t height;
  img_subsampling_t ss;
  int analysis;
  int client_id;
  int num_planes;
} img_frame_info_t;

/** img_plane_t
*    @plane_type: type of the plane
*    @addr: address of the plane
*    @stride: stride of the plane
*    @length: length of the plane
*    @fd: fd of the plane
*    @height: height of the plane
*    @width: width of the plane
*    @offset: offset of the valid data within the plane
*    @scanline: scanline of the plane
*
*    Represents each plane of the frame
**/
typedef struct {
  img_plane_type_t plane_type;
  uint8_t *addr;
  uint32_t stride;
  size_t length;
  int32_t fd;
  uint32_t height;
  uint32_t width;
  uint32_t offset;
  uint32_t scanline;
} img_plane_t;

/** img_sub_frame_t
*    @plane_cnt: number of planes
*    @plane: array of planes
*
*    Represents each image sub frame.
**/
typedef struct {
  uint32_t plane_cnt;
  img_plane_t plane[MAX_PLANE_CNT];
} img_sub_frame_t;

/** img_frame_t
*    @timestamp: timestamp of the frame
*    @plane: array of planes
*    @frame_cnt: frame count, 1 for 2D, 2 for 3D
*    @idx: unique ID of the frame
*    @info: frame information
*    @private_data: private data associated with the client
*    @ref_count: ref count of the buffer
*
*    Represents a frame (2D or 3D frame). 2D contains only one
*    sub frame where as 3D has 2 sub frames (left/right or
*    top/bottom)
**/
typedef struct {
  uint64_t timestamp;
  img_sub_frame_t frame[MAX_FRAME_CNT];
  int frame_cnt;
  uint32_t idx;
  uint32_t frame_id;
  img_frame_info_t info;
  void *private_data;
  int ref_count;
} img_frame_t;

/** img_size_t
*    @width: width of the image
*    @height: height of the image
*
*    Represents the image size
**/
typedef struct {
  int width;
  int height;
} img_size_t;

/** img_trans_info_t
 *   @h_scale: horizontal scale ratio to be applied on the
 *           result
 *   @v_scale: vertical scale ratio to be applied on the result.
*    @h_offset: horizontal offset
*    @v_offset: vertical offset
*
*    Translation information for the face cordinates
**/
typedef struct {
  float h_scale;
  float v_scale;
  int32_t h_offset;
  int32_t v_offset;
} img_trans_info_t;

/** img_pixel_t
*    @x: x cordinate of the pixel
*    @y: y cordinate of the pixel
*
*    Represents the image pixel
**/
typedef struct {
  int x;
  int y;
} img_pixel_t;

/** img_rect_t
*    @pos: position of the region
*    @size: size of the region
*
*    Represents the image region
**/
typedef struct {
  img_pixel_t pos;
  img_size_t size;
} img_rect_t;

/** img_3A_data_t
*    @lux_idx: Lux index
*    @gain: Gain value
*    @s_rnr_enabled: if skin rnr should be enabled
*
*    Common 3a data required by imaging modules
**/

typedef struct {
  float lux_idx;
  float gain;
  float prev_lux_value;
  float prev_gain_value;
  uint32_t s_rnr_enabled;
} img_3A_data_t;

/** img_dim_t
*    @width: Width of the imge
*    @height: height of the image
*    @scanline: scanline of the image
*    @stride: strie of the image
*
*    Image dimensions
**/
typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t scanline;
  uint32_t stride;
} img_dim_t;

/** img_gamma_t
*    @table: array of gamma values
*
*    Gamma table of size 64
**/
typedef struct {
  uint16_t table[GAMMA_TABLE_ENTRIES];
} img_gamma_t;

/** img_debug_info_t
*    @camera_dump_enabled: Flag indicating if dump
*        is enabled
*    @timestamp: Timestamp string when buffer was recieved
*    @meta_data: Debug data to be filled in
*
*   Debug Information
**/
typedef struct {
  uint8_t camera_dump_enabled;
  char timestamp[25];
  void *meta_data;
} img_debug_info_t;

/** img_comp_mode_t
 * IMG_SYNC_MODE: The component will be executed in
 *   syncronous mode - per frame.
 *  IMG_ASYNC_MODE: The component will spawn a thread and will
 *  be executed asyncronously in the context of the component
 *  thread.
 *
 **/
typedef enum {
  IMG_SYNC_MODE,
  IMG_ASYNC_MODE,
} img_comp_mode_t;

/** img_caps_t
 *   @num_input: number of input buffers
 *   @num_output: number of output buffers
 *   @num_meta: number of meta buffers
 *   @inplace_algo: Flag to indicate whether the algorithm is
 *     inplace. If not output buffers needs to be obtained
 *   @face_detect_tilt_cut_off: maximum angle for face tilt filter
 *   @num_release_buf: the number of total bufs released to HAL
 *   @ack_required: Flag to indicate whether the ack is required
 *   @share_client_per_session: Flag to indicate whether client is
 *     shared in session
 *   @num_overlap: number of overlap buffers needed for batch
 *     processing, has to be less than num_input
 *   @use_internal_bufs: flag to indicate if internal bufs
 *     should be used
 *   @preload_per_session: Flag to indicate whether preload
 *                       needs to be done per session
 *
 *   Capabilities
 **/
typedef struct {
  int8_t num_input;
  int8_t num_output;
  int8_t num_meta;
  int8_t inplace_algo;
  uint32_t face_detect_tilt_cut_off;
  int8_t num_release_buf;
  int8_t ack_required;
  int8_t share_client_per_session;
  int8_t num_overlap;
  int8_t use_internal_bufs;
  bool preload_per_session;
} img_caps_t;

/** img_init_params_t
 *    @refocus: enable refocus encoding
 *    @client_id: client id of the thread manager
 *
 *    Frameproc init params
 **/
typedef struct {
  int refocus_encode;
  uint32_t client_id;
} img_init_params_t;

/** img_frame_ops_t
 *    @get_frame: The function pointer to get the frame
 *    @release_frame: The function pointer to release the frame
 *    @dump_frame: The function pointer to dump frame
 *    @get_meta: The function pointer to get meta
 *    @set_meta: The function pointer to set meta
 *    @image_copy: The function pointer to image copy
 *    @image_scale: image downscaling
 *    @p_appdata: app data
 *
 *    Frame operations for intermediate buffer
 **/
typedef struct {
  int (*get_frame)(void *p_appdata, img_frame_t **pp_frame);
  int (*release_frame)(void *p_appdata, img_frame_t *p_frame,
    int is_dirty);
  void (*dump_frame)(img_frame_t *img_frame, const char* file_name,
    uint32_t number, void *p_meta);
  void *(*get_meta)(void *p_meta, uint32_t type);
  int32_t (*set_meta)(void *p_meta, uint32_t type, void* val);
  int (*image_copy)(img_frame_t *out_buff, img_frame_t *in_buff);
  int (*image_scale)(void *p_src, uint32_t src_width, uint32_t src_height,
    uint32_t src_stride, void *p_dst, uint32_t dst_stride);
  void *p_appdata;
} img_frame_ops_t;

/** img_base_ops_t
 *    @mem_ops: memory operations
 *    @thread_ops: thread operations
 *    @max_w: maximum width.
 *    @max_h: maximum height.
 *
 *    Structure to hold memory/thread ops table and preload
 *    parameters
 **/
typedef struct {
  img_mem_ops_t mem_ops;
  img_thread_ops_t thread_ops;
  uint32_t max_w;
  uint32_t max_h;
} img_base_ops_t;

/** face_proc_scale_mn_v_info_t
*    @height: The possiblly cropped input height in whole in
*           pixels (N)
*    @output_height: The required output height in whole in
*                  pixels (M)
*    @step: The vertical accumulated step for a plane
*    @count: The vertical accumulated count for a plane
*    @index: The vertical index of line being accumulated
*    @p_v_accum_line: The intermediate vertical accumulated line
*                   for a plane
*
*    Used for downscaling image
*
**/
typedef struct {
  uint32_t height;
  uint32_t output_height;
  uint32_t step;
  uint32_t count;
  uint16_t *p_v_accum_line;
} img_scale_mn_v_info_t;

// M/N division table in Q10
static const uint16_t mn_division_table[] =
{
  1024,     // not used
  1024,     // 1/1
  512,     // 1/2
  341,     // 1/3
  256,     // 1/4
  205,     // 1/5
  171,     // 1/6
  146,     // 1/7
  128      // 1/8
};

/** img_mmap_info_ion
*    @ion_fd: ION file instance
*    @virtual_addr: virtual address of the buffer
*    @bufsize: size of the buffer
*    @ion_info_fd: File instance for current buffer
*
*    Used for maping data
*
**/
typedef struct img_mmap_info_ion
{
    int               ion_fd;
    unsigned char    *virtual_addr;
    unsigned int      bufsize;
    struct ion_fd_data ion_info_fd;
} img_mmap_info_ion;

/** img_cache_ops_type
*   @CACHE_NO_OP: No operation
*   @CACHE_INVALIDATE: invalidation
*   @CACHE_CLEAN: cache clean
*   @CACHE_CLEAN_INVALIDATE: cache clean invalidation
*
*    Different cache operations
*
**/
typedef enum {
  CACHE_NO_OP,
  CACHE_INVALIDATE,
  CACHE_CLEAN,
  CACHE_CLEAN_INVALIDATE,
} img_cache_ops_type;

/** img_ops_core_type
*
*    Different operations based on core
*
**/
typedef enum {
  IMG_OPS_C,
  IMG_OPS_NEON,
  IMG_OPS_NEON_ASM,
  IMG_OPS_DSP,
  IMG_OPS_GPU,
  IMG_OPS_FCV,
} img_ops_core_type;

/** img_perf_t
*    @create: create handle
*    @destroy: destroy handle
*    @lock_start: start lock
*    @lock_end: end lock
*
*    Img lib perf handle
*
**/
typedef struct
{
  void* (*handle_create)();
  void (*handle_destroy)(void* p_perf);
  void* (*lock_start)(void* p_perf, int32_t* p_perf_lock_params,
    size_t perf_lock_params_size, int32_t duration);
  void (*lock_end)(void* p_perf, void* p_perf_lock);
} img_perf_t;

/** img_get_subsampling_factor
*    @ss_type: subsampling type
*    @p_w_factor: pointer to the width subsampling factor
*    @p_h_factor: pointer to height subsampling factor
*
*    Get the width and height subsampling factors given the type
**/
int img_get_subsampling_factor(img_subsampling_t ss_type, float *p_w_factor,
  float *p_h_factor);

/** img_wait_for_completion
*    @p_cond: pointer to pthread condition
*    @p_mutex: pointer to pthread mutex
*    @ms: timeout value in milliseconds
*
*    This function waits until one of the condition is met
*    1. conditional variable is signalled
*    2. timeout happens
**/
int img_wait_for_completion(pthread_cond_t *p_cond, pthread_mutex_t *p_mutex,
  int32_t ms);

/** img_image_copy:
 *  @out_buff: output buffer handler
 *  @in_buff: input buffer handler
 *
 * Function to copy image data from source to destination buffer
 *
 * Returns IMG_SUCCESS in case of success
 **/
int img_image_copy(img_frame_t *out_buff, img_frame_t *in_buff);

/**
 * Function: img_translate_cordinates
 *
 * Description: Translate the cordinates from one window
 *             dimension to another
 *
 * Input parameters:
 *   dim1 - dimension of 1st window
 *   dim2 - dimension of 2nd window
 *   p_in_region - pointer to the input region
 *   p_out_region - pointer to the output region
 *   zoom_factor - zoom factor
 *   p_zoom_tbl - zoom table
 *   num_entries - number of zoom table entries
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
int img_translate_cordinates_zoom(img_size_t dim1, img_size_t dim2,
  img_rect_t *p_in_region, img_rect_t *p_out_region,
  double zoom_factor, const uint32_t *p_zoom_tbl,
  uint32_t num_entries);

/**
 * Function: img_translate_cordinates
 *
 * Description: Translate the region from one window
 *             dimension to another
 *
 * Input parameters:
 *   dim1 - dimension of 1st window
 *   dim2 - dimension of 2nd window
 *   p_in_region - pointer to the input region
 *   p_out_region - pointer to the output region
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_INVALID_INPUT
 *
 * Notes:  none
 **/
int img_translate_cordinates(img_size_t dim1, img_size_t dim2,
  img_rect_t *p_in_region, img_rect_t *p_out_region);

/**
 * Function: img_sw_scale_init_mn
 *
 * Description: init downscaling
 *
 * Input parameters:
 *   vInfo - contains width/height info for scaling
 *   pSrc - pointer to original img buffer
 *   srcWidth - original image width
 *   srcHeight - original image height
 *   srcStride - original image stride
 *   pDst - pointer to scaled image buffer
 *   dstWidth - desired width of schaled image
 *   dstHeight - desired height of scaled image
 *   dstStride - scaled image stride
 *
 * Return values: none
 *
 * Notes:  none
 **/
void img_sw_scale_init_mn(img_scale_mn_v_info_t*  vInfo,
  uint8_t  *pSrc,
  uint32_t  srcWidth,
  uint32_t  srcHeight,
  uint32_t  srcStride,
  uint8_t  *pDst,
  uint32_t  dstWidth,
  uint32_t  dstHeight,
  uint32_t  dstStride);

/**
 * Function: img_sw_scale_mn_vscale_byte
 *
 * Description: init Vertical M/N scaling on an input lines,
 * which is one byte per pixel
 *
 * Input parameters:
 *   p_v_info - contains width/height info for scaling
 *   p_output_line
 *   output_width
 *   p_input_line
 *
 * Return values:
 *   0 - accumulating
 *   1 - outputting 1 line
 *
 * Notes:  none
 **/
int img_sw_scale_mn_vscale_byte(img_scale_mn_v_info_t *p_v_info,
  uint8_t *p_output_line,
  uint32_t output_width,
  uint8_t *p_input_line);

/**
 * Function: img_sw_scale_mn_hscale_byte
 *
 * Description: init horizontal scaling
 *
 * Input parameters:
 *   p_output_line
 *   output_width - M value
 *   p_input_line
 *   input_width - N value
 *
 * Return values: None
 *
 * Notes:  none
 **/
void img_sw_scale_mn_hscale_byte (uint8_t *p_output_line,
  uint32_t                          output_width,
  uint8_t                          *p_input_line,
  uint32_t                          input_width     );

/**
 * Function: scalingInitMN
 *
 * Description: Image downscaling using MN method
 *
 * Input parameters:
 *   pSrc - pointer to original img buffer
 *   srcWidth - original image width
 *   srcHeight - original image height
 *   srcStride - original image stride
 *   pDst - pointer to scaled image buffer
 *   dstWidth - desired width of scaled image
 *   dstHeight - desired height of scaled image
 *   dstStride - desired stride of scaled image
 *
 * Return values: none
 *
 * Notes:  none
 **/
void img_sw_downscale(uint8_t *src,uint32_t srcWidth,uint32_t srcHeight,
  uint32_t srcStride, uint8_t *dst, uint32_t dstWidth, uint32_t dstHeight,
  uint32_t dstStride);

/**
 * Function: img_sw_downscale_2by2
 *
 * Description: Optimized version of downscale 2by2.
 *
 * Input parameters:
 *   p_src - Pointer to source buffer.
 *   src_width - Source buffer width.
 *   src_height - Source buffer height.
 *   src_stride - original image stride.
 *   p_dst - Pointer to scaled destination buffer.
 *   dst_stride - Destination stride.
 *
 * Return values: imaging errors
 **/
int32_t img_sw_downscale_2by2(void *p_src, uint32_t src_width, uint32_t src_height,
  uint32_t src_stride, void *p_dst, uint32_t dst_stride);

/** img_image_stride_fill:
 *  @out_buff: output buffer handler
 *
 * Function to fill image stride with image data
 *
 * Returns IMG_SUCCESS in case of success
 **/
int img_image_stride_fill(img_frame_t *out_buff);

/** img_alloc_ion:
 *  @mapion_list: Ion structure list to memory blocks to be allocated
 *  @num: number of buffers to be allocated
 *  @ionheapid: ION heap ID
 *  @cached:
 *    TRUE: mappings of this buffer should be cached, ion will do cache
            maintenance when the buffer is mapped for dma
 *    FALSE: mappings of this buffer should not be cached
 *
 * Function to allocate a physically contiguous memory
 *
 * Returns IMG_SUCCESS in case of success
 **/
int img_alloc_ion(img_mmap_info_ion *mapion_list, int num, uint32_t ionheapid,
  int cached);

/** img_free_ion:
 *  @mapion_list: Ion structure list to the allocated memory blocks
 *  @num: number of buffers to be freed
 *
 * Free ion memory
 *
 *
 * Returns IMG_SUCCESS in case of success
 **/
int img_free_ion(img_mmap_info_ion* mapion_list, int num);

int img_cache_ops_external (void *p_buffer, size_t size, uint32_t offset, int fd,
  img_cache_ops_type type, int ion_device_fd);

/** img_get_timestamp
 *  @timestamp: pointer to a char buffer. The buffer should be
 *    allocated by the caller
 *  @size: size of the char buffer
 *
 *  Get the current timestamp and convert it to a string
 *
 *  Return: None.
 **/
void img_get_timestamp(char *timestamp, uint32_t size);

/** img_dump_frame
 *    @img_frame: frame handler
 *    @file_name: file name prefix
 *    @number: number to be appended at the end of the file name
 *    @p_meta: metadata handler
 *
 * Saves specified frame to folder /data/misc/camera/
 *
 * Returns None.
 **/
void img_dump_frame(img_frame_t *img_frame, const char* file_name,
  uint32_t number, void *p_meta);

/** img_perf_lock_handle_create
 *
 * Creates new performance handle
 *
 * Returns new performance handle
 **/
void* img_perf_handle_create();

/** img_perf_handle_destroy
 *    @p_perf: performance handle
 *
 * Destoyes performance handle
 *
 * Returns None.
 **/
void img_perf_handle_destroy(void* p_perf);

/** img_perf_lock_start
 *    @p_perf: performance handle
 *    @p_perf_lock_params: performance lock parameters
 *    @perf_lock_params_size: size of performance lock parameters
 *    @duration: duration
 *
 * Locks performance with specified parameters
 *
 * Returns new performance lock handle
 **/
void* img_perf_lock_start(void* p_perf, int32_t* p_perf_lock_params,
  size_t perf_lock_params_size, int32_t duration);

/** img_perf_lock_end
 *    @p_perf: performance handle
 *    @p_perf_lock: performance lock handle
 *
 * Locks performance with specified parameters
 *
 * Returns None.
 **/
void img_perf_lock_end(void* p_perf, void* p_perf_lock);

/**
 * parse callback function
 */
typedef void (*img_parse_cb_t) (void *, char *key, char *value);

/** img_parse_main
 *    @datafile: file to be parsed
 *    @p_userdata: userdata provided by the client
 *    @p_parse_cb: parse function provided by the client
 *
 *   Main function for parsing
 *
 * Returns imglib error values.
 **/
int img_parse_main(const char* datafile, void *p_userdata,
  img_parse_cb_t p_parse_cb);

/**
 * Function: img_plane_deinterleave.
 *
 * Description: Deinterleave single plane YUV format to
 *         semi-planar.
 *
 * Arguments:
 *   @p_src_buff - Pointer to src buffer.
 *   @type: format of the src buffer
 *   @p_frame - Pointer to face component frame where converted
 *     frame will be stored.
 *
 * Return values:
 *   IMG error codes.
 *
 * Notes: conversion to planar formats is not supported
 **/
int img_plane_deinterleave(uint8_t *p_src, img_plane_type_t type,
  img_frame_t *p_frame);

/**
 * Function: img_boost_linear_k
 *
 * Description: API to boost luma
 *
 * Arguments:
 *   @p_src_buff - Pointer to src buffer.
 *   @width: frame width
 *   @height: frame height
 *   @stride: frame stride
 *   @K: boost factor
 *   @use_asm: indicates whether assembly of C routine needs to
 *           be used
 *
 * Return values:
 *   None
 *
 **/
void img_boost_linear_k(uint8_t *p_src, uint32_t width, uint32_t height,
  int32_t stride, float K, int8_t use_asm);

/**
 * Function: img_sw_cds
 *
 * Description: Software CDS routine in neon intrinsics
 *
 * Input parameters:
 *   @p_src - Pointer to source buffer.
 *   @src_width - Source buffer width.
 *   @src_height - Source buffer height.
 *   @src_stride - original image stride.
 *   @p_dst - Pointer to scaled destination buffer.
 *   @dst_stride - Destination stride.
 *   @type: operation type
 *
 * Return values: none
 **/
int32_t img_sw_cds(uint8_t *p_src, uint32_t src_width,
  uint32_t src_height,
  uint32_t src_stride,
  uint8_t *p_dst,
  uint32_t dst_stride,
  img_ops_core_type type);

/**
 * Function: img_common_get_prop
 *
 * Description: This function returns property value in 32-bit
 * integer
 *
 * Arguments:
 *   @prop_name: name of the property
 *   @def_val: default value of the property
 *
 * Return values:
 *    value of the property in 32-bit integer
 *
 * Notes: API will return 0 in case of error
 **/
int32_t img_common_get_prop(const char* prop_name,
  const char* def_val);

#endif //__IMG_COMMON_H__
