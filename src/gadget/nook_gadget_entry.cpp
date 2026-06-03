#include "gadget/nook_gadget_runtime.h"

#include "agent_runtime/nook_script_runtime_bridge.h"
#include "framework/NookCommInternal.h"
#include "nook/NookGadget.h"

namespace {

NookStatus NookGadgetControlInitialize() {
    return nook::framework::EnsureControlChannelReadyForCurrentProcess();
}

NookStatus NookGadgetBridgeInitialize() {
    return nook::agent_runtime::NookScriptRuntimeBridgeInitialize();
}

NookStatus NookGadgetNotifyRuntimeReady() {
    return nook::framework::NotifyRuntimeReadyToServer();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor(220))) static void NookGadgetAutoInitialize() {
    (void)NookGadgetInitialize();
}
#endif

}  // namespace

extern "C" NookStatus NookGadgetInitialize(void) {
    nook::gadget::EnsureDefaultInitializers(&NookGadgetControlInitialize,
                                            &NookGadgetBridgeInitialize,
                                            &NookGadgetNotifyRuntimeReady);
    return nook::gadget::InitializeRuntime();
}
