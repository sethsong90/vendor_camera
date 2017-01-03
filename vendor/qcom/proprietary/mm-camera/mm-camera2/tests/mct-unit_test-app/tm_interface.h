/*============================================================================

  Copyright (c) 2015 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.

============================================================================*/

#ifndef TM_INTERFACE_H
#define TM_INTERFACE_H

#include <poll.h>
#include "cam_intf.h"
#include "mct_module.h"
#include "modules.h"

#define MAX_FN_NAME_LEN       128
#define MAX_BATCH_INDEXES     10
#define MAX_FILENAME_LEN      256
#define MAX_FILENAMES         160
#define TM_PARAMS_CNT_MAX     128
#define TM_PARAMS_GROUPS_MAX  32
#define TM_MAX_NUM_STREAMS    20
#define MODULES_ARRAY_ENTRIES 10
#define MAX_FILENAMES         160

#define READ_FD   0
#define WRITE_FD  1

#define TM_LOG_SILENT   0
#define TM_LOG_NORMAL   1
#define TM_LOG_DEBUG    2

#define TM_DBGLEVEL TM_LOG_DEBUG

#if (TM_DBGLEVEL == TM_LOG_SILENT)
  #define TMDBG(fmt, args...)       do {} while (0)
  #define TMDBG_NEOL(fmt, args...)  do {} while (0)
  #define TMDBG_HIGH(fmt, args...)  do {} while (0)

#elif (TM_DBGLEVEL == TM_LOG_NORMAL)
  #define TMDBG(fmt, args...)       do {} while (0)
  #define TMDBG_NEOL(fmt, args...)  do {} while (0)
  #define TMDBG_HIGH(fmt, args...)  printf(fmt"\n", ##args)
#else
  #define TMDBG(fmt, args...)       printf(fmt"\n", ##args)
  #define TMDBG_NEOL(fmt, args...)  printf(fmt, ##args)
  #define TMDBG_HIGH(fmt, args...)  printf(fmt"\n", ##args)
#endif

#define TMDBG_ERROR(fmt, args...) printf("\n%s:%d: Error: " fmt"\n\n", \
                                    __func__, __LINE__, ##args)

#define TMPRINT(fmt, args...)     printf(fmt"\n", ##args)


#define TMASSERT(cond, fatal, ...)  if (cond) { \
                                      TMDBG_ERROR(__VA_ARGS__); \
                                      if (fatal) {      \
                                        return FALSE;   \
                                      }                 \
                                    }

#define TMRETURN(cond, ...)         if (cond) { \
                                      TMDBG_ERROR(__VA_ARGS__); \
                                      return;         \
                                    }

#define TMEXIT(cond, fatal, label, ...)   if (cond) { \
                                            TMDBG_ERROR(__VA_ARGS__); \
                                            if (fatal) {      \
                                              goto label;     \
                                            }                 \
                                          }

#define TM_UNUSED(data) data = data;

namespace tm_interface {

class TM_Interface;
class IEvent;

typedef struct {
  struct v4l2_buffer buffer;
  unsigned long addr[VIDEO_MAX_PLANES];
  uint32_t size;
  struct ion_allocation_data ion_alloc[VIDEO_MAX_PLANES];
  struct ion_fd_data fd_data[VIDEO_MAX_PLANES];
} v4l2_frame_buffer_t;

typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t crop_x;
  uint32_t crop_y;
  uint32_t start_x;
  uint32_t start_y;

} tm_out_dims_t;

typedef struct {
  void            *vaddr;
  uint32_t        index;
  uint32_t        size;
  uint32_t        frame_id;
  struct timeval  timestamp;
  uint32_t        identity;
  tm_out_dims_t   dims;
} tm_intf_buffer_desc_t;

typedef enum {
  TM_DENOISE_2D,
  TM_DENOISE_TNR
} tm_denoise_e;

typedef enum {
  TM_CHECK_ITERATION,
  TM_CHECK_VALUE,
  TM_CHECK_KEY

}tm_intf_term_cond_type_t;

typedef enum {
  VALIDATE_METADATA,
  VALIDATE_OUTPUT_FRAME
}tm_intf_validation_type_t;

typedef enum {
  TM_EXECUTE_ALL,
  TM_EXECUTE_BY_NAME,
  TM_EXECUTE_BY_INDEX,
  TM_EXECUTE_BATCH,
  TM_EXECUTE_MAX

} tm_intf_execute_method_t;

typedef struct {
  uint32_t num_indexes;
  int      indexes[MAX_BATCH_INDEXES];

} tm_intf_execute_batch_t;

typedef struct {
  tm_intf_execute_method_t  method;
  uint8_t                  index;
  char                      name[MAX_FN_NAME_LEN];
  tm_intf_execute_batch_t   batch;
} tm_intf_execute_params_t;

typedef struct {
  mct_port_caps_t       caps;
  void                  *priv_data;
  mct_port_event_func   event_func;

} tm_uc_port_caps_t;

typedef struct {
  TM_Interface    *tmi;
  mct_pipeline_t  *pipeline;

} tm_bus_thread_data_t;

typedef struct {
  uint32_t     input_width;
  uint32_t     input_height;
  uint32_t     output_width[TM_MAX_NUM_STREAMS];
  uint32_t     output_height[TM_MAX_NUM_STREAMS];
  uint32_t     ref_img_width;
  uint32_t     ref_img_height;
  uint32_t     win_first_pixel;
  uint32_t     win_first_line;
  uint32_t     win_width;
  uint32_t     win_height;
  uint16_t     rotation;
  uint16_t     flip;
  double       h_scale_ratio;
  double       v_scale_ratio;
} tm_sizes_t;

typedef struct {
//  tm_sizes_t   sizes;
  cam_denoise_param_t         denoise_params;
  tm_denoise_e                denoise_type;
  uint32_t                    sharpness_ratio;
  boolean                     use_crop;
} tm_cpp_input_t;

typedef struct {
  int32_t         zoom;
  int32_t         saturation;
  int32_t         sce;
  int32_t         contrast;
  int32_t         tintless;
} tm_isp_input_t;

typedef struct {
  int32_t         hfr;
  cam_fps_range_t fps_range;
} tm_sensor_input_t;


typedef struct {
  cam_fd_set_parm_t fd_params;
} tm_imglib_input_t;

typedef struct {
  float        lux_idx;
  float        real_gain;
  float        exp_time;
  uint32_t     line_count;

} tm_stats_input_t;

typedef struct {
  cam_intf_parm_type_t val;
  uint8_t payload[64];
} tm_params_t;

typedef struct {
  tm_params_t tbl[TM_PARAMS_CNT_MAX];
  uint32_t cnt;
} tm_params_type_t;

typedef struct {
  char *type;
  tm_params_type_t  params_group[TM_PARAMS_GROUPS_MAX];
  uint32_t groups;
} tm_params_type_lut_t;

typedef struct {
  tm_sizes_t                sizes;
  tm_intf_execute_params_t  execute_params;
  boolean                   read_input_from_file;
  boolean                   read_input_y_only;
  char                      input_filename[MAX_FILENAMES][MAX_FILENAME_LEN];
  uint32_t                  num_in_images;
  char                      input_format[32];
  boolean                   use_out_buffer;
  char                      outfile_path[MAX_FILENAME_LEN];
  boolean                   have_ref_image;
  char                      ref_imgname[MAX_FILENAME_LEN];
  char                      chromatix_file[MAX_FILENAME_LEN];
  char                      lib_name[MAX_FILENAME_LEN];
  boolean                   print_usecases;
  uint32_t                  frame_count;
  uint32_t                  frames_skip;
  int32_t                   hal_version;
  uint32_t                  session_id;
  cam_stream_type_t         stream_types[TM_MAX_NUM_STREAMS];
  uint32_t                  num_streams;
  cam_streaming_mode_t      streaming_mode[TM_MAX_NUM_STREAMS];
  int                       num_burst[TM_MAX_NUM_STREAMS];
  boolean                   is_first_module_src;
  cam_effect_mode_type      effect;
  char                      out_format[TM_MAX_NUM_STREAMS][32];
  char                      modules[MODULES_ARRAY_ENTRIES][32];
  char                      uc_name[MAX_FILENAME_LEN];
  tm_cpp_input_t            cpp;
  tm_stats_input_t          stats;
  tm_params_type_lut_t      params_lut;
  boolean                   save_file;
} tm_intf_input_t;

typedef struct {
  char                    chromatix_file[MAX_FILENAME_LEN];
  char                    chromatix_com_file[MAX_FILENAME_LEN];
  char                    chromatix_cpp_file[MAX_FILENAME_LEN];
  char                    chromatix_3a_file[MAX_FILENAME_LEN];
  void                  *chromatixPrv_header;
  void                  *chromatixCom_header;
  void                  *chromatixCpp_header;
  void                  *chromatix3a_header;
  void                  *chromatixPtr;
  void                  *chromatixComPtr;
  void                  *chromatixCppPtr;
  void                  *chromatix3aPtr;
} tm_intf_chromatix_data_t;


typedef struct {
  mct_module_t            *prev_module;
  mct_port_t              *prev_port;
  TM_Interface            *tmi;
  mct_stream_t            *stream;
  boolean                 status;
} tm_intf_link_data_t;

typedef struct {
  uint32_t                  id;
  uint32_t                  identity;
  cam_stream_type_t         type;

} stream_ids_t;

typedef struct {
  stream_ids_t  ids[TM_MAX_NUM_STREAMS];
  uint32_t      num;
} streams_t;

typedef struct {
  uint8_t *p1;
  uint8_t *p2;
  uint8_t *p3;

} tm_buf_planes_t;

typedef struct {
  v4l2_frame_buffer_t       v4l_buf;
  mct_stream_map_buf_t      *map_buf;
} tm_img_buf_desc_t;

/* Interface API declarations */

class TM_Interface {

public:

  TM_Interface();

  ~TM_Interface();

  cam_format_t getFormat(char *in_format);

  uint32_t getNumPlanes(char *in_format);

  float getPixelSize(cam_format_t format);

  tm_intf_input_t getInput();

  boolean putInput(tm_intf_input_t *in_input);

  mct_port_t *createPort(const char *name, mct_port_direction_t direction,
    tm_uc_port_caps_t *port_data);

  boolean createInputPort(tm_uc_port_caps_t *port_data, uint32_t num_ports);

  boolean createOutputPort(tm_uc_port_caps_t *port_data, uint32_t num_ports);

  boolean createStatsPort(tm_uc_port_caps_t *port_data, uint32_t num_ports);

  void destroyInputPort(void);

  void destroyOutputPort(void);

  void destroyStatsPort(void);

  boolean streamOn(cam_stream_type_t stream_type);

  boolean streamOff(cam_stream_type_t stream_type);

  boolean doReprocess(uint32_t frame_idx, cam_stream_type_t stream_type, tm_img_buf_desc_t *p_in_buf_desc);

  void sendCtrlSof(mct_bus_msg_isp_sof_t *isp_sof_bus_msg);

  void prepSendCtrlSof(uint32_t identity, uint32_t frame_id);

  boolean sendIspOutDims(cam_stream_type_t stream_type);

  boolean sendStreamCropDims(cam_stream_type_t stream_type);

  boolean sendBufDivert(cam_stream_type_t stream_type, uint32_t cnt,
    uint32_t *ack_flag, tm_img_buf_desc_t *p_in_buf_desc);

  boolean triggerRestart(void);

  boolean sendSuperParamsXmlStyle(uint32_t parm_iter, uint32_t frame_id);

  boolean sendPortModuleEvent(mct_port_t *port, mct_event_module_type_t event_type,
    mct_event_direction direction, void *event_data, uint32_t identity);

  boolean sendModuleEvent(mct_module_t *module,
    mct_event_module_type_t event_type, mct_event_direction direction,
      void *event_data, uint32_t identity);

  boolean sendModuleEventDown(mct_event_module_type_t event_type,
    void *event_data, uint32_t identity);

  boolean sendModuleEventUp(mct_port_t *port, mct_event_module_type_t event_type,
    void *event_data, uint32_t identity);

  boolean send_control_event(mct_event_control_type_t event_type,
    mct_event_direction direction, void *event_data, uint32_t identity);

  boolean sendControlEventDown(mct_event_control_type_t event_type,
  void *event_data, uint32_t identity);

  boolean sendControlEventUp(mct_event_control_type_t event_type,
  void *event_data, uint32_t identity);

  void saveBusHandle(mct_bus_t *bus);

  boolean postBusMsg(mct_bus_msg_t *bus_msg);

  boolean saveBusMsg(mct_bus_msg_t *bus_msg,
    mct_bus_msg_t **out_bus_msg);

  boolean processMetadata(mct_bus_msg_type_t type,
    mct_bus_msg_t *ret_bus_msg);

  static void* busHandlerThread(void *data);

  boolean compareBuffers(void* buf1, uint32_t size1,
    void* buf2, uint32_t size2);

  boolean getOutBufDesc(tm_intf_buffer_desc_t *buf_desc, uint32_t identity);

  boolean getInBufDesc(tm_intf_buffer_desc_t *buf_desc, uint32_t identity,
    tm_img_buf_desc_t *p_in_buf_desc);

  boolean validateResults(tm_intf_validation_type_t v_type, void* data);

  boolean prepareSensorCaps(tm_uc_port_caps_t *caps);

  boolean prepareFrameCaps(tm_uc_port_caps_t *caps);

  boolean prepareStatsCaps(tm_uc_port_caps_t *caps);

  boolean checkForTermination(tm_intf_term_cond_type_t cond_type, void* data, cam_stream_type_t stream_type);

  boolean checkKeyPress();

  boolean setOutputBuffer(cam_stream_type_t stream_type);

  void bufReceiveInit(void);

  void bufReceiveDeinit(void);

  boolean bufReceiveMsg(tm_intf_buffer_desc_t *buf_desc);

  void notifyBufAckEvent(tm_intf_buffer_desc_t *buf_desc);

  boolean sendBufDivertAck(tm_intf_buffer_desc_t *buf_desc);

  void printBuf(tm_intf_buffer_desc_t *buf_desc);

  boolean saveOutFile(char *out_fname, void *addr, size_t len);

  void publishResults(const char *test_case_name, int frames_cnt, tm_intf_buffer_desc_t *buf_desc);

  void setModulesList(mct_list_t *modules_list);

  boolean calcBufSize(tm_intf_buffer_desc_t *buf_desc);

  boolean calcPlanesOffset(tm_intf_buffer_desc_t *buf_desc, tm_interface::tm_buf_planes_t *buf_palnes);

  void getStreamIspOutDims(tm_out_dims_t *out_dims, uint32_t identity);

  boolean getStreamOutDims(tm_out_dims_t *out_dims, uint32_t identity);

  void saveStreamIspOutDims(cam_dimension_t *dim, uint32_t identity);

void saveStreamIspOutCrop(mct_bus_msg_stream_crop_t *crop, uint32_t identity);

  mct_module_t *getModule(const char *name);

  mct_module_t *getFirstModule(void);

  mct_module_t *getListFirstModule(mct_list_t *lst);

  mct_port_t *getSinkPort(mct_module_t *module);

  mct_port_t *getSrcPort(mct_module_t *module);

  mct_port_t *getUnlinkedSrcPortType(mct_module_t *module,
    mct_port_caps_type_t type);

  mct_port_t *getUnlinkedSinkPortType(mct_module_t *module,
    mct_port_caps_type_t type);

  mct_port_t *reserveSinkPort(mct_port_t *src_port,
    mct_port_t *sink_port, mct_stream_info_t *stream_info);

  mct_port_t *reserveSrcPort(mct_port_t *src_port, mct_port_t *sink_port,
  mct_stream_info_t *stream_info);

  boolean configureStream(uint32_t feature_mask);

  boolean sendParam(cam_intf_parm_type_t type, void *data);

  boolean sendChromatixPointers(uint32_t identity);

  void fillMetadataChromatixPtrs(mct_bus_msg_sensor_metadata_t *sen_meta);

  boolean loadChromatixLibraries();

  void closeChromatixLibraries();

  void fillSensorInfo(sensor_out_info_t *sen_out, sensor_set_dim_t *sen_dims);


  void fillSensorCap(sensor_src_port_cap_t *sensor_cap,
    uint32_t session_id);

  void statsGetData(mct_event_t *event);

  boolean sendStatsUpdate(mct_port_t *port, uint32_t sof_id);

  boolean sendAWBStatsUpdate(void);

  void printBusMsg(mct_bus_msg_t *bus_msg);

  boolean parseMetadata(mct_bus_msg_t *bus_msg, tm_intf_buffer_desc_t *buf);

  void statsSetSuperparam(mct_event_t *event);

  boolean unlinkTwoPorts(mct_port_t *src_port,
    mct_port_t *sink_port, uint32_t identity);

  boolean unlinkTwoModules(mct_module_t *mod1, mct_module_t *mod2,
    mct_stream_info_t *stream_info);

  boolean linkTwoModules(mct_module_t *mod1, mct_module_t *mod2,
    mct_stream_info_t *stream_info);

  boolean linkTwoPorts(mct_port_t *src_port, mct_port_t *sink_port,
    mct_stream_info_t *stream_info);

  boolean allocateBuffer(tm_img_buf_desc_t *buf_desc, cam_mapping_buf_type buf_type,
    uint32_t num_planes, uint32_t buf_size);

  boolean allocateAndReadInputBufXml(void);

  boolean allocateAndReadInputBuf(const char *fname, tm_img_buf_desc_t *p_in_buf_desc, boolean read);

  boolean readInputFile(const char *fname, size_t data_len,
    tm_img_buf_desc_t *p_in_buf_desc);

  boolean allocateOutputBufs(uint32_t idx);

  boolean allocateScratchBufs(uint32_t idx);

  boolean allocateFillMetadata(streams_t *p_streams);

  boolean allocateAndReadRefImage(void);

  void freeInputBuffer(tm_img_buf_desc_t *p_in_buf_desc);

  void freeOutputBuffer(void);

  void freeMetadataBuffer(void);

  void freeReferenceBuffer(void);

  void freeScratchBuffer();

  mct_list_t *getImgBufsList(uint32_t stream_id);

  boolean openIonFD(void);

  void closeIonFD(void);

  uint32_t getStreamIdentity(cam_stream_type_t stream_type);

  uint32_t getParmStreamIdentity(void);

  void setParmStreamIdentity(uint32_t identity);

  void setPipelineInstance(mct_pipeline_t *mct_pipeline);

  boolean linkTestPorts(tm_intf_link_data_t *ld);

  mct_port_t *getFirstInputPort();

private:
  tm_intf_input_t         m_input;

  typedef struct {
    int32_t             pipe_frame[2];
    uint32_t            num_subscribed;
    struct pollfd       poll_fds[10];
  } tm_sync_ctx_t;

  typedef struct {
    mct_module_t              *module;
    mct_port_t                *first_port;
    uint32_t                  num_ports;

  } tm_dummy_port_t;

  tm_sync_ctx_t             tm_sync_ctx;

  mct_pipeline_t            *pipeline;
  mct_list_t                *modules;
  mct_bus_t                 *p_mct_bus;
  mct_stream_info_t         *p_stream_info;
  cam_stream_size_info_t    cam_stream_info;
  uint32_t                  parm_stream_identity;
  uint32_t                  session_id;
  tm_dummy_port_t           input_port;
  tm_dummy_port_t           stats_port;
  tm_dummy_port_t           output_port;

  mct_list_t                *img_buf_list[TM_MAX_NUM_STREAMS];
//  v4l2_frame_buffer_t       out_frame[TM_MAX_NUM_STREAMS];
//  v4l2_frame_buffer_t       scratch_frame[TM_MAX_NUM_STREAMS];
//  v4l2_frame_buffer_t       in_frame;
//  v4l2_frame_buffer_t       meta_frame;
//  mct_stream_map_buf_t      *img_buf_input;
  tm_img_buf_desc_t         in_buf_desc;
  tm_img_buf_desc_t         out_buf_desc[TM_MAX_NUM_STREAMS];
  tm_img_buf_desc_t         scratch_buf_desc[TM_MAX_NUM_STREAMS];
  tm_img_buf_desc_t         meta_buf_desc;
//  mct_stream_map_buf_t      *img_buf_scratch[TM_MAX_NUM_STREAMS];
//  mct_stream_map_buf_t      *img_buf_output[TM_MAX_NUM_STREAMS];
//  mct_stream_map_buf_t      *meta_buf;
  uint8_t                   *ref_img_buffer;
  int                       ionfd;
  uint32_t                  img_bufs_idx;

  tm_intf_chromatix_data_t  chromatix;

  cam_fps_range_t           cur_fps;

  char                      out_fname[256];

  tm_out_dims_t             isp_out_dims[TM_MAX_NUM_STREAMS];

private:

  boolean tmSendSuperparam(cam_intf_parm_type_t param, void *data,
    uint32_t frame_id);

  boolean tmSendParams(tm_params_type_lut_t *params_lut, uint32_t param_iter,
    uint32_t frame_id);

  int tm_do_munmap_ion(int ion_fd, struct ion_fd_data *ion_info_fd,
    void *addr, size_t size);

  void *tm_do_mmap_ion(int ion_fd, struct ion_allocation_data *alloc,
    struct ion_fd_data *ion_info_fd, int *mapFd);

  boolean appendToImgBufsList(mct_stream_map_buf_t *buf);

  boolean removeFromImgBufsList(mct_stream_map_buf_t *buf);

};

class superParam {

public:

  superParam(boolean is_src_first_module);

  ~superParam();

  boolean addParamToBatch(cam_intf_parm_type_t paramType, size_t paramLength,
    void *paramValue);

  boolean getParamFromBatch(cam_intf_parm_type_t paramType, size_t paramLength,
    void *paramValue);

  boolean sendSuperParams(TM_Interface *tm, uint32_t frame_id, uint32_t stream_identity);

  boolean loadDefaultParams(tm_intf_input_t *input);

private:

  parm_buffer_t       *p_table;
  uint32_t            num_super_params;
  boolean             is_first_module_src;

private:

  void addParmToTable(cam_intf_parm_type_t parm_type,
    void *data, parm_buffer_t *p_table, size_t parm_size);

  void getParmFromTable(cam_intf_parm_type_t parm_type,
    void *data, parm_buffer_t *p_table, size_t parm_size);

};

class SOF_Timer {

public:

  boolean startAsyncTimer(float in_fps);

  int waitForTimer(long long timeout);

  boolean stopAsyncTimer(void);

  void* timerThreadRun(void);

private:

  int timerCountdown(void);

  pthread_mutex_t         timer_lock;
  pthread_cond_t          timer_cond;
  pthread_t               timer_tid;
  int                     thread_run;
  float                   fps; // Desired fps at which test must run

};

class IEvent {

public:

  static boolean portEvent(mct_port_t *port, mct_event_t *event);

  static boolean moduleProcessEvent(mct_module_t *module, mct_event_t *event);

};

}


#endif // TM_INTERFACE_H

