#include "session_registry.h"

#include "../src/communication/session/session.h"

#include <algorithm>
#include <chrono>

namespace nook {
namespace server {

namespace {

bool SessionLooksAlive(comm::Session* session) {
    return session != nullptr && session->IsAlive();
}

int AgentReadyStageLifecycleRank(comm::AgentReadyStage stage) {
    switch (stage) {
        case comm::AgentReadyStage::kControl:
            return 0;
        case comm::AgentReadyStage::kRuntime:
            return 1;
        default:
            return -1;
    }
}

int PendingSpawnReadyStageRank(PendingSpawnReadyStage stage) {
    switch (stage) {
        case PendingSpawnReadyStage::kControlReady:
            return 0;
        case PendingSpawnReadyStage::kRuntimeReady:
            return 1;
        default:
            return -1;
    }
}

PendingSpawnReadyStage ToPendingSpawnReadyStage(comm::AgentReadyStage stage) {
    switch (stage) {
        case comm::AgentReadyStage::kControl:
            return PendingSpawnReadyStage::kControlReady;
        case comm::AgentReadyStage::kRuntime:
            return PendingSpawnReadyStage::kRuntimeReady;
        default:
            return PendingSpawnReadyStage::kNone;
    }
}

bool SpawnStateBlocksScriptMessageForwarding(SpawnTransactionState state) {
    return state == SpawnTransactionState::kWaitingAgentReady ||
           state == SpawnTransactionState::kWaitingRuntimeReady;
}

bool CanTransitionSpawnState(SpawnTransactionState current, SpawnTransactionState next) {
    if (current == next) {
        return true;
    }

    switch (current) {
        case SpawnTransactionState::kWaitingAgentReady:
            return next == SpawnTransactionState::kWaitingRuntimeReady ||
                   next == SpawnTransactionState::kReadyForScriptLoad ||
                   next == SpawnTransactionState::kScriptLoadDispatched;
        case SpawnTransactionState::kWaitingRuntimeReady:
            return next == SpawnTransactionState::kReadyForScriptLoad ||
                   next == SpawnTransactionState::kScriptLoadDispatched;
        case SpawnTransactionState::kReadyForScriptLoad:
            return next == SpawnTransactionState::kScriptLoadDispatched;
        case SpawnTransactionState::kScriptLoadDispatched:
            return next == SpawnTransactionState::kReadyForScriptLoad;
        default:
            return false;
    }
}

SpawnTransactionState SpawnStateForPendingReadyStage(PendingSpawnReadyStage stage) {
    switch (stage) {
        case PendingSpawnReadyStage::kRuntimeReady:
        case PendingSpawnReadyStage::kControlReady:
            return SpawnTransactionState::kWaitingRuntimeReady;
        default:
            return SpawnTransactionState::kWaitingAgentReady;
    }
}

comm::Session* ResolvePreferredControlReadySessionLocked(
    const std::unordered_map<int, comm::Session*>& agent_sessions,
    const std::unordered_map<int, comm::Session*>& agent_control_sessions,
    int pid) {
    auto control_it = agent_control_sessions.find(pid);
    if (control_it != agent_control_sessions.end() &&
        SessionLooksAlive(control_it->second)) {
        return control_it->second;
    }

    auto session_it = agent_sessions.find(pid);
    if (session_it != agent_sessions.end() &&
        SessionLooksAlive(session_it->second)) {
        return session_it->second;
    }

    return nullptr;
}

comm::Session* ResolvePreferredAuthoritativeSessionLocked(
    const std::unordered_map<int, comm::Session*>& agent_sessions,
    const std::unordered_map<int, comm::Session*>& agent_control_sessions,
    int pid) {
    auto session_it = agent_sessions.find(pid);
    if (session_it != agent_sessions.end() &&
        SessionLooksAlive(session_it->second)) {
        return session_it->second;
    }

    auto control_it = agent_control_sessions.find(pid);
    if (control_it != agent_control_sessions.end() &&
        SessionLooksAlive(control_it->second)) {
        return control_it->second;
    }

    return nullptr;
}

}  // namespace

void SessionRegistry::ClearAgentStateLocked(int pid) {
    agent_sessions_.erase(pid);
    agent_control_sessions_.erase(pid);
    agent_control_process_names_.erase(pid);
    agent_session_history_.erase(pid);
    agent_authoritative_ready_.erase(pid);
    agent_ready_stages_.erase(pid);
    agent_runtime_ready_.erase(pid);
    agent_ready_frames_.erase(pid);

    auto process_name_it = agent_pid_to_process_name_.find(pid);
    if (process_name_it != agent_pid_to_process_name_.end()) {
        auto current_pid_it = agent_process_name_to_pid_.find(process_name_it->second);
        if (current_pid_it != agent_process_name_to_pid_.end() &&
            current_pid_it->second == pid) {
            agent_process_name_to_pid_.erase(current_pid_it);
        }
        agent_pid_to_process_name_.erase(process_name_it);
    }

    for (auto it = owned_zygote_control_processes_.begin();
         it != owned_zygote_control_processes_.end();) {
        if (it->second.pid == pid) {
            it = owned_zygote_control_processes_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionRegistry::ClearAttachSideStateForPidLocked(int pid) {
    if (pid <= 0) {
        return;
    }

    attach_timeout_pids_.erase(pid);
    for (auto it = pending_attaches_.begin(); it != pending_attaches_.end();) {
        if (it->second.pid == pid) {
            it = pending_attaches_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionRegistry::ClearAgentProcessNameBindingLocked(int pid) {
    auto process_name_it = agent_pid_to_process_name_.find(pid);
    if (process_name_it != agent_pid_to_process_name_.end()) {
        auto current_pid_it = agent_process_name_to_pid_.find(process_name_it->second);
        if (current_pid_it != agent_process_name_to_pid_.end() &&
            current_pid_it->second == pid) {
            agent_process_name_to_pid_.erase(current_pid_it);
        }
        agent_pid_to_process_name_.erase(process_name_it);
    }
}

void SessionRegistry::CollectHostBoundPidsLocked(uint32_t session_id,
                                                 int preserved_pid,
                                                 std::vector<int>* out) {
    if (out == nullptr) {
        return;
    }

    for (auto it = pid_to_host_session_.begin(); it != pid_to_host_session_.end();) {
        if (it->second == session_id && it->first != preserved_pid) {
            out->push_back(it->first);
            it = pid_to_host_session_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionRegistry::CollectResolvedPendingSpawnPidsForHostLocked(
    uint32_t session_id,
    std::vector<int>* out) {
    if (out == nullptr) {
        return;
    }

    for (auto it = pending_spawns_.begin(); it != pending_spawns_.end();) {
        if (it->second.host_session_id == session_id) {
            if (it->second.pid > 0) {
                out->push_back(it->second.pid);
            }
            it = pending_spawns_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionRegistry::ClearPendingAttachesForHostLocked(uint32_t session_id) {
    for (auto it = pending_attaches_.begin(); it != pending_attaches_.end();) {
        if (it->second.host_session_id == session_id) {
            if (it->second.pid > 0) {
                attach_timeout_pids_.erase(it->second.pid);
            }
            it = pending_attaches_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionRegistry::ClearOwnedHostPidStateLocked(int pid,
                                                   uint32_t session_id,
                                                   bool clear_attach_timeout) {
    auto suspended_it = spawn_suspended_entries_.find(pid);
    if (suspended_it != spawn_suspended_entries_.end() &&
        suspended_it->second.host_session_id == session_id) {
        spawn_suspended_entries_.erase(suspended_it);
        ClearAgentStateLocked(pid);
        invalidated_agent_pids_[pid] = true;
    }

    if (clear_attach_timeout) {
        attach_timeout_pids_.erase(pid);
    }

    script_message_frames_.erase(pid);
}

void SessionRegistry::ClearResolvedPendingSpawnPidStateLocked(int pid) {
    ClearAgentStateLocked(pid);
    spawn_suspended_entries_.erase(pid);
    script_message_frames_.erase(pid);
    attach_timeout_pids_.erase(pid);
    invalidated_agent_pids_[pid] = true;
}

void SessionRegistry::ClearAgentSessionStateLocked(int pid,
                                                   bool clear_attach_side_state,
                                                   bool clear_script_and_spawn_state) {
    agent_sessions_.erase(pid);
    agent_control_sessions_.erase(pid);
    agent_control_process_names_.erase(pid);
    agent_session_history_.erase(pid);
    agent_authoritative_ready_.erase(pid);
    agent_ready_stages_.erase(pid);
    agent_runtime_ready_.erase(pid);
    agent_ready_frames_.erase(pid);

    if (clear_attach_side_state) {
        ClearAttachSideStateForPidLocked(pid);
    }
    if (clear_script_and_spawn_state) {
        script_message_frames_.erase(pid);
        spawn_suspended_entries_.erase(pid);
    }

    ClearAgentProcessNameBindingLocked(pid);

    for (auto it = owned_zygote_control_processes_.begin();
         it != owned_zygote_control_processes_.end();) {
        if (it->second.pid == pid) {
            it = owned_zygote_control_processes_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionRegistry::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = true;
    pending_spawns_.clear();
    pending_attaches_.clear();
    attach_timeout_pids_.clear();
    pending_spawn_cv_.notify_all();
    agent_ready_cv_.notify_all();
}

void SessionRegistry::RegisterHostSession(comm::Session* session) {
    if (session == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    host_sessions_[session->GetId()] = session;
}

void SessionRegistry::RemoveHostSession(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    host_sessions_.erase(session_id);
    std::vector<int> owned_pids;
    std::vector<int> resolved_pending_pids;
    CollectHostBoundPidsLocked(session_id, -1, &owned_pids);
    CollectResolvedPendingSpawnPidsForHostLocked(session_id, &resolved_pending_pids);
    ClearPendingAttachesForHostLocked(session_id);

    for (int pid : owned_pids) {
        ClearOwnedHostPidStateLocked(pid, session_id, true);
    }
    for (int pid : resolved_pending_pids) {
        ClearResolvedPendingSpawnPidStateLocked(pid);
    }
    pending_spawn_cv_.notify_all();
    agent_ready_cv_.notify_all();
}

comm::Session* SessionRegistry::FindHostSession(uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = host_sessions_.find(session_id);
    return it != host_sessions_.end() ? it->second : nullptr;
}

int SessionRegistry::FindPidByHostSession(uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : pid_to_host_session_) {
        if (entry.second == session_id) {
            return entry.first;
        }
    }
    return -1;
}

int SessionRegistry::FindOwnedSpawnPidByHostSession(uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : spawn_suspended_entries_) {
        if (entry.second.host_session_id == session_id && entry.second.suspended) {
            return entry.first;
        }
    }
    return -1;
}

bool SessionRegistry::IsRegisteredHostSession(uint32_t session_id,
                                              const comm::Session* session) const {
    if (session_id == 0 || session == nullptr) {
        return false;
    }

    return FindHostSession(session_id) == session;
}

void SessionRegistry::BindHostToPid(uint32_t session_id, int pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (host_sessions_.find(session_id) == host_sessions_.end()) {
        return;
    }
    std::vector<int> rebound_pids;
    CollectHostBoundPidsLocked(session_id, pid, &rebound_pids);

    for (int old_pid : rebound_pids) {
        ClearOwnedHostPidStateLocked(old_pid, session_id, false);
    }
    pid_to_host_session_[pid] = session_id;
}

void SessionRegistry::UnbindHostSession(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> released_pids;
    std::vector<int> resolved_pending_pids;
    CollectHostBoundPidsLocked(session_id, -1, &released_pids);
    CollectResolvedPendingSpawnPidsForHostLocked(session_id, &resolved_pending_pids);
    ClearPendingAttachesForHostLocked(session_id);

    for (int pid : released_pids) {
        ClearOwnedHostPidStateLocked(pid, session_id, false);
    }
    for (int pid : resolved_pending_pids) {
        ClearResolvedPendingSpawnPidStateLocked(pid);
    }
    pending_spawn_cv_.notify_all();
    agent_ready_cv_.notify_all();
}

comm::Session* SessionRegistry::FindHostSessionByPid(int pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pid_to_host_session_.find(pid);
    if (it == pid_to_host_session_.end()) {
        return nullptr;
    }

    auto host_it = host_sessions_.find(it->second);
    return host_it != host_sessions_.end() ? host_it->second : nullptr;
}

void SessionRegistry::MarkAttachTimeoutPid(int pid) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    attach_timeout_pids_[pid] = true;
}

void SessionRegistry::ClearAttachTimeoutPid(int pid) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    attach_timeout_pids_.erase(pid);
}

bool SessionRegistry::WasAttachTimeoutPid(int pid) const {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = attach_timeout_pids_.find(pid);
    return it != attach_timeout_pids_.end() && it->second;
}

bool SessionRegistry::IsInvalidatedAgentPid(int pid) const {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = invalidated_agent_pids_.find(pid);
    return it != invalidated_agent_pids_.end() && it->second;
}

void SessionRegistry::RegisterPendingAttach(const std::string& ready_token,
                                            int pid,
                                            const std::string& process_name,
                                            uint32_t host_session_id) {
    if (ready_token.empty() || pid <= 0 || host_session_id == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pending_attaches_[ready_token] = PendingAttachEntry{
        ready_token,
        process_name,
        host_session_id,
        pid,
    };
}

bool SessionRegistry::GetPendingAttach(const std::string& ready_token, PendingAttachEntry* out) const {
    if (ready_token.empty() || out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_attaches_.find(ready_token);
    if (it == pending_attaches_.end()) {
        return false;
    }
    *out = it->second;
    return true;
}

bool SessionRegistry::HasPendingAttachForPid(int pid, const std::string& process_name) const {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : pending_attaches_) {
        if (entry.second.pid != pid) {
            continue;
        }
        if (!process_name.empty() &&
            !entry.second.process_name.empty() &&
            entry.second.process_name != process_name) {
            continue;
        }
        return true;
    }
    return false;
}

void SessionRegistry::ClearPendingAttach(const std::string& ready_token) {
    if (ready_token.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pending_attaches_.erase(ready_token);
    agent_ready_cv_.notify_all();
}

bool SessionRegistry::ResolveAgentReadySpawnContext(
    int pid,
    const std::string& spawn_token,
    const std::string& process_name,
    AgentReadySpawnContext* out) const {
    if (pid <= 0 || out == nullptr) {
        return false;
    }

    AgentReadySpawnContext context;

    if (!spawn_token.empty()) {
        PendingSpawnEntry pending_spawn;
        if (GetPendingSpawn(spawn_token, &pending_spawn)) {
            context.expected_spawn_process_name = pending_spawn.process_name;
            context.has_pending_spawn_context = true;
        }
    }

    context.has_pending_attach_context =
        !spawn_token.empty() &&
        GetPendingAttach(spawn_token, &context.pending_attach);
    context.pending_attach_matches =
        context.has_pending_attach_context &&
        context.pending_attach.pid == pid &&
        (context.pending_attach.process_name.empty() ||
         process_name.empty() ||
         context.pending_attach.process_name == process_name);

    context.has_suspended_entry = GetSpawnSuspendedEntry(pid, &context.suspended_entry);
    if (context.has_suspended_entry) {
        context.has_spawn_suspended_context =
            !context.suspended_entry.target_process_name.empty() ||
            !context.suspended_entry.authoritative_process_name.empty();
        context.spawn_token_mismatches_existing_spawn_context =
            !spawn_token.empty() &&
            !context.suspended_entry.spawn_token.empty() &&
            spawn_token != context.suspended_entry.spawn_token;

        if (context.expected_spawn_process_name.empty()) {
            if (!context.suspended_entry.target_process_name.empty()) {
                context.expected_spawn_process_name =
                    context.suspended_entry.target_process_name;
            } else if (context.suspended_entry.authoritative_ready_stage ==
                           PendingSpawnReadyStage::kRuntimeReady &&
                       !context.suspended_entry.authoritative_process_name.empty()) {
                context.expected_spawn_process_name =
                    context.suspended_entry.authoritative_process_name;
            }
        }
    }

    *out = std::move(context);
    return true;
}

bool SessionRegistry::ShouldDropStaleAttachAgentReady(
    int pid,
    const std::string& process_name,
    const AgentReadyDropContext& context) const {
    if (pid <= 0) {
        return false;
    }

    return !context.has_pending_spawn_context &&
           !context.pending_attach_matches &&
           !context.has_spawn_suspended_context &&
           HasPendingAttachForPid(pid, process_name);
}

bool SessionRegistry::ShouldDropForeignAttachLikeAgentReady(
    const std::string& spawn_token,
    const AgentReadyDropContext& context) const {
    return !spawn_token.empty() &&
           !context.has_pending_spawn_context &&
           !context.pending_attach_matches &&
           !context.has_spawn_suspended_context;
}

bool SessionRegistry::ShouldDropMismatchedSpawnTokenAgentReady(
    const AgentReadyDropContext& context) const {
    return context.spawn_token_mismatches_existing_spawn_context;
}

bool SessionRegistry::ShouldDropOrphanAttachAgentReady(
    const std::string& spawn_token,
    int pid,
    const AgentReadyDropContext& context) const {
    if (pid <= 0) {
        return false;
    }

    return spawn_token.empty() &&
           !context.has_pending_spawn_context &&
           !context.has_spawn_suspended_context &&
           !context.has_bound_host &&
           WasAttachTimeoutPid(pid) &&
           !context.owned_zygote_control_target;
}

bool SessionRegistry::ShouldDropInvalidatedUnownedAgentReady(
    const std::string& spawn_token,
    int pid,
    const AgentReadyDropContext& context) const {
    if (pid <= 0) {
        return false;
    }

    return spawn_token.empty() &&
           !context.has_pending_spawn_context &&
           !context.has_spawn_suspended_context &&
           IsInvalidatedAgentPid(pid) &&
           !context.owned_zygote_control_target;
}

AgentReadyEarlyDropDecision SessionRegistry::EvaluateAgentReadyEarlyDrop(
    int pid,
    const std::string& spawn_token,
    const std::string& process_name,
    comm::Session* session,
    const AgentReadyDropContext& context) {
    AgentReadyEarlyDropDecision decision;
    if (pid <= 0 || session == nullptr) {
        return decision;
    }

    const bool mismatched_pending_attach_ready =
        context.has_pending_attach_context && !context.pending_attach_matches;
    if (mismatched_pending_attach_ready) {
        decision.reason = AgentReadyEarlyDropReason::kMismatchedPendingAttach;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    const bool orphaned_spawn_token_ready =
        !spawn_token.empty() &&
        !context.has_pending_spawn_context &&
        !context.pending_attach_matches &&
        !context.has_spawn_suspended_context &&
        !context.has_bound_host;
    if (orphaned_spawn_token_ready) {
        decision.reason = AgentReadyEarlyDropReason::kOrphanSpawnToken;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    if (ShouldDropStaleAttachAgentReady(pid, process_name, context)) {
        decision.reason = AgentReadyEarlyDropReason::kStaleAttachWhileNewAttachPending;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    if (ShouldDropForeignAttachLikeAgentReady(spawn_token, context)) {
        decision.reason = AgentReadyEarlyDropReason::kForeignAttachLike;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    if (ShouldDropMismatchedSpawnTokenAgentReady(context)) {
        decision.reason = AgentReadyEarlyDropReason::kMismatchedSpawnToken;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    if (ShouldDropOrphanAttachAgentReady(spawn_token, pid, context)) {
        decision.reason = AgentReadyEarlyDropReason::kOrphanAttach;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    if (ShouldDropInvalidatedUnownedAgentReady(spawn_token, pid, context)) {
        decision.reason = AgentReadyEarlyDropReason::kInvalidatedUnowned;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    if (!context.runtime_ready &&
        context.has_spawn_suspended_context &&
        context.has_suspended_entry &&
        !process_name.empty() &&
        HasKnownSpawnControlIdentityMismatchForSpawn(pid,
                                                     context.suspended_entry,
                                                     process_name)) {
        decision.reason =
            AgentReadyEarlyDropReason::kMismatchedControlStageKnownSpawnIdentity;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    return decision;
}

AgentReadyPreRegistrationDecision SessionRegistry::EvaluateAgentReadyPreRegistration(
    int pid,
    const std::string& process_name,
    bool runtime_ready,
    bool stale_control_after_runtime,
    bool mismatched_runtime_trace_for_spawn_target,
    comm::Session* session,
    const SpawnSuspendedEntry& suspended_entry) {
    AgentReadyPreRegistrationDecision decision;
    if (pid <= 0 || session == nullptr) {
        return decision;
    }

    if (ShouldDropLateControlAgentReadyAtRuntimeBoundary(pid,
                                                         suspended_entry,
                                                         process_name,
                                                         runtime_ready)) {
        decision.action =
            AgentReadyPreRegistrationAction::kDropLateControlAtRuntimeBoundary;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    if (stale_control_after_runtime) {
        decision.action =
            AgentReadyPreRegistrationAction::kDropLateControlFromNonCurrentSession;
        decision.dropped_session = DropAgentReadySessionIfMatches(pid, session);
        return decision;
    }

    if (mismatched_runtime_trace_for_spawn_target) {
        decision.action =
            AgentReadyPreRegistrationAction::kResetMismatchedRuntimeTrace;
        decision.reset_runtime_trace = ResetMismatchedRuntimeTraceForSpawn(pid);
        return decision;
    }

    return decision;
}

AgentReadyForwardDecision SessionRegistry::EvaluateAgentReadyForwarding(
    int pid,
    bool runtime_ready,
    bool eligible_runtime_ready,
    bool has_bound_host) const {
    AgentReadyForwardDecision decision;
    decision.can_expose_runtime_ready_immediately =
        pid > 0 &&
        eligible_runtime_ready &&
        CanExposeSpawnRuntimeReadyImmediately(pid);

    if (!has_bound_host && decision.can_expose_runtime_ready_immediately) {
        decision.action = AgentReadyForwardAction::kExposeRuntimeWithoutHost;
        return decision;
    }

    if (has_bound_host && decision.can_expose_runtime_ready_immediately) {
        decision.action = AgentReadyForwardAction::kForwardRuntimeToHost;
        return decision;
    }

    if (has_bound_host && eligible_runtime_ready) {
        decision.action = AgentReadyForwardAction::kHoldRuntimeUntilSpawnResponse;
        return decision;
    }

    if (has_bound_host && runtime_ready) {
        decision.action = AgentReadyForwardAction::kDropMismatchedRuntimeForHost;
        return decision;
    }

    if (has_bound_host) {
        decision.action = AgentReadyForwardAction::kHoldControlForHost;
        return decision;
    }

    decision.action = AgentReadyForwardAction::kNoBoundHost;
    return decision;
}

DerivedAgentReadyContext SessionRegistry::DeriveAgentReadyContext(
    int pid,
    const std::string& spawn_token,
    const std::string& process_name,
    comm::AgentReadyStage stage,
    const comm::Session* session) const {
    DerivedAgentReadyContext derived;
    if (pid <= 0) {
        return derived;
    }

    derived.runtime_ready = stage == comm::AgentReadyStage::kRuntime;
    (void) ResolveAgentReadySpawnContext(pid,
                                         spawn_token,
                                         process_name,
                                         &derived.spawn_context);
    derived.expected_spawn_process_name =
        derived.spawn_context.expected_spawn_process_name;

    const uint32_t owner_host_session_id = FindSuspendedSpawnOwnerHostSessionId(pid);
    if (owner_host_session_id != 0) {
        derived.host = FindHostSession(owner_host_session_id);
    } else {
        derived.host = FindHostSessionByPid(pid);
    }

    derived.owned_zygote_control_target =
        !process_name.empty() &&
        IsOwnedZygoteControlTarget(pid, process_name);
    derived.runtime_spawn_process_name_matches =
        DoesRuntimeSpawnProcessNameMatch(derived.runtime_ready,
                                         derived.expected_spawn_process_name,
                                         process_name);
    derived.mismatched_runtime_trace_for_spawn_target =
        derived.spawn_context.has_spawn_suspended_context &&
        !derived.runtime_ready &&
        derived.runtime_spawn_process_name_matches &&
        HasMismatchedRuntimeTraceForSpawn(pid,
                                          derived.spawn_context.suspended_entry,
                                          derived.expected_spawn_process_name,
                                          process_name);
    derived.runtime_already_recorded =
        HasRuntimeRecordedForSpawn(pid,
                                   derived.spawn_context.suspended_entry,
                                   derived.expected_spawn_process_name,
                                   process_name,
                                   derived.runtime_ready,
                                   derived.spawn_context.has_spawn_suspended_context);

    if (session != nullptr) {
        SpawnSuspendedEntry entry;
        if (!GetSpawnSuspendedEntry(pid, &entry) || !entry.suspended) {
            derived.current_agent_session_matches =
                IsAcceptedCurrentAgentSessionForPid(pid, session);
        } else {
            derived.current_agent_session_matches =
                IsAcceptedControlStageAgentSessionForSpawn(pid,
                                                           entry,
                                                           process_name,
                                                           session);
        }
    }

    derived.stale_control_after_runtime =
        ShouldDropLateControlAgentReadyFromNonCurrentSession(
            pid,
            derived.spawn_context.suspended_entry,
            derived.expected_spawn_process_name,
            process_name,
            derived.runtime_ready,
            derived.spawn_context.has_spawn_suspended_context,
            derived.current_agent_session_matches);

    return derived;
}

PostFinalizeSpawnContext SessionRegistry::ResolvePostFinalizeSpawnContext(
    int pid,
    PendingSpawnReadyStage fallback_stage,
    const PendingSpawnEntry& fallback_pending_spawn) const {
    PostFinalizeSpawnContext context;
    context.ready_stage = fallback_stage;
    context.runtime_process_name = fallback_pending_spawn.process_name;

    if (pid <= 0) {
        return context;
    }

    SpawnSuspendedEntry entry;
    if (!GetSpawnSuspendedEntry(pid, &entry) || !entry.suspended) {
        return context;
    }

    if (entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady) {
        context.ready_stage = PendingSpawnReadyStage::kRuntimeReady;
    } else if (fallback_stage == PendingSpawnReadyStage::kNone &&
               entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady) {
        context.ready_stage = PendingSpawnReadyStage::kControlReady;
    }

    const std::string resolved_runtime_process_name =
        ResolveSpawnRuntimeProcessName(entry);
    if (!resolved_runtime_process_name.empty()) {
        context.runtime_process_name = resolved_runtime_process_name;
    }
    return context;
}

LatePromotionEvaluation SessionRegistry::EvaluateLatePromotionEligibility(
    int pid,
    uint32_t host_session_id,
    const std::string& agent_path) const {
    LatePromotionEvaluation evaluation;
    if (pid <= 0 || host_session_id == 0) {
        evaluation.eligibility = LatePromotionEligibility::kInvalidArgs;
        return evaluation;
    }
    if (agent_path.empty()) {
        evaluation.eligibility = LatePromotionEligibility::kEmptyAgentPath;
        return evaluation;
    }

    SpawnSuspendedEntry entry;
    if (!GetSpawnSuspendedEntry(pid, &entry) || !entry.suspended) {
        evaluation.eligibility = LatePromotionEligibility::kSpawnEntryMissing;
        return evaluation;
    }
    evaluation.entry = entry;
    evaluation.has_entry = true;

    const bool runtime_ready =
        entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady;
    const bool control_ready =
        runtime_ready ||
        entry.authoritative_ready_stage == PendingSpawnReadyStage::kControlReady;
    if (runtime_ready || !control_ready) {
        evaluation.eligibility = LatePromotionEligibility::kNotControlReady;
        return evaluation;
    }

    const bool pre_runtime_wait_state =
        entry.state == SpawnTransactionState::kWaitingAgentReady ||
        entry.state == SpawnTransactionState::kWaitingRuntimeReady;
    if (entry.host_session_id != host_session_id || !pre_runtime_wait_state) {
        evaluation.eligibility = LatePromotionEligibility::kSpawnEntryMismatch;
        return evaluation;
    }

    if (FindHostSession(entry.host_session_id) == nullptr) {
        evaluation.eligibility = LatePromotionEligibility::kOwnedHostMissing;
        return evaluation;
    }

    evaluation.eligibility = LatePromotionEligibility::kEligible;
    return evaluation;
}

LatePromotionRecheckResult SessionRegistry::RecheckLatePromotionBeforeInject(
    int pid,
    uint32_t host_session_id,
    SpawnSuspendedEntry* out_entry) const {
    if (pid <= 0 || host_session_id == 0) {
        return LatePromotionRecheckResult::kTransactionMissingBeforeInject;
    }

    SpawnSuspendedEntry current_entry;
    if (!GetSpawnSuspendedEntry(pid, &current_entry) ||
        !current_entry.suspended ||
        current_entry.host_session_id != host_session_id) {
        return LatePromotionRecheckResult::kTransactionMissingBeforeInject;
    }

    if (out_entry != nullptr) {
        *out_entry = current_entry;
    }

    if (WaitForRuntimeReadyAgentSessionByIdentity(
            pid,
            ResolveSpawnRuntimeProcessName(current_entry),
            0) != nullptr) {
        return LatePromotionRecheckResult::kRuntimeReadyBeforeInject;
    }

    if (!GetSpawnSuspendedEntry(pid, &current_entry) ||
        !current_entry.suspended ||
        current_entry.host_session_id != host_session_id ||
        current_entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady ||
        current_entry.state == SpawnTransactionState::kReadyForScriptLoad ||
        current_entry.state == SpawnTransactionState::kScriptLoadDispatched) {
        return LatePromotionRecheckResult::kTransactionChanged;
    }

    if (out_entry != nullptr) {
        *out_entry = current_entry;
    }
    return LatePromotionRecheckResult::kProceed;
}

void SessionRegistry::CleanupTimedOutSpawnTransaction(const std::string& spawn_token,
                                                      int authoritative_pid) {
    PendingSpawnEntry timed_out_pending_spawn;
    if (TakePendingSpawn(spawn_token, &timed_out_pending_spawn)) {
        if (timed_out_pending_spawn.pid > 0) {
            ClearSpawnTransactionByPid(timed_out_pending_spawn.pid);
        }
    } else {
        ClearPendingSpawn(spawn_token);
    }

    if (authoritative_pid > 0) {
        ClearSpawnTransactionByPid(authoritative_pid);
    }
}

void SessionRegistry::CleanupFailedBoundSpawnTransaction(uint32_t host_session_id,
                                                         int authoritative_pid,
                                                         const std::string& spawn_token,
                                                         bool unbind_host_session) {
    RemoveAgentSessionByPid(authoritative_pid);
    if (unbind_host_session) {
        UnbindHostSession(host_session_id);
    }
    ClearSpawnTransactionByPid(authoritative_pid);
    ClearPendingSpawn(spawn_token);
}

void SessionRegistry::ConsumeOrClearPendingSpawn(const std::string& spawn_token,
                                                 PendingSpawnEntry* out) {
    if (!TakePendingSpawn(spawn_token, out)) {
        ClearPendingSpawn(spawn_token);
    }
}

void SessionRegistry::CleanupDroppedSuccessfulSpawnResponse(int authoritative_pid) {
    if (authoritative_pid <= 0) {
        return;
    }

    ClearSpawnTransactionByPid(authoritative_pid);
}

BindSuspendedSpawnAfterFinalizeResult SessionRegistry::BindSuspendedSpawnAfterFinalize(
    int pid,
    uint32_t host_session_id,
    const PostFinalizeSpawnContext& post_finalize_context,
    const PendingSpawnEntry& resolved_pending_spawn,
    const std::string& spawn_token) {
    BindSuspendedSpawnAfterFinalizeResult result;
    if (pid <= 0 || host_session_id == 0) {
        return result;
    }

    const bool resolved_at_runtime =
        post_finalize_context.ready_stage == PendingSpawnReadyStage::kRuntimeReady;
    MarkSpawnSuspended(pid,
                       host_session_id,
                       post_finalize_context.ready_stage,
                       post_finalize_context.runtime_process_name,
                       resolved_pending_spawn.process_name,
                       spawn_token);
    (void) SetSpawnResponsePending(pid, true);
    if (!resolved_at_runtime) {
        (void) UpdateSpawnState(pid, SpawnTransactionState::kWaitingRuntimeReady);
        result.waiting_runtime_ready = true;
    }
    result.bound = true;
    return result;
}

bool SessionRegistry::ReleaseSpawnResponseBoundaryAndResolvePostFinalize(
    int pid,
    PendingSpawnReadyStage fallback_stage,
    const PendingSpawnEntry& resolved_pending_spawn,
    PostFinalizeSpawnContext* out_context) {
    if (pid <= 0 || out_context == nullptr) {
        return false;
    }

    SpawnSuspendedEntry released_entry;
    if (ReleaseSpawnResponseBoundary(pid, &released_entry)) {
        out_context->ready_stage = released_entry.authoritative_ready_stage;
        out_context->runtime_process_name = ResolveSpawnRuntimeProcessName(released_entry);
        return true;
    }

    *out_context = ResolvePostFinalizeSpawnContext(pid,
                                                   fallback_stage,
                                                   resolved_pending_spawn);
    return false;
}

ResolvedPendingSpawnHandoffResult SessionRegistry::PrepareResolvedPendingSpawnHandoff(
    const std::string& spawn_token,
    int pid,
    uint32_t host_session_id,
    const comm::Session* host_session) {
    ResolvedPendingSpawnHandoffResult result;
    if (spawn_token.empty() || pid <= 0) {
        return result;
    }

    const bool host_registered =
        host_session_id != 0 && IsRegisteredHostSession(host_session_id, host_session);
    if (!host_registered) {
        ConsumeOrClearPendingSpawn(spawn_token, &result.pending_spawn);
        if (!result.pending_spawn.spawn_token.empty()) {
            result.disposition =
                ResolvedPendingSpawnHandoffDisposition::kConsumedWithoutRegisteredHost;
        }
        return result;
    }

    if (!BindHostToResolvedPendingSpawn(spawn_token, pid, &result.pending_spawn)) {
        result.disposition = ResolvedPendingSpawnHandoffDisposition::kMissingForRegisteredHost;
        return result;
    }

    PendingSpawnEntry consumed_pending_spawn;
    (void) TakePendingSpawn(spawn_token, &consumed_pending_spawn);
    result.disposition = ResolvedPendingSpawnHandoffDisposition::kBoundToRegisteredHost;
    return result;
}

SpawnResponseCommitResult SessionRegistry::CommitSpawnResponseSuccess(
    int pid,
    PendingSpawnReadyStage fallback_stage,
    const PendingSpawnEntry& resolved_pending_spawn) {
    SpawnResponseCommitResult result;
    if (pid <= 0) {
        return result;
    }

    result.released_boundary =
        ReleaseSpawnResponseBoundaryAndResolvePostFinalize(pid,
                                                           fallback_stage,
                                                           resolved_pending_spawn,
                                                           &result.context);
    if (!result.released_boundary) {
        result.context = ResolvePostFinalizeSpawnContext(pid,
                                                         fallback_stage,
                                                         resolved_pending_spawn);
    }
    result.should_replay_runtime_ready =
        result.context.ready_stage == PendingSpawnReadyStage::kRuntimeReady;
    return result;
}

RuntimeReadyCommitResult SessionRegistry::CommitRuntimeReadyVisibility(int pid) {
    RuntimeReadyCommitResult result;
    if (pid <= 0) {
        return result;
    }

    result.runtime_visible = MarkSpawnRuntimeReadyVisible(pid);
    result.should_replay_script_messages = result.runtime_visible;
    return result;
}

HostBoundAgentRequestTarget SessionRegistry::ResolveHostBoundAgentRequestTarget(
    uint32_t host_session_id) const {
    HostBoundAgentRequestTarget result;
    if (host_session_id == 0) {
        result.error = HostBoundAgentLookupError::kHostNotBound;
        return result;
    }

    result.pid = FindOwnedSpawnPidByHostSession(host_session_id);
    if (result.pid <= 0) {
        result.pid = FindPidByHostSession(host_session_id);
    }
    if (result.pid <= 0) {
        result.error = HostBoundAgentLookupError::kHostNotBound;
        return result;
    }
    if (IsForeignSuspendedSpawnOwner(result.pid, host_session_id)) {
        result.pid = -1;
        result.error = HostBoundAgentLookupError::kHostNotBound;
        return result;
    }

    SpawnSuspendedEntry entry;
    if (GetSpawnSuspendedEntry(result.pid, &entry) &&
        IsSpawnBlockedForScriptOperations(result.pid)) {
        result.error = HostBoundAgentLookupError::kSpawnNotReady;
        return result;
    }

    if (GetSpawnSuspendedEntry(result.pid, &entry) && entry.suspended) {
        result.agent = FindHostRequestAgentSessionForSpawn(result.pid, entry);
    } else {
        result.agent = FindAgentSessionByPid(result.pid);
    }
    if (result.agent == nullptr) {
        result.error = HostBoundAgentLookupError::kAgentNotReady;
        return result;
    }

    return result;
}

void SessionRegistry::RegisterAgentSession(int pid, comm::Session* session) {
    if (session == nullptr || pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    attach_timeout_pids_.erase(pid);
    invalidated_agent_pids_.erase(pid);

    auto process_name_it = agent_pid_to_process_name_.find(pid);
    if (process_name_it != agent_pid_to_process_name_.end()) {
        auto current_pid_it = agent_process_name_to_pid_.find(process_name_it->second);
        if (current_pid_it != agent_process_name_to_pid_.end() &&
            current_pid_it->second == pid) {
            agent_process_name_to_pid_.erase(current_pid_it);
        }
        agent_pid_to_process_name_.erase(process_name_it);
    }

    agent_sessions_[pid] = session;
    auto& session_list = agent_session_history_[pid];
    const auto existing = std::find(session_list.begin(), session_list.end(), session);
    if (existing == session_list.end()) {
        session_list.push_back(session);
    }
    agent_ready_cv_.notify_all();
}

void SessionRegistry::RegisterControlReadyAgentSession(int pid, comm::Session* session) {
    if (session == nullptr || pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    agent_control_sessions_[pid] = session;
    auto process_name_it = agent_pid_to_process_name_.find(pid);
    if (process_name_it != agent_pid_to_process_name_.end() &&
        !process_name_it->second.empty()) {
        agent_control_process_names_[pid] = process_name_it->second;
    }
    agent_ready_cv_.notify_all();
}

void SessionRegistry::RegisterAgentProcessName(int pid, const std::string& process_name) {
    if (pid <= 0 || process_name.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto existing_pid = agent_process_name_to_pid_.find(process_name);
    if (existing_pid != agent_process_name_to_pid_.end() && existing_pid->second != pid) {
        auto previous_pid_name = agent_pid_to_process_name_.find(existing_pid->second);
        if (previous_pid_name != agent_pid_to_process_name_.end() &&
            previous_pid_name->second == process_name) {
            agent_pid_to_process_name_.erase(previous_pid_name);
        }
    }
    auto previous_name = agent_pid_to_process_name_.find(pid);
    if (previous_name != agent_pid_to_process_name_.end() && previous_name->second != process_name) {
        agent_process_name_to_pid_.erase(previous_name->second);
    }
    agent_pid_to_process_name_[pid] = process_name;
    agent_process_name_to_pid_[process_name] = pid;
    auto control_it = agent_control_sessions_.find(pid);
    if (control_it != agent_control_sessions_.end()) {
        auto control_name_it = agent_control_process_names_.find(pid);
        if (control_name_it == agent_control_process_names_.end() ||
            control_name_it->second.empty()) {
            agent_control_process_names_[pid] = process_name;
        }
    }
    agent_ready_cv_.notify_all();
}

void SessionRegistry::MarkAgentAuthoritativeReady(int pid) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    agent_authoritative_ready_[pid] = true;
    agent_ready_cv_.notify_all();
}

bool SessionRegistry::IsAgentAuthoritativeReady(int pid) const {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agent_authoritative_ready_.find(pid);
    return it != agent_authoritative_ready_.end() && it->second;
}

void SessionRegistry::MarkAgentReadyStage(int pid, comm::AgentReadyStage stage) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agent_ready_stages_.find(pid);
    if (it == agent_ready_stages_.end() ||
        AgentReadyStageLifecycleRank(stage) >= AgentReadyStageLifecycleRank(it->second)) {
        agent_ready_stages_[pid] = stage;
    }
    agent_ready_cv_.notify_all();
}

bool SessionRegistry::IsAgentControlReady(int pid) const {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agent_ready_stages_.find(pid);
    if (it == agent_ready_stages_.end()) {
        return false;
    }
    return it->second == comm::AgentReadyStage::kControl ||
           it->second == comm::AgentReadyStage::kRuntime;
}

bool SessionRegistry::GetAgentReadyStage(int pid, comm::AgentReadyStage* stage) const {
    if (pid <= 0 || stage == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agent_ready_stages_.find(pid);
    if (it == agent_ready_stages_.end()) {
        return false;
    }
    *stage = it->second;
    return true;
}

void SessionRegistry::MarkAgentRuntimeReady(int pid) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    agent_runtime_ready_[pid] = true;
    agent_ready_cv_.notify_all();
}

void SessionRegistry::ClearAgentRuntimeReadyState(int pid) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    agent_runtime_ready_.erase(pid);
    agent_ready_frames_.erase(pid);
    agent_ready_cv_.notify_all();
}

void SessionRegistry::ForceAgentReadyStage(int pid, comm::AgentReadyStage stage) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    agent_ready_stages_[pid] = stage;
    agent_ready_cv_.notify_all();
}

bool SessionRegistry::ResetMismatchedRuntimeTraceForSpawn(int pid) {
    if (pid <= 0) {
        return false;
    }

    ClearAgentRuntimeReadyState(pid);
    ForceAgentReadyStage(pid, comm::AgentReadyStage::kControl);
    return true;
}

bool SessionRegistry::IsAgentRuntimeReady(int pid) const {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agent_runtime_ready_.find(pid);
    return it != agent_runtime_ready_.end() && it->second;
}

void SessionRegistry::RemoveAgentSessionByPid(int pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearAgentSessionStateLocked(pid, true, true);
    agent_ready_cv_.notify_all();
}

bool SessionRegistry::RemoveAgentSessionByPidIfMatches(int pid, comm::Session* session) {
    if (pid <= 0 || session == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    bool removed_any = false;
    auto history_it = agent_session_history_.find(pid);
    if (history_it != agent_session_history_.end()) {
        auto& session_list = history_it->second;
        const auto new_end = std::remove(session_list.begin(), session_list.end(), session);
        if (new_end != session_list.end()) {
            session_list.erase(new_end, session_list.end());
            removed_any = true;
        }
        if (session_list.empty()) {
            agent_session_history_.erase(history_it);
        }
    }

    auto it = agent_sessions_.find(pid);
    auto control_it = agent_control_sessions_.find(pid);
    if (control_it != agent_control_sessions_.end() && control_it->second == session) {
        agent_control_sessions_.erase(control_it);
        agent_control_process_names_.erase(pid);
        removed_any = true;
    }
    if (it == agent_sessions_.end() || it->second != session) {
        return removed_any;
    }

    if (!removed_any) {
        return false;
    }

    agent_sessions_.erase(it);
    ClearAttachSideStateForPidLocked(pid);
    auto rebound_history_it = agent_session_history_.find(pid);
    if (rebound_history_it != agent_session_history_.end() &&
        !rebound_history_it->second.empty()) {
        comm::Session* rebound_session = rebound_history_it->second.back();
        agent_sessions_[pid] = rebound_session;
        auto rebound_control_it = agent_control_sessions_.find(pid);
        if (rebound_control_it != agent_control_sessions_.end() &&
            rebound_control_it->second == rebound_session) {
            auto process_name_it = agent_pid_to_process_name_.find(pid);
            if (process_name_it != agent_pid_to_process_name_.end()) {
                auto current_pid_it = agent_process_name_to_pid_.find(process_name_it->second);
                if (current_pid_it != agent_process_name_to_pid_.end() &&
                    current_pid_it->second == pid) {
                    agent_process_name_to_pid_.erase(current_pid_it);
                }
            }
            auto control_name_it = agent_control_process_names_.find(pid);
            if (control_name_it != agent_control_process_names_.end() &&
                !control_name_it->second.empty()) {
                agent_pid_to_process_name_[pid] = control_name_it->second;
                agent_process_name_to_pid_[control_name_it->second] = pid;
            } else {
                agent_pid_to_process_name_.erase(pid);
            }
            agent_ready_stages_[pid] = comm::AgentReadyStage::kControl;
            agent_runtime_ready_.erase(pid);
            agent_ready_frames_.erase(pid);
            auto suspended_it = spawn_suspended_entries_.find(pid);
            if (suspended_it != spawn_suspended_entries_.end()) {
                suspended_it->second.authoritative_ready_stage =
                    PendingSpawnReadyStage::kControlReady;
                if (suspended_it->second.state != SpawnTransactionState::kWaitingAgentReady) {
                    suspended_it->second.state = SpawnTransactionState::kWaitingRuntimeReady;
                }
            }
        }
        agent_ready_cv_.notify_all();
        return true;
    }

    ClearAgentSessionStateLocked(pid, false, true);
    agent_ready_cv_.notify_all();
    return true;
}

bool SessionRegistry::DropAgentReadySessionIfMatches(int pid, comm::Session* session) {
    return RemoveAgentSessionByPidIfMatches(pid, session);
}

comm::Session* SessionRegistry::FindAgentSessionByPid(int pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agent_sessions_.find(pid);
    return it != agent_sessions_.end() ? it->second : nullptr;
}

bool SessionRegistry::IsAcceptedCurrentAgentSessionForPid(
    int pid,
    const comm::Session* session) const {
    if (pid <= 0 || session == nullptr) {
        return false;
    }

    comm::Session* current_agent = FindAgentSessionByPid(pid);
    if (current_agent == nullptr) {
        if (IsInvalidatedAgentPid(pid)) {
            return false;
        }
        return !WasAttachTimeoutPid(pid);
    }

    return current_agent == session;
}

comm::Session* SessionRegistry::FindAgentSessionByProcessName(const std::string& process_name) const {
    if (process_name.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto pid_it = agent_process_name_to_pid_.find(process_name);
    if (pid_it == agent_process_name_to_pid_.end()) {
        return nullptr;
    }

    auto session_it = agent_sessions_.find(pid_it->second);
    return session_it != agent_sessions_.end() ? session_it->second : nullptr;
}

comm::Session* SessionRegistry::FindAuthoritativeAgentSessionByPid(int pid) const {
    if (pid <= 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto ready_it = agent_authoritative_ready_.find(pid);
    if (ready_it == agent_authoritative_ready_.end() || !ready_it->second) {
        return nullptr;
    }

    auto session_it = agent_sessions_.find(pid);
    return session_it != agent_sessions_.end() ? session_it->second : nullptr;
}

comm::Session* SessionRegistry::FindAuthoritativeAgentSessionByProcessName(const std::string& process_name) const {
    if (process_name.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto pid_it = agent_process_name_to_pid_.find(process_name);
    if (pid_it == agent_process_name_to_pid_.end()) {
        return nullptr;
    }

    auto ready_it = agent_authoritative_ready_.find(pid_it->second);
    if (ready_it == agent_authoritative_ready_.end() || !ready_it->second) {
        return nullptr;
    }

    auto session_it = agent_sessions_.find(pid_it->second);
    return session_it != agent_sessions_.end() ? session_it->second : nullptr;
}

comm::Session* SessionRegistry::FindAuthoritativeAgentSessionByIdentity(int pid,
                                                                        const std::string& process_name) const {
    if (pid <= 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto ready_it = agent_authoritative_ready_.find(pid);
    if (ready_it == agent_authoritative_ready_.end() || !ready_it->second) {
        return nullptr;
    }

    if (!process_name.empty()) {
        auto process_it = agent_pid_to_process_name_.find(pid);
        if (process_it == agent_pid_to_process_name_.end() ||
            process_it->second != process_name) {
            return nullptr;
        }
    }

    auto session_it = agent_sessions_.find(pid);
    return session_it != agent_sessions_.end() ? session_it->second : nullptr;
}

comm::Session* SessionRegistry::FindRuntimeReadyAgentSessionByIdentity(int pid,
                                                                        const std::string& process_name) const {
    if (pid <= 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto runtime_it = agent_runtime_ready_.find(pid);
    auto authoritative_it = agent_authoritative_ready_.find(pid);
    auto stage_it = agent_ready_stages_.find(pid);
    if (runtime_it == agent_runtime_ready_.end() ||
        !runtime_it->second ||
        authoritative_it == agent_authoritative_ready_.end() ||
        !authoritative_it->second ||
        stage_it == agent_ready_stages_.end() ||
        stage_it->second != comm::AgentReadyStage::kRuntime) {
        return nullptr;
    }

    if (!process_name.empty()) {
        auto process_it = agent_pid_to_process_name_.find(pid);
        if (process_it == agent_pid_to_process_name_.end() ||
            process_it->second != process_name) {
            return nullptr;
        }

        auto pid_it = agent_process_name_to_pid_.find(process_name);
        if (pid_it == agent_process_name_to_pid_.end() || pid_it->second != pid) {
            return nullptr;
        }
    }

    auto session_it = agent_sessions_.find(pid);
    return session_it != agent_sessions_.end() ? session_it->second : nullptr;
}

comm::Session* SessionRegistry::FindRuntimeReadyAgentSessionForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry) const {
    if (pid <= 0 || !entry.suspended) {
        return nullptr;
    }

    const std::string expected_runtime_process_name =
        ResolveSpawnRuntimeProcessName(entry);
    if (expected_runtime_process_name.empty()) {
        return nullptr;
    }

    return FindRuntimeReadyAgentSessionByIdentity(pid,
                                                  expected_runtime_process_name);
}

bool SessionRegistry::IsAcceptedControlStageAgentSessionForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry,
    const std::string& process_name,
    const comm::Session* session) const {
    if (pid <= 0 || session == nullptr) {
        return false;
    }

    if (!entry.suspended) {
        return IsAcceptedCurrentAgentSessionForPid(pid, session);
    }

    comm::Session* control_agent =
        FindControlReadyAgentSessionForSpawn(pid, entry, process_name);
    if (control_agent != nullptr) {
        return control_agent == session;
    }

    return IsAcceptedCurrentAgentSessionForPid(pid, session);
}

comm::Session* SessionRegistry::FindControlReadyAgentSessionForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry,
    const std::string& process_name) const {
    if (pid <= 0 || !entry.suspended) {
        return nullptr;
    }

    if (!process_name.empty()) {
        const bool matches_target_process =
            !entry.target_process_name.empty() &&
            process_name == entry.target_process_name;
        const bool matches_authoritative_process =
            !entry.authoritative_process_name.empty() &&
            process_name == entry.authoritative_process_name;
        const bool has_known_control_identity =
            !entry.target_process_name.empty() ||
            !entry.authoritative_process_name.empty();
        if (has_known_control_identity &&
            !matches_target_process &&
            !matches_authoritative_process) {
            return nullptr;
        }
    }

    std::string expected_control_process_name;
    if (!entry.target_process_name.empty() &&
        !process_name.empty() &&
        process_name == entry.target_process_name) {
        expected_control_process_name = process_name;
    } else if (!entry.authoritative_process_name.empty()) {
        expected_control_process_name = entry.authoritative_process_name;
    }

    if (expected_control_process_name.empty()) {
        return FindAgentSessionByPid(pid);
    }
    comm::Session* resolved =
        FindControlReadyAgentSessionByIdentity(pid,
                                              expected_control_process_name);
    return resolved != nullptr ? resolved : FindAgentSessionByPid(pid);
}

bool SessionRegistry::IsAcceptedHostResponseAgentSessionForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry,
    const comm::Session* session) const {
    if (pid <= 0 || !entry.suspended || session == nullptr) {
        return false;
    }

    comm::Session* runtime_agent = FindRuntimeReadyAgentSessionForSpawn(pid, entry);
    if (runtime_agent != nullptr) {
        return runtime_agent == session;
    }

    return ResolveSpawnRuntimeProcessName(entry).empty();
}

bool SessionRegistry::IsAcceptedScriptMessageAgentSessionForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry,
    const comm::Session* session) const {
    if (pid <= 0 || !entry.suspended || session == nullptr) {
        return false;
    }

    if (entry.state == SpawnTransactionState::kWaitingAgentReady ||
        entry.state == SpawnTransactionState::kWaitingRuntimeReady) {
        comm::Session* control_agent =
            FindControlReadyAgentSessionForSpawn(pid, entry, "");
        return control_agent != nullptr ? control_agent == session : false;
    }

    comm::Session* runtime_agent = FindRuntimeReadyAgentSessionForSpawn(pid, entry);
    if (runtime_agent != nullptr) {
        return runtime_agent == session;
    }

    if (!ResolveSpawnRuntimeProcessName(entry).empty()) {
        comm::Session* current_agent = FindAgentSessionByPid(pid);
        if (current_agent == nullptr) {
            return false;
        }
        if (current_agent != session) {
            return false;
        }
        comm::AgentReadyStage stage = comm::AgentReadyStage::kControl;
        const bool has_runtime_stage =
            GetAgentReadyStage(pid, &stage) &&
            stage == comm::AgentReadyStage::kRuntime &&
            IsAgentRuntimeReady(pid);
        return !has_runtime_stage;
    }

    return false;
}

comm::Session* SessionRegistry::FindHostRequestAgentSessionForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry) const {
    if (pid <= 0 || !entry.suspended) {
        return nullptr;
    }

    comm::Session* runtime_agent = FindRuntimeReadyAgentSessionForSpawn(pid, entry);
    if (runtime_agent != nullptr) {
        return runtime_agent;
    }

    if (entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady ||
        entry.state == SpawnTransactionState::kReadyForScriptLoad ||
        entry.state == SpawnTransactionState::kScriptLoadDispatched) {
        return nullptr;
    }

    return FindAgentSessionByPid(pid);
}

bool SessionRegistry::HasAuthoritativeRuntimeBoundaryForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry,
    const std::string& process_name) const {
    if (pid <= 0 || !entry.suspended) {
        return false;
    }

    if (entry.authoritative_ready_stage != PendingSpawnReadyStage::kRuntimeReady) {
        return false;
    }

    return entry.authoritative_process_name.empty() ||
           process_name.empty() ||
           entry.authoritative_process_name == process_name;
}

bool SessionRegistry::HasMismatchedRuntimeTraceForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry,
    const std::string& expected_process_name,
    const std::string& candidate_process_name) const {
    if (pid <= 0 || !entry.suspended || !IsAgentRuntimeReady(pid)) {
        return false;
    }

    if (!expected_process_name.empty()) {
        if (FindRuntimeReadyAgentSessionByIdentity(pid, expected_process_name) != nullptr) {
            return false;
        }
        return FindAuthoritativeAgentSessionByPid(pid) != nullptr;
    }

    if (!candidate_process_name.empty()) {
        if (FindRuntimeReadyAgentSessionByIdentity(pid, candidate_process_name) != nullptr) {
            return false;
        }
        return FindAuthoritativeAgentSessionByPid(pid) != nullptr;
    }

    return false;
}

bool SessionRegistry::HasAcceptedRuntimeTraceForSpawn(
    int pid,
    const std::string& expected_process_name) const {
    if (pid <= 0 || !IsAgentRuntimeReady(pid)) {
        return false;
    }

    if (!expected_process_name.empty()) {
        return FindRuntimeReadyAgentSessionByIdentity(pid,
                                                      expected_process_name) != nullptr;
    }

    return FindAuthoritativeAgentSessionByPid(pid) != nullptr;
}

bool SessionRegistry::HasKnownSpawnControlIdentityMismatchForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry,
    const std::string& process_name) const {
    if (pid <= 0 || !entry.suspended || process_name.empty()) {
        return false;
    }

    const bool matches_target_process =
        !entry.target_process_name.empty() &&
        process_name == entry.target_process_name;
    const bool matches_authoritative_process =
        !entry.authoritative_process_name.empty() &&
        process_name == entry.authoritative_process_name;
    const bool has_known_control_identity =
        !entry.target_process_name.empty() ||
        !entry.authoritative_process_name.empty();
    return has_known_control_identity &&
           !matches_target_process &&
           !matches_authoritative_process;
}

bool SessionRegistry::DoesRuntimeSpawnProcessNameMatch(
    bool runtime_ready,
    const std::string& expected_process_name,
    const std::string& ready_process_name) const {
    return !runtime_ready ||
           expected_process_name.empty() ||
           ready_process_name.empty() ||
           expected_process_name == ready_process_name;
}

bool SessionRegistry::HasRuntimeRecordedForSpawn(
    int pid,
    const SpawnSuspendedEntry& entry,
    const std::string& expected_process_name,
    const std::string& ready_process_name,
    bool runtime_ready,
    bool has_spawn_suspended_context) const {
    const bool runtime_spawn_process_name_matches =
        DoesRuntimeSpawnProcessNameMatch(runtime_ready,
                                         expected_process_name,
                                         ready_process_name);
    const bool mismatched_runtime_trace_for_spawn_target =
        has_spawn_suspended_context &&
        !runtime_ready &&
        runtime_spawn_process_name_matches &&
        HasMismatchedRuntimeTraceForSpawn(pid,
                                          entry,
                                          expected_process_name,
                                          ready_process_name);
    const bool transaction_runtime_boundary =
        HasAuthoritativeRuntimeBoundaryForSpawn(pid, entry, ready_process_name);
    return transaction_runtime_boundary ||
           (!mismatched_runtime_trace_for_spawn_target &&
            HasAcceptedRuntimeTraceForSpawn(pid, expected_process_name));
}

bool SessionRegistry::ShouldDropLateControlAgentReadyAtRuntimeBoundary(
    int pid,
    const SpawnSuspendedEntry& entry,
    const std::string& ready_process_name,
    bool runtime_ready) const {
    return !runtime_ready &&
           HasAuthoritativeRuntimeBoundaryForSpawn(pid, entry, ready_process_name);
}

bool SessionRegistry::ShouldDropLateControlAgentReadyFromNonCurrentSession(
    int pid,
    const SpawnSuspendedEntry& entry,
    const std::string& expected_process_name,
    const std::string& ready_process_name,
    bool runtime_ready,
    bool has_spawn_suspended_context,
    bool current_agent_session_matches) const {
    return !runtime_ready &&
           HasRuntimeRecordedForSpawn(pid,
                                      entry,
                                      expected_process_name,
                                      ready_process_name,
                                      runtime_ready,
                                      has_spawn_suspended_context) &&
           !current_agent_session_matches;
}

AgentReadyRegistrationPlan SessionRegistry::PlanAgentReadyRegistration(
    bool runtime_ready,
    bool runtime_spawn_process_name_matches,
    bool runtime_already_recorded,
    bool pending_attach_matches,
    bool has_spawn_token) const {
    AgentReadyRegistrationPlan plan;

    plan.register_runtime_globally =
        runtime_ready && runtime_spawn_process_name_matches;
    plan.remove_runtime_session_for_mismatch =
        runtime_ready && !runtime_spawn_process_name_matches;
    plan.register_control_globally =
        !runtime_ready && !runtime_already_recorded;
    plan.register_control_identity = !runtime_ready;
    plan.clear_pending_attach =
        pending_attach_matches && runtime_ready && runtime_spawn_process_name_matches;
    plan.upgrade_spawn_authoritative_ready = runtime_spawn_process_name_matches;
    plan.spawn_ready_stage = runtime_spawn_process_name_matches
                                 ? (runtime_ready ? PendingSpawnReadyStage::kRuntimeReady
                                                  : PendingSpawnReadyStage::kControlReady)
                                 : PendingSpawnReadyStage::kNone;
    plan.resolve_pending_spawn =
        has_spawn_token && runtime_spawn_process_name_matches;
    plan.eligible_runtime_ready =
        runtime_ready && runtime_spawn_process_name_matches;

    return plan;
}

AgentReadyRegistrationResult SessionRegistry::ApplyAgentReadyRegistrationPlan(
    int previous_pid,
    int pid,
    const std::string& ready_token,
    const std::string& process_name,
    comm::AgentReadyStage ready_stage,
    const comm::Frame& ready_frame,
    comm::Session* session,
    const AgentReadyRegistrationPlan& plan) {
    AgentReadyRegistrationResult result;
    if (pid <= 0 || session == nullptr) {
        return result;
    }

    if (previous_pid > 0 &&
        previous_pid != pid &&
        plan.register_runtime_globally) {
        result.removed_previous_pid_session =
            RemoveAgentSessionByPidIfMatches(previous_pid, session);
    }

    if (plan.register_runtime_globally) {
        RegisterAgentSession(pid, session);
        RegisterAgentProcessName(pid, process_name);
        MarkAgentReadyStage(pid, ready_stage);
        StoreAgentReadyFrame(pid, ready_frame);
        MarkAgentAuthoritativeReady(pid);
        MarkAgentRuntimeReady(pid);
    } else if (plan.remove_runtime_session_for_mismatch) {
        result.removed_runtime_session_for_mismatch =
            RemoveAgentSessionByPidIfMatches(pid, session);
    } else {
        if (plan.register_control_globally) {
            RegisterAgentSession(pid, session);
            RegisterAgentProcessName(pid, process_name);
        }
        if (plan.register_control_identity) {
            RegisterControlReadyAgentSession(pid, session);
            MarkAgentReadyStage(pid, ready_stage);
            MarkAgentAuthoritativeReady(pid);
        }
    }

    if (plan.clear_pending_attach) {
        ClearPendingAttach(ready_token);
        result.cleared_pending_attach = true;
    }

    if (plan.upgrade_spawn_authoritative_ready) {
        result.upgraded_spawn_authoritative_ready =
            UpdateSpawnSuspendedAuthoritativeReady(pid,
                                                   plan.spawn_ready_stage,
                                                   process_name);
    }

    if (plan.resolve_pending_spawn && !ready_token.empty()) {
        result.resolved_pending_spawn =
            ResolvePendingSpawn(ready_token,
                                pid,
                                process_name,
                                ready_stage);
        if (result.resolved_pending_spawn) {
            result.bound_host_to_resolved_pending_spawn =
                BindHostToResolvedPendingSpawn(ready_token, pid, nullptr);
        }
    }

    return result;
}

comm::Session* SessionRegistry::FindControlReadyAgentSessionByPid(int pid) const {
    if (pid <= 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto ready_it = agent_authoritative_ready_.find(pid);
    if (ready_it == agent_authoritative_ready_.end() || !ready_it->second) {
        return nullptr;
    }
    auto stage_it = agent_ready_stages_.find(pid);
    if (stage_it == agent_ready_stages_.end() ||
        (stage_it->second != comm::AgentReadyStage::kControl &&
         stage_it->second != comm::AgentReadyStage::kRuntime)) {
        return nullptr;
    }

    return ResolvePreferredControlReadySessionLocked(agent_sessions_,
                                                     agent_control_sessions_,
                                                     pid);
}

comm::Session* SessionRegistry::FindControlReadyAgentSessionByProcessName(const std::string& process_name) const {
    if (process_name.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto pid_it = agent_process_name_to_pid_.find(process_name);
    if (pid_it == agent_process_name_to_pid_.end()) {
        return nullptr;
    }
    auto ready_it = agent_authoritative_ready_.find(pid_it->second);
    if (ready_it == agent_authoritative_ready_.end() || !ready_it->second) {
        return nullptr;
    }
    auto stage_it = agent_ready_stages_.find(pid_it->second);
    if (stage_it == agent_ready_stages_.end() ||
        (stage_it->second != comm::AgentReadyStage::kControl &&
         stage_it->second != comm::AgentReadyStage::kRuntime)) {
        return nullptr;
    }

    return ResolvePreferredControlReadySessionLocked(agent_sessions_,
                                                     agent_control_sessions_,
                                                     pid_it->second);
}

comm::Session* SessionRegistry::FindControlReadyAgentSessionByIdentity(int pid,
                                                                       const std::string& process_name) const {
    if (pid > 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto ready_it = agent_authoritative_ready_.find(pid);
        auto stage_it = agent_ready_stages_.find(pid);
        if (ready_it == agent_authoritative_ready_.end() ||
            !ready_it->second ||
            stage_it == agent_ready_stages_.end() ||
            (stage_it->second != comm::AgentReadyStage::kControl &&
             stage_it->second != comm::AgentReadyStage::kRuntime)) {
            return nullptr;
        }

        if (!process_name.empty()) {
            auto process_it = agent_pid_to_process_name_.find(pid);
            if (process_it == agent_pid_to_process_name_.end() ||
                process_it->second != process_name) {
                return nullptr;
            }

            auto pid_it = agent_process_name_to_pid_.find(process_name);
            if (pid_it == agent_process_name_to_pid_.end() || pid_it->second != pid) {
                return nullptr;
            }
        }

        return ResolvePreferredControlReadySessionLocked(agent_sessions_,
                                                         agent_control_sessions_,
                                                         pid);
    }

    if (process_name.empty()) {
        return nullptr;
    }
    return FindControlReadyAgentSessionByProcessName(process_name);
}

void SessionRegistry::MarkOwnedZygoteControlProcess(int pid, const std::string& process_name) {
    if (process_name.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    OwnedZygoteControlTarget target;
    target.pid = pid;
    target.process_name = process_name;
    owned_zygote_control_processes_[process_name] = target;
}

void SessionRegistry::ClearOwnedZygoteControlProcess(const std::string& process_name) {
    if (process_name.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    owned_zygote_control_processes_.erase(process_name);
}

bool SessionRegistry::IsOwnedZygoteControlProcess(const std::string& process_name) const {
    if (process_name.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return owned_zygote_control_processes_.find(process_name) !=
           owned_zygote_control_processes_.end();
}

bool SessionRegistry::IsOwnedZygoteControlTarget(int pid, const std::string& process_name) const {
    if (process_name.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = owned_zygote_control_processes_.find(process_name);
    if (it == owned_zygote_control_processes_.end()) {
        return false;
    }

    if (pid <= 0) {
        return true;
    }

    return it->second.pid == pid;
}

bool SessionRegistry::GetOwnedZygoteControlTarget(const std::string& process_name,
                                                  OwnedZygoteControlTarget* out) const {
    if (process_name.empty() || out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = owned_zygote_control_processes_.find(process_name);
    if (it == owned_zygote_control_processes_.end()) {
        return false;
    }

    *out = it->second;
    return true;
}

comm::Session* SessionRegistry::WaitForAgentSessionByIdentity(int pid,
                                                              const std::string& process_name,
                                                              uint32_t timeout_ms) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto resolve_locked = [&]() -> comm::Session* {
        if (shutdown_) {
            return nullptr;
        }
        if (pid > 0) {
            auto session_it = agent_sessions_.find(pid);
            if (session_it != agent_sessions_.end()) {
                return session_it->second;
            }

            if (!process_name.empty()) {
                auto pid_it = agent_process_name_to_pid_.find(process_name);
                if (pid_it != agent_process_name_to_pid_.end() && pid_it->second == pid) {
                    auto rebound_session_it = agent_sessions_.find(pid);
                    if (rebound_session_it != agent_sessions_.end()) {
                        return rebound_session_it->second;
                    }
                }
            }

            return nullptr;
        }

        if (process_name.empty()) {
            return nullptr;
        }

        auto pid_it = agent_process_name_to_pid_.find(process_name);
        if (pid_it == agent_process_name_to_pid_.end()) {
            return nullptr;
        }

        auto control_it = agent_control_sessions_.find(pid_it->second);
        if (control_it != agent_control_sessions_.end()) {
            return control_it->second;
        }

        auto session_it = agent_sessions_.find(pid_it->second);
        return session_it != agent_sessions_.end() ? session_it->second : nullptr;
    };

    comm::Session* resolved = resolve_locked();
    if (resolved != nullptr) {
        return resolved;
    }

    if (!agent_ready_cv_.wait_for(lock,
                                  std::chrono::milliseconds(timeout_ms),
                                  [&]() { return shutdown_ || resolve_locked() != nullptr; })) {
        return nullptr;
    }

    if (shutdown_) {
        return nullptr;
    }
    return resolve_locked();
}

comm::Session* SessionRegistry::WaitForAuthoritativeAgentSessionByIdentity(int pid,
                                                                           const std::string& process_name,
                                                                           uint32_t timeout_ms) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto resolve_locked = [&]() -> comm::Session* {
        if (shutdown_) {
            return nullptr;
        }
        if (pid > 0) {
            auto ready_it = agent_authoritative_ready_.find(pid);
            if (ready_it != agent_authoritative_ready_.end() && ready_it->second) {
                auto resolved = ResolvePreferredAuthoritativeSessionLocked(agent_sessions_,
                                                                          agent_control_sessions_,
                                                                          pid);
                if (resolved != nullptr) {
                    return resolved;
                }
            }

            if (!process_name.empty()) {
                auto pid_it = agent_process_name_to_pid_.find(process_name);
                if (pid_it != agent_process_name_to_pid_.end() && pid_it->second == pid) {
                    auto rebound_ready_it = agent_authoritative_ready_.find(pid);
                    auto rebound_session_it = agent_sessions_.find(pid);
                    if (rebound_ready_it != agent_authoritative_ready_.end() &&
                        rebound_ready_it->second &&
                        rebound_session_it != agent_sessions_.end()) {
                        return rebound_session_it->second;
                    }
                }
            }

            return nullptr;
        }

        if (process_name.empty()) {
            return nullptr;
        }

        auto pid_it = agent_process_name_to_pid_.find(process_name);
        if (pid_it == agent_process_name_to_pid_.end()) {
            return nullptr;
        }

        auto ready_it = agent_authoritative_ready_.find(pid_it->second);
        if (ready_it == agent_authoritative_ready_.end() || !ready_it->second) {
            return nullptr;
        }

        auto session_it = agent_sessions_.find(pid_it->second);
        return session_it != agent_sessions_.end() ? session_it->second : nullptr;
    };

    comm::Session* resolved = resolve_locked();
    if (resolved != nullptr) {
        return resolved;
    }

    if (!agent_ready_cv_.wait_for(lock,
                                  std::chrono::milliseconds(timeout_ms),
                                  [&]() { return shutdown_ || resolve_locked() != nullptr; })) {
        return nullptr;
    }

    if (shutdown_) {
        return nullptr;
    }
    return resolve_locked();
}

comm::Session* SessionRegistry::WaitForRuntimeReadyAgentSessionByIdentity(int pid,
                                                                          const std::string& process_name,
                                                                          uint32_t timeout_ms) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto resolve_locked = [&]() -> comm::Session* {
        if (shutdown_) {
            return nullptr;
        }
        if (pid > 0) {
            auto runtime_it = agent_runtime_ready_.find(pid);
            auto authoritative_it = agent_authoritative_ready_.find(pid);
            auto stage_it = agent_ready_stages_.find(pid);
            if (runtime_it != agent_runtime_ready_.end() &&
                runtime_it->second &&
                authoritative_it != agent_authoritative_ready_.end() &&
                authoritative_it->second &&
                stage_it != agent_ready_stages_.end() &&
                stage_it->second == comm::AgentReadyStage::kRuntime) {
                if (process_name.empty()) {
                    auto session_it = agent_sessions_.find(pid);
                    if (session_it != agent_sessions_.end()) {
                        return session_it->second;
                    }
                    return nullptr;
                }

                auto pid_it = agent_process_name_to_pid_.find(process_name);
                if (pid_it != agent_process_name_to_pid_.end() && pid_it->second == pid) {
                    auto process_it = agent_pid_to_process_name_.find(pid);
                    auto session_it = agent_sessions_.find(pid);
                    if (process_it != agent_pid_to_process_name_.end() &&
                        process_it->second == process_name &&
                        session_it != agent_sessions_.end()) {
                        return session_it->second;
                    }
                }
            }
            return nullptr;
        }

        if (process_name.empty()) {
            return nullptr;
        }

        auto pid_it = agent_process_name_to_pid_.find(process_name);
        if (pid_it == agent_process_name_to_pid_.end()) {
            return nullptr;
        }

        auto runtime_it = agent_runtime_ready_.find(pid_it->second);
        auto authoritative_it = agent_authoritative_ready_.find(pid_it->second);
        auto stage_it = agent_ready_stages_.find(pid_it->second);
        auto session_it = agent_sessions_.find(pid_it->second);
        if (runtime_it == agent_runtime_ready_.end() ||
            !runtime_it->second ||
            authoritative_it == agent_authoritative_ready_.end() ||
            !authoritative_it->second ||
            stage_it == agent_ready_stages_.end() ||
            stage_it->second != comm::AgentReadyStage::kRuntime ||
            session_it == agent_sessions_.end()) {
            return nullptr;
        }
        return session_it->second;
    };

    comm::Session* resolved = resolve_locked();
    if (resolved != nullptr) {
        return resolved;
    }

    if (!agent_ready_cv_.wait_for(lock,
                                  std::chrono::milliseconds(timeout_ms),
                                  [&]() { return shutdown_ || resolve_locked() != nullptr; })) {
        return nullptr;
    }

    if (shutdown_) {
        return nullptr;
    }
    return resolve_locked();
}

comm::Session* SessionRegistry::WaitForRuntimeReadyAgentSessionByToken(int pid,
                                                                       const std::string& process_name,
                                                                       const std::string& ready_token,
                                                                       uint32_t timeout_ms) const {
    if (ready_token.empty()) {
        return WaitForRuntimeReadyAgentSessionByIdentity(pid, process_name, timeout_ms);
    }

    std::unique_lock<std::mutex> lock(mutex_);
    auto resolve_locked = [&]() -> comm::Session* {
        if (shutdown_ || pid <= 0) {
            return nullptr;
        }

        auto runtime_it = agent_runtime_ready_.find(pid);
        auto authoritative_it = agent_authoritative_ready_.find(pid);
        if (runtime_it == agent_runtime_ready_.end() ||
            !runtime_it->second ||
            authoritative_it == agent_authoritative_ready_.end() ||
            !authoritative_it->second) {
            return nullptr;
        }

        auto frame_it = agent_ready_frames_.find(pid);
        if (frame_it == agent_ready_frames_.end()) {
            return nullptr;
        }

        comm::AgentReady ready;
        if (!comm::DecodeAgentReady(frame_it->second.GetPayload().data(),
                                    frame_it->second.GetPayload().size(),
                                    &ready)) {
            return nullptr;
        }

        if (ready.spawn_token != ready_token) {
            return nullptr;
        }
        if (!process_name.empty() && !ready.process_name.empty() && ready.process_name != process_name) {
            return nullptr;
        }

        auto session_it = agent_sessions_.find(pid);
        return session_it != agent_sessions_.end() ? session_it->second : nullptr;
    };
    auto pending_attach_still_exists_locked = [&]() -> bool {
        return pending_attaches_.find(ready_token) != pending_attaches_.end();
    };

    comm::Session* resolved = resolve_locked();
    if (resolved != nullptr) {
        return resolved;
    }

    if (!pending_attach_still_exists_locked()) {
        return nullptr;
    }

    if (!agent_ready_cv_.wait_for(lock,
                                  std::chrono::milliseconds(timeout_ms),
                                  [&]() {
                                      return shutdown_ ||
                                             resolve_locked() != nullptr ||
                                             !pending_attach_still_exists_locked();
                                  })) {
        return nullptr;
    }

    if (shutdown_) {
        return nullptr;
    }
    return resolve_locked();
}

comm::Session* SessionRegistry::WaitForControlReadyAgentSessionByIdentity(int pid,
                                                                          const std::string& process_name,
                                                                          uint32_t timeout_ms) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto is_control_capable = [](comm::AgentReadyStage stage) {
        return stage == comm::AgentReadyStage::kControl ||
               stage == comm::AgentReadyStage::kRuntime;
    };
    auto resolve_locked = [&]() -> comm::Session* {
        if (shutdown_) {
            return nullptr;
        }
        if (pid > 0) {
            auto ready_it = agent_authoritative_ready_.find(pid);
            auto stage_it = agent_ready_stages_.find(pid);
            if (ready_it != agent_authoritative_ready_.end() &&
                ready_it->second &&
                stage_it != agent_ready_stages_.end() &&
                is_control_capable(stage_it->second)) {
                auto resolved = ResolvePreferredControlReadySessionLocked(agent_sessions_,
                                                                         agent_control_sessions_,
                                                                         pid);
                if (resolved != nullptr) {
                    return resolved;
                }
            }

            if (!process_name.empty()) {
                auto pid_it = agent_process_name_to_pid_.find(process_name);
                if (pid_it != agent_process_name_to_pid_.end() && pid_it->second == pid) {
                    auto rebound_ready_it = agent_authoritative_ready_.find(pid);
                    auto rebound_stage_it = agent_ready_stages_.find(pid);
                    if (rebound_ready_it != agent_authoritative_ready_.end() &&
                        rebound_ready_it->second &&
                        rebound_stage_it != agent_ready_stages_.end() &&
                        is_control_capable(rebound_stage_it->second)) {
                        auto resolved = ResolvePreferredControlReadySessionLocked(agent_sessions_,
                                                                                 agent_control_sessions_,
                                                                                 pid);
                        if (resolved != nullptr) {
                            return resolved;
                        }
                    }
                }
            }

            return nullptr;
        }

        if (process_name.empty()) {
            return nullptr;
        }

        auto pid_it = agent_process_name_to_pid_.find(process_name);
        if (pid_it == agent_process_name_to_pid_.end()) {
            return nullptr;
        }

        auto ready_it = agent_authoritative_ready_.find(pid_it->second);
        auto stage_it = agent_ready_stages_.find(pid_it->second);
        if (ready_it == agent_authoritative_ready_.end() ||
            !ready_it->second ||
            stage_it == agent_ready_stages_.end() ||
            !is_control_capable(stage_it->second)) {
            return nullptr;
        }

        return ResolvePreferredControlReadySessionLocked(agent_sessions_,
                                                         agent_control_sessions_,
                                                         pid_it->second);
    };

    comm::Session* resolved = resolve_locked();
    if (resolved != nullptr) {
        return resolved;
    }

    if (!agent_ready_cv_.wait_for(lock,
                                  std::chrono::milliseconds(timeout_ms),
                                  [&]() { return shutdown_ || resolve_locked() != nullptr; })) {
        return nullptr;
    }

    if (shutdown_) {
        return nullptr;
    }
    return resolve_locked();
}

comm::Session* SessionRegistry::WaitForAgentSessionByProcessName(const std::string& process_name,
                                                                 uint32_t timeout_ms) const {
    if (process_name.empty()) {
        return nullptr;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    auto is_ready = [&]() {
        if (shutdown_) {
            return true;
        }
        auto pid_it = agent_process_name_to_pid_.find(process_name);
        if (pid_it == agent_process_name_to_pid_.end()) {
            return false;
        }
        return agent_sessions_.find(pid_it->second) != agent_sessions_.end();
    };

    if (!agent_ready_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), is_ready)) {
        return nullptr;
    }

    if (shutdown_) {
        return nullptr;
    }
    auto pid_it = agent_process_name_to_pid_.find(process_name);
    if (pid_it == agent_process_name_to_pid_.end()) {
        return nullptr;
    }
    auto session_it = agent_sessions_.find(pid_it->second);
    return session_it != agent_sessions_.end() ? session_it->second : nullptr;
}

bool SessionRegistry::WaitForAgentSessionDisconnectByIdentity(int pid,
                                                              const std::string& process_name,
                                                              uint32_t timeout_ms) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto is_disconnected_locked = [&]() -> bool {
        if (shutdown_) {
            return true;
        }

        if (pid > 0) {
            if (agent_sessions_.find(pid) != agent_sessions_.end()) {
                return false;
            }

            auto history_it = agent_session_history_.find(pid);
            if (history_it != agent_session_history_.end() && !history_it->second.empty()) {
                return false;
            }

            if (!process_name.empty()) {
                auto pid_it = agent_process_name_to_pid_.find(process_name);
                if (pid_it != agent_process_name_to_pid_.end() && pid_it->second == pid) {
                    return false;
                }
            }

            return true;
        }

        if (process_name.empty()) {
            return true;
        }

        auto pid_it = agent_process_name_to_pid_.find(process_name);
        if (pid_it == agent_process_name_to_pid_.end()) {
            return true;
        }

        return agent_sessions_.find(pid_it->second) == agent_sessions_.end();
    };

    if (is_disconnected_locked()) {
        return !shutdown_;
    }

    if (!agent_ready_cv_.wait_for(lock,
                                  std::chrono::milliseconds(timeout_ms),
                                  [&]() { return shutdown_ || is_disconnected_locked(); })) {
        return false;
    }

    if (shutdown_) {
        return false;
    }
    return is_disconnected_locked();
}

void SessionRegistry::StoreAgentReadyFrame(int pid, const comm::Frame& frame) {
    if (pid <= 0 || frame.GetType() != comm::MessageType::kAgentReady) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    agent_ready_frames_[pid] = frame;
    agent_ready_cv_.notify_all();
}

bool SessionRegistry::GetAgentReadyFrame(int pid, comm::Frame* frame) const {
    if (pid <= 0 || frame == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agent_ready_frames_.find(pid);
    if (it == agent_ready_frames_.end()) {
        return false;
    }

    *frame = it->second;
    return true;
}

bool SessionRegistry::GetAgentReadyFrameByIdentity(int pid,
                                                   const std::string& process_name,
                                                   comm::Frame* frame) const {
    if (pid <= 0 || frame == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agent_ready_frames_.find(pid);
    if (it == agent_ready_frames_.end()) {
        return false;
    }

    if (!process_name.empty()) {
        auto process_it = agent_pid_to_process_name_.find(pid);
        if (process_it == agent_pid_to_process_name_.end() ||
            process_it->second != process_name) {
            return false;
        }

        auto pid_it = agent_process_name_to_pid_.find(process_name);
        if (pid_it == agent_process_name_to_pid_.end() || pid_it->second != pid) {
            return false;
        }
    }

    comm::AgentReady ready;
    if (!comm::DecodeAgentReady(it->second.GetPayload().data(),
                                it->second.GetPayload().size(),
                                &ready)) {
        return false;
    }
    if (static_cast<int>(ready.pid) != pid) {
        return false;
    }
    if (!process_name.empty() && ready.process_name != process_name) {
        return false;
    }

    *frame = it->second;
    return true;
}

bool SessionRegistry::WaitForAgentRuntimeReady(int pid, uint32_t timeout_ms) const {
    if (pid <= 0) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    auto is_ready = [&]() {
        if (shutdown_) {
            return true;
        }
        auto it = agent_runtime_ready_.find(pid);
        return it != agent_runtime_ready_.end() && it->second;
    };

    if (!agent_ready_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), is_ready)) {
        return false;
    }

    if (shutdown_) {
        return false;
    }
    auto it = agent_runtime_ready_.find(pid);
    return it != agent_runtime_ready_.end() && it->second;
}

void SessionRegistry::StoreScriptMessageFrame(int pid, const comm::Frame& frame) {
    if (pid <= 0 || frame.GetType() != comm::MessageType::kScriptMessage) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    script_message_frames_[pid].push_back(frame);
}

std::vector<comm::Frame> SessionRegistry::GetScriptMessageFrames(int pid) const {
    if (pid <= 0) {
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = script_message_frames_.find(pid);
    if (it == script_message_frames_.end()) {
        return {};
    }
    return it->second;
}

std::vector<comm::Frame> SessionRegistry::TakeScriptMessageFrames(int pid) {
    if (pid <= 0) {
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = script_message_frames_.find(pid);
    if (it == script_message_frames_.end()) {
        return {};
    }

    std::vector<comm::Frame> frames = std::move(it->second);
    script_message_frames_.erase(it);
    return frames;
}

void SessionRegistry::ClearScriptMessageFrames(int pid) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    script_message_frames_.erase(pid);
}

void SessionRegistry::DropScriptMessageFramePrefix(int pid, size_t count) {
    if (pid <= 0 || count == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = script_message_frames_.find(pid);
    if (it == script_message_frames_.end()) {
        return;
    }

    std::vector<comm::Frame>& frames = it->second;
    if (count >= frames.size()) {
        script_message_frames_.erase(it);
        return;
    }

    frames.erase(frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(count));
}

bool SessionRegistry::ShouldCacheScriptMessageForPid(int pid) const {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto suspended_it = spawn_suspended_entries_.find(pid);
    if (suspended_it != spawn_suspended_entries_.end()) {
        if (SpawnStateBlocksScriptMessageForwarding(suspended_it->second.state)) {
            return true;
        }
    }

    for (const auto& pending_spawn : pending_spawns_) {
        if (pending_spawn.second.pid == pid) {
            return true;
        }
    }

    return false;
}

void SessionRegistry::MarkSpawnSuspended(int pid,
                                         uint32_t host_session_id,
                                         PendingSpawnReadyStage authoritative_ready_stage,
                                         const std::string& authoritative_process_name,
                                         const std::string& target_process_name,
                                         const std::string& spawn_token) {
    if (pid <= 0 || host_session_id == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto existing = spawn_suspended_entries_.find(pid);
    if (existing != spawn_suspended_entries_.end()) {
        existing->second.pid = pid;
        existing->second.host_session_id = host_session_id;
        if (!spawn_token.empty()) {
            existing->second.spawn_token = spawn_token;
        }
        existing->second.suspended = true;
        if (PendingSpawnReadyStageRank(authoritative_ready_stage) >=
            PendingSpawnReadyStageRank(existing->second.authoritative_ready_stage)) {
            existing->second.authoritative_ready_stage = authoritative_ready_stage;
            if (!authoritative_process_name.empty()) {
                existing->second.authoritative_process_name = authoritative_process_name;
            }
        }
        if (!target_process_name.empty()) {
            existing->second.target_process_name = target_process_name;
        }
        const SpawnTransactionState resolved_state =
            SpawnStateForPendingReadyStage(existing->second.authoritative_ready_stage);
        if (CanTransitionSpawnState(existing->second.state, resolved_state)) {
            existing->second.state = resolved_state;
        }
        return;
    }

    spawn_suspended_entries_[pid] = SpawnSuspendedEntry{
        pid,
        host_session_id,
        spawn_token,
        true,
        false,
        SpawnStateForPendingReadyStage(authoritative_ready_stage),
        authoritative_ready_stage,
        authoritative_process_name,
        target_process_name,
    };
}

bool SessionRegistry::IsSpawnSuspended(int pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    return it != spawn_suspended_entries_.end() && it->second.suspended;
}

bool SessionRegistry::GetSpawnSuspendedEntry(int pid, SpawnSuspendedEntry* out) const {
    if (pid <= 0 || out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end()) {
        return false;
    }

    *out = it->second;
    return true;
}

uint32_t SessionRegistry::FindSuspendedSpawnOwnerHostSessionId(int pid) const {
    if (pid <= 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end() || !it->second.suspended) {
        return 0;
    }

    return it->second.host_session_id;
}

bool SessionRegistry::IsForeignSuspendedSpawnOwner(int pid, uint32_t host_session_id) const {
    if (pid <= 0 || host_session_id == 0) {
        return false;
    }

    const uint32_t owner_host_session_id = FindSuspendedSpawnOwnerHostSessionId(pid);
    return owner_host_session_id != 0 && owner_host_session_id != host_session_id;
}

bool SessionRegistry::UpdateSpawnSuspendedAuthoritativeReady(
    int pid,
    PendingSpawnReadyStage authoritative_ready_stage,
    const std::string& authoritative_process_name) {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end()) {
        return false;
    }

    if (PendingSpawnReadyStageRank(authoritative_ready_stage) >=
        PendingSpawnReadyStageRank(it->second.authoritative_ready_stage)) {
        it->second.authoritative_ready_stage = authoritative_ready_stage;
        if (!authoritative_process_name.empty()) {
            it->second.authoritative_process_name = authoritative_process_name;
        }
    }
    return true;
}

bool SessionRegistry::SetSpawnResponsePending(int pid, bool pending) {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end()) {
        return false;
    }

    it->second.response_pending = pending;
    if (!pending &&
        it->second.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady &&
        CanTransitionSpawnState(it->second.state, SpawnTransactionState::kReadyForScriptLoad)) {
        it->second.state = SpawnTransactionState::kReadyForScriptLoad;
    }
    return true;
}

bool SessionRegistry::ReleaseSpawnResponseBoundary(int pid, SpawnSuspendedEntry* out) {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end()) {
        return false;
    }

    it->second.response_pending = false;
    if (it->second.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady &&
        CanTransitionSpawnState(it->second.state, SpawnTransactionState::kReadyForScriptLoad)) {
        it->second.state = SpawnTransactionState::kReadyForScriptLoad;
    }

    if (out != nullptr) {
        *out = it->second;
    }
    return true;
}

bool SessionRegistry::CanExposeSpawnRuntimeReadyImmediately(int pid) const {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end() || !it->second.suspended) {
        return false;
    }

    return !it->second.response_pending &&
           it->second.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady;
}

bool SessionRegistry::IsSpawnBlockedForScriptOperations(int pid) const {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end() || !it->second.suspended) {
        return false;
    }

    return it->second.state == SpawnTransactionState::kWaitingAgentReady ||
           it->second.state == SpawnTransactionState::kWaitingRuntimeReady;
}

bool SessionRegistry::MarkSpawnRuntimeReadyVisible(int pid) {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end()) {
        return false;
    }

    if (it->second.response_pending) {
        return false;
    }

    if (it->second.authoritative_ready_stage != PendingSpawnReadyStage::kRuntimeReady) {
        return false;
    }

    if (!CanTransitionSpawnState(it->second.state, SpawnTransactionState::kReadyForScriptLoad)) {
        return false;
    }

    it->second.state = SpawnTransactionState::kReadyForScriptLoad;
    return true;
}

bool SessionRegistry::MarkSpawnScriptLoadInFlight(int pid) {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end()) {
        return false;
    }

    if (!CanTransitionSpawnState(it->second.state, SpawnTransactionState::kScriptLoadDispatched)) {
        return false;
    }

    it->second.state = SpawnTransactionState::kScriptLoadDispatched;
    return true;
}

bool SessionRegistry::MarkSpawnScriptLoadComplete(int pid) {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end()) {
        return false;
    }

    if (!CanTransitionSpawnState(it->second.state, SpawnTransactionState::kReadyForScriptLoad)) {
        return false;
    }

    it->second.state = SpawnTransactionState::kReadyForScriptLoad;
    return true;
}

bool SessionRegistry::UpdateSpawnState(int pid, SpawnTransactionState state) {
    if (pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spawn_suspended_entries_.find(pid);
    if (it == spawn_suspended_entries_.end()) {
        return false;
    }

    if (!CanTransitionSpawnState(it->second.state, state)) {
        return false;
    }

    it->second.state = state;
    return true;
}

void SessionRegistry::ClearSpawnSuspended(int pid) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    spawn_suspended_entries_.erase(pid);
}

void SessionRegistry::ClearSpawnTransactionByPid(int pid) {
    if (pid <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    spawn_suspended_entries_.erase(pid);
    script_message_frames_.erase(pid);
    ClearAgentStateLocked(pid);
    attach_timeout_pids_.erase(pid);
    invalidated_agent_pids_[pid] = true;
    for (auto it = pid_to_host_session_.begin(); it != pid_to_host_session_.end();) {
        if (it->first == pid) {
            it = pid_to_host_session_.erase(it);
        } else {
            ++it;
        }
    }
    agent_ready_cv_.notify_all();
}

void SessionRegistry::RegisterPendingSpawn(const std::string& spawn_token,
                                           const std::string& process_name,
                                           uint32_t host_session_id) {
    if (spawn_token.empty() || host_session_id == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pending_spawns_[spawn_token] = PendingSpawnEntry{
        spawn_token,
        process_name,
        host_session_id,
        -1,
        false,
        PendingSpawnReadyStage::kNone,
        {},
    };
    pending_spawn_cv_.notify_all();
}

bool SessionRegistry::TakePendingSpawn(const std::string& spawn_token, PendingSpawnEntry* out) {
    if (spawn_token.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_spawns_.find(spawn_token);
    if (it == pending_spawns_.end()) {
        return false;
    }

    if (out != nullptr) {
        *out = it->second;
    }
    pending_spawns_.erase(it);
    pending_spawn_cv_.notify_all();
    return true;
}

bool SessionRegistry::BindHostToResolvedPendingSpawn(const std::string& spawn_token,
                                                     int pid,
                                                     PendingSpawnEntry* out) {
    if (spawn_token.empty() || pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto pending_it = pending_spawns_.find(spawn_token);
    if (pending_it == pending_spawns_.end()) {
        return false;
    }

    PendingSpawnEntry& pending = pending_it->second;
    if (!pending.ready || pending.pid != pid || pending.host_session_id == 0) {
        return false;
    }

    if (host_sessions_.find(pending.host_session_id) == host_sessions_.end()) {
        return false;
    }

    std::vector<int> rebound_pids;
    CollectHostBoundPidsLocked(pending.host_session_id, pid, &rebound_pids);
    pid_to_host_session_[pid] = pending.host_session_id;

    for (int old_pid : rebound_pids) {
        ClearOwnedHostPidStateLocked(old_pid, pending.host_session_id, false);
    }

    auto suspended_it = spawn_suspended_entries_.find(pid);
    if (suspended_it != spawn_suspended_entries_.end()) {
        suspended_it->second.pid = pid;
        suspended_it->second.host_session_id = pending.host_session_id;
        suspended_it->second.suspended = true;
        suspended_it->second.response_pending = true;
        const SpawnTransactionState resolved_state =
            SpawnStateForPendingReadyStage(pending.ready_stage);
        if (PendingSpawnReadyStageRank(pending.ready_stage) >=
            PendingSpawnReadyStageRank(suspended_it->second.authoritative_ready_stage)) {
            suspended_it->second.authoritative_ready_stage = pending.ready_stage;
            if (!pending.resolved_process_name.empty()) {
                suspended_it->second.authoritative_process_name = pending.resolved_process_name;
            }
        }
        if (CanTransitionSpawnState(suspended_it->second.state, resolved_state)) {
            suspended_it->second.state = resolved_state;
        }
        if (!pending.process_name.empty()) {
            suspended_it->second.target_process_name = pending.process_name;
        }
    } else {
        spawn_suspended_entries_[pid] = SpawnSuspendedEntry{
            pid,
            pending.host_session_id,
            pending.spawn_token,
            true,
            true,
            SpawnStateForPendingReadyStage(pending.ready_stage),
            pending.ready_stage,
            pending.resolved_process_name,
            pending.process_name,
        };
    }

    if (out != nullptr) {
        *out = pending;
    }
    return true;
}

bool SessionRegistry::ResolvePendingSpawn(const std::string& spawn_token,
                                          int pid,
                                          const std::string& process_name,
                                          comm::AgentReadyStage ready_stage) {
    if (spawn_token.empty() || pid <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_spawns_.find(spawn_token);
    if (it == pending_spawns_.end()) {
        return false;
    }

    if (it->second.host_session_id != 0) {
        auto bound_pid_it = std::find_if(pid_to_host_session_.begin(),
                                         pid_to_host_session_.end(),
                                         [&](const auto& entry) {
                                             return entry.second == it->second.host_session_id;
                                         });
        if (bound_pid_it != pid_to_host_session_.end() &&
            bound_pid_it->first > 0 &&
            bound_pid_it->first != pid) {
            const auto suspended_it = spawn_suspended_entries_.find(bound_pid_it->first);
            const bool rebound_from_owned_suspended_spawn =
                suspended_it != spawn_suspended_entries_.end() &&
                suspended_it->second.suspended &&
                suspended_it->second.host_session_id == it->second.host_session_id;
            if (!rebound_from_owned_suspended_spawn) {
                return false;
            }
        }
    }

    const PendingSpawnReadyStage pending_ready_stage = ToPendingSpawnReadyStage(ready_stage);
    if (it->second.ready && it->second.pid > 0 && it->second.pid != pid) {
        return false;
    }
    if (pending_ready_stage == PendingSpawnReadyStage::kRuntimeReady &&
        !process_name.empty() &&
        !it->second.process_name.empty() &&
        it->second.process_name != process_name) {
        return false;
    }

    it->second.pid = pid;
    it->second.ready = true;
    if (PendingSpawnReadyStageRank(pending_ready_stage) >=
        PendingSpawnReadyStageRank(it->second.ready_stage)) {
        it->second.ready_stage = pending_ready_stage;
        if (!process_name.empty()) {
            it->second.resolved_process_name = process_name;
        }
    }
    pending_spawn_cv_.notify_all();
    return true;
}

bool SessionRegistry::GetPendingSpawn(const std::string& spawn_token, PendingSpawnEntry* out) const {
    if (spawn_token.empty() || out == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_spawns_.find(spawn_token);
    if (it == pending_spawns_.end()) {
        return false;
    }

    *out = it->second;
    return true;
}

bool SessionRegistry::WaitForPendingSpawn(const std::string& spawn_token, uint32_t timeout_ms, int* pid) {
    if (spawn_token.empty()) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    auto is_ready = [&]() {
        if (shutdown_) {
            return true;
        }
        auto it = pending_spawns_.find(spawn_token);
        return it != pending_spawns_.end() && it->second.ready && it->second.pid > 0;
    };

    if (!pending_spawn_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), is_ready)) {
        return false;
    }

    if (shutdown_) {
        return false;
    }
    auto it = pending_spawns_.find(spawn_token);
    if (it == pending_spawns_.end() || !it->second.ready || it->second.pid <= 0) {
        return false;
    }

    if (pid != nullptr) {
        *pid = it->second.pid;
    }
    return true;
}

void SessionRegistry::ClearPendingSpawn(const std::string& spawn_token) {
    if (spawn_token.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto pending_it = pending_spawns_.find(spawn_token);
    if (pending_it == pending_spawns_.end()) {
        pending_spawn_cv_.notify_all();
        return;
    }

    const PendingSpawnEntry pending = pending_it->second;
    pending_spawns_.erase(pending_it);
    if (pending.pid > 0) {
        const auto suspended_it = spawn_suspended_entries_.find(pending.pid);
        const bool has_bound_spawn_context =
            suspended_it != spawn_suspended_entries_.end() &&
            suspended_it->second.suspended &&
            suspended_it->second.host_session_id == pending.host_session_id;
        const auto bound_host_it = pid_to_host_session_.find(pending.pid);
        const bool has_bound_host =
            bound_host_it != pid_to_host_session_.end() &&
            bound_host_it->second == pending.host_session_id;

        if (!has_bound_spawn_context && !has_bound_host) {
            ClearResolvedPendingSpawnPidStateLocked(pending.pid);
        }
    }
    pending_spawn_cv_.notify_all();
}

}  // namespace server
}  // namespace nook
