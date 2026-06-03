#pragma once

#include "gadget/nook_gadget_config.h"
#include "nook/Nook.h"

#include <cstdint>
#include <string>

namespace nook {
namespace comm {

struct RpcRequest;
struct RpcResponse;

}  // namespace comm
}  // namespace nook

namespace nook {
namespace gadget {

using ControlInitializer = NookStatus (*)();
using ConnectInitializer = NookStatus (*)(const GadgetConfig&);
using BridgeInitializer = NookStatus (*)();
using ListenInitializer = NookStatus (*)(const GadgetConfig&);
using OnLoadWaiter = NookStatus (*)(const GadgetConfig&);
using RuntimeWarmupInitializer = NookStatus (*)();
using RuntimeReadyNotifier = NookStatus (*)();
using StartupScriptInitializer = NookStatus (*)();
using AssetFileReader = bool (*)(const char*, std::string*);
using StartupScriptLoader = NookStatus (*)(const char*, const char*, uint32_t*);
using RuntimeDebugLogger = void (*)(const char*);

NookStatus InitializeRuntime();
NookStatus InitializeConfiguredControlChannelForTesting(const GadgetConfig& config,
                                                        ControlInitializer control_initializer);
NookStatus LoadConfiguredStartupScriptForTesting(const GadgetConfig& config,
                                                 AssetFileReader asset_reader,
                                                 StartupScriptLoader script_loader,
                                                 RuntimeDebugLogger debug_logger = nullptr);
NookStatus LoadConfiguredStartupScriptManuallyForTesting(const GadgetConfig& config,
                                                         AssetFileReader asset_reader,
                                                         StartupScriptLoader script_loader,
                                                         RuntimeDebugLogger debug_logger = nullptr);
bool TryHandleConfiguredStartupRpc(const nook::comm::RpcRequest& request,
                                   nook::comm::RpcResponse* response);
bool IsRuntimeInitialized();
bool ShouldDeferJavaReadyChecksForOnLoadWait();
void ResetRuntimeForTesting();
void SetAssetFileReaderForTesting(AssetFileReader asset_reader);
void ResetAssetFileReaderForTesting();
void SetStartupScriptLoaderForTesting(StartupScriptLoader script_loader);
void ResetStartupScriptLoaderForTesting();
void SetConnectInitializerForTesting(ConnectInitializer initializer);
void ResetConnectInitializerForTesting();
void EnsureDefaultInitializers(ControlInitializer control_initializer,
                               BridgeInitializer bridge_initializer,
                               RuntimeReadyNotifier runtime_ready_notifier);
void SetControlInitializerForTesting(ControlInitializer initializer);
void ResetControlInitializerForTesting();
void SetBridgeInitializerForTesting(BridgeInitializer initializer);
void ResetBridgeInitializerForTesting();
void SetListenInitializerForTesting(ListenInitializer initializer);
void ResetListenInitializerForTesting();
void SetOnLoadWaiterForTesting(OnLoadWaiter waiter);
void ResetOnLoadWaiterForTesting();
void SetRuntimeWarmupInitializerForTesting(RuntimeWarmupInitializer initializer);
void ResetRuntimeWarmupInitializerForTesting();
void SetRuntimeReadyNotifierForTesting(RuntimeReadyNotifier notifier);
void ResetRuntimeReadyNotifierForTesting();
void SetStartupScriptInitializerForTesting(StartupScriptInitializer initializer);
void ResetStartupScriptInitializerForTesting();

}  // namespace gadget
}  // namespace nook
