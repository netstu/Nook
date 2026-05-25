#include "nook/Nook.h"
#include "nook/NookComm.h"
#include "nook/NookInlineHook.h"
#include "nook/NookJavaHook.h"
#include "nook/NookPltHook.h"
#include "nook/NookNativeHook.h"

namespace {

void MessageCallback(uint32_t, const char*, const char*, const uint8_t*, size_t) {}

NookStatus ScriptCreateCallback(const char*, const char*, uint32_t* script_id) {
    if (script_id != nullptr) {
        *script_id = 1;
    }
    return NOOK_STATUS_OK;
}

NookStatus ScriptLoadCallback(uint32_t) {
    return NOOK_STATUS_OK;
}

}  // namespace

int main() {
    const char* version = NookGetVersion();
    NookStatus comm_status = NookCommInitialize();
    // Contract note: on Android this may now block until the server releases
    // a real target-side spawn gate, instead of returning immediately.
    NookStatus comm_resume_gate_status = NookCommWaitForResumeIfSpawned();
    NookStatus comm_message_status = NookCommSetMessageCallback(MessageCallback);
    NookStatus comm_create_status = NookCommSetScriptCreateCallback(ScriptCreateCallback);
    NookStatus comm_load_status = NookCommSetScriptLoadCallback(ScriptLoadCallback);
    NookStatus java_status = NookJavaHookInitialize();
    NookStatus native_status = NookNativeHookInitialize();
    NookStatus plt_status = NookPltHookInitialize();
    NookStatus inline_status = NookInlineHookInitialize();
    int java_hook_with_loader = NookJavaHookHookWithLoader(
        nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    int java_hook_deferred_with_loader = NookJavaHookHookDeferredWithLoader(
        nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    jclass java_find_class_with_loader = NookJavaHookFindClassWithLoader(nullptr, nullptr, nullptr);
    void* original = nullptr;
    void* hook_handle = nullptr;
    NookStatus inline_deferred_status =
            NookInlineHookSymbolDeferred("libtarget.so",
                                         "target_symbol",
                                         reinterpret_cast<void*>(0x1234),
                                         &original,
                                         &hook_handle);
    return (version != nullptr &&
            comm_status <= 0 &&
            comm_resume_gate_status <= 0 &&
            comm_message_status <= 0 &&
            comm_create_status <= 0 &&
            comm_load_status <= 0 &&
            java_status <= 0 &&
            native_status <= 0 &&
            plt_status <= 0 &&
            inline_status <= 0 &&
            java_hook_with_loader <= 0 &&
            java_hook_deferred_with_loader <= 0 &&
            java_find_class_with_loader == nullptr &&
            inline_deferred_status <= 0) ? 0 : 1;
}
