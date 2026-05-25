#include "java_hook_class_observer.h"

#include "../../framework/NookJavaHookInternal.h"
#include "pending_java_hook_registry.h"

#include <android/log.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <pthread.h>

namespace {

constexpr const char* kObserverTag = "NookJavaObserver";
constexpr int kRetryIntervalMs = 100;

std::atomic<int> g_install_state{0};
std::once_flag g_suppression_key_once;
pthread_key_t g_suppression_key = 0;
std::atomic<bool> g_suppression_key_ready{false};
std::mutex g_retry_mutex;
std::condition_variable g_retry_cv;
std::thread g_retry_worker;
bool g_retry_worker_started = false;
bool g_retry_requested = false;
bool g_retry_stop = false;

void InitializeSuppressionKey() {
    if (pthread_key_create(&g_suppression_key, nullptr) == 0) {
        g_suppression_key_ready.store(true, std::memory_order_release);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, kObserverTag, "pthread_key_create failed");
    }
}

bool EnsureSuppressionKey() {
    std::call_once(g_suppression_key_once, InitializeSuppressionKey);
    return g_suppression_key_ready.load(std::memory_order_acquire);
}

int GetSuppressionDepth() {
    if (!EnsureSuppressionKey()) {
        return 0;
    }
    return static_cast<int>(reinterpret_cast<intptr_t>(pthread_getspecific(g_suppression_key)));
}

void SetSuppressionDepth(int depth) {
    if (!EnsureSuppressionKey()) {
        return;
    }
    if (pthread_setspecific(g_suppression_key, reinterpret_cast<void*>(static_cast<intptr_t>(depth))) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kObserverTag, "pthread_setspecific failed");
    }
}

void RetryWorkerMain() {
    std::unique_lock<std::mutex> lock(g_retry_mutex);
    for (;;) {
        g_retry_cv.wait(lock, []() { return g_retry_requested || g_retry_stop; });
        if (g_retry_stop) {
            return;
        }

        g_retry_requested = false;
        lock.unlock();

        for (;;) {
            {
                JavaHookClassObserver::ScopedSuppression suppression;
                nook::java_hook_internal::ProcessPendingRequests(nullptr);
            }

            if (!PendingJavaHookRegistry::Instance().HasAnyPending()) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryIntervalMs));

            if (g_retry_stop) {
                return;
            }
        }

        lock.lock();
    }
}

void EnsureRetryWorkerStarted() {
    std::lock_guard<std::mutex> lock(g_retry_mutex);
    if (g_retry_worker_started) {
        return;
    }

    g_retry_stop = false;
    g_retry_requested = false;
    g_retry_worker = std::thread(RetryWorkerMain);
    g_retry_worker_started = true;
}

void StopRetryWorker() {
    std::unique_lock<std::mutex> lock(g_retry_mutex);
    if (!g_retry_worker_started) {
        return;
    }

    g_retry_stop = true;
    g_retry_requested = true;
    lock.unlock();
    g_retry_cv.notify_all();

    if (g_retry_worker.joinable()) {
        g_retry_worker.join();
    }

    lock.lock();
    g_retry_worker_started = false;
    g_retry_stop = false;
    g_retry_requested = false;
}

}

namespace JavaHookClassObserver {

ScopedSuppression::ScopedSuppression() {
    SetSuppressionDepth(GetSuppressionDepth() + 1);
}

ScopedSuppression::~ScopedSuppression() {
    const int depth = GetSuppressionDepth();
    if (depth > 0) {
        SetSuppressionDepth(depth - 1);
    }
}

bool IsSuppressed() {
    return GetSuppressionDepth() > 0;
}

bool EnsureInstalled() {
    int state = g_install_state.load(std::memory_order_acquire);
    if (state == 2) {
        return true;
    }
    if (state == 1) {
        return false;
    }

    int expected = 0;
    if (!g_install_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        return g_install_state.load(std::memory_order_acquire) == 2;
    }

    EnsureRetryWorkerStarted();
    g_install_state.store(2, std::memory_order_release);
    __android_log_print(ANDROID_LOG_INFO, kObserverTag, "deferred retry worker ready");
    return true;
}

void SchedulePendingRetry(const char* class_name, int delay_ms) {
    (void)delay_ms;

    const auto request_ids = PendingJavaHookRegistry::Instance().MarkRetryScheduled(class_name);
    if (request_ids.empty() && !PendingJavaHookRegistry::Instance().HasAnyPending()) {
        return;
    }

    EnsureRetryWorkerStarted();

    {
        std::lock_guard<std::mutex> lock(g_retry_mutex);
        g_retry_requested = true;
    }
    g_retry_cv.notify_all();

    if (!request_ids.empty()) {
        PendingJavaHookRegistry::Instance().ClearRetryScheduled(request_ids);
    }
}

void Reset() {
    StopRetryWorker();
    g_install_state.store(0, std::memory_order_release);
}

void ResetInheritedStateForChild() {
    new (&g_retry_mutex) std::mutex();
    new (&g_retry_cv) std::condition_variable();
    new (&g_retry_worker) std::thread();
    g_retry_worker_started = false;
    g_retry_requested = false;
    g_retry_stop = false;
    g_install_state.store(0, std::memory_order_release);
}

}
