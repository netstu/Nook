#include "zygote_control_rpc.h"

#include "../src/communication/protocol/frame.h"
#include "../src/communication/protocol/message_types.h"
#include "../src/communication/protocol/messages.h"
#include "../src/communication/session/session.h"
#include "session_registry.h"

#include <chrono>
#include <thread>

#if defined(__ANDROID__)
#include <android/log.h>
#define NOOK_ZYGOTE_RPC_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NookServer", __VA_ARGS__))
#define NOOK_ZYGOTE_RPC_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "NookServer", __VA_ARGS__))
#else
#define NOOK_ZYGOTE_RPC_LOGI(...) ((void)0)
#define NOOK_ZYGOTE_RPC_LOGE(...) ((void)0)
#endif

namespace nook {
namespace server {

namespace {

const char* NonEmptyOrUnknown(const char* value, const char* fallback) {
    return (value != nullptr && value[0] != '\0') ? value : fallback;
}

bool IsUsapProcessName(const std::string& process_name) {
    return process_name == "usap32" || process_name == "usap64";
}

bool HasImmediateControlSession(SessionRegistry* registry,
                                int process_pid,
                                const std::string& process_name) {
    if (registry == nullptr) {
        return false;
    }
    return registry->FindControlReadyAgentSessionByIdentity(process_pid, process_name) != nullptr;
}

bool HasOwnedImmediateControlSession(SessionRegistry* registry,
                                     int process_pid,
                                     const std::string& process_name) {
    if (registry == nullptr || process_name.empty()) {
        return false;
    }
    if (!registry->IsOwnedZygoteControlTarget(process_pid, process_name)) {
        return false;
    }
    return HasImmediateControlSession(registry, process_pid, process_name);
}

bool SendRpcRequestThroughSession(comm::Session* agent,
                                  uint32_t request_timeout_ms,
                                  const comm::RpcRequest& request,
                                  comm::RpcResponse* response,
                                  std::string* error_message) {
    if (agent == nullptr || response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "zygote control response is null";
        }
        return false;
    }

    comm::Frame response_frame;
    comm::Frame request_frame(comm::MessageType::kRpcRequest,
                              agent->NextMsgId(),
                              comm::EncodeRpcRequest(request));
    if (!agent->SendRequest(request_frame, &response_frame, request_timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "zygote control rpc timeout";
        }
        return false;
    }

    if (response_frame.GetType() != comm::MessageType::kRpcResponse ||
        !comm::DecodeRpcResponse(response_frame.GetPayload().data(),
                                 response_frame.GetPayload().size(),
                                 response)) {
        if (error_message != nullptr) {
            *error_message = "zygote control rpc decode failed";
        }
        return false;
    }

    if (!response->success) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                ? "zygote control rpc failed"
                : response->error.message;
        }
        return false;
    }

    return true;
}

bool SendSpawnInstallThroughSessionImpl(comm::Session* agent,
                                        uint32_t request_timeout_ms,
                                        const comm::SpawnInstallRequest& request,
                                        comm::SpawnInstallResponse* response,
                                        std::string* error_message) {
    if (agent == nullptr || response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "zygote control response is null";
        }
        return false;
    }

    comm::Frame response_frame;
    comm::Frame request_frame(comm::MessageType::kSpawnInstall,
                              agent->NextMsgId(),
                              comm::EncodeSpawnInstallRequest(request));
    if (!agent->SendRequest(request_frame, &response_frame, request_timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "zygote control rpc timeout";
        }
        return false;
    }

    if (response_frame.GetType() != comm::MessageType::kSpawnInstallResp ||
        !comm::DecodeSpawnInstallResponse(response_frame.GetPayload().data(),
                                          response_frame.GetPayload().size(),
                                          response)) {
        if (error_message != nullptr) {
            *error_message = "zygote control rpc decode failed";
        }
        return false;
    }

    if (!response->success) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                ? "zygote control rpc failed"
                : response->error.message;
        }
        return false;
    }

    return true;
}

bool SendSpawnUninstallThroughSessionImpl(comm::Session* agent,
                                          uint32_t request_timeout_ms,
                                          const comm::SpawnUninstallRequest& request,
                                          comm::SpawnUninstallResponse* response,
                                          std::string* error_message) {
    if (agent == nullptr || response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "zygote control response is null";
        }
        return false;
    }

    comm::Frame response_frame;
    comm::Frame request_frame(comm::MessageType::kSpawnUninstall,
                              agent->NextMsgId(),
                              comm::EncodeSpawnUninstallRequest(request));
    if (!agent->SendRequest(request_frame, &response_frame, request_timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "zygote control rpc timeout";
        }
        return false;
    }

    if (response_frame.GetType() != comm::MessageType::kSpawnUninstallResp ||
        !comm::DecodeSpawnUninstallResponse(response_frame.GetPayload().data(),
                                            response_frame.GetPayload().size(),
                                            response)) {
        if (error_message != nullptr) {
            *error_message = "zygote control rpc decode failed";
        }
        return false;
    }

    if (!response->success) {
        if (error_message != nullptr) {
            *error_message = response->error.message.empty()
                ? "zygote control rpc failed"
                : response->error.message;
        }
        return false;
    }

    return true;
}

bool IsSpawnControlCompatFallbackError(const std::string& error_message) {
    if (error_message.empty()) {
        return false;
    }
    return error_message.find("decode failed") != std::string::npos ||
           error_message.find("response is null") != std::string::npos ||
           error_message.find("timeout") != std::string::npos ||
           error_message.find("not supported") != std::string::npos ||
           error_message.find("unknown message") != std::string::npos;
}

}  // namespace

bool SendSpawnInstallThroughSession(comm::Session* agent,
                                    uint32_t request_timeout_ms,
                                    const comm::SpawnInstallRequest& request,
                                    comm::SpawnInstallResponse* response,
                                    std::string* error_message) {
    return SendSpawnInstallThroughSessionImpl(agent,
                                              request_timeout_ms,
                                              request,
                                              response,
                                              error_message);
}

bool SendSpawnUninstallThroughSession(comm::Session* agent,
                                      uint32_t request_timeout_ms,
                                      const comm::SpawnUninstallRequest& request,
                                      comm::SpawnUninstallResponse* response,
                                      std::string* error_message) {
    return SendSpawnUninstallThroughSessionImpl(agent,
                                                request_timeout_ms,
                                                request,
                                                response,
                                                error_message);
}

std::string FormatZygoteControlLogState(const char* stage,
                                        const char* event_name,
                                        int process_pid,
                                        const std::string& process_name,
                                        const char* method,
                                        const char* failure_class,
                                        const std::string& error_message) {
    std::string line = "zygote-control stage=";
    line += NonEmptyOrUnknown(stage, "unknown");
    line += " event=";
    line += NonEmptyOrUnknown(event_name, "unknown");
    line += " pid=";
    line += (process_pid > 0) ? std::to_string(process_pid) : std::string("unknown");
    line += " process=";
    line += process_name.empty() ? "unknown" : process_name;
    line += " method=";
    line += NonEmptyOrUnknown(method, "unknown");
    line += " class=";
    line += NonEmptyOrUnknown(failure_class, "hard");
    if (!error_message.empty()) {
        line += " error=";
        line += error_message;
    }
    return line;
}

bool IsZygoteControlReadyJson(const std::string& json) {
    return json.find("\"ready\":true") != std::string::npos;
}

bool ExtractZygoteControlState(const std::string& json, std::string* state) {
    if (state == nullptr) {
        return false;
    }
    state->clear();

    const std::string key = "\"state\":\"";
    const std::size_t start = json.find(key);
    if (start == std::string::npos) {
        return false;
    }

    const std::size_t value_start = start + key.size();
    const std::size_t value_end = json.find('"', value_start);
    if (value_end == std::string::npos) {
        return false;
    }

    *state = json.substr(value_start, value_end - value_start);
    return true;
}

bool CallZygoteControlRpc(SessionRegistry* registry,
                          int process_pid,
                          const std::string& process_name,
                          uint32_t request_timeout_ms,
                          const comm::RpcRequest& request,
                          comm::RpcResponse* response,
                          std::string* error_message) {
    return CallZygoteControlRpcWithSenderForTest(registry,
                                                 process_pid,
                                                 process_name,
                                                 request_timeout_ms,
                                                 request,
                                                 response,
                                                 error_message,
                                                 SendRpcRequestThroughSession);
}

bool CallZygoteControlRpcWithSenderForTest(SessionRegistry* registry,
                                           int process_pid,
                                           const std::string& process_name,
                                           uint32_t request_timeout_ms,
                                           const comm::RpcRequest& request,
                                           comm::RpcResponse* response,
                                           std::string* error_message,
                                           const ZygoteSessionRpcSender& sender) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "zygote control response is null";
        }
        return false;
    }
    if (registry == nullptr) {
        if (error_message != nullptr) {
            *error_message = "session registry unavailable";
        }
        return false;
    }

    const uint32_t session_wait_timeout_ms = request_timeout_ms > 0 ? request_timeout_ms : 1u;
    comm::Session* agent = registry->WaitForControlReadyAgentSessionByIdentity(process_pid,
                                                                               process_name,
                                                                               session_wait_timeout_ms);
    if (agent != nullptr &&
        process_pid > 0 &&
        registry->FindControlReadyAgentSessionByPid(process_pid) == nullptr &&
        !process_name.empty()) {
        NOOK_ZYGOTE_RPC_LOGI("zygote control rpc using process-name fallback pid=%d process=%s",
                             process_pid,
                             process_name.c_str());
    }
    if (agent == nullptr) {
        if (error_message != nullptr) {
            if (process_pid > 0) {
                *error_message = "zygote control-ready agent session not found pid=" +
                                 std::to_string(process_pid) +
                                 " process=" + process_name;
            } else {
                *error_message = "zygote control-ready agent session not found: " + process_name;
            }
        }
        return false;
    }

    const ZygoteSessionRpcSender& invoke_sender =
        sender ? sender : SendRpcRequestThroughSession;
    return invoke_sender(agent, request_timeout_ms, request, response, error_message);
}

bool SendSpawnInstallToControlSessionForTest(SessionRegistry* registry,
                                             int process_pid,
                                             const std::string& process_name,
                                             uint32_t request_timeout_ms,
                                             const comm::SpawnInstallRequest& request,
                                             comm::SpawnInstallResponse* response,
                                             std::string* error_message,
                                             const ZygoteSessionSpawnInstallSender& sender) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "zygote control response is null";
        }
        return false;
    }
    if (registry == nullptr) {
        if (error_message != nullptr) {
            *error_message = "session registry unavailable";
        }
        return false;
    }

    const uint32_t session_wait_timeout_ms = request_timeout_ms > 0 ? request_timeout_ms : 1u;
    comm::Session* agent = registry->WaitForControlReadyAgentSessionByIdentity(process_pid,
                                                                               process_name,
                                                                               session_wait_timeout_ms);
    if (agent == nullptr) {
        if (error_message != nullptr) {
            if (process_pid > 0) {
                *error_message = "zygote control-ready agent session not found pid=" +
                                 std::to_string(process_pid) +
                                 " process=" + process_name;
            } else {
                *error_message = "zygote control-ready agent session not found: " + process_name;
            }
        }
        return false;
    }

    const ZygoteSessionSpawnInstallSender& invoke_sender =
        sender ? sender : SendSpawnInstallThroughSession;
    return invoke_sender(agent, request_timeout_ms, request, response, error_message);
}

bool SendSpawnUninstallToControlSessionForTest(SessionRegistry* registry,
                                               int process_pid,
                                               const std::string& process_name,
                                               uint32_t request_timeout_ms,
                                               const comm::SpawnUninstallRequest& request,
                                               comm::SpawnUninstallResponse* response,
                                               std::string* error_message,
                                               const ZygoteSessionSpawnUninstallSender& sender) {
    if (response == nullptr) {
        if (error_message != nullptr) {
            *error_message = "zygote control response is null";
        }
        return false;
    }
    if (registry == nullptr) {
        if (error_message != nullptr) {
            *error_message = "session registry unavailable";
        }
        return false;
    }
    if (process_name.empty() || !registry->IsOwnedZygoteControlTarget(process_pid, process_name)) {
        if (error_message != nullptr) {
            *error_message = "zygote control target is not explicitly owned";
        }
        return false;
    }

    const uint32_t session_wait_timeout_ms = request_timeout_ms > 0 ? request_timeout_ms : 1u;
    comm::Session* agent = registry->WaitForControlReadyAgentSessionByIdentity(process_pid,
                                                                               process_name,
                                                                               session_wait_timeout_ms);
    if (agent == nullptr) {
        if (error_message != nullptr) {
            if (process_pid > 0) {
                *error_message = "zygote control-ready agent session not found pid=" +
                                 std::to_string(process_pid) +
                                 " process=" + process_name;
            } else {
                *error_message = "zygote control-ready agent session not found: " + process_name;
            }
        }
        return false;
    }

    const ZygoteSessionSpawnUninstallSender& invoke_sender =
        sender ? sender : SendSpawnUninstallThroughSession;
    return invoke_sender(agent, request_timeout_ms, request, response, error_message);
}

bool WaitForZygoteControlReady(SessionRegistry* registry,
                               int zygote_pid,
                               const std::string& process_name,
                               std::string* error_message,
                               const ZygoteRpcInvoker& rpc_invoker) {
    return WaitForZygoteControlReadyWithTimeoutForTest(registry,
                                                       zygote_pid,
                                                       process_name,
                                                       5000,
                                                       250,
                                                       25,
                                                       error_message,
                                                       rpc_invoker);
}

bool WaitForZygoteControlReadyWithTimeoutForTest(SessionRegistry* registry,
                                                 int zygote_pid,
                                                 const std::string& process_name,
                                                 uint32_t overall_timeout_ms,
                                                 uint32_t per_request_timeout_ms,
                                                 uint32_t retry_sleep_ms,
                                                 std::string* error_message,
                                                 const ZygoteRpcInvoker& rpc_invoker) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(overall_timeout_ms);
    const ZygoteRpcInvoker& invoke = rpc_invoker ? rpc_invoker : CallZygoteControlRpc;
    NOOK_ZYGOTE_RPC_LOGI("%s",
                         FormatZygoteControlLogState("ready-wait",
                                                     "begin",
                                                     zygote_pid,
                                                     process_name,
                                                     "nook.spawn.status",
                                                     "soft",
                                                     "")
                             .c_str());

    std::string last_error = "zygote control status unavailable";
    while (std::chrono::steady_clock::now() < deadline) {
        comm::Session* control_session = registry != nullptr
                                             ? registry->FindControlReadyAgentSessionByIdentity(zygote_pid,
                                                                                                process_name)
                                             : nullptr;
        if (control_session != nullptr) {
            NOOK_ZYGOTE_RPC_LOGI("%s",
                                 FormatZygoteControlLogState("ready-wait",
                                                             "session-present",
                                                             zygote_pid,
                                                             process_name,
                                                             "nook.spawn.status",
                                                             "soft",
                                                             "")
                                     .c_str());
        }

        comm::RpcRequest request;
        request.method = "nook.spawn.status";
        request.args_json = "{}";

        comm::RpcResponse response;
        std::string rpc_error;
        if (invoke(registry,
                   zygote_pid,
                   process_name,
                   per_request_timeout_ms,
                   request,
                   &response,
                   &rpc_error)) {
            if (IsZygoteControlReadyJson(response.result_json)) {
                NOOK_ZYGOTE_RPC_LOGI("%s",
                                     FormatZygoteControlLogState("ready-wait",
                                                                 "ready",
                                                                 zygote_pid,
                                                                 process_name,
                                                                 "nook.spawn.status",
                                                                 "soft",
                                                                 "")
                                         .c_str());
                if (error_message != nullptr) {
                    error_message->clear();
                }
                return true;
            }

            last_error = "zygote control reported not-ready";
            NOOK_ZYGOTE_RPC_LOGI("%s",
                                 FormatZygoteControlLogState("ready-wait",
                                                             "not-ready",
                                                             zygote_pid,
                                                             process_name,
                                                             "nook.spawn.status",
                                                             "soft",
                                                             last_error)
                                     .c_str());
        } else {
            last_error = rpc_error.empty() ? "zygote control status rpc failed" : rpc_error;
            NOOK_ZYGOTE_RPC_LOGE("%s",
                                 FormatZygoteControlLogState("ready-wait",
                                                             "rpc-fail",
                                                             zygote_pid,
                                                             process_name,
                                                             "nook.spawn.status",
                                                             "soft",
                                                             last_error)
                                     .c_str());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(retry_sleep_ms));
    }

    if (error_message != nullptr) {
        *error_message = last_error.empty()
                             ? "zygote control-ready wait timed out"
                             : "zygote control-ready wait timed out: " + last_error;
    }
    return false;
}

bool WaitForZygoteControlDisconnect(SessionRegistry* registry,
                                    int zygote_pid,
                                    const std::string& process_name,
                                    std::string* error_message) {
    return WaitForZygoteControlDisconnectWithTimeoutForTest(registry,
                                                            zygote_pid,
                                                            process_name,
                                                            5000,
                                                            error_message);
}

bool WaitForZygoteControlDisconnectWithTimeoutForTest(SessionRegistry* registry,
                                                      int zygote_pid,
                                                      const std::string& process_name,
                                                      uint32_t timeout_ms,
                                                      std::string* error_message) {
    if (registry == nullptr) {
        if (error_message != nullptr) {
            *error_message = "session registry unavailable";
        }
        return false;
    }

    NOOK_ZYGOTE_RPC_LOGI("%s",
                         FormatZygoteControlLogState("disconnect-wait",
                                                     "begin",
                                                     zygote_pid,
                                                     process_name,
                                                     "control-session-disconnect",
                                                     "soft",
                                                     "")
                             .c_str());

    comm::Session* session_before_wait =
        registry->FindControlReadyAgentSessionByIdentity(zygote_pid, process_name);
    NOOK_ZYGOTE_RPC_LOGI("%s",
                         FormatZygoteControlLogState("disconnect-wait",
                                                     session_before_wait != nullptr ? "session-present" : "session-missing",
                                                     zygote_pid,
                                                     process_name,
                                                     "control-session-disconnect",
                                                     "soft",
                                                     "")
                             .c_str());

    if (!registry->WaitForAgentSessionDisconnectByIdentity(zygote_pid, process_name, timeout_ms)) {
        if (error_message != nullptr) {
            *error_message = "zygote control disconnect wait timed out";
        }
        NOOK_ZYGOTE_RPC_LOGE("%s",
                             FormatZygoteControlLogState("disconnect-wait",
                                                         "timeout",
                                                         zygote_pid,
                                                         process_name,
                                                         "control-session-disconnect",
                                                         "soft",
                                                         error_message != nullptr ? *error_message : std::string())
                                 .c_str());
        return false;
    }

    if (error_message != nullptr) {
        error_message->clear();
    }
    NOOK_ZYGOTE_RPC_LOGI("%s",
                         FormatZygoteControlLogState("disconnect-wait",
                                                     "disconnected",
                                                     zygote_pid,
                                                     process_name,
                                                     "control-session-disconnect",
                                                     "soft",
                                                     "")
                             .c_str());
    return true;
}

bool DispatchZygoteControlRpc(SessionRegistry* registry,
                              int zygote_pid,
                              const std::string& process_name,
                              const char* stage,
                              const char* failure_class,
                              uint32_t timeout_ms,
                              const comm::RpcRequest& request,
                              std::string* error_message,
                              const ZygoteRpcInvoker& rpc_invoker) {
    const ZygoteRpcInvoker& invoke = rpc_invoker ? rpc_invoker : CallZygoteControlRpc;

    NOOK_ZYGOTE_RPC_LOGI("%s",
                         FormatZygoteControlLogState(stage,
                                                     "begin",
                                                     zygote_pid,
                                                     process_name,
                                                     request.method.c_str(),
                                                     failure_class,
                                                     "")
                             .c_str());

    comm::RpcResponse response;
    const bool ok = invoke(registry,
                           zygote_pid,
                           process_name,
                           timeout_ms,
                           request,
                           &response,
                           error_message);
    if (ok) {
        NOOK_ZYGOTE_RPC_LOGI("%s",
                             FormatZygoteControlLogState(stage,
                                                         "rpc-ok",
                                                         zygote_pid,
                                                         process_name,
                                                         request.method.c_str(),
                                                         failure_class,
                                                         "")
                                 .c_str());
    } else {
        NOOK_ZYGOTE_RPC_LOGE("%s",
                             FormatZygoteControlLogState(stage,
                                                         "rpc-fail",
                                                         zygote_pid,
                                                         process_name,
                                                         request.method.c_str(),
                                                         failure_class,
                                                         error_message != nullptr ? *error_message : std::string())
                                 .c_str());
    }

    return ok;
}

bool InstallZygoteForkHook(SessionRegistry* registry,
                           int zygote_pid,
                           const std::string& process_name,
                           const std::string& agent_path,
                           const std::string& target_package,
                           const std::string& spawn_token,
                           std::string* error_message,
                           const ZygoteRpcInvoker& rpc_invoker) {
    return InstallZygoteForkHookWithSendersForTest(registry,
                                                   zygote_pid,
                                                   process_name,
                                                   agent_path,
                                                   target_package,
                                                   spawn_token,
                                                   error_message,
                                                   rpc_invoker,
                                                   SendSpawnInstallThroughSession);
}

bool InstallZygoteForkHookWithSendersForTest(SessionRegistry* registry,
                                             int zygote_pid,
                                             const std::string& process_name,
                                             const std::string& agent_path,
                                             const std::string& target_package,
                                             const std::string& spawn_token,
                                             std::string* error_message,
                                             const ZygoteRpcInvoker& rpc_invoker,
                                             const ZygoteSessionSpawnInstallSender& spawn_install_sender) {
    (void) zygote_pid;
    (void) agent_path;
    comm::RpcRequest request;
    request.method = "nook.spawn.installForkHook";
    request.args_json = std::string("{\"target_package\":\"") + target_package +
                        "\",\"spawn_token\":\"" + spawn_token +
                        "\",\"mode\":\"stable\"}";
    comm::SpawnInstallRequest spawn_install_request;
    spawn_install_request.target_package = target_package;
    spawn_install_request.spawn_token = spawn_token;
    spawn_install_request.mode = "stable";

    comm::RpcRequest status_request;
    status_request.method = "nook.spawn.status";
    status_request.args_json = "{}";
    comm::RpcRequest clear_request;
    clear_request.method = "nook.spawn.clearForkHook";
    clear_request.args_json = "{}";

    if (!WaitForZygoteControlReady(registry,
                                   zygote_pid,
                                   process_name,
                                   error_message,
                                   rpc_invoker)) {
        if (registry != nullptr &&
            registry->IsOwnedZygoteControlTarget(zygote_pid, process_name)) {
            registry->ClearOwnedZygoteControlProcess(process_name);
        }
        NOOK_ZYGOTE_RPC_LOGE("%s",
                             FormatZygoteControlLogState("install",
                                                         "ready-fail",
                                                         zygote_pid,
                                                         process_name,
                                                         request.method.c_str(),
                                                         "soft",
                                                         error_message != nullptr ? *error_message : std::string())
                                 .c_str());
        return false;
    }

    NOOK_ZYGOTE_RPC_LOGI("%s",
                         FormatZygoteControlLogState("status",
                                                     "begin",
                                                     zygote_pid,
                                                     process_name,
                                                     status_request.method.c_str(),
                                                     "soft",
                                                     "")
                             .c_str());

    comm::RpcResponse status_response;
    const ZygoteRpcInvoker& invoke = rpc_invoker ? rpc_invoker : CallZygoteControlRpc;
    if (!invoke(registry,
                zygote_pid,
                process_name,
                5000,
                status_request,
                &status_response,
                error_message)) {
        NOOK_ZYGOTE_RPC_LOGE("%s",
                             FormatZygoteControlLogState("status",
                                                         "rpc-fail",
                                                         zygote_pid,
                                                         process_name,
                                                         status_request.method.c_str(),
                                                         "soft",
                                                         error_message != nullptr ? *error_message : std::string())
                                 .c_str());
        if (registry != nullptr && !process_name.empty()) {
            registry->ClearOwnedZygoteControlProcess(process_name);
        }
        return false;
    }
    NOOK_ZYGOTE_RPC_LOGI("%s",
                         FormatZygoteControlLogState("status",
                                                     "rpc-ok",
                                                     zygote_pid,
                                                     process_name,
                                                     status_request.method.c_str(),
                                                     "soft",
                                                     "")
                             .c_str());

    std::string controller_state;
    if (ExtractZygoteControlState(status_response.result_json, &controller_state) &&
        !controller_state.empty() &&
        controller_state != "idle") {
        NOOK_ZYGOTE_RPC_LOGI("%s",
                             FormatZygoteControlLogState("install",
                                                         "residual-state",
                                                         zygote_pid,
                                                         process_name,
                                                         request.method.c_str(),
                                                         "soft",
                                                         std::string("residual-state=") + controller_state)
                                 .c_str());
        if (!DispatchZygoteControlRpc(registry,
                                      zygote_pid,
                                      process_name,
                                      "clear",
                                      "soft",
                                      5000,
                                      clear_request,
                                      error_message,
                                      rpc_invoker)) {
            if (registry != nullptr && !process_name.empty()) {
                registry->ClearOwnedZygoteControlProcess(process_name);
            }
            return false;
        }
    }

    bool install_done = false;
    if (HasImmediateControlSession(registry, zygote_pid, process_name)) {
        NOOK_ZYGOTE_RPC_LOGI("%s",
                             FormatZygoteControlLogState("install",
                                                         "begin",
                                                         zygote_pid,
                                                         process_name,
                                                         "spawn.install",
                                                         "soft",
                                                         "")
                                 .c_str());
        comm::SpawnInstallResponse spawn_install_response;
        std::string spawn_install_error;
        if (SendSpawnInstallToControlSessionForTest(registry,
                                                    zygote_pid,
                                                    process_name,
                                                    5000,
                                                    spawn_install_request,
                                                    &spawn_install_response,
                                                    &spawn_install_error,
                                                    spawn_install_sender)) {
            NOOK_ZYGOTE_RPC_LOGI("%s",
                                 FormatZygoteControlLogState("install",
                                                             "spawn-ok",
                                                             zygote_pid,
                                                             process_name,
                                                             "spawn.install",
                                                             "soft",
                                                             "")
                                     .c_str());
            install_done = true;
        } else {
            NOOK_ZYGOTE_RPC_LOGE("%s",
                                 FormatZygoteControlLogState("install",
                                                             "spawn-fail",
                                                             zygote_pid,
                                                             process_name,
                                                             "spawn.install",
                                                             IsSpawnControlCompatFallbackError(spawn_install_error) ? "soft" : "hard",
                                                             spawn_install_error)
                                     .c_str());
            if (!IsSpawnControlCompatFallbackError(spawn_install_error)) {
                if (error_message != nullptr) {
                    *error_message = spawn_install_error;
                }
                if (registry != nullptr && !process_name.empty()) {
                    registry->ClearOwnedZygoteControlProcess(process_name);
                }
                return false;
            }
        }
    }

    if (!install_done) {
        if (!DispatchZygoteControlRpc(registry,
                                      zygote_pid,
                                      process_name,
                                      "install",
                                      "soft",
                                      5000,
                                      request,
                                      error_message,
                                      rpc_invoker)) {
            if (registry != nullptr && !process_name.empty()) {
                registry->ClearOwnedZygoteControlProcess(process_name);
            }
            return false;
        }
    }

    const bool ready = WaitForZygoteControlReadyWithTimeoutForTest(registry,
                                                                   zygote_pid,
                                                                   process_name,
                                                                   1500,
                                                                   500,
                                                                   25,
                                                                   error_message,
                                                                   rpc_invoker);
    if (ready && registry != nullptr && !process_name.empty()) {
        registry->MarkOwnedZygoteControlProcess(zygote_pid, process_name);
    } else if (registry != nullptr && !process_name.empty()) {
        registry->ClearOwnedZygoteControlProcess(process_name);
    }

    return ready;
}

bool UninstallZygoteForkHook(SessionRegistry* registry,
                             int zygote_pid,
                             const std::string& process_name,
                             std::string* error_message,
                             const ZygoteRpcInvoker& rpc_invoker) {
    return UninstallZygoteForkHookWithSendersForTest(registry,
                                                     zygote_pid,
                                                     process_name,
                                                     error_message,
                                                     rpc_invoker,
                                                     SendSpawnUninstallThroughSession);
}

bool UninstallZygoteForkHookWithSendersForTest(SessionRegistry* registry,
                                               int zygote_pid,
                                               const std::string& process_name,
                                               std::string* error_message,
                                               const ZygoteRpcInvoker& rpc_invoker,
                                               const ZygoteSessionSpawnUninstallSender& spawn_uninstall_sender) {
    (void) zygote_pid;
    comm::RpcRequest request;
    request.method = "nook.spawn.uninstallForkHook";
    request.args_json = "{}";
    comm::SpawnUninstallRequest spawn_uninstall_request;
    spawn_uninstall_request.spawn_token.clear();
    const bool owned_target = registry != nullptr &&
                              !process_name.empty() &&
                              registry->IsOwnedZygoteControlTarget(zygote_pid, process_name);

    if (HasOwnedImmediateControlSession(registry, zygote_pid, process_name)) {
        NOOK_ZYGOTE_RPC_LOGI("%s",
                             FormatZygoteControlLogState("uninstall",
                                                         "begin",
                                                         zygote_pid,
                                                         process_name,
                                                         "spawn.uninstall",
                                                         "hard",
                                                         "")
                                 .c_str());
        comm::SpawnUninstallResponse spawn_uninstall_response;
        std::string spawn_uninstall_error;
        if (SendSpawnUninstallToControlSessionForTest(registry,
                                                      zygote_pid,
                                                      process_name,
                                                      5000,
                                                      spawn_uninstall_request,
                                                      &spawn_uninstall_response,
                                                      &spawn_uninstall_error,
                                                      spawn_uninstall_sender)) {
            NOOK_ZYGOTE_RPC_LOGI("%s",
                                 FormatZygoteControlLogState("uninstall",
                                                             "spawn-ok",
                                                             zygote_pid,
                                                             process_name,
                                                             "spawn.uninstall",
                                                             "hard",
                                                             "")
                                     .c_str());
            if (registry != nullptr && !process_name.empty()) {
                registry->ClearOwnedZygoteControlProcess(process_name);
            }
            return true;
        }

        NOOK_ZYGOTE_RPC_LOGE("%s",
                             FormatZygoteControlLogState("uninstall",
                                                         "spawn-fail",
                                                         zygote_pid,
                                                         process_name,
                                                         "spawn.uninstall",
                                                         IsSpawnControlCompatFallbackError(spawn_uninstall_error) ? "soft" : "hard",
                                                         spawn_uninstall_error)
                                 .c_str());
        if (!IsSpawnControlCompatFallbackError(spawn_uninstall_error)) {
            if (error_message != nullptr) {
                *error_message = spawn_uninstall_error;
            }
            if (error_message != nullptr &&
                IsUsapProcessName(process_name) &&
                error_message->find("zygote control-ready agent session not found") != std::string::npos) {
                NOOK_ZYGOTE_RPC_LOGI("%s",
                                     FormatZygoteControlLogState("uninstall",
                                                                 "skip-missing-usap-session",
                                                                 zygote_pid,
                                                                 process_name,
                                                                 request.method.c_str(),
                                                                 "soft",
                                                                 *error_message)
                                         .c_str());
                if (registry != nullptr && !process_name.empty()) {
                    registry->ClearOwnedZygoteControlProcess(process_name);
                }
                error_message->clear();
                return true;
            }
            return false;
        }
    }

    if (owned_target && !HasImmediateControlSession(registry, zygote_pid, process_name)) {
        if (error_message != nullptr) {
            if (zygote_pid > 0) {
                *error_message = "zygote control-ready agent session not found pid=" +
                                 std::to_string(zygote_pid) +
                                 " process=" + process_name;
            } else {
                *error_message = "zygote control-ready agent session not found: " + process_name;
            }
        }
        NOOK_ZYGOTE_RPC_LOGI("%s",
                             FormatZygoteControlLogState("uninstall",
                                                         "skip-missing-owned-session",
                                                         zygote_pid,
                                                         process_name,
                                                         request.method.c_str(),
                                                         "soft",
                                                         error_message != nullptr ? *error_message : "")
                                 .c_str());
        if (registry != nullptr && !process_name.empty()) {
            registry->ClearOwnedZygoteControlProcess(process_name);
        }
        if (error_message != nullptr &&
            IsUsapProcessName(process_name) &&
            error_message->find("zygote control-ready agent session not found") != std::string::npos) {
            error_message->clear();
            return true;
        }
        return false;
    }

    if (DispatchZygoteControlRpc(registry,
                                 zygote_pid,
                                 process_name,
                                 "uninstall",
                                 "hard",
                                 5000,
                                 request,
                                 error_message,
                                 rpc_invoker)) {
        if (registry != nullptr && !process_name.empty()) {
            registry->ClearOwnedZygoteControlProcess(process_name);
        }
        return true;
    }

    if (error_message != nullptr &&
        IsUsapProcessName(process_name) &&
        error_message->find("zygote control-ready agent session not found") != std::string::npos) {
        NOOK_ZYGOTE_RPC_LOGI("%s",
                             FormatZygoteControlLogState("uninstall",
                                                         "skip-missing-usap-session",
                                                         zygote_pid,
                                                         process_name,
                                                         request.method.c_str(),
                                                         "soft",
                                                         *error_message)
                                 .c_str());
        if (registry != nullptr && !process_name.empty()) {
            registry->ClearOwnedZygoteControlProcess(process_name);
        }
        error_message->clear();
        return true;
    }

    return false;
}

}  // namespace server
}  // namespace nook
