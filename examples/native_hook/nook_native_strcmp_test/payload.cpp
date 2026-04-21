#include "strcmp_replacement.h"

#include "../common/nook_runtime_loader.h"

#include <android/log.h>
#include <pthread.h>

#include <atomic>
#include <unistd.h>

namespace {

constexpr char kTag[] = "NookStrcmpTest";
constexpr char kTargetModule[] = "libnative-lib.so";
constexpr char kTargetSymbol[] = "strcmp";
constexpr int kRetryCount = 300;
constexpr int kRetryDelayMs = 200;

std::atomic<bool> g_started{false};
std::atomic<bool> g_hook_installed{false};
int (*g_original_strcmp)(const char*, const char*) = nullptr;

void sleep_ms(int ms) {
    if (ms <= 0) {
        return;
    }
    usleep(static_cast<useconds_t>(ms) * 1000);
}

int hooked_strcmp(const char* a, const char* b) {
    __android_log_print(ANDROID_LOG_INFO,
                        kTag,
                        "hooked strcmp: a=%s b=%s",
                        a ? a : "<null>",
                        b ? b : "<null>");
    return NookTestAlwaysEqualStrcmp(a, b);
}

void install_hook_worker() {
    NookExampleRuntimeLoader::NookPltApi api = {};
    if (!NookExampleRuntimeLoader::ResolveNookPltApi(kTag, &api)) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "ResolveNookPltApi failed");
        return;
    }

    const NookStatus init_status = api.initialize();
    __android_log_print(ANDROID_LOG_INFO, kTag, "NookPltHookInitialize status=%d", init_status);
    if (init_status != NOOK_STATUS_OK) {
        return;
    }

    for (int attempt = 1; attempt <= kRetryCount && !g_hook_installed.load(); ++attempt) {
        void* original = nullptr;
        const NookStatus hook_status = api.hook_symbol(kTargetModule,
                                                       kTargetSymbol,
                                                       reinterpret_cast<void*>(hooked_strcmp),
                                                       &original);
        __android_log_print(ANDROID_LOG_INFO,
                            kTag,
                            "NookPltHookSymbol attempt=%d status=%d original=%p",
                            attempt,
                            hook_status,
                            original);
        if (hook_status == NOOK_STATUS_OK) {
            g_original_strcmp = reinterpret_cast<int (*)(const char*, const char*)>(original);
            g_hook_installed.store(true);
            __android_log_print(ANDROID_LOG_INFO, kTag, "All hooks installed");
            return;
        }
        __android_log_print(ANDROID_LOG_DEBUG,
                            kTag,
                            "Retry %d/%d (status=%d)",
                            attempt,
                            kRetryCount,
                            hook_status);
        sleep_ms(kRetryDelayMs);
    }

    __android_log_print(ANDROID_LOG_ERROR, kTag, "Failed to install strcmp hook after retries");
}

void* install_hook_thread_main(void*) {
    __android_log_print(ANDROID_LOG_INFO,
                        kTag,
                        "Start install thread (retry=%d interval=%dms)",
                        kRetryCount,
                        kRetryDelayMs);
    install_hook_worker();
    return nullptr;
}

void start_install_once() {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) {
        return;
    }

    pthread_t thread = 0;
    const int create_result = pthread_create(&thread, nullptr, install_hook_thread_main, nullptr);
    if (create_result != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "pthread_create failed: %d", create_result);
        return;
    }
    pthread_detach(thread);
}

}  // namespace

__attribute__((constructor(200)))
static void on_library_loaded() {
    start_install_once();
}
