#define main test_server_handlers_original_main
#include "test_server_handlers.cpp"
#undef main

int main() {
    TestDetachRequestUnbindsHost();
    TestDetachRequestCanBeIssuedFromDifferentHostSession();
    TestDetachRequestFailsWhileSpawnGateIsHeld();
    TestDetachRequestUsesSuspendedOwnerWhenPidBindingIsRebound();
    TestResumeRequestFailsForUnknownPid();
    TestResumeRequestFailsBeforeAuthoritativeAgentReady();
    TestResumeRequestReleasesGateHeldProcess();
    TestResumeRequestRejectsNonOwnerHostForSuspendedSpawn();
    TestResumeRequestKeepsGateHeldEntryWhenReleaseFails();
    TestResumeRequestAcceptsOnlyOneSuccessfulRelease();
    return 0;
}
