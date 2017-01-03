# ==============================================================================
# ---------------------------------------------------------------------------------
#           Make the libmmcamera_dbg.so
# ---------------------------------------------------------------------------------


LOCAL_PATH:= $(call my-dir)

LOCAL_MMCAMERA_PATH  := $(LOCAL_PATH)/../..

# trace logging lib
include $(CLEAR_VARS)
LOCAL_CFLAGS  := -D_ANDROID_
LOCAL_CFLAGS += -Werror -Wunused-parameter

include $(LOCAL_MMCAMERA_PATH)/mm-camera2/log_debug/autogen.mk

#************* MCT headers ************#
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/mm-camera2/includes

#************* HAL headers ************#
LOCAL_C_INCLUDES += \
 $(LOCAL_PATH)/../../../../../../hardware/qcom/camera/QCamera2/stack/common


#************* Kernel headers ************#
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_SRC_FILES = android/camera_dbg.c
LOCAL_SHARED_LIBRARIES := libdl libcutils
LOCAL_MODULE := libmmcamera_dbg
LOCAL_MODULE_TAGS := optional eng

LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)

