#pragma once

#include "injector.h"
#include "generated/nook_embedded_agent_blob.h"
#include "generated/nook_embedded_ncore_blob.h"
#include "generated/nook_embedded_zygote_helper_blob.h"
#include "process_manager.h"
#include "server_runtime.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace nook {
namespace server {

struct NinjectorSpawnConfig {
    std::string ncore_path;
    std::string runtime_dir;
    std::string spawn_source_process;
    bool enable_zygote_control = false;
};

struct NinjectorSpawnOps {
    std::function<int(const char* process_name)> get_pid;
    std::function<bool(int zygote_pid)> is_zygote_monitor_ready;
    std::function<bool(int zygote_pid,
                       const char* package_name,
                       const char* so_path,
                       const char* runtime_dir,
                       const char* spawn_token,
                       int* child_pid)> spawn_symbi;
    std::function<bool(int zygote_pid,
                       const char* package_name,
                       const char* runtime_dir,
                       const char* spawn_token,
                       int* child_pid)> spawn_symbi_embedded;
    std::function<bool(int zygote_pid,
                       const char* ncore_path,
                       const char* package_name,
                       const char* so_path,
                       const char* runtime_dir,
                       const char* spawn_token)> prepare_spawn;
    std::function<bool(int zygote_pid,
                       const char* ncore_path,
                       const char* runtime_dir,
                       const char* spawn_token)> clear_spawn;
    std::function<bool(int pid, const char* runtime_dir, const char* ready_token)> inject_embedded_agent_by_pid;
    std::function<bool(int pid, const char* so_path, const char* ready_token)> inject_so_by_pid;
    std::function<bool(int pid, const char* runtime_dir_or_so_path)> inject_zygote_so_by_pid;
    std::function<bool(int pid)> uninstall_embedded_zygote_control_hooks;
    std::function<std::string()> get_inject_error;
    std::function<bool(const char* package_name)> start_target_app;
    std::function<bool(int zygote_pid,
                       const char* package_name,
                       const char* spawn_token,
                       bool strict_request)> set_zygote_spawn_control;
    std::function<bool(int zygote_pid,
                       const char* spawn_token,
                       bool strict_request)> clear_zygote_spawn_control;
};

std::string FormatZygoteControlSpawnDecisionLog(const char* event_name,
                                                const std::string& package_name,
                                                bool strict_mode,
                                                const char* fallback_backend,
                                                const std::string& detail);
std::string FormatZygoteControlFinalizeDecisionLog(const char* event_name,
                                                   const std::string& package_name,
                                                   const char* backend_name,
                                                   const std::string& detail);
std::string FormatZygoteControlTerminalOutcomeLog(const char* stage_name,
                                                  const char* event_name,
                                                  const std::string& package_name,
                                                  const char* primary_backend,
                                                  const char* secondary_backend,
                                                  const char* state_name,
                                                  const std::string& detail);
std::string FormatZygoteControlLifecycleStageLog(const char* stage_name,
                                                 const char* event_name,
                                                 const std::string& package_name,
                                                 const char* state_name,
                                                 const std::string& detail);

enum class ZygoteControlFailureState {
    kUnknown = 0,
    kInjectAgent,
    kReadyWait,
    kArmControl,
    kInstallHook,
    kTargetsArmed,
    kLaunchApp,
    kFinalizeClear,
};

using ZygoteControlInstallHookFn = std::function<bool(int zygote_pid,
                                                      const std::string& process_name,
                                                      const std::string& agent_path,
                                                      const std::string& target_package,
                                                      const std::string& spawn_token,
                                                      std::string* error_message)>;
using ZygoteControlUninstallHookFn = std::function<bool(int zygote_pid,
                                                        const std::string& process_name,
                                                        std::string* error_message)>;
using HasPreexistingZygoteSessionFn = std::function<bool(int zygote_pid,
                                                         const std::string& process_name)>;
using EnumerateZygoteProcessesFn = std::function<std::vector<ProcessInfo>()>;

class NinjectorSpawnInjector final : public Injector {
public:
    explicit NinjectorSpawnInjector(NinjectorSpawnConfig config = DefaultConfig(),
                                    NinjectorSpawnOps ops = MakeDefaultOps(),
                                    ZygoteControlInstallHookFn install_hook = {},
                                    ZygoteControlUninstallHookFn uninstall_hook = {},
                                    HasPreexistingZygoteSessionFn has_preexisting_session = {},
                                    EnumerateZygoteProcessesFn enumerate_zygote_processes = {});

    bool Spawn(const comm::SpawnRequest& request,
               const std::string& agent_path,
               int* pid,
               std::string* error_message) override;
    bool FinalizeSpawn(const comm::SpawnRequest& request,
                       std::string* error_message) override;
    bool InjectAgent(int pid,
                     const std::string& agent_path,
                     const std::string& ready_token,
                     std::string* error_message) override;
    bool InjectSpawnChildAgent(int pid,
                               const std::string& agent_path,
                               std::string* error_message) override;

    static NinjectorSpawnConfig DefaultConfig();
    static NinjectorSpawnOps MakeDefaultOps();

private:
    enum class SpawnBackend {
        kNone = 0,
        // Symbi is the zygote gate path. It returns the real child pid on success
        // and owns only the spawn-time agent handoff, not prepare/clear lifecycle.
        kSymbi,
        // Legacy ncore is the stable prepare/clear path. It arms zygote state
        // before start and clears it during FinalizeSpawn().
        kLegacyNcore,
        // Zygote-control is the experimental agent-controlled spawn path. It is
        // intentionally separate from legacy prepare/clear semantics.
        kZygoteControl,
    };
    struct PreparedRuntimeArtifact {
        std::string resolved_path;
        bool materialized_embedded = false;
    };

    struct ZygoteControlOwnedTransaction {
        std::string identifier;
        std::string spawn_token;
        bool helper_only_local_control = false;
        ZygoteControlFailureState failure_state = ZygoteControlFailureState::kUnknown;
        ZygoteControlFailureState lifecycle_state = ZygoteControlFailureState::kUnknown;
        std::vector<std::pair<int, std::string>> targets;
    };
    struct ZygoteControlAttemptResult {
        bool success = false;
        int pid = 0;
        ZygoteControlOwnedTransaction owned_transaction;
        std::string error_message;
    };

    struct SpawnOwnedState {
        std::string identifier;
        std::string spawn_token;
        std::string ncore_path;
        std::string agent_path;
        bool materialized_ncore = false;
        bool materialized_agent = false;
        SpawnBackend backend = SpawnBackend::kNone;
    };
    enum class SpawnTerminalBackend {
        kNone = 0,
        kUnknown,
        kZygoteControl,
        kSymbi,
        kLegacy,
        kExplicitSymbi,
    };
    enum class SpawnRouteAttempt {
        kNone = 0,
        kZygoteControl,
        kSymbi,
        kLegacy,
        kExplicitSymbi,
    };
    enum class SpawnFallbackPolicy {
        kUnknown = 0,
        kNotNeeded,
        kAllowed,
        kForbidden,
    };
    enum class SpawnFinalStatus {
        kUnknown = 0,
        kSuccess,
        kAbort,
        kFallbackFailed,
        kBackendUnavailable,
        kHardStop,
    };
    enum class SpawnOwnershipState {
        kNone = 0,
        kZygoteControlOwned,
        kSymbiOwned,
        kLegacyOwned,
    };
    enum class SpawnPrimaryRoute {
        kLegacyDefault = 0,
        kSymbiDefault,
        kExplicitSymbi,
        kStrictZygoteControl,
    };
    struct ActiveSpawnOwner {
        // `spawn_state` carries request-correlated compatibility state for all backends.
        // It becomes the authoritative owner record for child-owned backends like symbi.
        SpawnOwnedState spawn_state;
        // `shell_owner_state` remains authoritative only for legacy shell-owned routes.
        SpawnOwnedState shell_owner_state;
        // Zygote-control ownership is carried by the transaction record instead of a shell owner.
        ZygoteControlOwnedTransaction zygote_control_transaction;
    };
    struct FinalizeSession {
        SpawnOwnershipState finalize_owner = SpawnOwnershipState::kNone;
        SpawnOwnedState owned_spawn_state;
        ZygoteControlOwnedTransaction owned_zygote_transaction;
        bool has_foreign_active_owner = false;
    };
    using PendingSpawnCommit = ActiveSpawnOwner;
    struct SpawnOutcome {
        SpawnTerminalBackend terminal_primary_backend = SpawnTerminalBackend::kNone;
        SpawnTerminalBackend terminal_secondary_backend = SpawnTerminalBackend::kNone;
        SpawnFallbackPolicy fallback_policy = SpawnFallbackPolicy::kUnknown;
        SpawnFinalStatus final_status = SpawnFinalStatus::kUnknown;
        ZygoteControlFailureState zygote_control_state = ZygoteControlFailureState::kUnknown;
        ZygoteControlOwnedTransaction failed_zygote_control_transaction;
        PendingSpawnCommit pending_commit;
        std::string zygote_control_error;
        std::string symbi_error;
        std::string legacy_error;
    };
    struct SpawnExecutionPolicy {
        SpawnPrimaryRoute primary_route = SpawnPrimaryRoute::kLegacyDefault;
        bool explicit_symbi_requested = false;
        bool should_try_symbi_first = false;
        bool strict_zygote_control = false;
        bool allow_symbi_backend = false;
        bool allow_legacy_backend_fallback = false;
    };
    enum class SpawnExecutionPhase {
        kInit = 0,
        kRouting,
        kRouteCommitted,
        kRouteDeferred,
        kTerminal,
        kTerminalResolved,
        kTerminalFinalized,
        kCompleted,
    };
    enum class SpawnExecutionReason {
        kNone = 0,
        kInitialized,
        kBeginRouting,
        kRouteCommittedFromZygoteControl,
        kRouteCommittedFromSymbi,
        kRouteCommittedFromLegacy,
        kRouteDeferredForTerminalClassification,
        kBeginTerminalClassification,
        kTerminalOutcomeResolved,
        kTerminalOutcomeFinalized,
        kCompletedAfterCommittedRoute,
        kCompletedAfterTerminalOutcome,
    };
    enum class SpawnRoutingState {
        kNotStarted = 0,
        kRunning,
        kCommittedFromZygoteControl,
        kCommittedFromSymbi,
        kCommittedFromLegacy,
        kDeferredToTerminal,
    };
    enum class SpawnRouteWindowState {
        kNotConsidered = 0,
        kSkippedByPolicy,
        kEntered,
        kProbeOnly,
    };
    enum class SpawnRoutingProgress {
        kNotStarted = 0,
        kEnteredRouting,
        kAfterZygoteControl,
        kAfterSymbi,
        kAfterLegacy,
    };
    enum class SpawnRouteStep {
        kNone = 0,
        kZygoteControl,
        kSymbi,
        kLegacy,
    };
    enum class SpawnZygoteControlRouteState {
        kNotStarted = 0,
        kSkipped,
        kEntered,
        kCommitted,
        kDeferredToFallback,
        kAborted,
    };
    struct SpawnRouteWindows {
        SpawnRouteWindowState zygote_control = SpawnRouteWindowState::kNotConsidered;
        SpawnRouteWindowState symbi = SpawnRouteWindowState::kNotConsidered;
        SpawnRouteWindowState legacy = SpawnRouteWindowState::kNotConsidered;
    };
    struct SpawnExecutionState {
        SpawnExecutionPhase phase = SpawnExecutionPhase::kInit;
        SpawnExecutionReason phase_reason = SpawnExecutionReason::kNone;
        SpawnOwnershipState ownership_state = SpawnOwnershipState::kNone;
        SpawnRoutingState routing_state = SpawnRoutingState::kNotStarted;
        SpawnRoutingProgress routing_progress = SpawnRoutingProgress::kNotStarted;
        SpawnRouteStep current_route_step = SpawnRouteStep::kNone;
        SpawnZygoteControlRouteState zygote_control_route_state =
            SpawnZygoteControlRouteState::kNotStarted;
        SpawnRouteWindows routing_windows;
        SpawnExecutionPolicy policy;
        ZygoteControlAttemptResult zygote_attempt;
        SpawnOutcome outcome;
    };
    struct SpawnRoutingSnapshot {
        bool update_routing_state = false;
        SpawnRoutingState routing_state = SpawnRoutingState::kNotStarted;
        bool update_routing_progress = false;
        SpawnRoutingProgress routing_progress = SpawnRoutingProgress::kNotStarted;
        bool update_current_route_step = false;
        SpawnRouteStep current_route_step = SpawnRouteStep::kNone;
        bool update_zygote_control_route_state = false;
        SpawnZygoteControlRouteState zygote_control_route_state =
            SpawnZygoteControlRouteState::kNotStarted;
        bool update_zygote_control_window = false;
        SpawnRouteWindowState zygote_control_window = SpawnRouteWindowState::kNotConsidered;
        bool update_symbi_window = false;
        SpawnRouteWindowState symbi_window = SpawnRouteWindowState::kNotConsidered;
        bool update_legacy_window = false;
        SpawnRouteWindowState legacy_window = SpawnRouteWindowState::kNotConsidered;
    };
    static void SetError(std::string* error_message, const std::string& message);
    static bool HasZygoteControlTransactionRecord(
        const ZygoteControlOwnedTransaction& transaction);
    static bool HasAuthoritativeSpawnOwner(const SpawnOwnedState& spawn_state);
    static const SpawnOwnedState* ResolveAuthoritativeSpawnOwner(
        const ActiveSpawnOwner& active_spawn_owner);
    static const char* SpawnTerminalBackendToString(SpawnTerminalBackend backend);
    static std::string ExtractSpawnToken(const comm::SpawnRequest& request);
    static bool IsExplicitSymbiSpawnRequested(const comm::SpawnRequest& request);
    SpawnExecutionPolicy BuildSpawnExecutionPolicy(const comm::SpawnRequest& request) const;
    SpawnExecutionState BuildSpawnExecutionState(const comm::SpawnRequest& request,
                                                 const std::string& agent_path);
    bool ApplySpawnRoutingSnapshot(SpawnExecutionState* state,
                                   const SpawnRoutingSnapshot& snapshot,
                                   std::string* error_message) const;
    bool TransitionSpawnOwnershipState(SpawnExecutionState* state,
                                       SpawnOwnershipState next_state,
                                       std::string* error_message) const;
    SpawnOwnershipState ResolveOwnershipStateFromBackend(SpawnBackend backend) const;
    bool BeginSpawnRouting(SpawnExecutionState* state, std::string* error_message) const;
    bool EnterZygoteControlRoute(SpawnExecutionState* state, std::string* error_message) const;
    bool SkipZygoteControlRoute(SpawnExecutionState* state, std::string* error_message) const;
    bool AbortZygoteControlRoute(SpawnExecutionState* state, std::string* error_message) const;
    bool CommitZygoteControlRoute(SpawnExecutionState* state, std::string* error_message) const;
    bool CommitNonZygoteControlRoute(SpawnExecutionState* state,
                                     SpawnBackend backend,
                                     std::string* error_message) const;
    bool ApplyZygoteControlRouting(const comm::SpawnRequest& request,
                                   const std::string& agent_path,
                                   SpawnExecutionState* state,
                                   int* pid,
                                   std::string* error_message);
    bool DeferZygoteControlRouteToFallback(SpawnExecutionState* state,
                                           std::string* error_message) const;
    bool AdvancePastZygoteControlRoute(SpawnExecutionState* state,
                                       std::string* error_message) const;
    bool TransitionSpawnExecutionPhase(SpawnExecutionState* state,
                                       SpawnExecutionPhase next_phase,
                                       SpawnExecutionReason reason,
                                       std::string* error_message) const;
    bool ApplySpawnRoutingAttempts(const comm::SpawnRequest& request,
                                   const std::string& agent_path,
                                   SpawnExecutionState* state,
                                   int* pid,
                                   std::string* error_message);
    bool AdmitSpawnRequest(const comm::SpawnRequest& request,
                           std::string* error_message);
    bool CompleteSpawnAfterRouting(const comm::SpawnRequest& request,
                                   const std::string& request_spawn_token,
                                   SpawnExecutionState* state,
                                   std::string* error_message);
    bool ApplyTerminalSpawnOutcome(const comm::SpawnRequest& request,
                                   SpawnExecutionState* state,
                                   std::string* error_message);
    // Experimental agent-controlled path. This is only attempted when explicit
    // symbi is not requested first by routing policy.
    ZygoteControlAttemptResult TrySpawnViaZygoteControl(const comm::SpawnRequest& request,
                                                        const std::string& agent_path);
    // Experimental zygote gate path. This is only preferred when the caller
    // explicitly requests symbi.
    bool SpawnViaSymbi(const comm::SpawnRequest& request,
                       const std::string& agent_path,
                       int* pid,
                       SpawnOwnedState* owned_state,
                       std::string* error_message);
    // Stable prepare/clear path used as the durable fallback backend.
    bool SpawnViaLegacyNcore(const comm::SpawnRequest& request,
                             const std::string& agent_path,
                             int* pid,
                             SpawnOwnedState* owned_state,
                             std::string* error_message);
    bool FinalizeSpawnOutcome(const comm::SpawnRequest& request,
                              const SpawnOutcome& outcome,
                              bool explicit_symbi_requested,
                              bool allow_symbi_backend,
                              bool allow_legacy_backend_fallback,
                              std::string* error_message);
    void ApplyTerminalOutcomeClassification(SpawnOutcome* outcome,
                                            SpawnFinalStatus final_status,
                                            SpawnTerminalBackend terminal_primary_backend,
                                            SpawnTerminalBackend terminal_secondary_backend) const;
    void ApplyFailedZygoteControlClassification(SpawnOutcome* outcome,
                                                const ZygoteControlOwnedTransaction& transaction,
                                                bool allow_fallback) const;
    void ClassifyTerminalSpawnOutcome(SpawnOutcome* outcome,
                                      bool explicit_symbi_requested,
                                      bool allow_symbi_backend,
                                      bool allow_legacy_backend_fallback) const;
    bool ShouldAllowZygoteControlFallback(const SpawnOutcome& outcome,
                                          bool strict_zygote_control) const;
    bool ApplyZygoteControlRouteAttempt(const comm::SpawnRequest& request,
                                        const ZygoteControlAttemptResult& attempt,
                                        bool strict_zygote_control,
                                        SpawnOutcome* outcome,
                                        int* pid,
                                        std::string* error_message);
    bool ApplySymbiRouteResult(const comm::SpawnRequest& request,
                               bool explicit_symbi_requested,
                               bool spawn_ok,
                               const std::string& symbi_error,
                               SpawnOwnedState owned_state,
                               SpawnOutcome* outcome,
                               int* pid,
                               std::string* error_message);
    bool ApplyLegacyRouteResult(const comm::SpawnRequest& request,
                                bool spawn_ok,
                                SpawnOwnedState owned_state,
                                const std::string& legacy_error,
                                SpawnOutcome* outcome,
                                int* pid,
                                std::string* error_message);
    bool ApplySuccessfulZygoteControlAttemptResult(SpawnOutcome* outcome,
                                                   const ZygoteControlAttemptResult& attempt,
                                                   SpawnOwnedState owned_state,
                                                   int* pid,
                                                   std::string* error_message);
    bool CommitSuccessfulSpawnOutcome(SpawnOutcome* outcome,
                                      SpawnOwnedState owned_state,
                                      int* pid,
                                      std::string* error_message,
                                      SpawnBackend backend,
                                      const std::string& identifier,
                                      ZygoteControlOwnedTransaction owned_transaction);
    PendingSpawnCommit BuildPendingSpawnCommit(SpawnBackend backend,
                                               const std::string& identifier,
                                               SpawnOwnedState owned_state,
                                               ZygoteControlOwnedTransaction owned_transaction) const;
    bool ApplySuccessfulRouteCommit(SpawnOutcome* outcome,
                                    SpawnBackend backend,
                                    const std::string& identifier,
                                    SpawnOwnedState owned_state,
                                    int committed_pid,
                                    SpawnFallbackPolicy fallback_policy,
                                    ZygoteControlOwnedTransaction owned_transaction,
                                    int* pid,
                                    std::string* error_message);
    bool ApplyFailedZygoteControlAttemptResult(SpawnOutcome* outcome,
                                               const ZygoteControlAttemptResult& attempt,
                                               bool strict_zygote_control) const;
    bool ApplyFailedZygoteControlOutcome(SpawnOutcome* outcome,
                                         const ZygoteControlOwnedTransaction& transaction,
                                         bool strict_zygote_control) const;
    void SnapshotFailedZygoteControlTransaction(SpawnOutcome* outcome,
                                                const ZygoteControlOwnedTransaction& transaction) const;
    void SnapshotCurrentZygoteControlTransactionState(
        ZygoteControlOwnedTransaction* transaction,
        const std::vector<std::pair<int, std::string>>* targets = nullptr) const;
    bool FailZygoteControlSpawn(ZygoteControlOwnedTransaction* transaction,
                                const std::vector<std::pair<int, std::string>>* targets,
                                const std::string& message,
                                std::string* error_message) const;
    ZygoteControlFailureState ResolveOutcomeZygoteControlState(const SpawnOutcome& outcome) const;
    bool FinalizeZygoteControlSpawn(const comm::SpawnRequest& request,
                                    ZygoteControlOwnedTransaction* owned_transaction,
                                    std::string* error_message);
    bool FinalizeLegacySpawn(const comm::SpawnRequest& request,
                             const std::string& owned_spawn_token,
                             const std::string& owned_ncore_path,
                             bool owned_materialized_ncore,
                             const std::string& owned_agent_path,
                             bool owned_materialized_agent,
                             std::string* error_message);
    bool TakeActiveOwnerForFinalize(const std::string& identifier,
                                    SpawnOwnershipState* finalize_owner,
                                    SpawnOwnedState* owned_spawn_state,
                                    ZygoteControlOwnedTransaction* owned_zygote_transaction,
                                    bool* has_foreign_active_owner,
                                    bool* has_residual_zygote_control_targets);
    bool ReleaseActiveOwnerAfterDeferredRouting(const std::string& identifier,
                                                const std::string& spawn_token);
    FinalizeSession BuildFinalizeSession(const comm::SpawnRequest& request);
    bool FinalizeOwnedSpawnByOwner(const comm::SpawnRequest& request,
                                   SpawnOwnershipState finalize_owner,
                                   const SpawnOwnedState& owned_spawn_state,
                                   ZygoteControlOwnedTransaction* owned_zygote_transaction,
                                   std::string* error_message);
    bool FinalizeWithoutOwnedBackend(const comm::SpawnRequest& request,
                                     bool has_foreign_active_owner,
                                     const ZygoteControlOwnedTransaction& residual_zygote_transaction,
                                     std::string* error_message);
    ZygoteControlFailureState ResolveTransactionZygoteControlState(
        const ZygoteControlOwnedTransaction* transaction,
        const std::string& detail) const;
    ZygoteControlFailureState ResolveCurrentZygoteControlState(const std::string& detail) const;
    void RecordZygoteControlLifecycleStage(ZygoteControlFailureState state) const;
    ZygoteControlFailureState ReadZygoteControlLifecycleStage() const;
    void ClearZygoteControlLifecycleStage() const;
    void RecordZygoteControlFailureState(ZygoteControlFailureState state) const;
    ZygoteControlFailureState ReadZygoteControlFailureState() const;
    void ClearZygoteControlFailureState() const;
    void CommitPendingSpawn(const PendingSpawnCommit& pending_commit);
    void StripCompatibilityArtifactResidueForMatchingShellOwnerLocked();
    void ClearAuthoritativeSpawnOwnerSlot(const SpawnOwnedState* authoritative_spawn_owner);
    void RestoreOwnedSpawnStateForRetry(const SpawnOwnedState& owned_spawn_state);
    static bool HasOwnedSpawnStateForRetry(const SpawnOwnedState& owned_spawn_state);
    static bool IsLegacyShellOwnedBackend(SpawnBackend backend);
    static bool IsChildOwnedBackend(SpawnBackend backend);
    bool PrepareRuntimeArtifact(const std::string& requested_path,
                                const unsigned char* embedded_blob,
                                size_t embedded_blob_size,
                                const char* empty_path_error,
                                const char* missing_path_error,
                                const char* materialize_error_prefix,
                                bool treat_embedded_sentinel_as_ready,
                                PreparedRuntimeArtifact* artifact,
                                std::string* error_message);
    void MaybeCleanupRuntimeArtifact(const PreparedRuntimeArtifact& artifact,
                                     const char* explicit_env_name);
    bool EnsureLegacyNcoreReady(std::string* resolved_ncore_path,
                                bool* materialized_embedded_ncore,
                                std::string* error_message);
    bool EnsureLegacyAgentReady(const std::string& agent_path,
                                std::string* resolved_agent_path,
                                bool* materialized_embedded_agent,
                                std::string* error_message);
    void MaybeCleanupLegacyNcoreArtifact(const std::string& resolved_ncore_path,
                                         bool materialized_embedded_ncore);
    void MaybeCleanupLegacyAgentArtifact(const std::string& resolved_agent_path,
                                         bool materialized_embedded_agent);

    NinjectorSpawnConfig config_;
    NinjectorSpawnOps ops_;
    ZygoteControlInstallHookFn install_zygote_hook_;
    ZygoteControlUninstallHookFn uninstall_zygote_hook_;
    HasPreexistingZygoteSessionFn has_preexisting_zygote_session_;
    EnumerateZygoteProcessesFn enumerate_zygote_processes_;
    mutable std::mutex transaction_mutex_;
    ActiveSpawnOwner active_spawn_owner_;
    mutable ZygoteControlFailureState current_zygote_control_lifecycle_stage_ =
        ZygoteControlFailureState::kUnknown;
    mutable ZygoteControlFailureState last_zygote_control_failure_state_ =
        ZygoteControlFailureState::kUnknown;
};

}  // namespace server
}  // namespace nook
