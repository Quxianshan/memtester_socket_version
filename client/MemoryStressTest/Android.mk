LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# We only want this apk build for tests.
LOCAL_MODULE_TAGS := optional
LOCAL_DEX_PREOPT := false

# Include all test java files.
LOCAL_SRC_FILES := $(call all-java-files-under, src)

LOCAL_PACKAGE_NAME := MemoryTest

LOCAL_CERTIFICATE := platform

include $(BUILD_PACKAGE)
#LOCAL_INSTRUMENTATION_FOR := Gallery2

