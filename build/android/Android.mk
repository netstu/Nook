LOCAL_PATH := $(call my-dir)
ROOT_PATH := $(LOCAL_PATH)/../..

NOOK_COMMON_INCLUDES := $(ROOT_PATH)/include
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src/framework
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src/java_hook
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src/common
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/third_party/elfio
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/third_party/xdl

NOOK_NATIVE_HOOK_SRC := \
    ../../src/framework/NookPltHook.cpp \
    ../../src/framework/NookInlineHook.cpp \
    ../../src/native_hook/core/native_hook_symbol_resolver.cpp \
    ../../src/native_hook/core/native_hook_dispatcher.cpp \
    ../../src/native_hook/core/module_info.cpp \
    ../../src/native_hook/core/module_match.cpp \
    ../../src/native_hook/core/runtime_patch.cpp \
    ../../src/native_hook/inline_hook/inline_hook_impl.cpp \
    ../../src/native_hook/inline_hook/pending_inline_hook_registry.cpp \
    ../../src/native_hook/inline_hook/inline_hook_module_observer.cpp \
    ../../src/native_hook/inline_hook/inline_hook_record.cpp \
    ../../src/native_hook/inline_hook/trampoline_allocator.cpp \
    ../../src/native_hook/inline_hook/arm64_instruction_relocator.cpp \
    ../../src/native_hook/plt_hook/elf_hash.cpp \
    ../../src/native_hook/plt_hook/elf_reader.cpp \
    ../../src/native_hook/plt_hook/elfio_image_parser.cpp \
    ../../src/native_hook/plt_hook/plt_hook_impl.cpp

NOOK_RUNTIME_SRC := \
    ../../src/framework/Nook.cpp \
    ../../src/framework/NookJavaHook.cpp \
    ../../src/framework/NookJavaHookPayload.cpp \
    $(NOOK_NATIVE_HOOK_SRC) \
    ../../src/java_hook/JavaHook.cpp \
    ../../src/java_hook/JVM.cpp \
    ../../src/common/JavaHookLog.cpp \
    ../../src/common/ArtStructDetector.cpp \
    ../../third_party/xdl/xdl.c \
    ../../third_party/xdl/xdl_util.c \
    ../../third_party/xdl/xdl_linker.c \
    ../../third_party/xdl/xdl_iterate.c \
    ../../third_party/xdl/xdl_lzma.c

include $(CLEAR_VARS)

LOCAL_MODULE := nook

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)
LOCAL_SRC_FILES := $(NOOK_RUNTIME_SRC)

LOCAL_CPPFLAGS := -std=c++17 -fPIC
LOCAL_CFLAGS := -std=c11 -fPIC
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_inline_observer_probe

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)

LOCAL_SRC_FILES := \
    ../../examples/native_hook/common/nook_inline_observer_probe.cpp

LOCAL_CPPFLAGS := -std=c++17 -fPIC
LOCAL_CFLAGS := -std=c11 -fPIC
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)
