#include "../src/communication/protocol/frame.h"

#include "spawn_controller.h"

#include "injector.h"
#include "server_handlers.h"
#include "session_registry.h"
#include "../src/communication/protocol/messages.h"
#include "../src/communication/session/session.h"

#include <atomic>
#include <sstream>

#if defined(__ANDROID__)
#include <android/log.h>
#define NOOK_SERVER_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NookServer", __VA_ARGS__))
#define NOOK_SERVER_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "NookServer", __VA_ARGS__))
#else
#define NOOK_SERVER_LOGI(...) ((void)0)
#define NOOK_SERVER_LOGE(...) ((void)0)
#endif

namespace nook {
namespace server {

namespace {

constexpr uint32_t kDefaultSpawnReadyTimeoutMs = 15000;

const char* PendingSpawnReadyStageName(PendingSpawnReadyStage stage) {
    switch (stage) {
        case PendingSpawnReadyStage::kControlReady:
            return "control";
        case PendingSpawnReadyStage::kRuntimeReady:
            return "runtime";
        default:
            return "unknown";
    }
}

std::string MakeSpawnToken(uint32_t host_session_id, const std::string& identifier) {
    static std::atomic<uint64_t> counter{1};
    std::ostringstream oss;
    oss << "spawn-" << host_session_id << "-" << counter.fetch_add(1, std::memory_order_relaxed);
    if (!identifier.empty()) {
        oss << "-" << identifier;
    }
    return oss.str();
}

bool SendSpawnResponse(comm::Session& session,
                       uint32_t msg_id,
                       const comm::SpawnResponse& response) {
    return session.SendFrame(comm::Frame(comm::MessageType::kSpawnResponse,
                                         msg_id,
                                         comm::EncodeSpawnResponse(response)));
}

void FinalizeSpawnBestEffort(Injector* injector, const comm::SpawnRequest& request) {
    if (injector == nullptr) {
        return;
    }

    std::string finalize_error;
    (void) injector->FinalizeSpawn(request, &finalize_error);
}

bool IsRegisteredHostSession(SessionRegistry* registry, comm::Session& session) {
    if (registry == nullptr) {
        return true;
    }

    return registry->IsRegisteredHostSession(session.GetId(), &session);
}

bool ReplySpawnFailureIfHostPresent(SessionRegistry* registry,
                                    comm::Session& session,
                                    uint32_t msg_id,
                                    int authoritative_pid,
                                    const char* drop_reason,
                                    const comm::ErrorInfo& error) {
    if (!IsRegisteredHostSession(registry, session)) {
        if (authoritative_pid > 0) {
            NOOK_SERVER_LOGI("drop spawn %s response host_session=%u pid=%d reason=host-missing",
                             drop_reason,
                             session.GetId(),
                             authoritative_pid);
        } else {
            NOOK_SERVER_LOGI("drop spawn %s response host_session=%u reason=host-missing",
                             drop_reason,
                             session.GetId());
        }
        return false;
    }

    SendSpawnResponse(session,
                      msg_id,
                      comm::SpawnResponse{0, error});
    return true;
}

void MaybePromoteLateBoundControlReadyChild(SessionRegistry* registry,
                                            Injector* injector,
                                            const ServerHandlerConfig& config,
                                            uint32_t host_session_id,
                                            int pid) {
    if (registry == nullptr || injector == nullptr || pid <= 0) {
        NOOK_SERVER_LOGI("late promotion skip pid=%d reason=invalid-args registry=%d injector=%d",
                         pid,
                         registry != nullptr ? 1 : 0,
                         injector != nullptr ? 1 : 0);
        return;
    }
    const LatePromotionEvaluation evaluation =
        registry->EvaluateLatePromotionEligibility(pid,
                                                   host_session_id,
                                                   config.agent_path);
    if (evaluation.eligibility != LatePromotionEligibility::kEligible) {
        switch (evaluation.eligibility) {
            case LatePromotionEligibility::kEmptyAgentPath:
                NOOK_SERVER_LOGI("late promotion skip pid=%d reason=empty-agent-path", pid);
                break;
            case LatePromotionEligibility::kSpawnEntryMissing:
                NOOK_SERVER_LOGI("late promotion skip pid=%d reason=spawn-entry-missing", pid);
                break;
            case LatePromotionEligibility::kNotControlReady:
                NOOK_SERVER_LOGI("late promotion skip pid=%d reason=ready-state runtime=%d control=%d entry_stage=%d",
                                 pid,
                                 evaluation.has_entry &&
                                         evaluation.entry.authoritative_ready_stage ==
                                             PendingSpawnReadyStage::kRuntimeReady
                                     ? 1
                                     : 0,
                                 evaluation.has_entry &&
                                             (evaluation.entry.authoritative_ready_stage ==
                                                  PendingSpawnReadyStage::kRuntimeReady ||
                                              evaluation.entry.authoritative_ready_stage ==
                                                  PendingSpawnReadyStage::kControlReady)
                                         ? 1
                                         : 0,
                                 evaluation.has_entry
                                     ? static_cast<int>(evaluation.entry.authoritative_ready_stage)
                                     : 0);
                break;
            case LatePromotionEligibility::kSpawnEntryMismatch:
                NOOK_SERVER_LOGI("late promotion skip pid=%d reason=spawn-entry-mismatch entry_host=%u expected_host=%u state=%d",
                                 pid,
                                 evaluation.has_entry ? evaluation.entry.host_session_id : 0,
                                 host_session_id,
                                 evaluation.has_entry ? static_cast<int>(evaluation.entry.state) : 0);
                break;
            case LatePromotionEligibility::kOwnedHostMissing:
                NOOK_SERVER_LOGI("late promotion skip pid=%d reason=spawn-owned-host-missing host_session=%u",
                                 pid,
                                 evaluation.has_entry ? evaluation.entry.host_session_id : 0);
                break;
            default:
                break;
        }
        return;
    }

    NOOK_SERVER_LOGI("late promotion begin pid=%d host_session=%u agent=%s",
                     pid,
                     host_session_id,
                     config.agent_path.c_str());
    const std::string agent_path = config.agent_path;
    SpawnSuspendedEntry current_entry;
    switch (registry->RecheckLatePromotionBeforeInject(pid, host_session_id, &current_entry)) {
        case LatePromotionRecheckResult::kTransactionMissingBeforeInject:
            NOOK_SERVER_LOGI("late promotion cancel pid=%d reason=transaction-missing-before-inject", pid);
            return;
        case LatePromotionRecheckResult::kRuntimeReadyBeforeInject:
            NOOK_SERVER_LOGI("late promotion cancel pid=%d reason=runtime-ready-before-inject", pid);
            return;
        case LatePromotionRecheckResult::kTransactionChanged:
            NOOK_SERVER_LOGI("late promotion cancel pid=%d reason=transaction-changed", pid);
            return;
        case LatePromotionRecheckResult::kProceed:
            break;
    }
    if (current_entry.host_session_id != host_session_id) {
        return;
    }
    std::string inject_error;
    const bool ok = injector->InjectSpawnChildAgent(pid, agent_path, &inject_error);
    if (!ok) {
        NOOK_SERVER_LOGE("spawn child full-agent late promotion failed pid=%d agent=%s error=%s",
                         pid,
                         agent_path.c_str(),
                         inject_error.empty() ? "inject failed" : inject_error.c_str());
        return;
    }
    registry->UpdateSpawnState(pid, SpawnTransactionState::kWaitingRuntimeReady);
    NOOK_SERVER_LOGI("spawn child full-agent late promotion completed pid=%d agent=%s",
                     pid,
                     agent_path.c_str());
}

}  // namespace

void ExecuteSpawnRequest(SessionRegistry* registry,
                         Injector* injector,
                         const ServerHandlerConfig& config,
                         comm::Session& session,
                         const comm::Frame& frame) {
    comm::SpawnRequest request;
    if (!comm::DecodeSpawnRequest(frame.GetPayload().data(), frame.GetPayload().size(), &request)) {
        NOOK_SERVER_LOGE("invalid SPAWN_REQUEST");
        SendSpawnResponse(session,
                          frame.GetMsgId(),
                          comm::SpawnResponse{0, comm::ErrorInfo{-1, "invalid spawn request"}});
        return;
    }

    if (request.identifier.empty()) {
        NOOK_SERVER_LOGE("spawn identifier is empty");
        SendSpawnResponse(session,
                          frame.GetMsgId(),
                          comm::SpawnResponse{0, comm::ErrorInfo{-2, "spawn identifier is empty"}});
        return;
    }

    const std::string spawn_token = MakeSpawnToken(session.GetId(), request.identifier);

    if (registry != nullptr) {
        registry->RegisterPendingSpawn(spawn_token, request.identifier, session.GetId());
    }

    int pid = 0;
    std::string error_message;
    comm::SpawnRequest injector_request = request;
    injector_request.argv.insert(injector_request.argv.begin(), std::string("--nook-spawn-token=") + spawn_token);
    if (injector == nullptr ||
        !injector->Spawn(injector_request, config.agent_path, &pid, &error_message) ||
        pid <= 0) {
        if (registry != nullptr) {
            registry->ClearPendingSpawn(spawn_token);
        }
        if (error_message.empty()) {
            error_message = "spawn injector failed";
        }
        NOOK_SERVER_LOGE("spawn failed pkg=%s agent=%s error=%s",
                         request.identifier.c_str(),
                         config.agent_path.c_str(),
                         error_message.c_str());
        SendSpawnResponse(session,
                          frame.GetMsgId(),
                          comm::SpawnResponse{0, comm::ErrorInfo{-3, error_message}});
        return;
    }

    int authoritative_pid = 0;
    PendingSpawnEntry resolved_pending_spawn;
    const uint32_t spawn_ready_timeout_ms =
        config.spawn_ready_timeout_ms > 0 ? config.spawn_ready_timeout_ms
                                          : kDefaultSpawnReadyTimeoutMs;
    if (registry == nullptr ||
        !registry->WaitForPendingSpawn(spawn_token,
                                       spawn_ready_timeout_ms,
                                       &authoritative_pid) ||
        authoritative_pid <= 0) {
        FinalizeSpawnBestEffort(injector, request);
        if (registry != nullptr) {
            registry->CleanupTimedOutSpawnTransaction(spawn_token, authoritative_pid);
        }
        NOOK_SERVER_LOGE("spawn failed pkg=%s agent=%s error=authoritative agent ready timeout",
                         request.identifier.c_str(),
                         config.agent_path.c_str());
        (void) ReplySpawnFailureIfHostPresent(registry,
                                              session,
                                              frame.GetMsgId(),
                                              authoritative_pid,
                                              "timeout",
                                              comm::ErrorInfo{-4, "spawn authoritative agent ready timed out"});
        return;
    }
    bool host_registered_for_spawn_transaction = true;
    if (registry != nullptr) {
        const ResolvedPendingSpawnHandoffResult handoff_result =
            registry->PrepareResolvedPendingSpawnHandoff(spawn_token,
                                                         authoritative_pid,
                                                         session.GetId(),
                                                         &session);
        host_registered_for_spawn_transaction =
            handoff_result.disposition ==
            ResolvedPendingSpawnHandoffDisposition::kBoundToRegisteredHost;
        resolved_pending_spawn = handoff_result.pending_spawn;
        if (handoff_result.disposition ==
            ResolvedPendingSpawnHandoffDisposition::kMissingForRegisteredHost) {
                FinalizeSpawnBestEffort(injector, request);
                registry->CleanupFailedBoundSpawnTransaction(session.GetId(),
                                                             authoritative_pid,
                                                             spawn_token,
                                                             false);
                NOOK_SERVER_LOGE("spawn failed pkg=%s agent=%s error=pending spawn entry disappeared after ready",
                                 request.identifier.c_str(),
                                 config.agent_path.c_str());
                (void) ReplySpawnFailureIfHostPresent(registry,
                                                      session,
                                                      frame.GetMsgId(),
                                                      authoritative_pid,
                                                      "missing-pending",
                                                      comm::ErrorInfo{-4, "spawn authoritative agent ready timed out"});
                return;
        }
        if (host_registered_for_spawn_transaction) {
            if (config.on_spawn_context_bound) {
                config.on_spawn_context_bound(authoritative_pid, spawn_token);
            }
        }
    }

    if (injector != nullptr) {
        std::string finalize_error;
        if (!injector->FinalizeSpawn(request, &finalize_error)) {
            if (registry != nullptr) {
                registry->CleanupFailedBoundSpawnTransaction(
                    session.GetId(),
                    authoritative_pid,
                    spawn_token,
                    host_registered_for_spawn_transaction);
            }
            if (finalize_error.empty()) {
                finalize_error = "spawn finalize failed";
            }
            NOOK_SERVER_LOGE("spawn finalize failed pkg=%s agent=%s error=%s",
                             request.identifier.c_str(),
                             config.agent_path.c_str(),
                             finalize_error.c_str());
            (void) ReplySpawnFailureIfHostPresent(registry,
                                                  session,
                                                  frame.GetMsgId(),
                                                  authoritative_pid,
                                                  "finalize-failure",
                                                  comm::ErrorInfo{-5, finalize_error});
            return;
        }
    }

    if (registry != nullptr && host_registered_for_spawn_transaction) {
        MaybePromoteLateBoundControlReadyChild(registry,
                                               injector,
                                               config,
                                               session.GetId(),
                                               authoritative_pid);
    }

    PostFinalizeSpawnContext post_finalize_context{
        resolved_pending_spawn.ready_stage,
        resolved_pending_spawn.resolved_process_name
    };
    if (registry != nullptr) {
        post_finalize_context = registry->ResolvePostFinalizeSpawnContext(
            authoritative_pid,
            resolved_pending_spawn.ready_stage,
            resolved_pending_spawn);
    }

    NOOK_SERVER_LOGI("spawn success pkg=%s pid=%d agent=%s ready_stage=%s",
                     request.identifier.c_str(),
                     authoritative_pid,
                     config.agent_path.c_str(),
                     PendingSpawnReadyStageName(post_finalize_context.ready_stage));
    if (registry != nullptr && host_registered_for_spawn_transaction) {
        const BindSuspendedSpawnAfterFinalizeResult bind_result =
            registry->BindSuspendedSpawnAfterFinalize(authoritative_pid,
                                                      session.GetId(),
                                                      post_finalize_context,
                                                      resolved_pending_spawn,
                                                      spawn_token);
        if (bind_result.waiting_runtime_ready) {
            NOOK_SERVER_LOGI("spawn waiting runtime-ready pid=%d host_session=%u",
                             authoritative_pid,
                             session.GetId());
        }
    }
    if (!IsRegisteredHostSession(registry, session)) {
        if (registry != nullptr) {
            registry->CleanupDroppedSuccessfulSpawnResponse(authoritative_pid);
        }
        NOOK_SERVER_LOGI("drop spawn success response host_session=%u pid=%d reason=host-missing",
                         session.GetId(),
                         authoritative_pid);
        return;
    }
    if (!SendSpawnResponse(session,
                           frame.GetMsgId(),
                           comm::SpawnResponse{static_cast<uint32_t>(authoritative_pid), {}})) {
        if (registry != nullptr) {
            registry->CleanupDroppedSuccessfulSpawnResponse(authoritative_pid);
        }
        NOOK_SERVER_LOGI("drop spawn success replay host_session=%u pid=%d reason=response-send-failed",
                         session.GetId(),
                         authoritative_pid);
        return;
    }
    bool should_replay_runtime_ready = false;
    if (registry != nullptr && host_registered_for_spawn_transaction) {
        const SpawnResponseCommitResult commit_result =
            registry->CommitSpawnResponseSuccess(authoritative_pid,
                                                 post_finalize_context.ready_stage,
                                                 resolved_pending_spawn);
        post_finalize_context = commit_result.context;
        should_replay_runtime_ready = commit_result.should_replay_runtime_ready;
    } else if (registry != nullptr) {
        should_replay_runtime_ready =
            post_finalize_context.ready_stage == PendingSpawnReadyStage::kRuntimeReady;
    }
    if (registry != nullptr && should_replay_runtime_ready) {
        const std::string runtime_process_name =
            post_finalize_context.runtime_process_name;
        (void) ReplayCachedAgentReadyThenScriptMessages(registry,
                                                        authoritative_pid,
                                                        runtime_process_name,
                                                        session,
                                                        session.GetId(),
                                                        true);
    }
}

}  // namespace server
}  // namespace nook
