#include "server_handlers.h"

#include "spawn_controller.h"

#include "../src/communication/handler/message_dispatcher.h"
#include "../src/communication/protocol/frame.h"
#include "../src/communication/protocol/message_types.h"
#include "../src/communication/protocol/messages.h"
#include "../src/communication/session/session.h"
#include "injector.h"
#include "session_registry.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define NOOK_SERVER_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NookServer", __VA_ARGS__))
#define NOOK_SERVER_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "NookServer", __VA_ARGS__))
#else
#define NOOK_SERVER_LOGI(...) ((void)0)
#define NOOK_SERVER_LOGE(...) ((void)0)
#endif

#include <atomic>
#include <memory>
#include <sstream>
#include <thread>

namespace nook {
namespace server {

bool ReplayCachedScriptMessages(SessionRegistry* registry,
                                int pid,
                                comm::Session& session,
                                const char* log_context,
                                uint32_t host_session_id) {
    if (registry == nullptr || pid <= 0) {
        return true;
    }

    std::vector<comm::Frame> cached_messages = registry->GetScriptMessageFrames(pid);
    bool replay_ok = true;
    size_t sent_count = 0;
    for (const comm::Frame& cached_message : cached_messages) {
        NOOK_SERVER_LOGI("%s pid=%d host_session=%u", log_context, pid, host_session_id);
        if (!session.SendFrame(cached_message)) {
            replay_ok = false;
            break;
        }
        ++sent_count;
    }
    if (replay_ok && !cached_messages.empty()) {
        registry->ClearScriptMessageFrames(pid);
    } else if (!replay_ok && sent_count > 0) {
        registry->DropScriptMessageFramePrefix(pid, sent_count);
    }
    return replay_ok;
}

bool ReplayCachedAgentReadyThenScriptMessages(SessionRegistry* registry,
                                              int pid,
                                              const std::string& process_name,
                                              comm::Session& session,
                                              uint32_t host_session_id,
                                              bool require_runtime_stage) {
    if (registry == nullptr || pid <= 0) {
        return true;
    }

    comm::Frame cached_ready;
    bool ready_replayed = true;
    if (registry->GetAgentReadyFrameByIdentity(pid, process_name, &cached_ready)) {
        if (require_runtime_stage) {
            comm::AgentReady ready;
            if (comm::DecodeAgentReady(cached_ready.GetPayload().data(),
                                       cached_ready.GetPayload().size(),
                                       &ready) &&
                ready.stage == comm::AgentReadyStage::kRuntime) {
                NOOK_SERVER_LOGI("replay cached AGENT_READY pid=%d host_session=%u",
                                 pid,
                                 host_session_id);
                ready_replayed = session.SendFrame(cached_ready);
            } else {
                NOOK_SERVER_LOGI("skip replay non-runtime AGENT_READY pid=%d host_session=%u",
                                 pid,
                                 host_session_id);
            }
        } else {
            NOOK_SERVER_LOGI("replay cached AGENT_READY pid=%d host_session=%u",
                             pid,
                             host_session_id);
            ready_replayed = session.SendFrame(cached_ready);
        }
    } else if (require_runtime_stage) {
        NOOK_SERVER_LOGI("runtime-ready without cached AGENT_READY pid=%d host_session=%u",
                         pid,
                         host_session_id);
    }

    if (!ready_replayed) {
        return false;
    }

    return ReplayCachedScriptMessages(registry,
                                      pid,
                                      session,
                                      "replay cached SCRIPT_MESSAGE",
                                      host_session_id);
}

bool MarkRuntimeReadyVisibleAndReplayCachedScriptMessages(SessionRegistry* registry,
                                                          int pid,
                                                          comm::Session& session,
                                                          uint32_t host_session_id) {
    if (registry == nullptr || pid <= 0) {
        return false;
    }

    const RuntimeReadyCommitResult commit_result =
        registry->CommitRuntimeReadyVisibility(pid);
    if (!commit_result.should_replay_script_messages) {
        return commit_result.runtime_visible;
    }
    return ReplayCachedScriptMessages(registry,
                                      pid,
                                      session,
                                      "replay cached SCRIPT_MESSAGE after runtime-ready",
                                      host_session_id);
}

bool CanExposeRuntimeReadyImmediately(SessionRegistry* registry,
                                      int pid,
                                      bool eligible_runtime_ready) {
    if (registry == nullptr || pid <= 0 || !eligible_runtime_ready) {
        return false;
    }
    return registry->CanExposeSpawnRuntimeReadyImmediately(pid);
}

namespace {

bool IsRuntimeReady(const comm::AgentReady& ready);
bool IsAcceptedSpawnControlStageSession(SessionRegistry* registry,
                                        int pid,
                                        const std::string& process_name,
                                        const comm::Session* session);

void HandleAgentReadyForwarding(SessionRegistry* registry,
                                int ready_pid,
                                const comm::AgentReady& ready,
                                const comm::Frame& frame,
                                const std::string& expected_spawn_process_name,
                                comm::Session* host,
                                bool runtime_ready,
                                bool eligible_runtime_ready) {
    const AgentReadyForwardDecision decision =
        registry != nullptr
            ? registry->EvaluateAgentReadyForwarding(
                  ready_pid,
                  runtime_ready,
                  eligible_runtime_ready,
                  host != nullptr)
            : AgentReadyForwardDecision{};

    if (decision.action == AgentReadyForwardAction::kExposeRuntimeWithoutHost) {
        (void) registry->MarkSpawnRuntimeReadyVisible(ready_pid);
    }
    if (decision.action == AgentReadyForwardAction::kForwardRuntimeToHost &&
        host != nullptr) {
        NOOK_SERVER_LOGI("forward runtime-stage AGENT_READY pid=%u name=%s host_session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         host->GetId());
        if (host->SendFrame(comm::Frame(comm::MessageType::kAgentReady,
                                        frame.GetMsgId(),
                                        frame.GetPayload()))) {
            (void) MarkRuntimeReadyVisibleAndReplayCachedScriptMessages(registry,
                                                                        ready_pid,
                                                                        *host,
                                                                        host->GetId());
        } else {
            NOOK_SERVER_LOGI("runtime-stage AGENT_READY forward failed pid=%u name=%s host_session=%u",
                             ready.pid,
                             ready.process_name.c_str(),
                             host->GetId());
        }
    } else if (decision.action ==
                   AgentReadyForwardAction::kHoldRuntimeUntilSpawnResponse &&
               host != nullptr) {
        NOOK_SERVER_LOGI("hold runtime-stage AGENT_READY until spawn response pid=%u name=%s host_session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         host->GetId());
    } else if (decision.action ==
                   AgentReadyForwardAction::kDropMismatchedRuntimeForHost &&
               host != nullptr) {
        NOOK_SERVER_LOGI("drop mismatched runtime-stage AGENT_READY pid=%u name=%s expected=%s host_session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         expected_spawn_process_name.c_str(),
                         host->GetId());
    } else if (decision.action == AgentReadyForwardAction::kHoldControlForHost &&
               host != nullptr) {
        NOOK_SERVER_LOGI("hold control-stage AGENT_READY pid=%u name=%s host_session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         host->GetId());
    } else {
        NOOK_SERVER_LOGI("%s-stage AGENT_READY without bound host pid=%u name=%s",
                         runtime_ready ? "runtime" : "control",
                         ready.pid,
                         ready.process_name.c_str());
    }
}

bool IsAcceptedAgentSessionForHostResponse(SessionRegistry* registry,
                                           int pid,
                                           const comm::Session* session);
comm::Session* ResolveBoundHostSessionForPid(SessionRegistry* registry, int pid);
comm::Session* ResolveBoundAgentSessionForHostRequest(SessionRegistry* registry, int pid);
int ResolveHostOwnedPidForRequest(SessionRegistry* registry, uint32_t host_session_id);

std::string MakeAttachReadyToken(uint32_t host_session_id,
                                 uint32_t msg_id,
                                 int pid,
                                 const std::string& process_name) {
    std::ostringstream oss;
    oss << "attach-" << host_session_id << "-" << msg_id << "-" << pid << "-" << process_name;
    return oss.str();
}

bool GetSpawnEntry(SessionRegistry* registry, int pid, SpawnSuspendedEntry* entry) {
    return registry != nullptr && pid > 0 && registry->GetSpawnSuspendedEntry(pid, entry);
}

uint32_t ResolveSuspendedSpawnOwnerHostSessionId(SessionRegistry* registry, int pid) {
    if (registry == nullptr || pid <= 0) {
        return 0;
    }
    return registry->FindSuspendedSpawnOwnerHostSessionId(pid);
}

bool IsForeignSuspendedSpawnOwner(SessionRegistry* registry, int pid, uint32_t host_session_id) {
    if (registry == nullptr || host_session_id == 0) {
        return false;
    }
    return registry->IsForeignSuspendedSpawnOwner(pid, host_session_id);
}

enum class HostResponseForwardError : uint8_t {
    kNone = 0,
    kInvalidSource,
    kHostNotFound,
};

struct AgentReadyPreRegistrationResult {
    bool should_return = false;
};

struct AgentReadyEarlyDropResult {
    bool should_return = false;
};

struct HostResponseForwardTarget {
    int pid = -1;
    comm::Session* host = nullptr;
    HostResponseForwardError error = HostResponseForwardError::kNone;
};

struct HostRequestFailure {
    bool failed = false;
    int error_code = 0;
    std::string error_message;
};

HostRequestFailure DescribeHostBoundAgentLookupFailure(HostBoundAgentLookupError error,
                                                       const char* operation_name) {
    HostRequestFailure failure;
    switch (error) {
        case HostBoundAgentLookupError::kNone:
            return failure;
        case HostBoundAgentLookupError::kHostNotBound:
            failure.failed = true;
            failure.error_code = -3;
            failure.error_message = "host session is not bound to a pid";
            return failure;
        case HostBoundAgentLookupError::kSpawnNotReady:
            failure.failed = true;
            failure.error_code = -5;
            failure.error_message =
                std::string("spawned pid is not ready for ") + operation_name;
            return failure;
        case HostBoundAgentLookupError::kAgentNotReady:
            failure.failed = true;
            failure.error_code = -4;
            failure.error_message = "agent session not ready for bound pid";
            return failure;
        default:
            failure.failed = true;
            failure.error_code = -4;
            failure.error_message = "agent session not ready for bound pid";
            return failure;
    }
}

template <typename FailureResponder>
bool HandleHostBoundAgentRequestLookupFailure(const HostBoundAgentRequestTarget& resolved,
                                              const HostRequestFailure& failure,
                                              const char* operation_name,
                                              uint32_t host_session_id,
                                              FailureResponder&& send_failure) {
    if (resolved.error == HostBoundAgentLookupError::kHostNotBound) {
        NOOK_SERVER_LOGI("%s without bound pid host_session=%u",
                         operation_name,
                         host_session_id);
    } else if (resolved.error == HostBoundAgentLookupError::kAgentNotReady) {
        NOOK_SERVER_LOGI("%s without agent session pid=%d host_session=%u",
                         operation_name,
                         resolved.pid,
                         host_session_id);
    }

    if (!failure.failed) {
        return false;
    }

    send_failure(failure);
    return true;
}

template <typename FailureResponder>
void HandleHostBoundAgentRequestSendFailure(const char* operation_name,
                                            int pid,
                                            uint32_t host_session_id,
                                            FailureResponder&& send_failure) {
    NOOK_SERVER_LOGI("%s send failed pid=%d host_session=%u",
                     operation_name,
                     pid,
                     host_session_id);
    send_failure();
}

template <typename FailureResponder, typename SendFailureResponder>
bool ForwardHostBoundAgentRequest(const HostBoundAgentRequestTarget& resolved,
                                  const HostRequestFailure& failure,
                                  const char* operation_name,
                                  uint32_t host_session_id,
                                  const comm::Frame& frame,
                                  FailureResponder&& send_lookup_failure,
                                  SendFailureResponder&& send_send_failure) {
    if (HandleHostBoundAgentRequestLookupFailure(resolved,
                                                 failure,
                                                 operation_name,
                                                 host_session_id,
                                                 std::forward<FailureResponder>(send_lookup_failure))) {
        return false;
    }

    NOOK_SERVER_LOGI("forward %s pid=%d host_session=%u",
                     operation_name,
                     resolved.pid,
                     host_session_id);
    if (!resolved.agent->SendFrame(frame)) {
        HandleHostBoundAgentRequestSendFailure(operation_name,
                                               resolved.pid,
                                               host_session_id,
                                               std::forward<SendFailureResponder>(send_send_failure));
        return false;
    }
    return true;
}

template <typename LookupFailureResponder, typename SendFailureResponder>
bool ForwardHostBoundScriptLoadRequest(SessionRegistry* registry,
                                       const HostBoundAgentRequestTarget& resolved,
                                       const HostRequestFailure& failure,
                                       uint32_t host_session_id,
                                       const comm::Frame& frame,
                                       LookupFailureResponder&& send_lookup_failure,
                                       SendFailureResponder&& send_send_failure) {
    if (HandleHostBoundAgentRequestLookupFailure(
            resolved,
            failure,
            "script load",
            host_session_id,
            std::forward<LookupFailureResponder>(send_lookup_failure))) {
        return false;
    }

    NOOK_SERVER_LOGI("forward SCRIPT_LOAD pid=%d host_session=%u",
                     resolved.pid,
                     host_session_id);
    if (registry != nullptr) {
        (void) registry->MarkSpawnScriptLoadInFlight(resolved.pid);
    }
    if (!resolved.agent->SendFrame(frame)) {
        HandleHostBoundAgentRequestSendFailure(
            "script load",
            resolved.pid,
            host_session_id,
            [&]() {
                if (registry != nullptr) {
                    (void) registry->MarkSpawnScriptLoadComplete(resolved.pid);
                }
                send_send_failure();
            });
        return false;
    }

    return true;
}

template <typename LookupFailureHandler, typename SendFailureHandler>
bool ForwardHostBoundAgentRequestNoResponse(const HostBoundAgentRequestTarget& resolved,
                                            const char* operation_name,
                                            uint32_t host_session_id,
                                            const comm::Frame& frame,
                                            LookupFailureHandler&& on_lookup_failure,
                                            SendFailureHandler&& on_send_failure) {
    if (resolved.error == HostBoundAgentLookupError::kHostNotBound) {
        NOOK_SERVER_LOGI("%s without bound pid host_session=%u",
                         operation_name,
                         host_session_id);
        on_lookup_failure(resolved.error, resolved.pid);
        return false;
    }
    if (resolved.error == HostBoundAgentLookupError::kSpawnNotReady) {
        NOOK_SERVER_LOGI("drop %s while spawn runtime not ready pid=%d host_session=%u",
                         frame.GetType() == comm::MessageType::kScriptPost ? "SCRIPT_POST" : operation_name,
                         resolved.pid,
                         host_session_id);
        on_lookup_failure(resolved.error, resolved.pid);
        return false;
    }
    if (resolved.error == HostBoundAgentLookupError::kAgentNotReady) {
        NOOK_SERVER_LOGI("%s without agent session pid=%d host_session=%u",
                         operation_name,
                         resolved.pid,
                         host_session_id);
        on_lookup_failure(resolved.error, resolved.pid);
        return false;
    }

    NOOK_SERVER_LOGI("forward %s pid=%d host_session=%u",
                     frame.GetType() == comm::MessageType::kScriptPost ? "SCRIPT_POST" : operation_name,
                     resolved.pid,
                     host_session_id);
    if (!resolved.agent->SendFrame(frame)) {
        NOOK_SERVER_LOGI("%s send failed pid=%d host_session=%u",
                         operation_name,
                         resolved.pid,
                         host_session_id);
        on_send_failure(resolved.pid);
        return false;
    }
    return true;
}

template <typename LookupFailureResponder, typename SendFailureResponder>
bool ForwardHostBoundScriptOperationRequest(const HostBoundAgentRequestTarget& resolved,
                                            const HostRequestFailure& failure,
                                            const char* operation_name,
                                            uint32_t host_session_id,
                                            const comm::Frame& frame,
                                            LookupFailureResponder&& send_lookup_failure,
                                            SendFailureResponder&& send_send_failure) {
    return ForwardHostBoundAgentRequest(
        resolved,
        failure,
        operation_name,
        host_session_id,
        frame,
        std::forward<LookupFailureResponder>(send_lookup_failure),
        std::forward<SendFailureResponder>(send_send_failure));
}

void HandleScriptMessageCacheOrForward(SessionRegistry* registry,
                                       int pid,
                                       const comm::Frame& frame) {
    if (registry == nullptr || pid <= 0) {
        return;
    }

    if (registry->ShouldCacheScriptMessageForPid(pid)) {
        registry->StoreScriptMessageFrame(pid, frame);
        NOOK_SERVER_LOGI("cache SCRIPT_MESSAGE before spawn ready pid=%d", pid);
        return;
    }

    comm::Session* host = ResolveBoundHostSessionForPid(registry, pid);
    if (host != nullptr) {
        NOOK_SERVER_LOGI("forward SCRIPT_MESSAGE pid=%d host_session=%u",
                         pid,
                         host->GetId());
        if (!host->SendFrame(frame)) {
            NOOK_SERVER_LOGI("script message forward failed pid=%d host_session=%u",
                             pid,
                             host->GetId());
        }
        return;
    }

    NOOK_SERVER_LOGI("drop SCRIPT_MESSAGE without cache window pid=%d", pid);
}

AgentReadyEarlyDropResult HandleAgentReadyEarlyDropChecks(
    SessionRegistry* registry,
    comm::Session& session,
    const comm::AgentReady& ready,
    int ready_pid,
    bool runtime_ready,
    const AgentReadySpawnContext& ready_spawn_context,
    comm::Session* host,
    bool owned_zygote_control_target) {
    AgentReadyEarlyDropResult result;
    if (registry == nullptr) {
        return result;
    }

    const bool has_pending_spawn_context =
        ready_spawn_context.has_pending_spawn_context;
    const bool has_pending_attach_context =
        ready_spawn_context.has_pending_attach_context;
    const bool pending_attach_matches =
        ready_spawn_context.pending_attach_matches;
    const bool has_spawn_suspended_context =
        ready_spawn_context.has_spawn_suspended_context;
    const bool spawn_token_mismatches_existing_spawn_context =
        ready_spawn_context.spawn_token_mismatches_existing_spawn_context;
    const PendingAttachEntry& pending_attach =
        ready_spawn_context.pending_attach;
    const SpawnSuspendedEntry& suspended_entry =
        ready_spawn_context.suspended_entry;
    const bool has_suspended_entry =
        ready_spawn_context.has_suspended_entry;

    const AgentReadyDropContext drop_context{
        has_pending_spawn_context,
        has_pending_attach_context,
        pending_attach_matches,
        has_spawn_suspended_context,
        has_suspended_entry,
        spawn_token_mismatches_existing_spawn_context,
        runtime_ready,
        suspended_entry,
        owned_zygote_control_target,
        host != nullptr,
    };
    const AgentReadyEarlyDropDecision early_drop_decision =
        registry->EvaluateAgentReadyEarlyDrop(ready_pid,
                                              ready.spawn_token,
                                              ready.process_name,
                                              &session,
                                              drop_context);
    if (early_drop_decision.reason == AgentReadyEarlyDropReason::kOrphanSpawnToken) {
        NOOK_SERVER_LOGI("drop orphan spawn-token AGENT_READY pid=%u name=%s token=%s session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         ready.spawn_token.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    if (early_drop_decision.reason == AgentReadyEarlyDropReason::kMismatchedPendingAttach) {
        NOOK_SERVER_LOGI("drop mismatched pending-attach AGENT_READY pid=%u name=%s token=%s expected_pid=%d expected_name=%s session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         ready.spawn_token.c_str(),
                         pending_attach.pid,
                         pending_attach.process_name.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    if (early_drop_decision.reason ==
        AgentReadyEarlyDropReason::kStaleAttachWhileNewAttachPending) {
        NOOK_SERVER_LOGI("drop stale attach AGENT_READY while new attach pending pid=%u name=%s token=%s session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         ready.spawn_token.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    if (early_drop_decision.reason ==
        AgentReadyEarlyDropReason::kForeignAttachLike) {
        NOOK_SERVER_LOGI("drop foreign attach-like AGENT_READY pid=%u name=%s token=%s session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         ready.spawn_token.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    if (early_drop_decision.reason == AgentReadyEarlyDropReason::kMismatchedSpawnToken) {
        NOOK_SERVER_LOGI("drop mismatched spawn-token AGENT_READY for existing spawn context pid=%u name=%s token=%s session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         ready.spawn_token.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    if (early_drop_decision.reason == AgentReadyEarlyDropReason::kOrphanAttach) {
        NOOK_SERVER_LOGI("drop orphan attach %s-stage AGENT_READY pid=%u name=%s session=%u",
                         runtime_ready ? "runtime" : "control",
                         ready.pid,
                         ready.process_name.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    if (early_drop_decision.reason == AgentReadyEarlyDropReason::kInvalidatedUnowned) {
        NOOK_SERVER_LOGI("drop invalidated unowned %s-stage AGENT_READY pid=%u name=%s session=%u",
                         runtime_ready ? "runtime" : "control",
                         ready.pid,
                         ready.process_name.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    if (early_drop_decision.reason ==
        AgentReadyEarlyDropReason::kMismatchedControlStageKnownSpawnIdentity) {
        NOOK_SERVER_LOGI("drop mismatched control-stage AGENT_READY for known spawn identity pid=%u name=%s session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    return result;
}

AgentReadyPreRegistrationResult HandleAgentReadyPreRegistrationAdjustments(
    SessionRegistry* registry,
    comm::Session& session,
    const comm::AgentReady& ready,
    int previous_pid,
    int ready_pid,
    bool runtime_ready,
    bool runtime_spawn_process_name_matches,
    bool stale_control_after_runtime,
    bool mismatched_runtime_trace_for_spawn_target,
    const std::string& expected_spawn_process_name,
    const SpawnSuspendedEntry& suspended_entry) {
    AgentReadyPreRegistrationResult result;
    if (registry == nullptr) {
        return result;
    }

    const AgentReadyPreRegistrationDecision decision =
        registry->EvaluateAgentReadyPreRegistration(
            ready_pid,
            ready.process_name,
            runtime_ready,
            stale_control_after_runtime,
            mismatched_runtime_trace_for_spawn_target,
            &session,
            suspended_entry);

    if (decision.action ==
        AgentReadyPreRegistrationAction::kDropLateControlAtRuntimeBoundary) {
        NOOK_SERVER_LOGI("drop late control-stage AGENT_READY at transaction runtime boundary pid=%u name=%s session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    if (decision.action ==
        AgentReadyPreRegistrationAction::kDropLateControlFromNonCurrentSession) {
        NOOK_SERVER_LOGI("drop late control-stage AGENT_READY from non-current session pid=%u name=%s session=%u",
                         ready.pid,
                         ready.process_name.c_str(),
                         session.GetId());
        result.should_return = true;
        return result;
    }

    if (decision.action ==
        AgentReadyPreRegistrationAction::kResetMismatchedRuntimeTrace) {
        NOOK_SERVER_LOGI("clear mismatched runtime trace before accepting control-stage AGENT_READY pid=%u expected=%s incoming=%s session=%u",
                         ready.pid,
                         expected_spawn_process_name.c_str(),
                         ready.process_name.c_str(),
                         session.GetId());
    }

    return result;
}

template <typename LookupFailureResponder, typename SendFailureResponder>
bool ForwardHostBoundScriptOperationRequestWithLookup(SessionRegistry* registry,
                                                      uint32_t host_session_id,
                                                      const char* operation_name,
                                                      const comm::Frame& frame,
                                                      LookupFailureResponder&& send_lookup_failure,
                                                      SendFailureResponder&& send_send_failure) {
    const HostBoundAgentRequestTarget resolved =
        registry != nullptr
            ? registry->ResolveHostBoundAgentRequestTarget(host_session_id)
            : HostBoundAgentRequestTarget{};
    const HostRequestFailure failure =
        DescribeHostBoundAgentLookupFailure(resolved.error, operation_name);
    return ForwardHostBoundScriptOperationRequest(
        resolved,
        failure,
        operation_name,
        host_session_id,
        frame,
        std::forward<LookupFailureResponder>(send_lookup_failure),
        std::forward<SendFailureResponder>(send_send_failure));
}

template <typename Request,
          typename DecodeFn,
          typename InvalidResponder,
          typename RegistryUnavailableResponder,
          typename LookupFailureResponder,
          typename SendFailureResponder>
bool DecodeAndForwardHostBoundScriptOperationRequest(SessionRegistry* registry,
                                                     comm::Session& session,
                                                     const comm::Frame& frame,
                                                     DecodeFn&& decode,
                                                     const char* invalid_log_message,
                                                     const char* operation_name,
                                                     InvalidResponder&& send_invalid_request,
                                                     RegistryUnavailableResponder&& send_registry_unavailable,
                                                     LookupFailureResponder&& send_lookup_failure,
                                                     SendFailureResponder&& send_send_failure,
                                                     Request* out) {
    if (!DecodeRequestWithRegistryGuard(
            registry,
            session,
            frame,
            std::forward<DecodeFn>(decode),
            invalid_log_message,
            std::forward<InvalidResponder>(send_invalid_request),
            std::forward<RegistryUnavailableResponder>(send_registry_unavailable),
            out)) {
        return false;
    }

    return ForwardHostBoundScriptOperationRequestWithLookup(
        registry,
        session.GetId(),
        operation_name,
        frame,
        std::forward<LookupFailureResponder>(send_lookup_failure),
        std::forward<SendFailureResponder>(send_send_failure));
}

HostResponseForwardTarget ResolveHostForwardTargetForAgentResponse(SessionRegistry* registry,
                                                                   int pid,
                                                                   const comm::Session* agent_session) {
    HostResponseForwardTarget result;
    result.pid = pid;
    if (registry == nullptr || pid <= 0 || agent_session == nullptr) {
        result.error = HostResponseForwardError::kInvalidSource;
        return result;
    }

    if (!IsAcceptedAgentSessionForHostResponse(registry, pid, agent_session)) {
        result.error = HostResponseForwardError::kInvalidSource;
        return result;
    }

    result.host = ResolveBoundHostSessionForPid(registry, pid);
    if (result.host == nullptr) {
        result.error = HostResponseForwardError::kHostNotFound;
        return result;
    }

    return result;
}

template <typename LookupFailureResponder, typename SendFailureResponder>
bool ForwardHostBoundScriptLoadRequestWithLookup(SessionRegistry* registry,
                                                 uint32_t host_session_id,
                                                 const comm::Frame& frame,
                                                 LookupFailureResponder&& send_lookup_failure,
                                                 SendFailureResponder&& send_send_failure) {
    const HostBoundAgentRequestTarget resolved =
        registry != nullptr
            ? registry->ResolveHostBoundAgentRequestTarget(host_session_id)
            : HostBoundAgentRequestTarget{};
    const HostRequestFailure failure =
        DescribeHostBoundAgentLookupFailure(resolved.error, "script load");
    return ForwardHostBoundScriptLoadRequest(
        registry,
        resolved,
        failure,
        host_session_id,
        frame,
        std::forward<LookupFailureResponder>(send_lookup_failure),
        std::forward<SendFailureResponder>(send_send_failure));
}

template <typename InvalidSourceHandler,
          typename HostMissingHandler,
          typename BeforeForwardHandler>
bool ForwardAgentResponseToBoundHost(SessionRegistry* registry,
                                     comm::Session& session,
                                     const comm::Frame& frame,
                                     const char* response_name,
                                     const char* missing_host_log_name,
                                     InvalidSourceHandler&& on_invalid_source,
                                     HostMissingHandler&& on_host_missing,
                                     BeforeForwardHandler&& before_forward) {
    const int pid = session.GetPeerPid();
    if (registry == nullptr || pid <= 0) {
        return false;
    }

    const HostResponseForwardTarget forward_target =
        ResolveHostForwardTargetForAgentResponse(registry, pid, &session);
    if (forward_target.error == HostResponseForwardError::kInvalidSource) {
        NOOK_SERVER_LOGI("drop %s from non-current agent session pid=%d session=%u",
                         response_name,
                         pid,
                         session.GetId());
        on_invalid_source(pid);
        return false;
    }
    if (forward_target.error == HostResponseForwardError::kHostNotFound) {
        NOOK_SERVER_LOGI("%s without host pid=%d", missing_host_log_name, pid);
        on_host_missing(pid);
        return false;
    }

    before_forward(pid);
    NOOK_SERVER_LOGI("forward %s pid=%d host_session=%u",
                     response_name,
                     pid,
                     forward_target.host->GetId());
    if (!forward_target.host->SendFrame(frame)) {
        NOOK_SERVER_LOGI("%s forward failed pid=%d host_session=%u",
                         missing_host_log_name,
                         pid,
                         forward_target.host->GetId());
    }
    return true;
}

bool IsRuntimeReady(const comm::AgentReady& ready) {
    return ready.stage == comm::AgentReadyStage::kRuntime;
}

bool IsAcceptedCurrentAgentSession(SessionRegistry* registry,
                                   int pid,
                                   const comm::Session* session) {
    return registry != nullptr &&
           registry->IsAcceptedCurrentAgentSessionForPid(pid, session);
}

bool IsAcceptedSpawnControlStageSession(SessionRegistry* registry,
                                        int pid,
                                        const std::string& process_name,
                                        const comm::Session* session) {
    if (registry == nullptr || pid <= 0 || session == nullptr) {
        return false;
    }

    SpawnSuspendedEntry entry;
    if (!registry->GetSpawnSuspendedEntry(pid, &entry) || !entry.suspended) {
        return registry->IsAcceptedCurrentAgentSessionForPid(pid, session);
    }
    return registry->IsAcceptedControlStageAgentSessionForSpawn(pid,
                                                                entry,
                                                                process_name,
                                                                session);
}

comm::Session* ResolveRuntimeReadyAgentSessionForSuspendedSpawn(SessionRegistry* registry,
                                                                int pid,
                                                                const SpawnSuspendedEntry& entry) {
    if (registry == nullptr || pid <= 0) {
        return nullptr;
    }
    return registry->FindRuntimeReadyAgentSessionForSpawn(pid, entry);
}

comm::Session* ResolveBoundAgentSessionForHostRequest(SessionRegistry* registry, int pid) {
    if (registry == nullptr || pid <= 0) {
        return nullptr;
    }

    SpawnSuspendedEntry entry;
    if (registry->GetSpawnSuspendedEntry(pid, &entry) && entry.suspended) {
        return registry->FindHostRequestAgentSessionForSpawn(pid, entry);
    }

    return registry->FindAgentSessionByPid(pid);
}

bool IsAcceptedAgentSessionForHostResponse(SessionRegistry* registry,
                                           int pid,
                                           const comm::Session* session) {
    if (registry == nullptr || pid <= 0 || session == nullptr) {
        return false;
    }

    SpawnSuspendedEntry entry;
    if (registry->GetSpawnSuspendedEntry(pid, &entry)) {
        if (!registry->IsAcceptedHostResponseAgentSessionForSpawn(pid, entry, session)) {
            if (!ResolveSpawnRuntimeProcessName(entry).empty()) {
                return false;
            }
        } else {
            comm::Session* runtime_agent =
                ResolveRuntimeReadyAgentSessionForSuspendedSpawn(registry, pid, entry);
            if (runtime_agent != nullptr) {
                return true;
            }
        }
        if (!ResolveSpawnRuntimeProcessName(entry).empty() &&
            !registry->IsAcceptedHostResponseAgentSessionForSpawn(pid, entry, session)) {
            return false;
        }
    }

    return IsAcceptedCurrentAgentSession(registry, pid, session);
}

bool IsAcceptedAgentSessionForScriptMessage(SessionRegistry* registry,
                                            int pid,
                                            const comm::Session* session) {
    if (registry == nullptr || pid <= 0 || session == nullptr) {
        return false;
    }

    SpawnSuspendedEntry entry;
    if (registry->GetSpawnSuspendedEntry(pid, &entry)) {
        if (registry->IsSpawnBlockedForScriptOperations(pid)) {
            return IsAcceptedSpawnControlStageSession(registry, pid, {}, session);
        }

        if (entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady ||
            entry.state == SpawnTransactionState::kReadyForScriptLoad ||
            entry.state == SpawnTransactionState::kScriptLoadDispatched) {
            return registry->IsAcceptedScriptMessageAgentSessionForSpawn(pid, entry, session);
        }
    }

    return IsAcceptedCurrentAgentSession(registry, pid, session);
}

comm::Session* ResolveBoundHostSessionForPid(SessionRegistry* registry, int pid) {
    if (registry == nullptr || pid <= 0) {
        return nullptr;
    }

    const uint32_t owner_host_session_id =
        ResolveSuspendedSpawnOwnerHostSessionId(registry, pid);
    if (owner_host_session_id != 0) {
        return registry->FindHostSession(owner_host_session_id);
    }

    return registry->FindHostSessionByPid(pid);
}

int ResolveHostOwnedPidForRequest(SessionRegistry* registry, uint32_t host_session_id) {
    if (registry == nullptr || host_session_id == 0) {
        return -1;
    }

    const int owned_spawn_pid = registry->FindOwnedSpawnPidByHostSession(host_session_id);
    if (owned_spawn_pid > 0) {
        return owned_spawn_pid;
    }

    const int bound_pid = registry->FindPidByHostSession(host_session_id);
    if (bound_pid <= 0) {
        return -1;
    }

    if (IsForeignSuspendedSpawnOwner(registry, bound_pid, host_session_id)) {
        return -1;
    }

    return bound_pid;
}

bool ResolveTargetProcess(const ServerHandlerConfig& config,
                          const comm::AttachRequest& request,
                          int* pid,
                          std::string* process_name,
                          std::string* error_message) {
    if (pid == nullptr || process_name == nullptr) {
        if (error_message != nullptr) {
            *error_message = "attach resolve output is null";
        }
        return false;
    }

    *pid = -1;
    process_name->clear();
    if (request.pid != 0) {
        *pid = static_cast<int>(request.pid);
    }
    if (!request.identifier.empty()) {
        *process_name = request.identifier;
    }

    if (!config.enumerate_processes) {
        if (*pid > 0 && !process_name->empty()) {
            return true;
        }
        if (error_message != nullptr) {
            *error_message = "process enumeration unavailable";
        }
        return false;
    }

    const std::vector<ProcessInfo> processes = config.enumerate_processes();
    if (*pid > 0) {
        for (const ProcessInfo& process : processes) {
            if (process.pid == *pid) {
                if (process_name->empty()) {
                    *process_name = process.name;
                }
                return true;
            }
        }
        if (process_name->empty()) {
            if (error_message != nullptr) {
                *error_message = "target pid not found";
            }
            return false;
        }
        return true;
    }

    if (!request.identifier.empty()) {
        for (const ProcessInfo& process : processes) {
            if (process.name == request.identifier) {
                *pid = process.pid;
                *process_name = process.name;
                return true;
            }
        }
        if (error_message != nullptr) {
            *error_message = "target process not found";
        }
        return false;
    }

    if (error_message != nullptr) {
        *error_message = "attach target is empty";
    }
    return false;
}

void CompleteSpawnScriptLoadIfPresent(SessionRegistry* registry, int pid) {
    if (registry == nullptr || pid <= 0) {
        return;
    }

    (void) registry->MarkSpawnScriptLoadComplete(pid);
}

void SendAttachResponse(comm::Session& session, uint32_t msg_id, const comm::AttachResponse& response) {
    session.SendFrame(comm::Frame(comm::MessageType::kAttachResponse, msg_id, comm::EncodeAttachResponse(response)));
}

void SendDetachResponse(comm::Session& session, uint32_t msg_id, const comm::DetachResponse& response) {
    session.SendFrame(comm::Frame(comm::MessageType::kDetachResponse, msg_id, comm::EncodeDetachResponse(response)));
}

void SendResumeResponse(comm::Session& session, uint32_t msg_id, const comm::ResumeResponse& response) {
    session.SendFrame(comm::Frame(comm::MessageType::kResumeResponse, msg_id, comm::EncodeResumeResponse(response)));
}

void SendDetachImmediateError(comm::Session& session,
                              uint32_t msg_id,
                              uint32_t session_id,
                              int error_code,
                              const std::string& error_message) {
    SendDetachResponse(session,
                       msg_id,
                       comm::DetachResponse{
                           session_id,
                           comm::ErrorInfo{error_code, error_message}});
}

void SendResumeImmediateError(comm::Session& session,
                              uint32_t msg_id,
                              uint32_t pid,
                              int error_code,
                              const std::string& error_message) {
    SendResumeResponse(session,
                       msg_id,
                       comm::ResumeResponse{
                           pid,
                           comm::ErrorInfo{error_code, error_message}});
}

void SendAttachImmediateError(comm::Session& session,
                              uint32_t msg_id,
                              uint32_t session_id,
                              uint32_t pid,
                              const std::string& process_name,
                              int error_code,
                              const std::string& error_message) {
    SendAttachResponse(session,
                       msg_id,
                       comm::AttachResponse{
                           session_id,
                           pid,
                           process_name,
                           comm::ErrorInfo{error_code, error_message}});
}

void SendAttachAttachFailure(comm::Session& session,
                             uint32_t msg_id,
                             uint32_t pid,
                             const std::string& process_name,
                             const std::string& error_message) {
    SendAttachImmediateError(session,
                             msg_id,
                             0u,
                             pid,
                             process_name,
                             -3,
                             error_message);
}

void CleanupFailedAttachAttempt(SessionRegistry* registry,
                                uint32_t host_session_id,
                                const std::string& ready_token,
                                int pid) {
    if (registry == nullptr) {
        return;
    }

    registry->ClearPendingAttach(ready_token);
    registry->MarkAttachTimeoutPid(pid);
    registry->UnbindHostSession(host_session_id);
}

void FinalizeSuccessfulAttach(SessionRegistry* registry,
                              const std::string& ready_token,
                              int pid) {
    if (registry == nullptr) {
        return;
    }

    registry->ClearPendingAttach(ready_token);
    registry->ClearAttachTimeoutPid(pid);
}

void FinalizeSuccessfulResume(SessionRegistry* registry, int pid) {
    if (registry == nullptr || pid <= 0) {
        return;
    }

    registry->ClearSpawnSuspended(pid);
    registry->ClearScriptMessageFrames(pid);
}

void SendScriptCreateResponse(comm::Session& session, uint32_t msg_id, const comm::ScriptCreateResponse& response) {
    session.SendFrame(comm::Frame(comm::MessageType::kScriptCreateResp, msg_id, comm::EncodeScriptCreateResponse(response)));
}

void SendScriptResponse(comm::Session& session, comm::MessageType type, uint32_t msg_id, const comm::ScriptResponse& response) {
    session.SendFrame(comm::Frame(type, msg_id, comm::EncodeScriptResponse(response)));
}

void SendRpcResponse(comm::Session& session, uint32_t msg_id, const comm::RpcResponse& response) {
    session.SendFrame(comm::Frame(comm::MessageType::kRpcResponse, msg_id, comm::EncodeRpcResponse(response)));
}

void SendScriptImmediateError(comm::Session& session,
                              comm::MessageType type,
                              uint32_t msg_id,
                              uint32_t script_id,
                              int error_code,
                              const std::string& error_message) {
    SendScriptResponse(session,
                       type,
                       msg_id,
                       comm::ScriptResponse{
                           script_id,
                           false,
                           comm::ErrorInfo{error_code, error_message}});
}

void SendScriptCreateImmediateError(comm::Session& session,
                                    uint32_t msg_id,
                                    int error_code,
                                    const std::string& error_message) {
    SendScriptCreateResponse(session,
                             msg_id,
                             comm::ScriptCreateResponse{
                                 0u,
                                 false,
                                 comm::ErrorInfo{error_code, error_message}});
}

void SendRpcImmediateError(comm::Session& session,
                           uint32_t msg_id,
                           uint32_t script_id,
                           int error_code,
                           const std::string& error_message) {
    SendRpcResponse(session,
                    msg_id,
                    comm::RpcResponse{
                        script_id,
                        false,
                        {},
                        comm::ErrorInfo{error_code, error_message}});
}

void SendProcessListResponse(comm::Session& session,
                             uint32_t msg_id,
                             const comm::ProcessListResponse& response) {
    session.SendFrame(comm::Frame(comm::MessageType::kProcessListResp,
                                  msg_id,
                                  comm::EncodeProcessListResponse(response)));
}

void SendAppListResponse(comm::Session& session,
                         uint32_t msg_id,
                         const comm::AppListResponse& response) {
    session.SendFrame(comm::Frame(comm::MessageType::kAppListResp,
                                  msg_id,
                                  comm::EncodeAppListResponse(response)));
}

template <typename Request,
          typename Response,
          typename DecodeFn,
          typename EnumerateFn,
          typename AppendFn,
          typename LogFn,
          typename SendFn>
void HandleEnumerationListRequest(comm::Session& session,
                                  const comm::Frame& frame,
                                  DecodeFn&& decode,
                                  const char* invalid_log_message,
                                  int invalid_error_code,
                                  const char* invalid_error_message,
                                  int unavailable_error_code,
                                  const char* unavailable_error_message,
                                  EnumerateFn&& enumerate,
                                  AppendFn&& append_entries,
                                  LogFn&& log_response,
                                  SendFn&& send_response) {
    Request request;
    if (!decode(frame.GetPayload().data(), frame.GetPayload().size(), &request)) {
        NOOK_SERVER_LOGE("%s", invalid_log_message);
        Response response;
        response.error = {invalid_error_code, invalid_error_message};
        send_response(session, frame.GetMsgId(), response);
        return;
    }

    Response response;
    if (!enumerate) {
        response.error = {unavailable_error_code, unavailable_error_message};
    } else {
        append_entries(enumerate(), &response);
    }

    log_response(response);
    send_response(session, frame.GetMsgId(), response);
}

template <typename Request,
          typename DecodeFn,
          typename InvalidResponder,
          typename RegistryUnavailableResponder>
bool DecodeRequestWithRegistryGuard(SessionRegistry* registry,
                                    comm::Session& session,
                                    const comm::Frame& frame,
                                    DecodeFn&& decode,
                                    const char* invalid_log_message,
                                    InvalidResponder&& send_invalid_response,
                                    RegistryUnavailableResponder&& send_registry_unavailable_response,
                                    Request* out) {
    if (out == nullptr) {
        return false;
    }

    if (!decode(frame.GetPayload().data(), frame.GetPayload().size(), out)) {
        NOOK_SERVER_LOGE("%s", invalid_log_message);
        send_invalid_response();
        return false;
    }

    if (registry == nullptr) {
        send_registry_unavailable_response(*out);
        return false;
    }

    return true;
}

template <typename Message,
          typename DecodeFn>
bool DecodeResponseNoReply(const comm::Frame& frame,
                           DecodeFn&& decode,
                           const char* invalid_log_message,
                           Message* out) {
    if (out == nullptr) {
        return false;
    }

    if (!decode(frame.GetPayload().data(), frame.GetPayload().size(), out)) {
        NOOK_SERVER_LOGE("%s", invalid_log_message);
        return false;
    }

    return true;
}

template <typename Response,
          typename DecodeFn,
          typename InvalidSourceHandler,
          typename HostMissingHandler,
          typename BeforeForwardHandler>
bool DecodeAndForwardAgentResponseWithHooks(SessionRegistry* registry,
                                            comm::Session& session,
                                            const comm::Frame& frame,
                                            DecodeFn&& decode,
                                            const char* invalid_log_message,
                                            const char* response_name,
                                            const char* missing_host_log_name,
                                            InvalidSourceHandler&& on_invalid_source,
                                            HostMissingHandler&& on_host_missing,
                                            BeforeForwardHandler&& before_forward,
                                            Response* out) {
    if (!DecodeResponseNoReply(frame,
                               std::forward<DecodeFn>(decode),
                               invalid_log_message,
                               out)) {
        return false;
    }

    return ForwardAgentResponseToBoundHost(
        registry,
        session,
        frame,
        response_name,
        missing_host_log_name,
        std::forward<InvalidSourceHandler>(on_invalid_source),
        std::forward<HostMissingHandler>(on_host_missing),
        std::forward<BeforeForwardHandler>(before_forward));
}

template <typename Response,
          typename DecodeFn>
bool DecodeAndForwardAgentResponseNoHooks(SessionRegistry* registry,
                                          comm::Session& session,
                                          const comm::Frame& frame,
                                          DecodeFn&& decode,
                                          const char* invalid_log_message,
                                          const char* response_name,
                                          const char* missing_host_log_name,
                                          Response* out) {
    return DecodeAndForwardAgentResponseWithHooks(
        registry,
        session,
        frame,
        std::forward<DecodeFn>(decode),
        invalid_log_message,
        response_name,
        missing_host_log_name,
        [&](int) {},
        [&](int) {},
        [&](int) {},
        out);
}

void HandleSpawnRequest(SessionRegistry* registry, Injector* injector, const ServerHandlerConfig& config, comm::Session& session, const comm::Frame& frame) {
    ExecuteSpawnRequest(registry, injector, config, session, frame);
}

void HandleAttachRequest(SessionRegistry* registry, Injector* injector, const ServerHandlerConfig& config, comm::Session& session, const comm::Frame& frame) {
    comm::AttachRequest request;
    if (!comm::DecodeAttachRequest(frame.GetPayload().data(), frame.GetPayload().size(), &request)) {
        NOOK_SERVER_LOGE("invalid ATTACH_REQUEST");
        SendAttachImmediateError(session,
                                 frame.GetMsgId(),
                                 0u,
                                 0u,
                                 "",
                                 -1,
                                 "invalid attach request");
        return;
    }

    int pid = -1;
    std::string process_name;
    std::string error_message;
    if (!ResolveTargetProcess(config, request, &pid, &process_name, &error_message) || pid <= 0) {
        if (error_message.empty()) {
            error_message = "attach target resolve failed";
        }
        NOOK_SERVER_LOGE("attach resolve failed pid=%u identifier=%s error=%s", request.pid, request.identifier.c_str(), error_message.c_str());
        SendAttachImmediateError(session,
                                 frame.GetMsgId(),
                                 0u,
                                 0u,
                                 "",
                                 -2,
                                 error_message);
        return;
    }

    if (registry != nullptr) {
        registry->BindHostToPid(session.GetId(), pid);
    }

    comm::Frame cached_ready;
    const std::string attach_ready_token =
        MakeAttachReadyToken(session.GetId(), frame.GetMsgId(), pid, process_name);
    const bool has_agent_session = registry != nullptr && registry->FindAgentSessionByPid(pid) != nullptr;
    const bool has_runtime_ready = registry != nullptr &&
                                   registry->FindRuntimeReadyAgentSessionByIdentity(pid, process_name) != nullptr;
    if (!has_runtime_ready) {
        if (injector == nullptr) {
            if (registry != nullptr) {
                registry->UnbindHostSession(session.GetId());
            }
            error_message = "attach injector failed";
            NOOK_SERVER_LOGE("attach failed pid=%d agent=%s error=%s", pid, config.agent_path.c_str(), error_message.c_str());
            SendAttachAttachFailure(session,
                                    frame.GetMsgId(),
                                    static_cast<uint32_t>(pid),
                                    process_name,
                                    error_message);
            return;
        }

        struct AttachInjectState {
            std::atomic<bool> done{false};
            std::atomic<bool> success{false};
            std::string error;
        };
        auto inject_state = std::make_shared<AttachInjectState>();
        const std::string agent_path = config.agent_path;
        if (registry != nullptr) {
            registry->RegisterPendingAttach(attach_ready_token,
                                            pid,
                                            process_name,
                                            session.GetId());
        }
        std::thread([injector, pid, agent_path, attach_ready_token, inject_state]() {
            std::string inject_error;
            const bool ok = injector->InjectAgent(pid,
                                                  agent_path,
                                                  attach_ready_token,
                                                  &inject_error);
            inject_state->success.store(ok, std::memory_order_release);
            inject_state->error = std::move(inject_error);
            inject_state->done.store(true, std::memory_order_release);
        }).detach();

        const bool agent_ready = registry != nullptr &&
                                 registry->WaitForRuntimeReadyAgentSessionByToken(pid,
                                                                                  process_name,
                                                                                  attach_ready_token,
                                                                                  4500) != nullptr;
        if (agent_ready && registry != nullptr) {
            (void) registry->GetAgentReadyFrameByIdentity(pid, process_name, &cached_ready);
        }

        if (!agent_ready) {
            if (inject_state->done.load(std::memory_order_acquire)) {
                if (!inject_state->success.load(std::memory_order_acquire)) {
                    error_message = inject_state->error.empty()
                                        ? "attach injector failed"
                                        : inject_state->error;
                } else {
                    error_message = "attach injector finished but agent-ready timed out";
                }
            } else {
                error_message = "attach agent-ready timeout";
            }

            CleanupFailedAttachAttempt(registry, session.GetId(), attach_ready_token, pid);
            NOOK_SERVER_LOGE("attach failed pid=%d agent=%s error=%s", pid, config.agent_path.c_str(), error_message.c_str());
            SendAttachAttachFailure(session,
                                    frame.GetMsgId(),
                                    static_cast<uint32_t>(pid),
                                    process_name,
                                    error_message);
            return;
        }
    }

    FinalizeSuccessfulAttach(registry, attach_ready_token, pid);

    NOOK_SERVER_LOGI("attach success session=%u pid=%d name=%s reused=%d", session.GetId(), pid, process_name.c_str(), has_agent_session ? 1 : 0);
    SendAttachResponse(session, frame.GetMsgId(), comm::AttachResponse{session.GetId(), static_cast<uint32_t>(pid), process_name, {}});

    if (registry != nullptr) {
        (void) ReplayCachedAgentReadyThenScriptMessages(registry,
                                                        pid,
                                                        process_name,
                                                        session,
                                                        session.GetId(),
                                                        false);
    }
}

void HandleDetachRequest(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::DetachRequest request;
    if (!DecodeRequestWithRegistryGuard(
            registry,
            session,
            frame,
            comm::DecodeDetachRequest,
            "invalid DETACH_REQUEST",
            [&]() {
                SendDetachImmediateError(session,
                                         frame.GetMsgId(),
                                         0u,
                                         -1,
                                         "invalid detach request");
            },
            [&](const comm::DetachRequest& decoded_request) {
                SendDetachImmediateError(session,
                                         frame.GetMsgId(),
                                         decoded_request.session_id,
                                         -2,
                                         "session registry unavailable");
            },
            &request)) {
        return;
    }

    const int pid = ResolveHostOwnedPidForRequest(registry, request.session_id);
    if (pid <= 0) {
        SendDetachImmediateError(session,
                                 frame.GetMsgId(),
                                 request.session_id,
                                 -3,
                                 "session not attached");
        return;
    }

    SpawnSuspendedEntry spawn_entry;
    if (registry->GetSpawnSuspendedEntry(pid, &spawn_entry) && spawn_entry.suspended) {
        SendDetachImmediateError(session,
                                 frame.GetMsgId(),
                                 request.session_id,
                                 -4,
                                 "spawned pid is still gate-held; resume or wait for failure cleanup first");
        return;
    }

    registry->UnbindHostSession(request.session_id);
    NOOK_SERVER_LOGI("detach success session=%u pid=%d requested_by=%u", request.session_id, pid, session.GetId());
    SendDetachResponse(session, frame.GetMsgId(), comm::DetachResponse{request.session_id, {}});
}

void HandleResumeRequest(SessionRegistry* registry, const ServerHandlerConfig& config, comm::Session& session, const comm::Frame& frame) {
    comm::ResumeRequest request;
    if (!DecodeRequestWithRegistryGuard(
            registry,
            session,
            frame,
            comm::DecodeResumeRequest,
            "invalid RESUME_REQUEST",
            [&]() {
                SendResumeImmediateError(session,
                                         frame.GetMsgId(),
                                         0u,
                                         -1,
                                         "invalid resume request");
            },
            [&](const comm::ResumeRequest& decoded_request) {
                SendResumeImmediateError(session,
                                         frame.GetMsgId(),
                                         decoded_request.pid,
                                         -2,
                                         "session registry unavailable");
            },
            &request)) {
        return;
    }

    SpawnSuspendedEntry entry;
    if (!registry->GetSpawnSuspendedEntry(static_cast<int>(request.pid), &entry) || !entry.suspended) {
        NOOK_SERVER_LOGE("resume failed pid=%u error=spawn gate not held", request.pid);
        SendResumeImmediateError(session,
                                 frame.GetMsgId(),
                                 request.pid,
                                 -3,
                                 "process is not suspended");
        return;
    }

    if (IsForeignSuspendedSpawnOwner(registry,
                                     static_cast<int>(request.pid),
                                     session.GetId())) {
        NOOK_SERVER_LOGE("resume failed pid=%u error=spawn owned by another host owner=%u requester=%u",
                         request.pid,
                         entry.host_session_id,
                         session.GetId());
        SendResumeImmediateError(session,
                                 frame.GetMsgId(),
                                 request.pid,
                                 -7,
                                 "spawned process is owned by another host session");
        return;
    }

    if (registry->IsSpawnBlockedForScriptOperations(static_cast<int>(request.pid))) {
        NOOK_SERVER_LOGE("resume failed pid=%u error=authoritative agent not ready", request.pid);
        SendResumeImmediateError(session,
                                 frame.GetMsgId(),
                                 request.pid,
                                 -6,
                                 "spawned process is not ready to resume");
        return;
    }

    if (!config.resume_process) {
        SendResumeImmediateError(session,
                                 frame.GetMsgId(),
                                 request.pid,
                                 -4,
                                 "spawn gate release callback unavailable");
        return;
    }

    if (!config.resume_process(static_cast<int>(request.pid))) {
        NOOK_SERVER_LOGE("resume failed pid=%u error=spawn gate release failed", request.pid);
        SendResumeImmediateError(session,
                                 frame.GetMsgId(),
                                 request.pid,
                                 -5,
                                 "release spawned process gate failed");
        return;
    }

    FinalizeSuccessfulResume(registry, static_cast<int>(request.pid));
    NOOK_SERVER_LOGI("resume success pid=%u host_session=%u", request.pid, session.GetId());
    SendResumeResponse(session, frame.GetMsgId(), comm::ResumeResponse{request.pid, {}});
}

void HandleAgentReady(SessionRegistry* registry,
                      Injector* injector,
                      const ServerHandlerConfig& config,
                      comm::Session& session,
                      const comm::Frame& frame) {
    comm::AgentReady ready;
    if (!comm::DecodeAgentReady(frame.GetPayload().data(), frame.GetPayload().size(), &ready)) {
        NOOK_SERVER_LOGE("invalid AGENT_READY");
        return;
    }

    const int previous_pid = session.GetPeerPid();
    session.SetPeerPid(static_cast<int>(ready.pid));
    if (registry != nullptr) {
        const int ready_pid = static_cast<int>(ready.pid);
        const DerivedAgentReadyContext derived =
            registry->DeriveAgentReadyContext(ready_pid,
                                              ready.spawn_token,
                                              ready.process_name,
                                              ready.stage,
                                              &session);
        const AgentReadyEarlyDropResult early_drop_result =
            HandleAgentReadyEarlyDropChecks(registry,
                                            session,
                                            ready,
                                            ready_pid,
                                            derived.runtime_ready,
                                            derived.spawn_context,
                                            derived.host,
                                            derived.owned_zygote_control_target);
        if (early_drop_result.should_return) {
            return;
        }

        const bool transaction_runtime_boundary =
            registry->HasAuthoritativeRuntimeBoundaryForSpawn(ready_pid,
                                                              derived.spawn_context.suspended_entry,
                                                              ready.process_name);
        const AgentReadyPreRegistrationResult pre_registration_result =
            HandleAgentReadyPreRegistrationAdjustments(registry,
                                                       session,
                                                       ready,
                                                       previous_pid,
                                                       ready_pid,
                                                       derived.runtime_ready,
                                                       derived.runtime_spawn_process_name_matches,
                                                       derived.stale_control_after_runtime,
                                                       derived.mismatched_runtime_trace_for_spawn_target,
                                                       derived.expected_spawn_process_name,
                                                       derived.spawn_context.suspended_entry);
        if (pre_registration_result.should_return) {
            return;
        }

        const AgentReadyRegistrationPlan registration_plan =
            registry->PlanAgentReadyRegistration(derived.runtime_ready,
                                                 derived.runtime_spawn_process_name_matches,
                                                 derived.runtime_already_recorded,
                                                 derived.spawn_context.pending_attach_matches,
                                                 !ready.spawn_token.empty());

        const AgentReadyRegistrationResult registration_result =
            registry->ApplyAgentReadyRegistrationPlan(previous_pid,
                                                      ready_pid,
                                                      ready.spawn_token,
                                                      ready.process_name,
                                                      ready.stage,
                                                      frame,
                                                      &session,
                                                      registration_plan);

        if (registration_plan.remove_runtime_session_for_mismatch) {
            NOOK_SERVER_LOGI("skip runtime-stage global registration pid=%u name=%s expected=%s",
                             ready.pid,
                             ready.process_name.c_str(),
                             derived.expected_spawn_process_name.c_str());
        }
        if (!registration_plan.upgrade_spawn_authoritative_ready) {
            NOOK_SERVER_LOGI("skip spawn suspended authoritative upgrade pid=%u name=%s expected=%s stage=%s",
                             ready.pid,
                             ready.process_name.c_str(),
                             derived.expected_spawn_process_name.c_str(),
                             derived.runtime_ready ? "runtime" : "control");
        }
        if (registration_result.resolved_pending_spawn) {
            if (registration_result.bound_host_to_resolved_pending_spawn) {
                NOOK_SERVER_LOGI("resolved pending spawn pid=%u token=%s stage=%s",
                                 ready.pid,
                                 ready.spawn_token.c_str(),
                                 derived.runtime_ready ? "runtime" : "control");
            } else {
                NOOK_SERVER_LOGI("skip pending spawn bind pid=%u token=%s reason=entry-cleared-or-mismatched",
                                 ready.pid,
                                 ready.spawn_token.c_str());
            }
        }
        HandleAgentReadyForwarding(registry,
                                   ready_pid,
                                   ready,
                                   frame,
                                   derived.expected_spawn_process_name,
                                   derived.host,
                                   derived.runtime_ready,
                                   registration_plan.eligible_runtime_ready);
    }

}

void HandleScriptMessage(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::ScriptMessage message;
    if (!comm::DecodeScriptMessage(frame.GetPayload().data(), frame.GetPayload().size(), &message)) {
        NOOK_SERVER_LOGE("invalid SCRIPT_MESSAGE");
        return;
    }

    const int pid = session.GetPeerPid();
    if (registry == nullptr || pid <= 0) {
        NOOK_SERVER_LOGI("script message without valid pid");
        return;
    }

    if (!IsAcceptedAgentSessionForScriptMessage(registry, pid, &session)) {
        NOOK_SERVER_LOGI("drop SCRIPT_MESSAGE from non-current agent session pid=%d session=%u",
                         pid,
                         session.GetId());
        return;
    }

    HandleScriptMessageCacheOrForward(registry, pid, frame);
}

void HandleScriptPost(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::ScriptPost post;
    if (!DecodeRequestWithRegistryGuard(
            registry,
            session,
            frame,
            comm::DecodeScriptPost,
            "invalid SCRIPT_POST",
            []() {},
            [&](const comm::ScriptPost&) {
                NOOK_SERVER_LOGI("script post without registry");
            },
            &post)) {
        return;
    }

    const HostBoundAgentRequestTarget resolved =
        registry != nullptr
            ? registry->ResolveHostBoundAgentRequestTarget(session.GetId())
            : HostBoundAgentRequestTarget{};
    (void) ForwardHostBoundAgentRequestNoResponse(
        resolved,
        "script post",
        session.GetId(),
        frame,
        [&](HostBoundAgentLookupError, int) {},
        [&](int) {});
}

void HandleScriptCreate(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::ScriptCreate create;
    if (!DecodeAndForwardHostBoundScriptOperationRequest(
            registry,
            session,
            frame,
            comm::DecodeScriptCreate,
            "invalid SCRIPT_CREATE",
            "script create",
            [&]() {
                SendScriptCreateImmediateError(session,
                                               frame.GetMsgId(),
                                               -1,
                                               "invalid script create request");
            },
            [&](const comm::ScriptCreate&) {
                SendScriptCreateImmediateError(session,
                                               frame.GetMsgId(),
                                               -2,
                                               "session registry unavailable");
            },
            [&](const HostRequestFailure& request_failure) {
                SendScriptCreateImmediateError(session,
                                               frame.GetMsgId(),
                                               request_failure.error_code,
                                               request_failure.error_message);
            },
            [&]() {
                SendScriptCreateImmediateError(session,
                                               frame.GetMsgId(),
                                               -4,
                                               "agent session not ready for bound pid");
            },
            &create)) {
        return;
    }
}

void HandleScriptCreateResp(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::ScriptCreateResponse response;
    (void) DecodeAndForwardAgentResponseNoHooks(registry,
                                                session,
                                                frame,
                                                comm::DecodeScriptCreateResponse,
                                                "invalid SCRIPT_CREATE_RESP",
                                                "SCRIPT_CREATE_RESP",
                                                "script create resp",
                                                &response);
}

void HandleScriptLoad(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::ScriptLoad load;
    if (!DecodeRequestWithRegistryGuard(
            registry,
            session,
            frame,
            comm::DecodeScriptLoad,
            "invalid SCRIPT_LOAD",
            [&]() {
                SendScriptImmediateError(session,
                                         comm::MessageType::kScriptLoadResp,
                                         frame.GetMsgId(),
                                         0u,
                                         -1,
                                         "invalid script load request");
            },
            [&](const comm::ScriptLoad& decoded_load) {
                SendScriptImmediateError(session,
                                         comm::MessageType::kScriptLoadResp,
                                         frame.GetMsgId(),
                                         decoded_load.script_id,
                                         -2,
                                         "session registry unavailable");
            },
            &load)) {
        return;
    }

    (void) ForwardHostBoundScriptLoadRequestWithLookup(
        registry,
        session.GetId(),
        frame,
        [&](const HostRequestFailure& request_failure) {
            SendScriptImmediateError(session,
                                     comm::MessageType::kScriptLoadResp,
                                     frame.GetMsgId(),
                                     load.script_id,
                                     request_failure.error_code,
                                     request_failure.error_message);
        },
        [&]() {
            SendScriptImmediateError(session,
                                     comm::MessageType::kScriptLoadResp,
                                     frame.GetMsgId(),
                                     load.script_id,
                                     -4,
                                     "agent session not ready for bound pid");
        });
}

void HandleScriptLoadResp(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::ScriptResponse response;
    (void) DecodeAndForwardAgentResponseWithHooks(
        registry,
        session,
        frame,
        comm::DecodeScriptResponse,
        "invalid SCRIPT_LOAD_RESP",
        "SCRIPT_LOAD_RESP",
        "script load resp",
        [&](int pid) { CompleteSpawnScriptLoadIfPresent(registry, pid); },
        [&](int pid) { CompleteSpawnScriptLoadIfPresent(registry, pid); },
        [&](int pid) { CompleteSpawnScriptLoadIfPresent(registry, pid); },
        &response);
}

void HandleScriptUnload(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::ScriptUnload unload;
    if (!DecodeAndForwardHostBoundScriptOperationRequest(
            registry,
            session,
            frame,
            comm::DecodeScriptUnload,
            "invalid SCRIPT_UNLOAD",
            "script unload",
            [&]() {
                SendScriptImmediateError(session,
                                         comm::MessageType::kScriptUnloadResp,
                                         frame.GetMsgId(),
                                         0u,
                                         -1,
                                         "invalid script unload request");
            },
            [&](const comm::ScriptUnload& decoded_unload) {
                SendScriptImmediateError(session,
                                         comm::MessageType::kScriptUnloadResp,
                                         frame.GetMsgId(),
                                         decoded_unload.script_id,
                                         -2,
                                         "session registry unavailable");
            },
            [&](const HostRequestFailure& request_failure) {
                SendScriptImmediateError(session,
                                         comm::MessageType::kScriptUnloadResp,
                                         frame.GetMsgId(),
                                         unload.script_id,
                                         request_failure.error_code,
                                         request_failure.error_message);
            },
            [&]() {
                SendScriptImmediateError(session,
                                         comm::MessageType::kScriptUnloadResp,
                                         frame.GetMsgId(),
                                         unload.script_id,
                                         -4,
                                         "agent session not ready for bound pid");
            },
            &unload)) {
        return;
    }
}

void HandleScriptUnloadResp(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::ScriptResponse response;
    (void) DecodeAndForwardAgentResponseNoHooks(registry,
                                                session,
                                                frame,
                                                comm::DecodeScriptResponse,
                                                "invalid SCRIPT_UNLOAD_RESP",
                                                "SCRIPT_UNLOAD_RESP",
                                                "script unload resp",
                                                &response);
}

void HandleRpcRequest(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::RpcRequest request;
    if (!DecodeAndForwardHostBoundScriptOperationRequest(
            registry,
            session,
            frame,
            comm::DecodeRpcRequest,
            "invalid RPC_REQUEST",
            "rpc request",
            [&]() {
                SendRpcImmediateError(session,
                                      frame.GetMsgId(),
                                      0u,
                                      -1,
                                      "invalid rpc request");
            },
            [&](const comm::RpcRequest& decoded_request) {
                SendRpcImmediateError(session,
                                      frame.GetMsgId(),
                                      decoded_request.script_id,
                                      -2,
                                      "session registry unavailable");
            },
            [&](const HostRequestFailure& request_failure) {
                SendRpcImmediateError(session,
                                      frame.GetMsgId(),
                                      request.script_id,
                                      request_failure.error_code,
                                      request_failure.error_message);
            },
            [&]() {
                SendRpcImmediateError(session,
                                      frame.GetMsgId(),
                                      request.script_id,
                                      -4,
                                      "agent session not ready for bound pid");
            },
            &request)) {
        return;
    }
}

void HandleRpcResponse(SessionRegistry* registry, comm::Session& session, const comm::Frame& frame) {
    comm::RpcResponse response;
    (void) DecodeAndForwardAgentResponseNoHooks(registry,
                                                session,
                                                frame,
                                                comm::DecodeRpcResponse,
                                                "invalid RPC_RESPONSE",
                                                "RPC_RESPONSE",
                                                "rpc response",
                                                &response);
}

void HandleProcessListRequest(const ServerHandlerConfig& config, comm::Session& session, const comm::Frame& frame) {
    HandleEnumerationListRequest<comm::ProcessListRequest, comm::ProcessListResponse>(
        session,
        frame,
        comm::DecodeProcessListRequest,
        "invalid PROCESS_LIST_REQ",
        -1,
        "invalid process list request",
        -2,
        "process enumeration unavailable",
        config.enumerate_processes,
        [](const std::vector<ProcessInfo>& processes, comm::ProcessListResponse* response) {
            if (response == nullptr) {
                return;
            }
            response->processes.reserve(processes.size());
            for (const ProcessInfo& process : processes) {
                response->processes.push_back(
                    comm::ProcessEntry{static_cast<uint32_t>(process.pid), process.name});
            }
        },
        [](const comm::ProcessListResponse& response) {
            NOOK_SERVER_LOGI("process list response count=%zu", response.processes.size());
        },
        SendProcessListResponse);
}

void HandleAppListRequest(const ServerHandlerConfig& config, comm::Session& session, const comm::Frame& frame) {
    HandleEnumerationListRequest<comm::AppListRequest, comm::AppListResponse>(
        session,
        frame,
        comm::DecodeAppListRequest,
        "invalid APP_LIST_REQ",
        -1,
        "invalid app list request",
        -2,
        "app enumeration unavailable",
        config.enumerate_apps,
        [](const std::vector<AppInfo>& apps, comm::AppListResponse* response) {
            if (response == nullptr) {
                return;
            }
            response->apps.reserve(apps.size());
            for (const AppInfo& app : apps) {
                response->apps.push_back(comm::AppEntry{app.package_name});
            }
        },
        [](const comm::AppListResponse& response) {
            NOOK_SERVER_LOGI("app list response count=%zu", response.apps.size());
        },
        SendAppListResponse);
}

}  // namespace

void RegisterServerHandlers(comm::MessageDispatcher* dispatcher,
                            SessionRegistry* registry,
                            Injector* injector,
                            const ServerHandlerConfig& config) {
    if (dispatcher == nullptr) {
        return;
    }

    dispatcher->RegisterHandler(comm::MessageType::kSpawnRequest, [registry, injector, config](comm::Session& session, const comm::Frame& frame) {
        HandleSpawnRequest(registry, injector, config, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kAttachRequest, [registry, injector, config](comm::Session& session, const comm::Frame& frame) {
        HandleAttachRequest(registry, injector, config, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kDetachRequest, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleDetachRequest(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kResumeRequest, [registry, config](comm::Session& session, const comm::Frame& frame) {
        HandleResumeRequest(registry, config, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kProcessListReq, [config](comm::Session& session, const comm::Frame& frame) {
        HandleProcessListRequest(config, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kAppListReq, [config](comm::Session& session, const comm::Frame& frame) {
        HandleAppListRequest(config, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kAgentReady, [registry, injector, config](comm::Session& session, const comm::Frame& frame) {
        HandleAgentReady(registry, injector, config, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kScriptMessage, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleScriptMessage(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kScriptPost, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleScriptPost(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kScriptCreate, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleScriptCreate(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kScriptCreateResp, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleScriptCreateResp(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kScriptLoad, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleScriptLoad(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kScriptLoadResp, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleScriptLoadResp(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kScriptUnload, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleScriptUnload(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kScriptUnloadResp, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleScriptUnloadResp(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kRpcRequest, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleRpcRequest(registry, session, frame);
    });
    dispatcher->RegisterHandler(comm::MessageType::kRpcResponse, [registry](comm::Session& session, const comm::Frame& frame) {
        HandleRpcResponse(registry, session, frame);
    });
}

}  // namespace server
}  // namespace nook

