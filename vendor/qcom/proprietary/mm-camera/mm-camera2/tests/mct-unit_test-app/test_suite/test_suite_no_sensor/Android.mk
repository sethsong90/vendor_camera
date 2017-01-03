ifeq ($(call is-vendor-board-platform,QCOM),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
#
# unit test executables
#

# Global flag and include definitions
LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

TEST_CFLAGS := -DAMSS_VERSION=$(AMSS_VERSION) \
  $(mmcamera_debug_defines) \
  $(mmcamera_debug_cflags) \
  -DMSM_CAMERA_BIONIC

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
TEST_C_INCLUDES+= \
 $(LOCAL_MMCAMERA_PATH)/../../../../../hardware/qcom/camera/QCamera2/stack/common


LIB_NAME := test_suite_no_sensor

include $(CLEAR_VARS)

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

LOCAL_CFLAGS := $(TEST_CFLAGS)
LOCAL_CFLAGS  += -D_ANDROID_

LOCAL_C_INCLUDES := $(TEST_C_INCLUDES)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/media
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/pproc-new/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/isp2/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/isp2/common/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/server-imaging/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/server-tuning/tuning/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/sensor/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/sensor/libs/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/actuator/module/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/actuator/libs/
#LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/actuators/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/
#LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/chromatix/$(CHROMATIX_VERSION)
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/csid/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/csiphy/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/eeprom/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/led_flash/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/strobe_flash/
LOCAL_C_INCLUDES += external/libxml2/include/
LOCAL_C_INCLUDES += external/libxml2/include/libxml/
LOCAL_C_INCLUDES += external/icu4c/common/

LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_SRC_FILES:= $(LIB_NAME).cpp

LOCAL_SHARED_LIBRARIES:= libcutils libmmcamera2_mct libmmcamera2_pproc_modules libdl \
  libmmcamera2_isp_modules libmmcamera2_sensor_modules libmmcamera2_iface_modules \
  libmmcamera2_imglib_modules libtm_interface libmmcamera_dbg

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
