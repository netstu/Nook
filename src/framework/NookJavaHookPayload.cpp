#include "nook/NookJavaHookMacros.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <unistd.h>

#if defined(__ANDROID__)
#include "../java_hook/JVM.h"
#endif

static constexpr unsigned int kMaxDecls = 64;
static const NookJavaHookDecl* g_java_decls[kMaxDecls] = {};
static unsigned int g_java_decl_count = 0;
static std::atomic<bool> g_started{false};
static std::atomic<bool> g_initialized{false};

extern "C" void NookPayloadRegisterJavaHook(const NookJavaHookDecl* decl) {
    if (decl == nullptr) {
        return;
    }

    unsigned int index = __atomic_fetch_add(&g_java_decl_count, 1u, __ATOMIC_RELAXED);
    if (index >= kMaxDecls) {
        return;
    }
    g_java_decls[index] = decl;
}

extern "C" __attribute__((weak)) const NookJavaHookPayloadConfig* NookPayloadGetConfig(void) {
    static const NookJavaHookPayloadConfig config = {"NOOK", 5, 200};
    return &config;
}

extern "C" void NookPayloadSetJavaVM(JavaVM* vm) {
#if defined(__ANDROID__)
    JavaEnv::SetJavaVM(vm);
#else
    (void)vm;
#endif
}

static void NookPayloadInstallThread(void) {
    if (g_initialized.exchange(true)) {
        return;
    }

    const NookJavaHookPayloadConfig* config = NookPayloadGetConfig();
    if (NookJavaHookInitialize() != NOOK_STATUS_OK) {
        return;
    }

    int retry_count = config != nullptr ? config->retry_count : 5;
    int retry_interval_ms = config != nullptr ? config->retry_interval_ms : 200;

    for (int attempt = 0; attempt < retry_count; ++attempt) {
        unsigned int total = __atomic_load_n(&g_java_decl_count, __ATOMIC_ACQUIRE);
        if (total > kMaxDecls) {
            total = kMaxDecls;
        }

        unsigned int installed = 0;
        for (unsigned int index = 0; index < total; ++index) {
            const NookJavaHookDecl* decl = g_java_decls[index];
            if (decl == nullptr) {
                continue;
            }

            int hook_id = NookJavaHookHook(
                decl->class_name,
                decl->method_name,
                decl->signature,
                decl->is_static,
                decl->callback);
            if (hook_id >= 0) {
                installed++;
            }
        }

        if (installed == total) {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms));
    }
}

extern "C" void NookPayloadStart(void) {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) {
        return;
    }

    std::thread([]() {
#if !defined(_WIN32)
        usleep(100000);
#endif
        NookPayloadInstallThread();
    }).detach();
}

extern "C" void NookPayloadStop(void) {
    NookJavaHookUnhookAll();
}
