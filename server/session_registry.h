#pragma once

#include "../src/communication/protocol/frame.h"
#include "../src/communication/protocol/messages.h"

#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nook {
namespace comm {
class Session;
}
namespace server {

enum class SpawnTransactionState : uint8_t {
    kWaitingAgentReady = 0,
    kWaitingRuntimeReady = 1,
    kReadyForScriptLoad = 2,
    kScriptLoadDispatched = 3,
};

enum class PendingSpawnReadyStage : uint8_t {
    kNone = 0,
    kControlReady = 1,
    kRuntimeReady = 2,
};

struct SpawnSuspendedEntry {
    int pid = -1;
    uint32_t host_session_id = 0;
    std::string spawn_token;
    bool suspended = false;
    bool response_pending = false;
    SpawnTransactionState state = SpawnTransactionState::kWaitingAgentReady;
    PendingSpawnReadyStage authoritative_ready_stage = PendingSpawnReadyStage::kNone;
    std::string authoritative_process_name;
    std::string target_process_name;
};

inline std::string ResolveSpawnRuntimeProcessName(const SpawnSuspendedEntry& entry) {
    if (entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady &&
        !entry.authoritative_process_name.empty()) {
        return entry.authoritative_process_name;
    }
    if (!entry.target_process_name.empty()) {
        return entry.target_process_name;
    }
    return entry.authoritative_process_name;
}

struct PendingSpawnEntry {
    std::string spawn_token;
    std::string process_name;
    uint32_t host_session_id = 0;
    int pid = -1;
    bool ready = false;
    PendingSpawnReadyStage ready_stage = PendingSpawnReadyStage::kNone;
    std::string resolved_process_name;
};

struct OwnedZygoteControlTarget {
    int pid = -1;
    std::string process_name;
};

struct PendingAttachEntry {
    std::string ready_token;
    std::string process_name;
    uint32_t host_session_id = 0;
    int pid = -1;
};

struct AgentReadySpawnContext {
    std::string expected_spawn_process_name;
    bool has_pending_spawn_context = false;
    bool has_pending_attach_context = false;
    bool pending_attach_matches = false;
    bool has_spawn_suspended_context = false;
    bool spawn_token_mismatches_existing_spawn_context = false;
    bool has_suspended_entry = false;
    PendingAttachEntry pending_attach;
    SpawnSuspendedEntry suspended_entry;
};

struct AgentReadyDropContext {
    bool has_pending_spawn_context = false;
    bool has_pending_attach_context = false;
    bool pending_attach_matches = false;
    bool has_spawn_suspended_context = false;
    bool has_suspended_entry = false;
    bool spawn_token_mismatches_existing_spawn_context = false;
    bool runtime_ready = false;
    SpawnSuspendedEntry suspended_entry;
    bool owned_zygote_control_target = false;
    bool has_bound_host = false;
};

enum class AgentReadyEarlyDropReason : uint8_t {
    kNone = 0,
    kOrphanSpawnToken,
    kMismatchedPendingAttach,
    kStaleAttachWhileNewAttachPending,
    kForeignAttachLike,
    kMismatchedSpawnToken,
    kOrphanAttach,
    kInvalidatedUnowned,
    kMismatchedControlStageKnownSpawnIdentity,
};

struct AgentReadyEarlyDropDecision {
    AgentReadyEarlyDropReason reason = AgentReadyEarlyDropReason::kNone;
    bool dropped_session = false;
};

enum class AgentReadyPreRegistrationAction : uint8_t {
    kNone = 0,
    kDropLateControlAtRuntimeBoundary,
    kDropLateControlFromNonCurrentSession,
    kResetMismatchedRuntimeTrace,
};

struct AgentReadyPreRegistrationDecision {
    AgentReadyPreRegistrationAction action = AgentReadyPreRegistrationAction::kNone;
    bool dropped_session = false;
    bool reset_runtime_trace = false;
};

enum class AgentReadyForwardAction : uint8_t {
    kNoBoundHost = 0,
    kExposeRuntimeWithoutHost,
    kForwardRuntimeToHost,
    kHoldRuntimeUntilSpawnResponse,
    kDropMismatchedRuntimeForHost,
    kHoldControlForHost,
};

struct AgentReadyForwardDecision {
    AgentReadyForwardAction action = AgentReadyForwardAction::kNoBoundHost;
    bool can_expose_runtime_ready_immediately = false;
};

struct DerivedAgentReadyContext {
    AgentReadySpawnContext spawn_context;
    std::string expected_spawn_process_name;
    comm::Session* host = nullptr;
    bool runtime_ready = false;
    bool owned_zygote_control_target = false;
    bool runtime_spawn_process_name_matches = false;
    bool mismatched_runtime_trace_for_spawn_target = false;
    bool runtime_already_recorded = false;
    bool current_agent_session_matches = false;
    bool stale_control_after_runtime = false;
};

struct PostFinalizeSpawnContext {
    PendingSpawnReadyStage ready_stage = PendingSpawnReadyStage::kNone;
    std::string runtime_process_name;
};

enum class LatePromotionEligibility : uint8_t {
    kEligible = 0,
    kInvalidArgs,
    kEmptyAgentPath,
    kSpawnEntryMissing,
    kNotControlReady,
    kSpawnEntryMismatch,
    kOwnedHostMissing,
};

struct LatePromotionEvaluation {
    LatePromotionEligibility eligibility = LatePromotionEligibility::kInvalidArgs;
    SpawnSuspendedEntry entry;
    bool has_entry = false;
};

struct BindSuspendedSpawnAfterFinalizeResult {
    bool bound = false;
    bool waiting_runtime_ready = false;
};

enum class ResolvedPendingSpawnHandoffDisposition : uint8_t {
    kNone = 0,
    kBoundToRegisteredHost,
    kConsumedWithoutRegisteredHost,
    kMissingForRegisteredHost,
};

struct ResolvedPendingSpawnHandoffResult {
    ResolvedPendingSpawnHandoffDisposition disposition =
        ResolvedPendingSpawnHandoffDisposition::kNone;
    PendingSpawnEntry pending_spawn;
};

struct SpawnResponseCommitResult {
    bool released_boundary = false;
    bool should_replay_runtime_ready = false;
    PostFinalizeSpawnContext context;
};

struct RuntimeReadyCommitResult {
    bool runtime_visible = false;
    bool should_replay_script_messages = false;
};

enum class HostBoundAgentLookupError : uint8_t {
    kNone = 0,
    kHostNotBound,
    kSpawnNotReady,
    kAgentNotReady,
};

struct HostBoundAgentRequestTarget {
    int pid = -1;
    comm::Session* agent = nullptr;
    HostBoundAgentLookupError error = HostBoundAgentLookupError::kNone;
};

enum class LatePromotionRecheckResult : uint8_t {
    kProceed = 0,
    kTransactionMissingBeforeInject,
    kRuntimeReadyBeforeInject,
    kTransactionChanged,
};

struct AgentReadyRegistrationPlan {
    bool register_runtime_globally = false;
    bool remove_runtime_session_for_mismatch = false;
    bool register_control_globally = false;
    bool register_control_identity = false;
    bool clear_pending_attach = false;
    bool upgrade_spawn_authoritative_ready = false;
    PendingSpawnReadyStage spawn_ready_stage = PendingSpawnReadyStage::kNone;
    bool resolve_pending_spawn = false;
    bool eligible_runtime_ready = false;
};

struct AgentReadyRegistrationResult {
    bool removed_previous_pid_session = false;
    bool removed_runtime_session_for_mismatch = false;
    bool cleared_pending_attach = false;
    bool upgraded_spawn_authoritative_ready = false;
    bool resolved_pending_spawn = false;
    bool bound_host_to_resolved_pending_spawn = false;
};

class SessionRegistry {
public:
    void Shutdown();

    void RegisterHostSession(comm::Session* session);
    void RemoveHostSession(uint32_t session_id);
    comm::Session* FindHostSession(uint32_t session_id) const;
    int FindPidByHostSession(uint32_t session_id) const;
    int FindOwnedSpawnPidByHostSession(uint32_t session_id) const;
    bool IsRegisteredHostSession(uint32_t session_id, const comm::Session* session) const;

    void BindHostToPid(uint32_t session_id, int pid);
    void UnbindHostSession(uint32_t session_id);
    comm::Session* FindHostSessionByPid(int pid) const;
    void MarkAttachTimeoutPid(int pid);
    void ClearAttachTimeoutPid(int pid);
    bool WasAttachTimeoutPid(int pid) const;
    bool IsInvalidatedAgentPid(int pid) const;
    void RegisterPendingAttach(const std::string& ready_token,
                               int pid,
                               const std::string& process_name,
                               uint32_t host_session_id);
    bool GetPendingAttach(const std::string& ready_token, PendingAttachEntry* out) const;
    bool HasPendingAttachForPid(int pid, const std::string& process_name) const;
    void ClearPendingAttach(const std::string& ready_token);
    bool ResolveAgentReadySpawnContext(int pid,
                                       const std::string& spawn_token,
                                       const std::string& process_name,
                                       AgentReadySpawnContext* out) const;
    bool ShouldDropStaleAttachAgentReady(int pid,
                                         const std::string& process_name,
                                         const AgentReadyDropContext& context) const;
    bool ShouldDropForeignAttachLikeAgentReady(const std::string& spawn_token,
                                               const AgentReadyDropContext& context) const;
    bool ShouldDropMismatchedSpawnTokenAgentReady(const AgentReadyDropContext& context) const;
    bool ShouldDropOrphanAttachAgentReady(const std::string& spawn_token,
                                          int pid,
                                          const AgentReadyDropContext& context) const;
    bool ShouldDropInvalidatedUnownedAgentReady(const std::string& spawn_token,
                                                int pid,
                                                const AgentReadyDropContext& context) const;
    AgentReadyEarlyDropDecision EvaluateAgentReadyEarlyDrop(int pid,
                                                            const std::string& spawn_token,
                                                            const std::string& process_name,
                                                            comm::Session* session,
                                                            const AgentReadyDropContext& context);
    AgentReadyPreRegistrationDecision EvaluateAgentReadyPreRegistration(
        int pid,
        const std::string& process_name,
        bool runtime_ready,
        bool stale_control_after_runtime,
        bool mismatched_runtime_trace_for_spawn_target,
        comm::Session* session,
        const SpawnSuspendedEntry& suspended_entry);
    AgentReadyForwardDecision EvaluateAgentReadyForwarding(int pid,
                                                          bool runtime_ready,
                                                          bool eligible_runtime_ready,
                                                          bool has_bound_host) const;
    DerivedAgentReadyContext DeriveAgentReadyContext(int pid,
                                                     const std::string& spawn_token,
                                                     const std::string& process_name,
                                                     comm::AgentReadyStage stage,
                                                     const comm::Session* session) const;
    PostFinalizeSpawnContext ResolvePostFinalizeSpawnContext(
        int pid,
        PendingSpawnReadyStage fallback_stage,
        const PendingSpawnEntry& fallback_pending_spawn) const;
    LatePromotionEvaluation EvaluateLatePromotionEligibility(
        int pid,
        uint32_t host_session_id,
        const std::string& agent_path) const;
    LatePromotionRecheckResult RecheckLatePromotionBeforeInject(
        int pid,
        uint32_t host_session_id,
        SpawnSuspendedEntry* out_entry = nullptr) const;
    void CleanupTimedOutSpawnTransaction(const std::string& spawn_token, int authoritative_pid);
    void CleanupFailedBoundSpawnTransaction(uint32_t host_session_id,
                                           int authoritative_pid,
                                           const std::string& spawn_token,
                                           bool unbind_host_session);
    void ConsumeOrClearPendingSpawn(const std::string& spawn_token,
                                    PendingSpawnEntry* out = nullptr);
    void CleanupDroppedSuccessfulSpawnResponse(int authoritative_pid);
    BindSuspendedSpawnAfterFinalizeResult BindSuspendedSpawnAfterFinalize(
        int pid,
        uint32_t host_session_id,
        const PostFinalizeSpawnContext& post_finalize_context,
        const PendingSpawnEntry& resolved_pending_spawn,
        const std::string& spawn_token);
    bool ReleaseSpawnResponseBoundaryAndResolvePostFinalize(
        int pid,
        PendingSpawnReadyStage fallback_stage,
        const PendingSpawnEntry& resolved_pending_spawn,
        PostFinalizeSpawnContext* out_context);
    ResolvedPendingSpawnHandoffResult PrepareResolvedPendingSpawnHandoff(
        const std::string& spawn_token,
        int pid,
        uint32_t host_session_id,
        const comm::Session* host_session);
    SpawnResponseCommitResult CommitSpawnResponseSuccess(
        int pid,
        PendingSpawnReadyStage fallback_stage,
        const PendingSpawnEntry& resolved_pending_spawn);
    RuntimeReadyCommitResult CommitRuntimeReadyVisibility(int pid);
    HostBoundAgentRequestTarget ResolveHostBoundAgentRequestTarget(
        uint32_t host_session_id) const;

    void RegisterAgentSession(int pid, comm::Session* session);
    void RegisterControlReadyAgentSession(int pid, comm::Session* session);
    void RegisterAgentProcessName(int pid, const std::string& process_name);
    void MarkAgentAuthoritativeReady(int pid);
    bool IsAgentAuthoritativeReady(int pid) const;
    void MarkAgentReadyStage(int pid, comm::AgentReadyStage stage);
    bool IsAgentControlReady(int pid) const;
    bool GetAgentReadyStage(int pid, comm::AgentReadyStage* stage) const;
    void MarkAgentRuntimeReady(int pid);
    void ClearAgentRuntimeReadyState(int pid);
    void ForceAgentReadyStage(int pid, comm::AgentReadyStage stage);
    bool ResetMismatchedRuntimeTraceForSpawn(int pid);
    bool IsAgentRuntimeReady(int pid) const;
    void RemoveAgentSessionByPid(int pid);
    bool RemoveAgentSessionByPidIfMatches(int pid, comm::Session* session);
    bool DropAgentReadySessionIfMatches(int pid, comm::Session* session);
    comm::Session* FindAgentSessionByPid(int pid) const;
    bool IsAcceptedCurrentAgentSessionForPid(int pid,
                                             const comm::Session* session) const;
    comm::Session* FindAgentSessionByProcessName(const std::string& process_name) const;
    comm::Session* FindAuthoritativeAgentSessionByPid(int pid) const;
    comm::Session* FindAuthoritativeAgentSessionByProcessName(const std::string& process_name) const;
    comm::Session* FindAuthoritativeAgentSessionByIdentity(int pid,
                                                           const std::string& process_name) const;
    comm::Session* FindRuntimeReadyAgentSessionByIdentity(int pid,
                                                          const std::string& process_name) const;
    comm::Session* FindRuntimeReadyAgentSessionForSpawn(int pid,
                                                        const SpawnSuspendedEntry& entry) const;
    bool IsAcceptedControlStageAgentSessionForSpawn(int pid,
                                                    const SpawnSuspendedEntry& entry,
                                                    const std::string& process_name,
                                                    const comm::Session* session) const;
    comm::Session* FindControlReadyAgentSessionForSpawn(int pid,
                                                        const SpawnSuspendedEntry& entry,
                                                        const std::string& process_name) const;
    bool IsAcceptedHostResponseAgentSessionForSpawn(int pid,
                                                    const SpawnSuspendedEntry& entry,
                                                    const comm::Session* session) const;
    bool IsAcceptedScriptMessageAgentSessionForSpawn(int pid,
                                                     const SpawnSuspendedEntry& entry,
                                                     const comm::Session* session) const;
    comm::Session* FindHostRequestAgentSessionForSpawn(int pid,
                                                       const SpawnSuspendedEntry& entry) const;
    bool HasAuthoritativeRuntimeBoundaryForSpawn(int pid,
                                                 const SpawnSuspendedEntry& entry,
                                                 const std::string& process_name) const;
    bool HasMismatchedRuntimeTraceForSpawn(int pid,
                                           const SpawnSuspendedEntry& entry,
                                           const std::string& expected_process_name,
                                           const std::string& candidate_process_name) const;
    bool HasAcceptedRuntimeTraceForSpawn(int pid,
                                         const std::string& expected_process_name) const;
    bool HasKnownSpawnControlIdentityMismatchForSpawn(int pid,
                                                      const SpawnSuspendedEntry& entry,
                                                      const std::string& process_name) const;
    bool DoesRuntimeSpawnProcessNameMatch(bool runtime_ready,
                                          const std::string& expected_process_name,
                                          const std::string& ready_process_name) const;
    bool HasRuntimeRecordedForSpawn(int pid,
                                    const SpawnSuspendedEntry& entry,
                                    const std::string& expected_process_name,
                                    const std::string& ready_process_name,
                                    bool runtime_ready,
                                    bool has_spawn_suspended_context) const;
    bool ShouldDropLateControlAgentReadyAtRuntimeBoundary(int pid,
                                                          const SpawnSuspendedEntry& entry,
                                                          const std::string& ready_process_name,
                                                          bool runtime_ready) const;
    bool ShouldDropLateControlAgentReadyFromNonCurrentSession(
        int pid,
        const SpawnSuspendedEntry& entry,
        const std::string& expected_process_name,
        const std::string& ready_process_name,
        bool runtime_ready,
        bool has_spawn_suspended_context,
        bool current_agent_session_matches) const;
    AgentReadyRegistrationPlan PlanAgentReadyRegistration(
        bool runtime_ready,
        bool runtime_spawn_process_name_matches,
        bool runtime_already_recorded,
        bool pending_attach_matches,
        bool has_spawn_token) const;
    AgentReadyRegistrationResult ApplyAgentReadyRegistrationPlan(
        int previous_pid,
        int pid,
        const std::string& ready_token,
        const std::string& process_name,
        comm::AgentReadyStage ready_stage,
        const comm::Frame& ready_frame,
        comm::Session* session,
        const AgentReadyRegistrationPlan& plan);
    comm::Session* FindControlReadyAgentSessionByPid(int pid) const;
    comm::Session* FindControlReadyAgentSessionByProcessName(const std::string& process_name) const;
    comm::Session* FindControlReadyAgentSessionByIdentity(int pid,
                                                          const std::string& process_name) const;
    void MarkOwnedZygoteControlProcess(int pid, const std::string& process_name);
    void ClearOwnedZygoteControlProcess(const std::string& process_name);
    bool IsOwnedZygoteControlProcess(const std::string& process_name) const;
    bool IsOwnedZygoteControlTarget(int pid, const std::string& process_name) const;
    bool GetOwnedZygoteControlTarget(const std::string& process_name,
                                     OwnedZygoteControlTarget* out) const;
    comm::Session* WaitForAgentSessionByIdentity(int pid,
                                                 const std::string& process_name,
                                                 uint32_t timeout_ms) const;
    comm::Session* WaitForAgentSessionByProcessName(const std::string& process_name,
                                                    uint32_t timeout_ms) const;
    comm::Session* WaitForAuthoritativeAgentSessionByIdentity(int pid,
                                                              const std::string& process_name,
                                                              uint32_t timeout_ms) const;
    comm::Session* WaitForRuntimeReadyAgentSessionByIdentity(int pid,
                                                             const std::string& process_name,
                                                             uint32_t timeout_ms) const;
    comm::Session* WaitForRuntimeReadyAgentSessionByToken(int pid,
                                                          const std::string& process_name,
                                                          const std::string& ready_token,
                                                          uint32_t timeout_ms) const;
    comm::Session* WaitForControlReadyAgentSessionByIdentity(int pid,
                                                             const std::string& process_name,
                                                             uint32_t timeout_ms) const;
    bool WaitForAgentSessionDisconnectByIdentity(int pid,
                                                 const std::string& process_name,
                                                 uint32_t timeout_ms) const;
    void StoreAgentReadyFrame(int pid, const comm::Frame& frame);
    bool GetAgentReadyFrame(int pid, comm::Frame* frame) const;
    bool GetAgentReadyFrameByIdentity(int pid,
                                      const std::string& process_name,
                                      comm::Frame* frame) const;
    bool WaitForAgentRuntimeReady(int pid, uint32_t timeout_ms) const;
    void StoreScriptMessageFrame(int pid, const comm::Frame& frame);
    std::vector<comm::Frame> GetScriptMessageFrames(int pid) const;
    std::vector<comm::Frame> TakeScriptMessageFrames(int pid);
    void ClearScriptMessageFrames(int pid);
    void DropScriptMessageFramePrefix(int pid, size_t count);
    bool ShouldCacheScriptMessageForPid(int pid) const;

    void MarkSpawnSuspended(int pid,
                            uint32_t host_session_id,
                            PendingSpawnReadyStage authoritative_ready_stage = PendingSpawnReadyStage::kNone,
                            const std::string& authoritative_process_name = {},
                            const std::string& target_process_name = {},
                            const std::string& spawn_token = {});
    bool IsSpawnSuspended(int pid) const;
    bool GetSpawnSuspendedEntry(int pid, SpawnSuspendedEntry* out) const;
    uint32_t FindSuspendedSpawnOwnerHostSessionId(int pid) const;
    bool IsForeignSuspendedSpawnOwner(int pid, uint32_t host_session_id) const;
    bool UpdateSpawnSuspendedAuthoritativeReady(int pid,
                                                PendingSpawnReadyStage authoritative_ready_stage,
                                                const std::string& authoritative_process_name);
    bool SetSpawnResponsePending(int pid, bool pending);
    bool ReleaseSpawnResponseBoundary(int pid, SpawnSuspendedEntry* out = nullptr);
    bool CanExposeSpawnRuntimeReadyImmediately(int pid) const;
    bool IsSpawnBlockedForScriptOperations(int pid) const;
    bool MarkSpawnRuntimeReadyVisible(int pid);
    bool MarkSpawnScriptLoadInFlight(int pid);
    bool MarkSpawnScriptLoadComplete(int pid);
    bool UpdateSpawnState(int pid, SpawnTransactionState state);
    void ClearSpawnSuspended(int pid);
    void ClearSpawnTransactionByPid(int pid);

    void RegisterPendingSpawn(const std::string& spawn_token,
                              const std::string& process_name,
                              uint32_t host_session_id);
    bool TakePendingSpawn(const std::string& spawn_token, PendingSpawnEntry* out);
    bool BindHostToResolvedPendingSpawn(const std::string& spawn_token,
                                        int pid,
                                        PendingSpawnEntry* out = nullptr);
    bool ResolvePendingSpawn(const std::string& spawn_token,
                             int pid,
                             const std::string& process_name,
                             comm::AgentReadyStage ready_stage);
    bool GetPendingSpawn(const std::string& spawn_token, PendingSpawnEntry* out) const;
    bool WaitForPendingSpawn(const std::string& spawn_token, uint32_t timeout_ms, int* pid);
    void ClearPendingSpawn(const std::string& spawn_token);

private:
    void ClearAgentStateLocked(int pid);
    void ClearAttachSideStateForPidLocked(int pid);
    void ClearAgentProcessNameBindingLocked(int pid);
    void CollectHostBoundPidsLocked(uint32_t session_id,
                                    int preserved_pid,
                                    std::vector<int>* out);
    void CollectResolvedPendingSpawnPidsForHostLocked(uint32_t session_id,
                                                      std::vector<int>* out);
    void ClearPendingAttachesForHostLocked(uint32_t session_id);
    void ClearOwnedHostPidStateLocked(int pid,
                                      uint32_t session_id,
                                      bool clear_attach_timeout);
    void ClearResolvedPendingSpawnPidStateLocked(int pid);
    void ClearAgentSessionStateLocked(int pid,
                                      bool clear_attach_side_state,
                                      bool clear_script_and_spawn_state);

    mutable std::mutex mutex_;
    mutable std::condition_variable pending_spawn_cv_;
    mutable std::condition_variable agent_ready_cv_;
    bool shutdown_ = false;
    std::unordered_map<uint32_t, comm::Session*> host_sessions_;
    std::unordered_map<int, uint32_t> pid_to_host_session_;
    std::unordered_map<int, bool> attach_timeout_pids_;
    std::unordered_map<int, bool> invalidated_agent_pids_;
    std::unordered_map<int, comm::Session*> agent_sessions_;
    std::unordered_map<int, comm::Session*> agent_control_sessions_;
    std::unordered_map<int, std::string> agent_control_process_names_;
    std::unordered_map<int, std::vector<comm::Session*>> agent_session_history_;
    std::unordered_map<std::string, int> agent_process_name_to_pid_;
    std::unordered_map<int, std::string> agent_pid_to_process_name_;
    std::unordered_map<int, bool> agent_authoritative_ready_;
    std::unordered_map<int, comm::AgentReadyStage> agent_ready_stages_;
    std::unordered_map<int, bool> agent_runtime_ready_;
    std::unordered_map<int, comm::Frame> agent_ready_frames_;
    std::unordered_map<int, std::vector<comm::Frame>> script_message_frames_;
    std::unordered_map<int, SpawnSuspendedEntry> spawn_suspended_entries_;
    std::unordered_map<std::string, PendingAttachEntry> pending_attaches_;
    std::unordered_map<std::string, PendingSpawnEntry> pending_spawns_;
    std::unordered_map<std::string, OwnedZygoteControlTarget> owned_zygote_control_processes_;
};

}  // namespace server
}  // namespace nook
