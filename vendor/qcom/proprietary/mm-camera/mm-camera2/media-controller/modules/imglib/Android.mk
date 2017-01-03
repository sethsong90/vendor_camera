#======================================================================
# makefile for libmmcamera2_imglib_modules.so for mm-camera2
#======================================================================
ifeq ($(call is-vendor-board-platform,QCOM),true)

LOCAL_PATH := $(call my-dir)
LOCAL_IMGLIB_PATH  := $(LOCAL_PATH)

COMMON_DEFINES := -DHDR_LIB_GHOSTBUSTER

ifeq ($(call is-board-platform-in-list, msm8992 msm8996 msm8952),true)
  FD_USES_DSP := true
else
  FD_USES_DSP := false
endif


IMGLIB_USES_FASTCV := true

include $(CLEAR_VARS)

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

LOCAL_CFLAGS :=  -DAMSS_VERSION=$(AMSS_VERSION) \
  $(mmcamera_debug_defines) \
  $(mmcamera_debug_cflags)
LOCAL_CFLAGS += $(COMMON_DEFINES)

ifeq ($(CHROMATIX_VERSION), 0304)
  LOCAL_CFLAGS += -DCHROMATIX_304
endif
ifeq ($(CHROMATIX_VERSION), 0306)
  LOCAL_CFLAGS += -DCHROMATIX_306
endif
ifeq ($(CHROMATIX_VERSION), 0307)
  LOCAL_CFLAGS += -DCHROMATIX_307
endif
ifeq ($(CHROMATIX_VERSION), 0308)
  LOCAL_CFLAGS += -DCHROMATIX_308
endif
ifeq ($(CHROMATIX_VERSION), 0309)
  LOCAL_CFLAGS += -DCHROMATIX_309
endif
ifeq ($(CHROMATIX_VERSION), 0308E)
  LOCAL_CFLAGS += -DCHROMATIX_308E
  LOCAL_CFLAGS += -D_USE_OEM_CHROMATIX_
else
  LOCAL_CFLAGS += -D__IMGLIB_USE_OEM_FEAT1_STUB__
endif

ifeq ($(MSM_VERSION), 8952)
  LOCAL_CFLAGS += -D__IMGLIB_USE_OEM_FEAT1_STUB__
endif

LOCAL_MMCAMERA_PATH  := $(LOCAL_PATH)/../../../../mm-camera2

USE_CAC_V1:= false
USE_CAC_V2:= false
USE_CAC_V3:= true

LOCAL_C_INCLUDES += $(LOCAL_PATH)

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/includes/

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/bus/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/controller/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/event/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/object/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/pipeline/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/port/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/stream/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/tools/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/debug/

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/includes/
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/common
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/hdr
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/wd
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/faceproc
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/frameproc
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/cac
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/cac_v2
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/cac_v3
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/include
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/lib
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/lib/faceproc
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/components/lib/cac
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/modules
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/modules/common
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/modules/base
LOCAL_C_INCLUDES += $(LOCAL_IMGLIB_PATH)/utils

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/include/mm-camera-interface
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/$(CHROMATIX_VERSION)

LOCAL_CFLAGS += -Werror -Wno-unused-parameter -Wno-unused-function
LOCAL_CFLAGS += -Wno-unused-variable -Wall

ifeq ($(call is-board-platform-in-list, msm8994),true)
  LOCAL_CFLAGS += -DCAC_PREALLOC_BUFFERS
endif

ifeq ($(strip $(USE_CAC_V1)),true)
  LOCAL_CFLAGS += -DUSE_CAC_V1
endif
ifeq ($(strip $(USE_CAC_V2)),true)
  LOCAL_CFLAGS += -DUSE_CAC_V2
endif
ifeq ($(strip $(USE_CAC_V3)),true)
  LOCAL_CFLAGS += -DUSE_CAC_V3
endif

ifeq ($(strip $(FD_USES_DSP)),true)
mmimg_defines += -DFD_WITH_DSP_NEW
endif

LOCAL_CFLAGS += $(mmimg_defines)

LOCAL_SRC_DIR := $(LOCAL_PATH)/modules
LOCAL_SRC_FILES += $(shell find $(LOCAL_SRC_DIR) -name '*.c' | sed s:^$(LOCAL_PATH)::g )

LOCAL_MODULE           := libmmcamera2_imglib_modules
#LOCAL_CLANG            := true

LOCAL_SHARED_LIBRARIES := liblog libcutils libmmcamera2_mct libmmcamera_imglib libmmcamera_dbg
LOCAL_SHARED_LIBRARIES += libdl

LOCAL_MODULE_TAGS      := optional eng
ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif

include $(BUILD_SHARED_LIBRARY)
################ faceproc prebuilt library #############

LOCAL_PATH:= $(LOCAL_IMGLIB_PATH)

ifeq ($(FEATURE_FACE_PROC),true)
  include $(CLEAR_VARS)
  LOCAL_LDFLAGS := $(mmcamera_debug_lflags)
  ifeq (1,$(filter 1,$(shell echo "$$(( $(PLATFORM_SDK_VERSION) >= 23 ))" )))
    LOCAL_PATH := $(LOCAL_IMGLIB_PATH)/components/lib/faceproc
  else
    LOCAL_PATH := $(LOCAL_MMCAMERA_PATH)/../server/frameproc/face_proc/engine
  endif
  LOCAL_MODULE       := libmmcamera_faceproc
  LOCAL_MODULE_SUFFIX := .so
  LOCAL_MODULE_CLASS := SHARED_LIBRARIES
  LOCAL_SRC_FILES := libmmcamera_faceproc.so
  LOCAL_MODULE_TAGS := optional eng
  LOCAL_MODULE_OWNER := qti
  LOCAL_PROPRIETARY_MODULE := true

  ifeq ($(32_BIT_FLAG), true)
  LOCAL_32_BIT_ONLY := true
  endif

  include $(BUILD_PREBUILT)
endif

################ component library ######################
include $(CLEAR_VARS)

LOCAL_PATH:= $(LOCAL_IMGLIB_PATH)

mmimg_defines :=  -DAMSS_VERSION=$(AMSS_VERSION) \
       $(mmcamera_debug_defines) \
       $(mmcamera_debug_cflags) \
       -g -O0 \
       -include img_dbg.h

mmimg_defines += -Werror -Wno-unused-parameter -Wno-unused-function
mmimg_defines += -Wno-unused-variable -Wall

mmimg_defines += -DLOGE=ALOGE

ifeq ($(strip $(TARGET_USES_ION)),true)
mmimg_defines += -DUSE_ION
endif


LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/common
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/hdr
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/wd
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/faceproc
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/faceproc_dsp
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/faceproc_hw
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/frameproc
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/cac
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/cac_v2
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/cac_v3
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/lib2d
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/lib
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/lib/faceproc
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/lib/cac
LOCAL_C_INCLUDES += $(LOCAL_PATH)/utils
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/include/adreno/
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/include/adreno200/
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/include/mm-camera-interface

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/include/mm-camera-interface
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
mmimg_defines += -include $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/linux/ion.h
mmimg_defines += -include $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/linux/videodev2.h
endif

ifeq ($(strip $(FD_USES_DSP)),true)
mmimg_defines += -DFD_WITH_DSP_NEW
endif
LOCAL_LDFLAGS := $(mmcamera_debug_lflags)
LOCAL_CFLAGS := $(mmimg_defines)
LOCAL_CFLAGS += $(COMMON_DEFINES)

ifeq ($(call is-board-platform-in-list,msm8610),true)
  LOCAL_CFLAGS  += -DUSE_SMMU_BUFFERS_FOR_WNR
endif #msm8610

ifeq ($(call is-board-platform-in-list,msm8952),true)
  LOCAL_CFLAGS  += -DUSE_SMMU_BUFFERS_FOR_FDDSP
endif

ifeq ($(call is-board-platform-in-list,msm8937),true)
  LOCAL_CFLAGS  += -DUSE_SMMU_BUFFERS_FOR_FDDSP
endif

ifeq ($(call is-board-platform-in-list,msm8953),true)
  LOCAL_CFLAGS  += -DUSE_SMMU_BUFFERS_FOR_FDDSP
endif
ifeq ($(strip $(IMGLIB_USES_FASTCV)),true)
  LOCAL_CFLAGS += -D__CAM_FCV__
endif

ifeq ($(CHROMATIX_VERSION), 0304)
  LOCAL_CFLAGS += -DCHROMATIX_304
endif
ifeq ($(CHROMATIX_VERSION), 0306)
  LOCAL_CFLAGS += -DCHROMATIX_306
endif
ifeq ($(CHROMATIX_VERSION), 0307)
  LOCAL_CFLAGS += -DCHROMATIX_307
endif
ifeq ($(CHROMATIX_VERSION), 0308)
  LOCAL_CFLAGS += -DCHROMATIX_308
endif
ifeq ($(CHROMATIX_VERSION), 0309)
  LOCAL_CFLAGS += -DCHROMATIX_309
endif
ifeq ($(CHROMATIX_VERSION), 0308E)
  LOCAL_CFLAGS += -DCHROMATIX_308E
endif
LOCAL_SRC_FILES := utils/img_queue.c
LOCAL_SRC_FILES += utils/img_buffer.c
LOCAL_SRC_FILES += utils/img_list.c
LOCAL_SRC_FILES += utils/img_thread.c
LOCAL_SRC_FILES += utils/img_rb.c
LOCAL_SRC_FILES += utils/img_dsp_dl_mgr.c
LOCAL_SRC_FILES += utils/img_fcv.c
LOCAL_SRC_FILES += components/common/img_thread_mgr.c
LOCAL_SRC_FILES += components/common/img_common.c
LOCAL_SRC_FILES += components/common/img_meta.c
LOCAL_SRC_FILES += components/common/img_comp.c
LOCAL_SRC_FILES += components/common/img_comp_factory.c
LOCAL_SRC_FILES += components/common/ds_2by2_asm.s
LOCAL_SRC_FILES += components/common/boost_armv7_gcc.s

LOCAL_SRC_FILES += components/wd/wd_comp.c
LOCAL_SRC_FILES += components/hdr/hdr_comp.c
LOCAL_SRC_FILES += components/faceproc/faceproc_comp_eng.c
LOCAL_SRC_FILES += components/faceproc/faceproc_comp.c
LOCAL_SRC_FILES += components/faceproc_dsp/faceproc_dsp_comp.c
LOCAL_SRC_FILES += components/faceproc_hw/faceproc_hw_comp.c
LOCAL_SRC_FILES += components/lib/faceproc/wrappers/facial_parts_wrapper.c
LOCAL_SRC_FILES += components/frameproc/frameproc_comp.c
LOCAL_SRC_FILES += components/frameproc/dual_frameproc_comp.c
LOCAL_SRC_FILES += components/faceproc/dynamic_work_partition.c
LOCAL_SRC_FILES += components/cac/cac_comp.c
LOCAL_SRC_FILES += components/cac_v2/cac_v2_comp.c
LOCAL_SRC_FILES += components/cac_v3/cac_v3_comp.c
LOCAL_SRC_FILES += components/lib2d/lib2d_comp.c
LOCAL_SRC_FILES += components/lib2d/c2d_utils.c


LOCAL_MODULE           := libmmcamera_imglib
#LOCAL_CLANG            := true
LOCAL_PRELINK_MODULE   := false
LOCAL_SHARED_LIBRARIES := libdl libcutils liblog libmmcamera_dbg
ifeq ($(strip $(IMGLIB_USES_FASTCV)),true)
LOCAL_SHARED_LIBRARIES += libfastcvopt
endif


ifeq ($(strip $(IMGLIB_USES_FASTCV)),true)
LOCAL_SHARED_LIBRARIES += libfastcvopt
endif

LOCAL_MODULE_TAGS := optional
ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr 
include $(BUILD_SHARED_LIBRARY)


################ Imglib headers copy ############

OLD_LOCAL_COPY_HEADERS_TO := $(LOCAL_COPY_HEADERS_TO)
OLD_LOCAL_COPY_HEADERS := $(LOCAL_COPY_HEADERS)

IMGLIB_HEADER_PATH := mm-camera/imglib
LOCAL_COPY_HEADERS_TO := $(IMGLIB_HEADER_PATH)
LOCAL_COPY_HEADERS := ./utils/img_buffer.h
LOCAL_COPY_HEADERS += ./components/include/img_common.h
LOCAL_COPY_HEADERS += ./components/include/img_comp.h
LOCAL_COPY_HEADERS += ./components/include/img_meta.h
LOCAL_COPY_HEADERS += ./components/include/img_comp_factory.h
LOCAL_COPY_HEADERS += ./components/include/lib2d.h
LOCAL_COPY_HEADERS += ./components/lib/img_mem_ops.h
LOCAL_COPY_HEADERS += ./components/lib/img_thread_ops.h
include $(BUILD_COPY_HEADERS)

LOCAL_COPY_HEADERS_TO := $(OLD_LOCAL_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(OLD_LOCAL_COPY_HEADERS)

################ Face Detection adsp stub library ############

ifeq ($(strip $(FD_USES_DSP)),true)
  include $(CLEAR_VARS)
  LOCAL_LDFLAGS := $(mmcamera_debug_lflags)
  LOCAL_PATH:= $(LOCAL_IMGLIB_PATH)
  LOCAL_MODULE       := libmmcamera_imglib_faceproc_adspstub
  LOCAL_MODULE_SUFFIX := .so
  LOCAL_MODULE_CLASS := SHARED_LIBRARIES
  LOCAL_C_INCLUDES = $(TARGET_OUT_HEADERS)/adsprpc/inc
  LOCAL_SRC_FILES := components/faceproc_dsp/adsp_fd_stub.c
  LOCAL_SHARED_LIBRARIES := libadsprpc libcutils libdl liblog
  LOCAL_MODULE_TAGS := optional
ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
  LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif
  LOCAL_MODULE_OWNER := qcom
  LOCAL_PRELINK_MODULE   := false
  LOCAL_PROPRIETARY_MODULE := true

  ifeq ($(32_BIT_FLAG), true)
  LOCAL_32_BIT_ONLY := true
  endif

  include $(BUILD_SHARED_LIBRARY)
endif

################ Face Detection Q6 prebuilt library #############

ifeq ($(strip $(FD_USES_DSP)),true)
 include $(CLEAR_VARS)
  LOCAL_MODULE       := libadsp_fd_skel
  LOCAL_MODULE_SUFFIX := .so
  LOCAL_MODULE_CLASS := ETC
  LOCAL_SRC_FILES := components/lib/faceproc/libadsp_fd_skel.so
  LOCAL_MODULE_TAGS := optional
  LOCAL_MODULE_OWNER := qti
  LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/lib/rfsa/adsp

  ifeq ($(32_BIT_FLAG), true)
  LOCAL_32_BIT_ONLY := true
  endif

  include $(BUILD_PREBUILT)
endif
##########################test app ##############################
include $(CLEAR_VARS)

LOCAL_PATH:= $(LOCAL_IMGLIB_PATH)

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

mmimg_defines:= \
       $(mmcamera_debug_defines) \
       $(mmcamera_debug_cflags) \
       -DAMSS_VERSION=$(AMSS_VERSION) \
       -g -O0 \
       -D_ANDROID_ \
       -include img_dbg.h

mmimg_defines += -DLOGE=ALOGE -Werror
mmimg_defines += -Werror -Wno-unused-parameter -Wno-unused-function
mmimg_defines += -Wno-unused-variable -Wall
ifeq ($(strip $(TARGET_USES_ION)),true)
mmimg_defines += -DUSE_ION
endif

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/common
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/hdr
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/wd
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/faceproc
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/cac
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/frameproc
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/include/faceproc
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/test
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/lib
LOCAL_C_INCLUDES += $(LOCAL_PATH)/utils
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/lib

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/include/mm-camera-interface
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
mmimg_defines += -include $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/linux/ion.h
mmimg_defines += -include $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/linux/videodev2.h
endif

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

LOCAL_CFLAGS := $(mmimg_defines)
LOCAL_CFLAGS += $(COMMON_DEFINES)
ifeq ($(CHROMATIX_VERSION), 0304)
  LOCAL_CFLAGS += -DCHROMATIX_304
endif
ifeq ($(CHROMATIX_VERSION), 0306)
  LOCAL_CFLAGS += -DCHROMATIX_306
endif
ifeq ($(CHROMATIX_VERSION), 0307)
  LOCAL_CFLAGS += -DCHROMATIX_307
endif
ifeq ($(CHROMATIX_VERSION), 0308)
  LOCAL_CFLAGS += -DCHROMATIX_308
endif
ifeq ($(CHROMATIX_VERSION), 0309)
  LOCAL_CFLAGS += -DCHROMATIX_309
endif
ifeq ($(CHROMATIX_VERSION), 0308E)
  LOCAL_CFLAGS += -DCHROMATIX_308E
endif
LOCAL_SRC_FILES := components/test/img_test.c
LOCAL_SRC_FILES += components/test/hdr_test.c
LOCAL_SRC_FILES += components/test/denoise_test.c
LOCAL_SRC_FILES += components/test/faceproc_test.c
LOCAL_SRC_FILES += components/test/cac_test.c
LOCAL_SRC_FILES += components/test/dual_frameproc_test.c
LOCAL_SRC_FILES += components/test/buf_pool_test.c

LOCAL_SHARED_LIBRARIES:= \
    libmmcamera_imglib libcutils libdl libmmcamera_dbg
LOCAL_MODULE_OWNER := qti
LOCAL_MODULE:= mm-imglib-test
#LOCAL_CLANG := true

LOCAL_MODULE_TAGS := optional

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr  
include $(BUILD_EXECUTABLE)

##########################hdr module test app ##############################
include $(CLEAR_VARS)

LOCAL_PATH:= $(LOCAL_IMGLIB_PATH)

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

mmimg_defines:= \
       $(mmcamera_debug_defines) \
       $(mmcamera_debug_cflags) \
       -DAMSS_VERSION=$(AMSS_VERSION) \
       -g -O0 \
       -D_ANDROID_

mmimg_defines += -DLOGE=ALOGE -Werror
mmimg_defines += -Werror -Wno-unused-parameter -Wno-unused-function
mmimg_defines += -Wno-unused-variable -Wall

ifeq ($(strip $(TARGET_USES_ION)),true)
mmimg_defines += -DUSE_ION
endif
ifeq ($(CHROMATIX_VERSION), 0308)
  LOCAL_CFLAGS += -DCHROMATIX_308
endif
ifeq ($(CHROMATIX_VERSION), 0309)
  LOCAL_CFLAGS += -DCHROMATIX_309
endif
ifeq ($(CHROMATIX_VERSION), 0308E)
  LOCAL_CFLAGS += -DCHROMATIX_308E
endif
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/includes/

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/bus/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/controller/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/event/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/object/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/pipeline/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/port/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/stream/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/tools/

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/utils

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += hardware/qcom/camera/QCamera2/stack/common

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

LOCAL_CFLAGS := $(mmimg_defines)
LOCAL_CFLAGS += $(COMMON_DEFINES)

LOCAL_SRC_FILES := test/test_module_hdr.c

LOCAL_SHARED_LIBRARIES:= \
    libmmcamera2_imglib_modules libcutils libdl libmmcamera2_mct libmmcamera_dbg

LOCAL_MODULE_OWNER := qti
LOCAL_MODULE:= mm-module-hdr-test
#LOCAL_CLANG := true

LOCAL_MODULE_TAGS := optional

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif

include $(BUILD_EXECUTABLE)

##########################module test app ##############################
include $(CLEAR_VARS)

LOCAL_PATH:= $(LOCAL_IMGLIB_PATH)

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

mmimg_defines:= \
       $(mmcamera_debug_defines) \
       $(mmcamera_debug_cflags) \
       -DAMSS_VERSION=$(AMSS_VERSION) \
       -g -O0 \
       -D_ANDROID_ \

mmimg_defines += -DLOGE=ALOGE -Werror
mmimg_defines += -Werror -Wno-unused-parameter -Wno-unused-function
mmimg_defines += -Wno-unused-variable -Wall

ifeq ($(strip $(TARGET_USES_ION)),true)
mmimg_defines += -DUSE_ION
endif

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/includes/

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/bus/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/controller/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/event/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/object/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/pipeline/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/port/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/stream/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/mct/tools/

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/utils

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += hardware/qcom/camera/QCamera2/stack/common

LOCAL_SRC_FILES := test/test_module_imglib.c

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

LOCAL_CFLAGS := $(mmimg_defines)

LOCAL_SHARED_LIBRARIES:= \
    libmmcamera2_imglib_modules libcutils libdl libmmcamera2_mct libmmcamera_dbg

LOCAL_MODULE_OWNER := qti
LOCAL_MODULE:= mm-module-imglib-test
#LOCAL_CLANG := true

LOCAL_MODULE_TAGS := optional

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif
ifeq ($(CHROMATIX_VERSION), 0308)
  LOCAL_CFLAGS += -DCHROMATIX_308
endif
ifeq ($(CHROMATIX_VERSION), 0309)
  LOCAL_CFLAGS += -DCHROMATIX_309
endif
ifeq ($(CHROMATIX_VERSION), 0308E)
  LOCAL_CFLAGS += -DCHROMATIX_308E
endif
include $(BUILD_EXECUTABLE)

##########################module test app ##############################
include $(CLEAR_VARS)

LOCAL_PATH:= $(LOCAL_IMGLIB_PATH)

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

mmimg_defines:= -DAMSS_VERSION=$(AMSS_VERSION) \
       $(mmcamera_debug_defines) \
       $(mmcamera_debug_cflags) \
       -g -O0 \
       -D_ANDROID_ \
       -include img_dbg.h \

mmimg_defines += -DLOGE=ALOGE -Werror
mmimg_defines += -Werror -Wno-unused-parameter -Wno-unused-function
mmimg_defines += -Wno-unused-variable -Wall

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/includes/

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/include/mm-camera-interface
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/components/lib

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

LOCAL_CFLAGS := $(mmimg_defines)
LOCAL_CFLAGS += $(COMMON_DEFINES)

LOCAL_SRC_FILES := test/imgalgo_dummy.c

LOCAL_MODULE           := libmmcamera_dummyalgo
#LOCAL_CLANG            := true
LOCAL_PRELINK_MODULE   := false
LOCAL_SHARED_LIBRARIES := libdl libcutils liblog libmmcamera_imglib libmmcamera_dbg

LOCAL_MODULE_TAGS := optional
ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif
include $(BUILD_SHARED_LIBRARY)


########################## SW2D Module ##############################
ifeq ($(strip $(IMGLIB_USES_FASTCV)),true)
include $(LOCAL_IMGLIB_PATH)/components/sw2d/Android.mk
endif

endif # is-vendor-board-platform,QCOM
