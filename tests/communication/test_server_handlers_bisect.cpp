#define main test_server_handlers_full_main_unused
#include "test_server_handlers.cpp"
#undef main

int main(int argc, char** argv) {
    const int group = (argc > 1) ? std::atoi(argv[1]) : 0;

    switch (group) {
    case 1:
        TestSpawnRequestMarksGateHeldChildAndBindsHost();
        TestSpawnRequestDoesNotRequireCoarseSuspendCallback();
        TestAttachRequestInjectsAgentAndBindsHost();
        TestAttachRequestReusesExistingReadyAgentSession();
        TestAttachRequestReusesExistingRuntimeReadySessionWithoutCachedReadyFrame();
        TestAttachRequestAfterDetachReplaysCachedReady();
        TestAttachRequestAfterHostCloseReplaysCachedReady();
        TestAttachRequestTimeoutKeepsHostUnboundEvenIfLateAgentReadyArrives();
        TestSpawnRequestFailureReturnsError();
        TestSpawnRequestTimesOutWithoutAuthoritativeAgentReadyAndClearsPendingSpawn();
        TestSpawnRequestFinalizeFailureDoesNotBindHostOrKeepPendingSpawn();
        break;
    case 2:
        TestDetachRequestUnbindsHost();
        TestDetachRequestCanBeIssuedFromDifferentHostSession();
        TestDetachRequestFailsWhileSpawnGateIsHeld();
        TestSessionRegistryTracksGateHeldEntries();
        TestSessionRegistryRebindsHostToNewestPidOnly();
        TestResumeRequestFailsForUnknownPid();
        TestResumeRequestFailsBeforeAuthoritativeAgentReady();
        TestResumeRequestReleasesGateHeldProcess();
        TestResumeRequestKeepsGateHeldEntryWhenReleaseFails();
        TestResumeRequestAcceptsOnlyOneSuccessfulRelease();
        TestAgentReadyForwardsToBoundHost();
        break;
    case 3:
        TestControlStageAgentReadyDoesNotForwardToBoundHost();
        TestRuntimeAgentReadyReplacesEarlierControlStageConnection();
        TestSpawnRequestReplaysRuntimeAgentReadyToBoundHost();
        TestSpawnRequestUsesAuthoritativeAgentReadyPidInsteadOfInjectorPid();
        TestSpawnRequestUsesRuntimeReadyStateWithoutCachedReadyFrame();
        TestAgentReadyWithMismatchedSpawnTokenDoesNotResolvePendingSpawn();
        TestControlStageAgentReadyWithMatchingSpawnTokenResolvesPendingSpawn();
        TestControlStageAgentReadyForSpawnedChildTriggersFullAgentInjection();
        TestControlStageAgentReadyResolvesPendingSpawnAndTriggersImmediatePromotion();
        TestControlStageAgentReadyDoesNotInjectAgainWhenRuntimeSessionAlreadyPresent();
        TestSpawnRequestDoesNotLatePromoteChildAlreadyPromotedByControlReady();
        break;
    case 4:
        TestScriptMessageForwardsToBoundHost();
        TestScriptMessageAfterDetachIsNotReplayedToNextAttach();
        TestSpawnRequestReplaysEarlyScriptMessageToBoundHost();
        TestScriptPostForwardsToBoundAgent();
        TestScriptCreateForwardsToBoundAgent();
        TestScriptCreateReturnsImmediateErrorWithoutAgentSession();
        TestScriptCreateRespForwardsToBoundHost();
        TestScriptLoadForwardsToBoundAgent();
        TestScriptLoadRequiresAuthoritativeAgentReadyForSpawn();
        TestScriptLoadReturnsImmediateErrorWithoutAgentSession();
        TestScriptLoadRespForwardsToBoundHost();
        TestScriptUnloadForwardsToBoundAgent();
        TestScriptUnloadRespForwardsToBoundHost();
        TestRpcRequestForwardsToBoundAgent();
        TestRpcResponseForwardsToBoundHost();
        TestProcessListRequestReturnsProcesses();
        TestAppListRequestReturnsApps();
        TestScriptCreateRequiresRuntimeReadyForSpawn();
        break;
    default:
        return 2;
    }

    return 0;
}
