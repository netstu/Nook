#define main nook_test_server_handlers_main_unused
#include "test_server_handlers.cpp"
#undef main

int main() {
    TestAttachRequestDoesNotReuseStaleGlobalRuntimeReadyWithoutAgentSession();
    TestAttachRequestWaitsForMatchingRuntimeReadyInsteadOfStaleRuntimeBit();
    TestAttachRequestDoesNotReuseRuntimeReadySessionWhenIdentityMismatches();
    return 0;
}
