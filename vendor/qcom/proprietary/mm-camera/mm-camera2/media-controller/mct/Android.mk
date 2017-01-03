#======================================================================
#makefile for libmmcamera2_mct.so from mm-camera2
#======================================================================
ifeq ($(call is-vendor-board-platform,QCOM),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CFLAGS  := -D_ANDROID_
LOCAL_CFLAGS += -DMCT_STUCK_FLAG

LOCAL_MMCAMERA_PATH  := $(LOCAL_PATH)/../../../mm-camera2

LOCAL_C_INCLUDES += $(LOCAL_PATH)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/includes/

#************* MCT headers ************#
LOCAL_C_INCLUDES += $(LOCAL_PATH)/bus/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/controller/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/event/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/module/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/object/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/pipeline/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/port/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/stream/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/tools/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/debug/

#************* HAL headers ************#
LOCAL_C_INCLUDES += \
 $(LOCAL_PATH)/../../../../../../../hardware/qcom/camera/QCamera2/stack/common

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_CFLAGS  += -Werror

LOCAL_SRC_DIR := $(LOCAL_PATH)
LOCAL_SRC_FILES += $(shell find $(LOCAL_SRC_DIR) -name '*.c' | sed s:^$(LOCAL_PATH)::g )

LOCAL_MODULE           := libmmcamera2_mct

LOCAL_SHARED_LIBRARIES := libdl liblog libcutils libmmcamera_dbg
LOCAL_MODULE_TAGS      := optional eng
LOCAL_ADDITIONAL_DEPENDENCIES  := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true
ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif

include $(BUILD_SHARED_LIBRARY)

endif # is-vendor-board-platform,QCOM
