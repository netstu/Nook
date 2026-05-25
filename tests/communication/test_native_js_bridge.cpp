#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>

#include "nook/NookInlineHook.h"
#include "agent_runtime/nook_native_js_bridge.h"

using namespace nook::agent_runtime;

namespace {

struct ResolveLoadedSymbolCallCapture {
    std::string module_name;
    std::string symbol_name;
    void** target_address_out = nullptr;
};

ResolveLoadedSymbolCallCapture& GetResolveLoadedSymbolCallCapture() {
    static ResolveLoadedSymbolCallCapture capture;
    return capture;
}

struct InlineHookAddressCallCapture {
    void* target_address = nullptr;
    void* replacement = nullptr;
    void** original_out = nullptr;
    void** hook_handle_out = nullptr;
};

InlineHookAddressCallCapture& GetInlineHookAddressCallCapture() {
    static InlineHookAddressCallCapture capture;
    return capture;
}

struct InlineHookUnhookCallCapture {
    int call_count = 0;
    void* hook_handle = nullptr;
};

InlineHookUnhookCallCapture& GetInlineHookUnhookCallCapture() {
    static InlineHookUnhookCallCapture capture;
    return capture;
}

int& GetNativeJsHookEventNotifyCount() {
    static int count = 0;
    return count;
}

int& GetEnsureObserverAsyncCallCount() {
    static int count = 0;
    return count;
}

NookStatus& GetEnsureObserverAsyncStatus() {
    static NookStatus status = NOOK_STATUS_OK;
    return status;
}

bool FakeInlineInstaller(const NativeJsHookRequest& request,
                         void** hook_handle,
                         std::string* error_message) {
    (void)error_message;
    if (request.type != "inline") {
        return false;
    }
    *hook_handle = reinterpret_cast<void*>(0x1234u);
    return true;
}

bool FailingInlineInstaller(const NativeJsHookRequest& request,
                            void** hook_handle,
                            std::string* error_message) {
    (void)request;
    (void)hook_handle;
    if (error_message != nullptr) {
        *error_message = "fake install failed";
    }
    return false;
}

bool FakeResolveLoadedSymbolAddress(const char* module_name,
                                    const char* symbol_name,
                                    void** target_address) {
    ResolveLoadedSymbolCallCapture& capture = GetResolveLoadedSymbolCallCapture();
    capture.module_name = module_name != nullptr ? module_name : "";
    capture.symbol_name = symbol_name != nullptr ? symbol_name : "";
    capture.target_address_out = target_address;
    if (target_address != nullptr) {
        *target_address = reinterpret_cast<void*>(0x10000000u);
    }
    return true;
}

bool FailingResolveLoadedSymbolAddress(const char* module_name,
                                       const char* symbol_name,
                                       void** target_address) {
    ResolveLoadedSymbolCallCapture& capture = GetResolveLoadedSymbolCallCapture();
    capture.module_name = module_name != nullptr ? module_name : "";
    capture.symbol_name = symbol_name != nullptr ? symbol_name : "";
    capture.target_address_out = target_address;
    if (target_address != nullptr) {
        *target_address = nullptr;
    }
    return false;
}

bool FakeResolveSymbolAddressFallback(const char* module_name,
                                      const char* symbol_name,
                                      void** target_address) {
    ResolveLoadedSymbolCallCapture& capture = GetResolveLoadedSymbolCallCapture();
    capture.module_name = module_name != nullptr ? module_name : "";
    capture.symbol_name = symbol_name != nullptr ? symbol_name : "";
    capture.target_address_out = target_address;
    if (target_address == nullptr) {
        return false;
    }
    *target_address = nullptr;
    if (std::strcmp(module_name, "libc.so") == 0 &&
        std::strcmp(symbol_name, "strcmp") == 0) {
        *target_address = reinterpret_cast<void*>(0x70000000u);
        return true;
    }
    return false;
}

bool UnsafeInlineHookSymbolChecker(const char* module_name,
                                   const char* symbol_name,
                                   void* target_address) {
    (void)module_name;
    (void)symbol_name;
    return target_address != reinterpret_cast<void*>(0x10000000u);
}

NookStatus FakeInlineHookAddressInvoker(void* target_address,
                                        void* replacement,
                                        void** original,
                                        void** hook_handle) {
    InlineHookAddressCallCapture& capture = GetInlineHookAddressCallCapture();
    capture.target_address = target_address;
    capture.replacement = replacement;
    capture.original_out = original;
    capture.hook_handle_out = hook_handle;
    auto fake_original = +[](uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t,
                             uint64_t) -> uint64_t {
        return 0x4242u;
    };
    if (original != nullptr) {
        *original = reinterpret_cast<void*>(fake_original);
    }
    if (hook_handle != nullptr) {
        *hook_handle = reinterpret_cast<void*>(0x9abcu);
    }
    return NOOK_STATUS_OK;
}

NookStatus FakeInlineHookUnhookInvoker(void* hook_handle) {
    InlineHookUnhookCallCapture& capture = GetInlineHookUnhookCallCapture();
    ++capture.call_count;
    capture.hook_handle = hook_handle;
    return NOOK_STATUS_OK;
}

uint64_t ReentrantOriginalInvokedDuringGuard(uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t);

uint64_t& GetReentrantOriginalCallCount() {
    static uint64_t count = 0u;
    return count;
}

uint64_t ReentrantOriginalInvokedDuringGuard(uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t,
                                             uint64_t) {
    ++GetReentrantOriginalCallCount();
    return 0x5151u;
}

NookStatus ReentrantInlineHookAddressInvoker(void* target_address,
                                             void* replacement,
                                             void** original,
                                             void** hook_handle) {
    InlineHookAddressCallCapture& capture = GetInlineHookAddressCallCapture();
    capture.target_address = target_address;
    capture.replacement = replacement;
    capture.original_out = original;
    capture.hook_handle_out = hook_handle;
    if (original != nullptr) {
        *original = reinterpret_cast<void*>(&ReentrantOriginalInvokedDuringGuard);
    }
    if (hook_handle != nullptr) {
        *hook_handle = reinterpret_cast<void*>(0x4567u);
    }
    return NOOK_STATUS_OK;
}

void FakeNativeJsHookEventNotifier() {
    ++GetNativeJsHookEventNotifyCount();
}

NookStatus FakeEnsureInlineHookModuleObserverAsync() {
    ++GetEnsureObserverAsyncCallCount();
    return GetEnsureObserverAsyncStatus();
}

void TestInstallNativeJsHookAssignsIncrementingHookIds() {
    ResetNativeJsHookRegistryForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;

    NativeJsHookRequest first = {};
    first.type = "inline";
    first.module_name = "libdemo.so";
    first.symbol_name = "target_one";

    NativeJsHookRecord first_record = {};
    std::string error_message;
    assert(InstallNativeJsHook(first, dependencies, &first_record, &error_message));
    assert(first_record.hook_id == 1u);
    assert(first_record.module_name == "libdemo.so");
    assert(first_record.symbol_name == "target_one");
    assert(first_record.hook_handle == reinterpret_cast<void*>(0x1234u));

    NativeJsHookRequest second = {};
    second.type = "inline";
    second.module_name = "libdemo.so";
    second.symbol_name = "target_two";

    NativeJsHookRecord second_record = {};
    assert(InstallNativeJsHook(second, dependencies, &second_record, &error_message));
    assert(second_record.hook_id == 2u);

    NativeJsHookRecord stored = {};
    assert(GetNativeJsHookRecordForTesting(1u, &stored));
    assert(stored.symbol_name == "target_one");
    assert(stored.blocking);
    assert(GetInstalledNativeJsHookCountForTesting() == 2u);
}

void TestInstallNativeJsHookRejectsUnsupportedTypeAndInstallerFailure() {
    ResetNativeJsHookRegistryForTesting();

    NativeJsHookInstallerDependencies dependencies = {};
    dependencies.install_inline_hook = &FakeInlineInstaller;

    NativeJsHookRequest unsupported = {};
    unsupported.type = "plt";
    unsupported.module_name = "libdemo.so";
    unsupported.symbol_name = "target";

    NativeJsHookRecord record = {};
    std::string error_message;
    assert(!InstallNativeJsHook(unsupported, dependencies, &record, &error_message));
    assert(error_message.find("not implemented yet") != std::string::npos);

    dependencies.install_inline_hook = &FailingInlineInstaller;

    NativeJsHookRequest failing = {};
    failing.type = "inline";
    failing.module_name = "libdemo.so";
    failing.symbol_name = "target";

    error_message.clear();
    assert(!InstallNativeJsHook(failing, dependencies, &record, &error_message));
    assert(error_message.find("fake install failed") != std::string::npos);
}

void TestInstallNativeJsHookUsesDefaultInlineHookAdapterThroughResolvedAddress() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetInlineHookAddressCallCapture() = {};
    GetInlineHookUnhookCallCapture() = {};
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libdemo.so";
    request.symbol_name = "target";

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.hook_id == 1u);
    assert(record.hook_handle == reinterpret_cast<void*>(0x9abcu));

    const ResolveLoadedSymbolCallCapture& resolve_capture = GetResolveLoadedSymbolCallCapture();
    assert(resolve_capture.module_name == "libdemo.so");
    assert(resolve_capture.symbol_name == "target");
    assert(resolve_capture.target_address_out != nullptr);

    const InlineHookAddressCallCapture& capture = GetInlineHookAddressCallCapture();
    assert(capture.target_address == reinterpret_cast<void*>(0x10000000u));
    assert(capture.replacement != nullptr);
    assert(capture.original_out != nullptr);
    assert(capture.hook_handle_out != nullptr);

    using ReplacementEntry = uint64_t (*)(uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t);
    auto replacement = reinterpret_cast<ReplacementEntry>(capture.replacement);
    const uint64_t return_value = replacement(0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u);
    assert(return_value == 0x4242u);

    HookEvent event = {};
    assert(TryDequeueNativeJsHookEvent(&event));
    assert(event.hook_id == 1u);
    assert(event.phase == HookEventPhase::kEnter);
    assert(event.argument_count == 8u);
    assert(event.argument_values[0] == 0x11u);
    assert(event.argument_values[7] == 0x88u);

    event = {};
    assert(TryDequeueNativeJsHookEvent(&event));
    assert(event.hook_id == 1u);
    assert(event.phase == HookEventPhase::kLeave);
    assert(event.return_value == 0x4242u);

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookUnhookInvokerForTesting();
}

void TestInstallNativeJsHookUsesDirectTargetAddress() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetInlineHookAddressCallCapture() = {};
    GetInlineHookUnhookCallCapture() = {};
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.has_target_address = true;
    request.target_address = 0x20000000u;

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.hook_id == 1u);
    assert(record.has_target_address);
    assert(record.target_address == 0x20000000u);
    assert(record.hook_handle == reinterpret_cast<void*>(0x9abcu));

    const ResolveLoadedSymbolCallCapture& resolve_capture = GetResolveLoadedSymbolCallCapture();
    assert(resolve_capture.module_name.empty());
    assert(resolve_capture.symbol_name.empty());

    const InlineHookAddressCallCapture& capture = GetInlineHookAddressCallCapture();
    assert(capture.target_address == reinterpret_cast<void*>(0x20000000u));
    assert(capture.replacement != nullptr);

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookUnhookInvokerForTesting();
}

void TestInstallNativeJsHookRegistersDeferredHookWhenModuleIsMissing() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    ResetNativeJsHookStatusEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetEnsureObserverAsyncCallCount() = 0;
    GetEnsureObserverAsyncStatus() = NOOK_STATUS_OK;
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(
            &FakeEnsureInlineHookModuleObserverAsync);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libnative-lib.so";
    request.symbol_name = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.hook_id == 1u);
    assert(record.hook_handle == nullptr);
    assert(GetInstalledNativeJsHookCountForTesting() == 1u);
    assert(GetPendingNativeJsHookCountForTesting() == 1u);

    NativeJsPendingHookRecord pending = {};
    assert(GetPendingNativeJsHookRecordForTesting(1u, &pending));
    assert(pending.hook_id == 1u);
    assert(pending.module_name == "libnative-lib.so");
    assert(pending.symbol_name == "Java_com_demo_target_LoginFragment_verifyPasswordNative");
    assert(!pending.installed);
    assert(GetEnsureObserverAsyncCallCount() == 1);

    NativeJsHookStatusEvent status_event = {};
    assert(TryDequeueNativeJsHookStatusEvent(&status_event));
    assert(status_event.hook_id == 1u);
    assert(status_event.state == NativeJsHookStatusState::kPending);
    assert(status_event.module_name == "libnative-lib.so");
    assert(status_event.symbol_name == "Java_com_demo_target_LoginFragment_verifyPasswordNative");
    assert(status_event.error_message.empty());

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting();
}

void TestInstallNativeJsHookFailsWhenDeferredObserverScheduleFails() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetEnsureObserverAsyncCallCount() = 0;
    GetEnsureObserverAsyncStatus() = NOOK_STATUS_INTERNAL_ERROR;
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(
            &FakeEnsureInlineHookModuleObserverAsync);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libnative-lib.so";
    request.symbol_name = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(!InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(error_message.find("inline hook module observer async schedule failed") !=
           std::string::npos);
    assert(GetEnsureObserverAsyncCallCount() == 1);
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetPendingNativeJsHookCountForTesting() == 0u);

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting();
}

void TestNotifyNativeJsHookModuleLoadedInstallsDeferredHook() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    ResetNativeJsHookStatusEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetInlineHookAddressCallCapture() = {};
    GetInlineHookUnhookCallCapture() = {};
    GetEnsureObserverAsyncCallCount() = 0;
    GetEnsureObserverAsyncStatus() = NOOK_STATUS_OK;
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);
    nook::agent_runtime::SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(
            &FakeEnsureInlineHookModuleObserverAsync);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libnative-lib.so";
    request.symbol_name = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.hook_handle == nullptr);
    assert(GetPendingNativeJsHookCountForTesting() == 1u);

    NativeJsHookStatusEvent status_event = {};
    assert(TryDequeueNativeJsHookStatusEvent(&status_event));
    assert(status_event.state == NativeJsHookStatusState::kPending);

    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);
    const size_t installed = NotifyNativeJsHookModuleLoaded("libnative-lib.so", &error_message);
    assert(installed == 1u);
    assert(GetPendingNativeJsHookCountForTesting() == 0u);

    NativeJsPendingHookRecord pending = {};
    assert(GetPendingNativeJsHookRecordForTesting(1u, &pending));
    assert(pending.installed);
    assert(pending.native_hook_handle == reinterpret_cast<void*>(0x9abcu));

    NativeJsHookRecord installed_record = {};
    assert(GetNativeJsHookRecordForTesting(1u, &installed_record));
    assert(installed_record.hook_handle == reinterpret_cast<void*>(0x9abcu));

    assert(TryDequeueNativeJsHookStatusEvent(&status_event));
    assert(status_event.hook_id == 1u);
    assert(status_event.state == NativeJsHookStatusState::kInstalled);
    assert(status_event.module_name == "libnative-lib.so");
    assert(status_event.symbol_name == "Java_com_demo_target_LoginFragment_verifyPasswordNative");
    assert(status_event.error_message.empty());

    const InlineHookAddressCallCapture& capture = GetInlineHookAddressCallCapture();
    assert(capture.target_address == reinterpret_cast<void*>(0x10000000u));
    assert(capture.replacement != nullptr);

    using ReplacementEntry = uint64_t (*)(uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t);
    auto replacement = reinterpret_cast<ReplacementEntry>(capture.replacement);
    const uint64_t return_value = replacement(0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u);
    assert(return_value == 0x4242u);

    HookEvent event = {};
    assert(TryDequeueNativeJsHookEvent(&event));
    assert(event.hook_id == 1u);
    assert(event.phase == HookEventPhase::kEnter);
    assert(TryDequeueNativeJsHookEvent(&event));
    assert(event.hook_id == 1u);
    assert(event.phase == HookEventPhase::kLeave);
    assert(event.return_value == 0x4242u);

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookUnhookInvokerForTesting();
    nook::agent_runtime::ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting();
}

void TestNotifyNativeJsHookModuleLoadedReportsDeferredInstallFailure() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    ResetNativeJsHookStatusEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetEnsureObserverAsyncCallCount() = 0;
    GetEnsureObserverAsyncStatus() = NOOK_STATUS_OK;
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(
            &FakeEnsureInlineHookModuleObserverAsync);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libnative-lib.so";
    request.symbol_name = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));

    NativeJsHookStatusEvent status_event = {};
    assert(TryDequeueNativeJsHookStatusEvent(&status_event));
    assert(status_event.state == NativeJsHookStatusState::kPending);

    const size_t installed = NotifyNativeJsHookModuleLoaded("libnative-lib.so", &error_message);
    assert(installed == 0u);
    assert(TryDequeueNativeJsHookStatusEvent(&status_event));
    assert(status_event.hook_id == 1u);
    assert(status_event.state == NativeJsHookStatusState::kFailed);
    assert(status_event.error_message.find("resolve") != std::string::npos);

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting();
}

void TestInstallNativeJsHookUsesFallbackResolverBeforeDeferring() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    ResetNativeJsHookStatusEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetInlineHookAddressCallCapture() = {};
    GetEnsureObserverAsyncCallCount() = 0;
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(
            &FailingResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsResolveSymbolAddressForTesting(
            &FakeResolveSymbolAddressFallback);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(
            &FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(
            &FakeEnsureInlineHookModuleObserverAsync);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libc.so";
    request.symbol_name = "strcmp";

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.hook_handle == reinterpret_cast<void*>(0x9abcu));
    assert(!record.deferred);
    assert(GetPendingNativeJsHookCountForTesting() == 0u);
    assert(GetEnsureObserverAsyncCallCount() == 0);

    const InlineHookAddressCallCapture& capture = GetInlineHookAddressCallCapture();
    assert(capture.target_address == reinterpret_cast<void*>(0x70000000u));

    NativeJsHookStatusEvent status_event = {};
    assert(!TryDequeueNativeJsHookStatusEvent(&status_event));

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsResolveSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting();
}

void TestNotifyNativeJsHookModuleLoadedUsesFallbackResolver() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    ResetNativeJsHookStatusEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetInlineHookAddressCallCapture() = {};
    GetEnsureObserverAsyncCallCount() = 0;
    GetEnsureObserverAsyncStatus() = NOOK_STATUS_OK;
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(
            &FailingResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(
            &FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(
            &FakeEnsureInlineHookModuleObserverAsync);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libc.so";
    request.symbol_name = "strcmp";

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.deferred);
    assert(GetPendingNativeJsHookCountForTesting() == 1u);

    NativeJsHookStatusEvent status_event = {};
    assert(TryDequeueNativeJsHookStatusEvent(&status_event));
    assert(status_event.state == NativeJsHookStatusState::kPending);

    nook::agent_runtime::SetNativeJsResolveSymbolAddressForTesting(
            &FakeResolveSymbolAddressFallback);

    const size_t installed = NotifyNativeJsHookModuleLoaded("/apex/com.android.runtime/lib64/bionic/libc.so",
                                                            &error_message);
    assert(installed == 1u);

    assert(TryDequeueNativeJsHookStatusEvent(&status_event));
    assert(status_event.hook_id == 1u);
    assert(status_event.state == NativeJsHookStatusState::kInstalled);
    assert(status_event.error_message.empty());

    NativeJsHookRecord installed_record = {};
    assert(GetNativeJsHookRecordForTesting(1u, &installed_record));
    assert(!installed_record.deferred);
    assert(installed_record.hook_handle == reinterpret_cast<void*>(0x9abcu));

    const InlineHookAddressCallCapture& capture = GetInlineHookAddressCallCapture();
    assert(capture.target_address == reinterpret_cast<void*>(0x70000000u));

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsResolveSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting();
}

void TestUninstallNativeJsHookRemovesInstalledHook() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetInlineHookAddressCallCapture() = {};
    GetInlineHookUnhookCallCapture() = {};
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libdemo.so";
    request.symbol_name = "target";

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.hook_handle == reinterpret_cast<void*>(0x9abcu));

    HookEvent event = {};
    event.hook_id = record.hook_id;
    event.phase = HookEventPhase::kEnter;
    assert(EnqueueNativeJsHookEvent(event, &error_message));

    assert(UninstallNativeJsHook(record.hook_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetPendingNativeJsHookCountForTesting() == 0u);
    assert(!TryDequeueNativeJsHookEvent(&event));
    assert(GetInlineHookUnhookCallCapture().call_count == 1);
    assert(GetInlineHookUnhookCallCapture().hook_handle == reinterpret_cast<void*>(0x9abcu));

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookUnhookInvokerForTesting();
}

void TestFindNativeJsExportByNameReturnsResolvedAddressOrMiss() {
    ResetNativeJsHookRegistryForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FakeResolveLoadedSymbolAddress);

    uint64_t target_address = 0;
    std::string error_message;
    assert(FindNativeJsExportByName("libdemo.so", "target", &target_address, &error_message));
    assert(target_address == 0x10000000u);

    target_address = 0;
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    assert(FindNativeJsExportByName("libdemo.so", "missing", &target_address, &error_message));
    assert(target_address == 0u);

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
}

void TestFindNativeJsExportByNameUsesFallbackAfterLoadedMiss() {
    ResetNativeJsHookRegistryForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(
            &FailingResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsResolveSymbolAddressForTesting(
            &FakeResolveSymbolAddressFallback);

    uint64_t target_address = 0u;
    std::string error_message;
    assert(FindNativeJsExportByName("libc.so",
                                    "strcmp",
                                    &target_address,
                                    &error_message));
    assert(target_address == 0x70000000u);

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsResolveSymbolAddressForTesting();
}

void TestFindNativeJsExportByNameStillReturnsResolvedAddressForUnsafeInlineHookSymbol() {
    ResetNativeJsHookRegistryForTesting();
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(
            &FakeResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookSymbolSafetyCheckerForTesting(
            &UnsafeInlineHookSymbolChecker);

    uint64_t target_address = 0xdeadbeefu;
    std::string error_message;
    assert(FindNativeJsExportByName("libc.so", "strcmp", &target_address, &error_message));
    assert(target_address == 0x10000000u);

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookSymbolSafetyCheckerForTesting();
}

void TestUninstallNativeJsHookRemovesPendingDeferredHook() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetResolveLoadedSymbolCallCapture() = {};
    GetInlineHookUnhookCallCapture() = {};
    GetEnsureObserverAsyncCallCount() = 0;
    GetEnsureObserverAsyncStatus() = NOOK_STATUS_OK;
    nook::agent_runtime::SetNativeJsResolveLoadedSymbolAddressForTesting(&FailingResolveLoadedSymbolAddress);
    nook::agent_runtime::SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);
    nook::agent_runtime::SetNativeJsInlineHookUnhookInvokerForTesting(&FakeInlineHookUnhookInvoker);
    nook::agent_runtime::SetNativeJsEnsureInlineHookModuleObserverAsyncForTesting(
            &FakeEnsureInlineHookModuleObserverAsync);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.module_name = "libnative-lib.so";
    request.symbol_name = "Java_com_demo_target_LoginFragment_verifyPasswordNative";

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.hook_handle == nullptr);
    assert(GetPendingNativeJsHookCountForTesting() == 1u);

    assert(UninstallNativeJsHook(record.hook_id, &error_message));
    assert(GetInstalledNativeJsHookCountForTesting() == 0u);
    assert(GetPendingNativeJsHookCountForTesting() == 0u);
    assert(GetInlineHookUnhookCallCapture().call_count == 0);

    nook::agent_runtime::ResetNativeJsResolveLoadedSymbolAddressForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookAddressInvokerForTesting();
    nook::agent_runtime::ResetNativeJsInlineHookUnhookInvokerForTesting();
    nook::agent_runtime::ResetNativeJsEnsureInlineHookModuleObserverAsyncForTesting();
}

void TestNativeJsHookEventQueuePreservesOrderAndPayload() {
    ResetNativeJsHookEventQueueForTesting();

    HookEvent enter_event = {};
    enter_event.hook_id = 1u;
    enter_event.phase = HookEventPhase::kEnter;
    enter_event.argument_count = 2u;
    enter_event.argument_values[0] = 0x1234u;
    enter_event.argument_values[1] = 0x5678u;
    std::string error_message;
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));

    HookEvent leave_event = {};
    leave_event.hook_id = 1u;
    leave_event.phase = HookEventPhase::kLeave;
    leave_event.return_value = 0x9abcu;
    assert(EnqueueNativeJsHookEvent(leave_event, &error_message));

    HookEvent drained = {};
    assert(TryDequeueNativeJsHookEvent(&drained));
    assert(drained.hook_id == 1u);
    assert(drained.phase == HookEventPhase::kEnter);
    assert(drained.argument_count == 2u);
    assert(drained.argument_values[0] == 0x1234u);
    assert(drained.argument_values[1] == 0x5678u);

    drained = {};
    assert(TryDequeueNativeJsHookEvent(&drained));
    assert(drained.hook_id == 1u);
    assert(drained.phase == HookEventPhase::kLeave);
    assert(drained.return_value == 0x9abcu);

    drained = {};
    assert(!TryDequeueNativeJsHookEvent(&drained));
}

void TestNativeJsHookEventNotifierRunsOnEnqueue() {
    ResetNativeJsHookEventQueueForTesting();
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventNotifier();
    GetNativeJsHookEventNotifyCount() = 0;
    SetNativeJsHookEventNotifier(&FakeNativeJsHookEventNotifier);

    HookEvent enter_event = {};
    enter_event.hook_id = 7u;
    enter_event.phase = HookEventPhase::kEnter;
    std::string error_message;
    assert(EnqueueNativeJsHookEvent(enter_event, &error_message));
    assert(GetNativeJsHookEventNotifyCount() == 1);

    ResetNativeJsHookEventNotifier();
}

void TestInvokeInstalledNativeJsHookNonblockingReturnsWithoutWaiting() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.has_target_address = true;
    request.target_address = 0x20000000u;
    request.blocking = false;

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(!record.blocking);

    const std::array<uint64_t, 8> arguments = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    uint64_t return_value = 0u;
    const auto start = std::chrono::steady_clock::now();
    assert(InvokeInstalledNativeJsHookForTesting(record.hook_id, arguments, &return_value));
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    assert(return_value == 0x4242u);
    assert(elapsed_ms < 1000);

    ResetNativeJsInlineHookAddressInvokerForTesting();
}

void TestInvokeInstalledNativeJsHookBypassesRecursiveReentry() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    ResetNativeJsHookStatusEventQueueForTesting();
    GetInlineHookAddressCallCapture() = {};
    GetReentrantOriginalCallCount() = 0u;
    SetNativeJsInlineHookAddressInvokerForTesting(&ReentrantInlineHookAddressInvoker);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.has_target_address = true;
    request.target_address = 0x20000000u;
    request.blocking = true;

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.blocking);

    using ReplacementEntry = uint64_t (*)(uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t,
                                          uint64_t);
    const InlineHookAddressCallCapture& capture = GetInlineHookAddressCallCapture();
    assert(capture.replacement != nullptr);
    auto replacement = reinterpret_cast<ReplacementEntry>(capture.replacement);

    const uint64_t nested_return =
        replacement(0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u, 0x70u, 0x80u);
    assert(nested_return == 0x5151u);
    assert(GetReentrantOriginalCallCount() == 1u);

    HookEvent event = {};
    assert(TryDequeueNativeJsHookEvent(&event));
    assert(event.hook_id == record.hook_id);
    assert(event.phase == HookEventPhase::kEnter);
    assert(!TryDequeueNativeJsHookEvent(&event));

    ResetNativeJsInlineHookAddressInvokerForTesting();
}

void TestInvokeInstalledNativeJsHookBypassesWhenCurrentThreadIsIgnored() {
    ResetNativeJsHookRegistryForTesting();
    ResetNativeJsHookEventQueueForTesting();
    GetInlineHookAddressCallCapture() = {};
    SetNativeJsInlineHookAddressInvokerForTesting(&FakeInlineHookAddressInvoker);

    NativeJsHookRequest request = {};
    request.type = "inline";
    request.has_target_address = true;
    request.target_address = 0x20000000u;
    request.blocking = true;

    NativeJsHookInstallerDependencies dependencies = {};
    NativeJsHookRecord record = {};
    std::string error_message;
    assert(InstallNativeJsHook(request, dependencies, &record, &error_message));
    assert(record.blocking);

    const std::array<uint64_t, 8> arguments = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u};
    uint64_t return_value = 0u;

    PushNativeJsInlineHookIgnoreForTesting();
    assert(InvokeInstalledNativeJsHookForTesting(record.hook_id, arguments, &return_value));
    PopNativeJsInlineHookIgnoreForTesting();

    assert(return_value == 0x4242u);

    HookEvent event = {};
    assert(!TryDequeueNativeJsHookEvent(&event));

    ResetNativeJsInlineHookAddressInvokerForTesting();
}

}  // namespace

int main() {
    TestInstallNativeJsHookAssignsIncrementingHookIds();
    TestInstallNativeJsHookRejectsUnsupportedTypeAndInstallerFailure();
    TestInstallNativeJsHookUsesDefaultInlineHookAdapterThroughResolvedAddress();
    TestInstallNativeJsHookUsesDirectTargetAddress();
    TestInstallNativeJsHookRegistersDeferredHookWhenModuleIsMissing();
    TestInstallNativeJsHookFailsWhenDeferredObserverScheduleFails();
    TestNotifyNativeJsHookModuleLoadedInstallsDeferredHook();
    TestNotifyNativeJsHookModuleLoadedReportsDeferredInstallFailure();
    TestInstallNativeJsHookUsesFallbackResolverBeforeDeferring();
    TestNotifyNativeJsHookModuleLoadedUsesFallbackResolver();
    TestUninstallNativeJsHookRemovesInstalledHook();
    TestUninstallNativeJsHookRemovesPendingDeferredHook();
    TestFindNativeJsExportByNameReturnsResolvedAddressOrMiss();
    TestFindNativeJsExportByNameUsesFallbackAfterLoadedMiss();
    TestFindNativeJsExportByNameStillReturnsResolvedAddressForUnsafeInlineHookSymbol();
    TestNativeJsHookEventQueuePreservesOrderAndPayload();
    TestNativeJsHookEventNotifierRunsOnEnqueue();
    TestInvokeInstalledNativeJsHookNonblockingReturnsWithoutWaiting();
    TestInvokeInstalledNativeJsHookBypassesRecursiveReentry();
    TestInvokeInstalledNativeJsHookBypassesWhenCurrentThreadIsIgnored();
    return 0;
}
