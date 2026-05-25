#pragma once

#include "../src/communication/protocol/messages.h"

#include <cstdint>
#include <functional>
#include <string>

namespace nook {
namespace comm {
class Session;
}
namespace server {

class SessionRegistry;

using ZygoteRpcInvoker = std::function<bool(SessionRegistry*,
                                            int,
                                            const std::string&,
                                            uint32_t,
                                            const comm::RpcRequest&,
                                            comm::RpcResponse*,
                                            std::string*)>;
using ZygoteSessionRpcSender = std::function<bool(comm::Session*,
                                                  uint32_t,
                                                  const comm::RpcRequest&,
                                                  comm::RpcResponse*,
                                                  std::string*)>;
using ZygoteSessionSpawnInstallSender = std::function<bool(comm::Session*,
                                                           uint32_t,
                                                           const comm::SpawnInstallRequest&,
                                                           comm::SpawnInstallResponse*,
                                                           std::string*)>;
using ZygoteSessionSpawnUninstallSender = std::function<bool(comm::Session*,
                                                             uint32_t,
                                                             const comm::SpawnUninstallRequest&,
                                                             comm::SpawnUninstallResponse*,
                                                             std::string*)>;

std::string FormatZygoteControlLogState(const char* stage,
                                        const char* event_name,
                                        int process_pid,
                                        const std::string& process_name,
                                        const char* method,
                                        const char* failure_class,
                                        const std::string& error_message);

bool IsZygoteControlReadyJson(const std::string& json);
bool ExtractZygoteControlState(const std::string& json, std::string* state);

bool CallZygoteControlRpc(SessionRegistry* registry,
                          int process_pid,
                          const std::string& process_name,
                          uint32_t request_timeout_ms,
                          const comm::RpcRequest& request,
                          comm::RpcResponse* response,
                          std::string* error_message);

bool CallZygoteControlRpcWithSenderForTest(SessionRegistry* registry,
                                           int process_pid,
                                           const std::string& process_name,
                                           uint32_t request_timeout_ms,
                                           const comm::RpcRequest& request,
                                           comm::RpcResponse* response,
                                           std::string* error_message,
                                           const ZygoteSessionRpcSender& sender);

bool WaitForZygoteControlReady(SessionRegistry* registry,
                               int zygote_pid,
                               const std::string& process_name,
                               std::string* error_message,
                               const ZygoteRpcInvoker& rpc_invoker = {});

bool WaitForZygoteControlReadyWithTimeoutForTest(SessionRegistry* registry,
                                                 int zygote_pid,
                                                 const std::string& process_name,
                                                 uint32_t overall_timeout_ms,
                                                 uint32_t per_request_timeout_ms,
                                                 uint32_t retry_sleep_ms,
                                                 std::string* error_message,
                                                 const ZygoteRpcInvoker& rpc_invoker = {});

bool WaitForZygoteControlDisconnect(SessionRegistry* registry,
                                    int zygote_pid,
                                    const std::string& process_name,
                                    std::string* error_message);

bool WaitForZygoteControlDisconnectWithTimeoutForTest(SessionRegistry* registry,
                                                      int zygote_pid,
                                                      const std::string& process_name,
                                                      uint32_t timeout_ms,
                                                      std::string* error_message);

bool DispatchZygoteControlRpc(SessionRegistry* registry,
                              int zygote_pid,
                              const std::string& process_name,
                              const char* stage,
                              const char* failure_class,
                              uint32_t timeout_ms,
                              const comm::RpcRequest& request,
                              std::string* error_message,
                              const ZygoteRpcInvoker& rpc_invoker = {});

bool InstallZygoteForkHook(SessionRegistry* registry,
                           int zygote_pid,
                           const std::string& process_name,
                           const std::string& agent_path,
                           const std::string& target_package,
                           const std::string& spawn_token,
                           std::string* error_message,
                           const ZygoteRpcInvoker& rpc_invoker = {});
bool InstallZygoteForkHookWithSendersForTest(SessionRegistry* registry,
                                             int zygote_pid,
                                             const std::string& process_name,
                                             const std::string& agent_path,
                                             const std::string& target_package,
                                             const std::string& spawn_token,
                                             std::string* error_message,
                                             const ZygoteRpcInvoker& rpc_invoker,
                                             const ZygoteSessionSpawnInstallSender& spawn_install_sender);

bool UninstallZygoteForkHook(SessionRegistry* registry,
                             int zygote_pid,
                             const std::string& process_name,
                             std::string* error_message,
                             const ZygoteRpcInvoker& rpc_invoker = {});
bool UninstallZygoteForkHookWithSendersForTest(SessionRegistry* registry,
                                               int zygote_pid,
                                               const std::string& process_name,
                                               std::string* error_message,
                                               const ZygoteRpcInvoker& rpc_invoker,
                                               const ZygoteSessionSpawnUninstallSender& spawn_uninstall_sender);

bool SendSpawnInstallThroughSession(comm::Session* agent,
                                    uint32_t request_timeout_ms,
                                    const comm::SpawnInstallRequest& request,
                                    comm::SpawnInstallResponse* response,
                                    std::string* error_message);
bool SendSpawnUninstallThroughSession(comm::Session* agent,
                                      uint32_t request_timeout_ms,
                                      const comm::SpawnUninstallRequest& request,
                                      comm::SpawnUninstallResponse* response,
                                      std::string* error_message);
bool SendSpawnInstallToControlSessionForTest(SessionRegistry* registry,
                                             int process_pid,
                                             const std::string& process_name,
                                             uint32_t request_timeout_ms,
                                             const comm::SpawnInstallRequest& request,
                                             comm::SpawnInstallResponse* response,
                                             std::string* error_message,
                                             const ZygoteSessionSpawnInstallSender& sender);
bool SendSpawnUninstallToControlSessionForTest(SessionRegistry* registry,
                                               int process_pid,
                                               const std::string& process_name,
                                               uint32_t request_timeout_ms,
                                               const comm::SpawnUninstallRequest& request,
                                               comm::SpawnUninstallResponse* response,
                                               std::string* error_message,
                                               const ZygoteSessionSpawnUninstallSender& sender);

}  // namespace server
}  // namespace nook
