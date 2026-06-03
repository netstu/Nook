#pragma once

#include "../communication/protocol/messages.h"
#include "nook/Nook.h"

#include <functional>
#include <string>

namespace nook {
namespace framework {

using RpcRequestHandler = std::function<comm::RpcResponse(const comm::RpcRequest&)>;

void SetPendingScriptCallbackError(std::string message);
std::string TakePendingScriptCallbackError();
std::string MakeScriptCallbackErrorMessage(const char* fallback_message);
void RegisterInternalRpcRequestHandler(const std::string& method, RpcRequestHandler handler);
void UnregisterInternalRpcRequestHandler(const std::string& method);
bool HasInternalRpcRequestHandlers();
comm::RpcResponse DispatchInternalRpcRequest(const comm::RpcRequest& request);
void SetInternalRpcRequestHandler(RpcRequestHandler handler);
void RefreshAgentCallbacksForInternalRpc();
NookStatus EnsureControlChannelReadyForCurrentProcess();
NookStatus EnsureFullAgentReadyForCurrentProcess();
NookStatus NotifyZygoteControlReadyToServer();
NookStatus ResetZygoteControlConnectionStateForReinit();
void RequestControlChannelDisconnectAfterCurrentReply(const char* reason);
void ScheduleControlChannelHardCloseAfterDelay(const char* reason, uint32_t delay_ms);
void PrepareInheritedChildAgentActivation(const std::string& process_name,
                                          const std::string& spawn_token,
                                          bool arm_spawn_gate);
void ResetInheritedInternalSynchronizationForChild();
void ResetInheritedInternalRpcStateForChild();
void ResetInheritedForkChildConnectionState();
void ResetInheritedConnectionStateForChild(const std::string& process_name,
                                           const std::string& spawn_token,
                                           bool arm_spawn_gate);
bool SuspendAgentConnectionForFork();
bool ResumeAgentConnectionAfterFork();
bool IsCurrentProcessSpawnGateHeld();
bool IsCurrentProcessStrictLifecycleSpawnGateHeld();

}  // namespace framework
}  // namespace nook
