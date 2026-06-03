LOCAL_PATH := $(call my-dir)
ROOT_PATH := $(LOCAL_PATH)/../..

NOOK_COMMON_INCLUDES := $(ROOT_PATH)/include
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src/framework
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src/java_hook
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src/java_hook/router
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src/common
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/src/communication
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/third_party/elfio
NOOK_COMMON_INCLUDES += $(ROOT_PATH)/third_party/xdl

NOOK_QUICKJS_CONFIG_HEADER := $(ROOT_PATH)/third_party/quickjs/quickjs-2025-09-13/nook_quickjs_config.h

NOOK_COMMON_CPPFLAGS := -std=c++17 -fPIC
NOOK_COMMON_CFLAGS := -std=c11 -fPIC -fwrapv -include $(NOOK_QUICKJS_CONFIG_HEADER)

# Communication module sources
NOOK_COMM_SRC := \
    ../../src/communication/agent/agent_connection.cpp \
    ../../src/communication/protocol/frame.cpp \
    ../../src/communication/protocol/tlv.cpp \
    ../../src/communication/protocol/messages.cpp \
    ../../src/communication/session/session.cpp \
    ../../src/communication/session/session_manager.cpp \
    ../../src/communication/transport/path_utils.cpp \
    ../../src/communication/transport/spawn_marker.cpp \
    ../../src/communication/transport/transport.cpp \
    ../../src/communication/transport/tcp_transport.cpp \
    ../../src/communication/transport/unix_transport.cpp

NOOK_NATIVE_HOOK_SRC := \
    ../../src/framework/NookPltHook.cpp \
    ../../src/framework/NookInlineHook.cpp \
    ../../src/framework/NookNativeHook.cpp \
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

NOOK_QUICKJS_SRC := \
    ../../third_party/quickjs/quickjs-2025-09-13/quickjs.c \
    ../../third_party/quickjs/quickjs-2025-09-13/dtoa.c \
    ../../third_party/quickjs/quickjs-2025-09-13/libregexp.c \
    ../../third_party/quickjs/quickjs-2025-09-13/libunicode.c \
    ../../third_party/quickjs/quickjs-2025-09-13/cutils.c

NOOK_XDL_SRC := \
    ../../third_party/xdl/xdl.c \
    ../../third_party/xdl/xdl_util.c \
    ../../third_party/xdl/xdl_linker.c \
    ../../third_party/xdl/xdl_iterate.c \
    ../../third_party/xdl/xdl_lzma.c

NOOK_JAVA_ROUTER_SRC := \
    ../../src/java_hook/router/arm64_writer.c \
    ../../src/java_hook/router/arm64_relocator.c \
    ../../src/java_hook/router/hook_engine.c \
    ../../src/java_hook/router/hook_engine_mem.c \
    ../../src/java_hook/router/hook_engine_inline.c \
    ../../src/java_hook/router/hook_engine_redir.c \
    ../../src/java_hook/router/hook_engine_art.c

NOOK_AGENT_RUNTIME_SRC := \
    ../../src/agent_runtime/js_runtime.cpp \
    ../../src/agent_runtime/script_registry.cpp \
    ../../src/agent_runtime/nook_native_js_bridge.cpp \
    ../../src/agent_runtime/nook_java_js_bridge.cpp \
    ../../src/agent_runtime/nook_script_runtime_bridge.cpp \
    $(NOOK_QUICKJS_SRC)

NOOK_RUNTIME_SRC := \
    ../../src/framework/Nook.cpp \
    ../../src/framework/NookComm.cpp \
    ../../src/framework/NookCommInternal.cpp \
    ../../src/framework/NookZygoteSpawn.cpp \
    ../../src/framework/nook_agent_runtime.cpp \
    ../../src/framework/nook_agent_init_policy.cpp \
    ../../src/framework/nook_zygote_control.cpp \
    $(NOOK_AGENT_RUNTIME_SRC) \
    ../../src/framework/NookJavaHook.cpp \
    ../../src/framework/NookJavaHookPayload.cpp \
    $(NOOK_NATIVE_HOOK_SRC) \
    $(NOOK_COMM_SRC) \
    ../../src/java_hook/JavaHook.cpp \
    ../../src/java_hook/JVM.cpp \
    ../../src/java_hook/deferred/java_hook_loader_resolver.cpp \
    ../../src/java_hook/deferred/pending_java_hook_registry.cpp \
    ../../src/java_hook/deferred/java_hook_class_observer.cpp \
    ../../src/common/JavaHookLog.cpp \
    ../../src/common/ArtStructDetector.cpp \
    $(NOOK_JAVA_ROUTER_SRC) \
    $(NOOK_XDL_SRC)

NOOK_AGENT_SRC := \
    ../../src/framework/Nook.cpp \
    ../../src/framework/NookComm.cpp \
    ../../src/framework/NookCommInternal.cpp \
    ../../src/framework/NookZygoteSpawn.cpp \
    ../../src/framework/nook_agent_runtime.cpp \
    ../../src/framework/nook_agent_init_policy.cpp \
    ../../src/framework/nook_zygote_control.cpp \
    $(NOOK_AGENT_RUNTIME_SRC) \
    ../../src/framework/NookJavaHook.cpp \
    ../../src/framework/NookJavaHookPayload.cpp \
    $(NOOK_NATIVE_HOOK_SRC) \
    $(NOOK_COMM_SRC) \
    ../../src/java_hook/JavaHook.cpp \
    ../../src/java_hook/JVM.cpp \
    ../../src/java_hook/deferred/java_hook_loader_resolver.cpp \
    ../../src/java_hook/deferred/pending_java_hook_registry.cpp \
    ../../src/java_hook/deferred/java_hook_class_observer.cpp \
    ../../src/common/JavaHookLog.cpp \
    ../../src/common/ArtStructDetector.cpp \
    $(NOOK_JAVA_ROUTER_SRC) \
    $(NOOK_XDL_SRC)

NOOK_ZYGOTE_HELPER_SRC := \
    ../../src/framework/Nook.cpp \
    ../../src/framework/NookComm.cpp \
    ../../src/framework/NookCommInternal.cpp \
    ../../src/framework/NookZygoteSpawn.cpp \
    ../../src/framework/nook_agent_runtime.cpp \
    ../../src/framework/nook_agent_init_policy.cpp \
    ../../src/framework/nook_zygote_control.cpp \
    ../../src/framework/NookJavaHook.cpp \
    ../../src/framework/NookJavaHookPayload.cpp \
    $(NOOK_NATIVE_HOOK_SRC) \
    $(NOOK_COMM_SRC) \
    ../../src/java_hook/JavaHook.cpp \
    ../../src/java_hook/JVM.cpp \
    ../../src/java_hook/deferred/java_hook_loader_resolver.cpp \
    ../../src/java_hook/deferred/pending_java_hook_registry.cpp \
    ../../src/java_hook/deferred/java_hook_class_observer.cpp \
    ../../src/common/JavaHookLog.cpp \
    ../../src/common/ArtStructDetector.cpp \
    $(NOOK_JAVA_ROUTER_SRC) \
    $(NOOK_XDL_SRC)

NOOK_SERVER_SRC := \
    ../../server/embedded_blob_defs.cpp \
    ../../server/server_main.cpp \
    ../../server/server_handlers.cpp \
    ../../server/server_runtime.cpp \
    ../../server/session_registry.cpp \
    ../../server/spawn_controller.cpp \
    ../../server/zygote_control_rpc.cpp \
    ../../server/process_manager.cpp \
    ../../server/ninjector_spawn_injector.cpp \
    ../../server/ninjector_compat.cpp \
    ../../server/symbi_injector_shim.cpp \
    ../../src/communication/handler/message_dispatcher.cpp \
    ../../src/communication/io/io_loop.cpp \
    ../../src/communication/protocol/frame.cpp \
    ../../src/communication/protocol/messages.cpp \
    ../../src/communication/protocol/tlv.cpp \
    ../../src/communication/session/session.cpp \
    ../../src/communication/session/session_manager.cpp \
    ../../src/communication/transport/path_utils.cpp \
    ../../src/communication/transport/spawn_marker.cpp \
    ../../src/communication/transport/transport.cpp \
    ../../src/communication/transport/tcp_transport.cpp \
    ../../src/communication/transport/unix_transport.cpp

NOOK_NCORE_FALLBACK_SRC := \
    ../../server/embedded_agent_blob_defs.cpp \
    ../../server/ncore_fallback.cpp \
    ../../src/native_hook/core/module_info.cpp \
    ../../src/native_hook/core/module_match.cpp \
    ../../src/native_hook/core/native_hook_symbol_resolver.cpp \
    ../../src/native_hook/core/runtime_patch.cpp \
    ../../src/native_hook/inline_hook/inline_hook_impl.cpp \
    ../../src/native_hook/inline_hook/inline_hook_record.cpp \
    ../../src/native_hook/inline_hook/trampoline_allocator.cpp \
    ../../src/native_hook/inline_hook/arm64_instruction_relocator.cpp \
    ../../src/native_hook/plt_hook/elf_hash.cpp \
    ../../src/native_hook/plt_hook/elf_reader.cpp \
    ../../src/native_hook/plt_hook/elfio_image_parser.cpp \
    $(NOOK_XDL_SRC)

include $(CLEAR_VARS)

LOCAL_MODULE := nook

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)
LOCAL_SRC_FILES := $(NOOK_RUNTIME_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_agent
LOCAL_MODULE_FILENAME := libnook-agent

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)
LOCAL_SRC_FILES := $(NOOK_AGENT_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_STATIC_LIBRARIES := c++_static
LOCAL_LDFLAGS := -Wl,--exclude-libs,ALL
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_zygote_helper
LOCAL_MODULE_FILENAME := libnook-zygote-helper

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)
LOCAL_SRC_FILES := $(NOOK_ZYGOTE_HELPER_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS) -DNOOK_ZYGOTE_HELPER_ONLY=1
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_STATIC_LIBRARIES := c++_static
LOCAL_LDFLAGS := -Wl,--exclude-libs,ALL
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_agent_message_smoke

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)
LOCAL_SRC_FILES := \
    ../../examples/communication/nook_agent_message_smoke.cpp \
    $(NOOK_AGENT_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_agent_post_echo

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)
LOCAL_SRC_FILES := \
    ../../examples/communication/nook_agent_post_echo.cpp \
    $(NOOK_AGENT_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_agent_script_smoke

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)
LOCAL_SRC_FILES := \
    ../../examples/communication/nook_agent_script_smoke.cpp \
    $(NOOK_AGENT_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_ncore
LOCAL_MODULE_FILENAME := libncore

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)
LOCAL_SRC_FILES := $(NOOK_NCORE_FALLBACK_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_server
LOCAL_MODULE_FILENAME := nook-server

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)
LOCAL_C_INCLUDES += $(ROOT_PATH)/server

LOCAL_SRC_FILES := $(NOOK_SERVER_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_inline_observer_probe

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)

LOCAL_SRC_FILES := \
    ../../examples/native_hook/common/nook_inline_observer_probe.cpp

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_adwall_loadad_block

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)

LOCAL_SRC_FILES := \
    ../../examples/java_hook/nook_adwall_loadad_block.cpp \
    $(NOOK_RUNTIME_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_adwall_loadad_block_macro

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)

LOCAL_SRC_FILES := \
    ../../examples/java_hook/nook_adwall_loadad_block_macro.cpp \
    $(NOOK_RUNTIME_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_java_test_replace_num_macro

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)

LOCAL_SRC_FILES := \
    ../../examples/java_hook/nook_java_test_replace_num_macro.cpp \
    $(NOOK_RUNTIME_SRC)

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_java_hook_example

LOCAL_C_INCLUDES := $(ROOT_PATH)/include
LOCAL_C_INCLUDES += $(ROOT_PATH)/src/java_hook

LOCAL_SRC_FILES := ../../examples/java_hook/nook_java_hook_example.cpp

LOCAL_CPPFLAGS := -std=c++17 -fPIC
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl
LOCAL_SHARED_LIBRARIES := nook

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_native_strcmp_test

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)

LOCAL_SRC_FILES := \
    ../../examples/native_hook/nook_native_strcmp_test/payload.cpp \
    ../../examples/native_hook/nook_native_strcmp_test/strcmp_replacement.cpp

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_native_inline_test

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)

LOCAL_SRC_FILES := \
    ../../examples/native_hook/nook_native_inline_test/payload.cpp \
    ../../examples/native_hook/nook_native_inline_test/target_replacement.cpp

LOCAL_CPPFLAGS := $(NOOK_COMMON_CPPFLAGS)
LOCAL_CFLAGS := $(NOOK_COMMON_CFLAGS)
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := nook_native_verify_password_inline_test

LOCAL_C_INCLUDES := $(NOOK_COMMON_INCLUDES)

LOCAL_SRC_FILES := \
    ../../examples/native_hook/nook_native_verify_password_inline_test/payload.cpp \
    ../../examples/native_hook/nook_native_verify_password_inline_test/verify_password_replacement.cpp

LOCAL_CPPFLAGS := -std=c++17 -fPIC
LOCAL_CFLAGS := -std=c11 -fPIC
LOCAL_CPP_FEATURES := exceptions
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)

