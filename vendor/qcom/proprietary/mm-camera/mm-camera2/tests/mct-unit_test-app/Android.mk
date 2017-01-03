ifeq ($(call is-vendor-board-platform,QCOM),true)

LOCAL_PATH := $(call my-dir)

LOCAL_START_PATH := $(LOCAL_PATH)

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


#
# tm_interface
#
include $(CLEAR_VARS)

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

LOCAL_CFLAGS := $(TEST_CFLAGS)
LOCAL_CFLAGS  += -D_ANDROID_

LOCAL_C_INCLUDES := $(TEST_C_INCLUDES)

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/media
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/includes/
#LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/isp2/module/
#LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/isp2/common/
#LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/stats/q3a/
#LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/
#LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/../../mm-camerasdk/sensor/includes/$(CHROMATIX_VERSION)
#LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/sensors/module/
#LOCAL_C_INCLUDES += $(LOCAL_PATH)/../media-controller/modules/sensors/sensor/module/
#LOCAL_C_INCLUDES += $(LOCAL_PATH)/../media-controller/modules/sensors/sensor/libs/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/server-imaging/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/server-tuning/tuning/
LOCAL_C_INCLUDES += external/libxml2/include/
LOCAL_C_INCLUDES += external/libxml2/include/libxml/
LOCAL_C_INCLUDES += external/icu/icu4c/source/common/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/test_suite/

LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_SRC_FILES:= tm_interface.cpp test_suite/test_suite.cpp test_suite/test_module.cpp

LOCAL_SHARED_LIBRARIES:= libcutils libmmcamera2_mct libdl libmmcamera_dbg

LOCAL_MODULE:= libtm_interface
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif

include $(BUILD_SHARED_LIBRARY)

#
# mct-unit-test-app
#
include $(CLEAR_VARS)

APP_NAME := mct-unit-test-app

LOCAL_LDFLAGS := $(mmcamera_debug_lflags)

TEST_CFLAGS += -DUNIT_TEST_APP_NAME=\"$(APP_NAME)\"
LOCAL_CFLAGS := $(TEST_CFLAGS)
LOCAL_CFLAGS  += -D_ANDROID_

LOCAL_C_INCLUDES := $(TEST_C_INCLUDES)
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/media
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/media-controller/modules/includes/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/server-imaging/
LOCAL_C_INCLUDES += $(LOCAL_MMCAMERA_PATH)/server-tuning/tuning/
LOCAL_C_INCLUDES += external/libxml2/include/
LOCAL_C_INCLUDES += external/libxml2/include/libxml/
LOCAL_C_INCLUDES += external/icu/icu4c/source/common/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/test_suite/

LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_SRC_FILES:= main.cpp test_manager.cpp

LOCAL_SHARED_LIBRARIES:= libcutils libmmcamera2_mct libdl \
  libxml2 libtm_interface libmmcamera2_pproc_modules \
  libmmcamera2_isp_modules libmmcamera2_sensor_modules \
  libmmcamera2_stats_modules libmmcamera2_iface_modules \
  libmmcamera2_imglib_modules libmmcamera_dbg

LOCAL_MODULE:= $(APP_NAME)
LOCAL_MODULE_TAGS := optional

ifeq ($(32_BIT_FLAG), true)
LOCAL_32_BIT_ONLY := true
endif

include $(BUILD_EXECUTABLE)

include $(LOCAL_PATH)/test_suite/Android.mk

#END
endif
