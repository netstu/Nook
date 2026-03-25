LOCAL_PATH := $(call my-dir)
ROOT_PATH := $(LOCAL_PATH)/../..

include $(CLEAR_VARS)

LOCAL_MODULE := nook

LOCAL_C_INCLUDES := $(ROOT_PATH)/include
LOCAL_C_INCLUDES += $(ROOT_PATH)/src/framework
LOCAL_C_INCLUDES += $(ROOT_PATH)/src/java_hook
LOCAL_C_INCLUDES += $(ROOT_PATH)/src/common
LOCAL_C_INCLUDES += $(ROOT_PATH)/third_party/elfio
LOCAL_C_INCLUDES += $(ROOT_PATH)/third_party/xdl

LOCAL_SRC_FILES := \
    ../../src/framework/Nook.cpp \
    ../../src/framework/NookJavaHook.cpp \
    ../../src/framework/NookJavaHookPayload.cpp \
    ../../src/native_hook/NookNativeHook.cpp \
    ../../src/java_hook/JavaHook.cpp \
    ../../src/java_hook/JVM.cpp \
    ../../src/common/JavaHookLog.cpp \
    ../../src/common/ArtStructDetector.cpp \
    ../../third_party/xdl/xdl.c \
    ../../third_party/xdl/xdl_util.c \
    ../../third_party/xdl/xdl_linker.c \
    ../../third_party/xdl/xdl_iterate.c \
    ../../third_party/xdl/xdl_lzma.c

LOCAL_CPPFLAGS := -std=c++17 -fPIC
LOCAL_CFLAGS := -std=c11 -fPIC
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_java_test_replace_num_macro

LOCAL_C_INCLUDES := $(ROOT_PATH)/include
LOCAL_C_INCLUDES += $(ROOT_PATH)/src/framework
LOCAL_C_INCLUDES += $(ROOT_PATH)/src/java_hook
LOCAL_C_INCLUDES += $(ROOT_PATH)/src/common
LOCAL_C_INCLUDES += $(ROOT_PATH)/third_party/elfio
LOCAL_C_INCLUDES += $(ROOT_PATH)/third_party/xdl

LOCAL_SRC_FILES := \
    ../../examples/java_hook/nook_java_test_replace_num_macro.cpp \
    ../../src/framework/Nook.cpp \
    ../../src/framework/NookJavaHook.cpp \
    ../../src/framework/NookJavaHookPayload.cpp \
    ../../src/native_hook/NookNativeHook.cpp \
    ../../src/java_hook/JavaHook.cpp \
    ../../src/java_hook/JVM.cpp \
    ../../src/common/JavaHookLog.cpp \
    ../../src/common/ArtStructDetector.cpp \
    ../../third_party/xdl/xdl.c \
    ../../third_party/xdl/xdl_util.c \
    ../../third_party/xdl/xdl_linker.c \
    ../../third_party/xdl/xdl_iterate.c \
    ../../third_party/xdl/xdl_lzma.c

LOCAL_CPPFLAGS := -std=c++17 -fPIC
LOCAL_CFLAGS := -std=c11 -fPIC
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)
