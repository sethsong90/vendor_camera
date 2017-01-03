#======================================================================
#makefile for libmmcamera2_stats_modules.so form mm-camera2
#======================================================================
ifeq ($(call is-vendor-board-platform,QCOM),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(call is-board-platform-in-list, msm8610),true)
  LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

  LOCAL_CFLAGS := -DAMSS_VERSION=$(AMSS_VERSION) \
    $(mmcamera_debug_defines) \
    $(mmcamera_debug_cflags) -DFEATURE_SKIP_STATS
else
  LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

  LOCAL_CFLAGS := -DAMSS_VERSION=$(AMSS_VERSION) \
    $(mmcamera_debug_defines) \
    $(mmcamera_debug_cflags)
ifeq ($(call is-board-platform-in-list,msm8937,msm8917,msm8916),true)
  USE_ANDROID_GYRO := true
endif
ifeq ($(USE_ANDROID_GYRO),true)
 FEATURE_GYRO_DSPS := false
else
 LOCAL_CFLAGS += -DFEATURE_GYRO_DSPS
 FEATURE_GYRO_DSPS := true
endif

ifeq ($(CHROMATIX_VERSION), 0308E)
 LOCAL_CFLAGS += -DCHROMATIX_308E
endif

endif

ifeq ($(OEM_CHROMATIX_0308), 1)
 LOCAL_CFLAGS += -D_ENABLE_AEC_BG_STATS_ -D_OEM_CHROMATIX_0308_
endif

#Check if version is LMR1 and below
ifneq (1,$(filter 1,$(shell echo "$$(( $(PLATFORM_SDK_VERSION) <= 22 ))" )))
 LOCAL_CFLAGS += -DUPGRADE_SENSOR_MANAGER
endif

LOCAL_MMCAMERA_PATH  := $(LOCAL_PATH)/../../../../mm-camera2

LOCAL_C_INCLUDES += $(LOCAL_PATH)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/debug-data

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/$(CHROMATIX_VERSION)/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/module/

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/includes/

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a/include
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a/platform/linux
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a/aec
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a/af
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a/af/af_algo_interface
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a/af/examples
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a/awb
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/asd
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/afd

LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/is
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../../../mm-camera-lib/is/sensor_lib
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/gyro
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/gyro/dsps
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../../../sensors/dsps/api
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../../../qmi/core/lib/inc

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

#add gyro sensor API header locations
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/sensors/inc
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/qmi/inc
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/include/adreno

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
endif
LOCAL_C_INCLUDES += \
 $(LOCAL_PATH)/../../../../../../../../hardware/qcom/camera/QCamera2/stack/common

LOCAL_CFLAGS  += -Werror -Wunused-parameter

LOCAL_SRC_DIR := $(LOCAL_PATH)
LOCAL_SRC_FILES += $(shell find $(LOCAL_SRC_DIR) -name '*.c' | sed s:^$(LOCAL_PATH)::g )
LOCAL_SRC_FILES += $(shell find $(LOCAL_SRC_DIR) -name '*.cpp' | sed s:^$(LOCAL_PATH)::g )

LOCAL_MODULE           := libmmcamera2_stats_modules
LOCAL_CLANG := false

#Shared libraries
LOCAL_SHARED_LIBRARIES := libdl libcutils libmmcamera2_mct libmmcamera2_is libmmcamera2_stats_algorithm libui libgui
LOCAL_SHARED_LIBRARIES += libandroid libutils libmmcamera_dbg

ifeq ($(FEATURE_GYRO_DSPS), true)
  LOCAL_SHARED_LIBRARIES += libsensor1
endif

LOCAL_MODULE_TAGS      := optional eng
ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif

include $(LOCAL_PATH)/external/ext_awb_build_option.mk

include $(BUILD_SHARED_LIBRARY)

include $(LOCAL_PATH)/external/ext_awb_add_prebuilt.mk

endif # is-vendor-board-platform,QCOM
