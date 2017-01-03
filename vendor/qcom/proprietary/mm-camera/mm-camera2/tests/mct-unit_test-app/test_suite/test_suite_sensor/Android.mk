ifeq ($(call is-vendor-board-platform,QCOM),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
#
# unit test executables
#

# Global flag and include definitions
LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

TEST_CFLAGS := \
  $(mmcamera_debug_defines) \
  $(mmcamera_debug_cflags)

TEST_CFLAGS  += -Werror

ifneq ($(call is-platform-sdk-version-at-least,17),true)
  TEST_CFLAGS += -include bionic/libc/kernel/common/linux/types.h
  TEST_CFLAGS += -include bionic/libc/kernel/common/linux/socket.h
  TEST_CFLAGS += -include bionic/libc/kernel/common/linux/in.h
  TEST_CFLAGS += -include bionic/libc/kernel/common/linux/un.h
endif

TEST_C_INCLUDES:= $(LOCAL_PATH)
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/includes/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/mct/bus/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/mct/controller/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/mct/object/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/includes/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/mct/tools/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/mct/event/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/mct/pipeline/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/mct/stream/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/mct/module/
TEST_C_INCLUDES+= $(LOCAL_MMCAMERA_PATH)/media-controller/mct/port/
TEST_C_INCLUDES+= hardware/qcom/camera/QCamera2/stack/common


LIB_NAME := test_suite_sensor

include $(CLEAR_VARS)

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

LOCAL_CFLAGS := $(TEST_CFLAGS)

LOCAL_C_INCLUDES := $(TEST_C_INCLUDES)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/sensor/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/actuator/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/actuator/libs/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/csid/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/csiphy/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/eeprom/module/

LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_SRC_FILES:= $(LIB_NAME).cpp

LOCAL_SHARED_LIBRARIES:= libcutils libmmcamera2_mct libdl libtm_interface\
  libmmcamera2_sensor_modules libmmcamera_dbg


LOCAL_MODULE:= $(LIB_NAME)
LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif

include $(BUILD_SHARED_LIBRARY)


#END
endif
