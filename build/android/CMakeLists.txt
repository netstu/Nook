cmake_minimum_required(VERSION 3.18.1)
project(nook)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/java_hook
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/elfio
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/xdl
)

set(NOOK_NATIVE_HOOK_SRC
    ../../src/framework/NookPltHook.cpp
    ../../src/framework/NookInlineHook.cpp
    ../../src/framework/NookNativeHook.cpp
    ../../src/native_hook/core/native_hook_symbol_resolver.cpp
    ../../src/native_hook/core/native_hook_dispatcher.cpp
    ../../src/native_hook/core/module_info.cpp
    ../../src/native_hook/core/module_match.cpp
    ../../src/native_hook/core/runtime_patch.cpp
    ../../src/native_hook/inline_hook/inline_hook_impl.cpp
    ../../src/native_hook/inline_hook/pending_inline_hook_registry.cpp
    ../../src/native_hook/inline_hook/inline_hook_module_observer.cpp
    ../../src/native_hook/inline_hook/inline_hook_record.cpp
    ../../src/native_hook/inline_hook/trampoline_allocator.cpp
    ../../src/native_hook/inline_hook/arm64_instruction_relocator.cpp
    ../../src/native_hook/plt_hook/elf_hash.cpp
    ../../src/native_hook/plt_hook/elf_reader.cpp
    ../../src/native_hook/plt_hook/elfio_image_parser.cpp
    ../../src/native_hook/plt_hook/plt_hook_impl.cpp
)

set(NOOK_RUNTIME_SRC
    ../../src/framework/Nook.cpp
    ../../src/framework/NookJavaHook.cpp
    ../../src/framework/NookJavaHookPayload.cpp
    ${NOOK_NATIVE_HOOK_SRC}
    ../../src/java_hook/JavaHook.cpp
    ../../src/java_hook/JVM.cpp
    ../../src/java_hook/deferred/java_hook_loader_resolver.cpp
    ../../src/java_hook/deferred/pending_java_hook_registry.cpp
    ../../src/java_hook/deferred/java_hook_class_observer.cpp
    ../../src/common/JavaHookLog.cpp
    ../../src/common/ArtStructDetector.cpp
    ../../third_party/xdl/xdl.c
    ../../third_party/xdl/xdl_util.c
    ../../third_party/xdl/xdl_linker.c
    ../../third_party/xdl/xdl_iterate.c
    ../../third_party/xdl/xdl_lzma.c
)

add_library(nook SHARED
    ${NOOK_RUNTIME_SRC}
)

target_link_libraries(nook
    log
    dl
)

add_library(nook_inline_observer_probe SHARED
    ../../examples/native_hook/common/nook_inline_observer_probe.cpp
)

target_link_libraries(nook_inline_observer_probe
    log
    dl
)

add_library(nook_java_hook_example SHARED
    ../../examples/java_hook/nook_java_hook_example.cpp
)

target_link_libraries(nook_java_hook_example
    nook
    log
    dl
)

add_library(nook_native_strcmp_test SHARED
    ../../examples/native_hook/nook_native_strcmp_test/payload.cpp
    ../../examples/native_hook/nook_native_strcmp_test/strcmp_replacement.cpp
)

target_link_libraries(nook_native_strcmp_test
    log
    dl
)

add_library(nook_native_inline_test SHARED
    ../../examples/native_hook/nook_native_inline_test/payload.cpp
    ../../examples/native_hook/nook_native_inline_test/target_replacement.cpp
)

target_link_libraries(nook_native_inline_test
    log
    dl
)

add_library(nook_native_verify_password_inline_test SHARED
    ../../examples/native_hook/nook_native_verify_password_inline_test/payload.cpp
    ../../examples/native_hook/nook_native_verify_password_inline_test/verify_password_replacement.cpp
)

target_link_libraries(nook_native_verify_password_inline_test
    log
    dl
)
