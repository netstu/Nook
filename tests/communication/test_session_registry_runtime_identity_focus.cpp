#define main nook_test_session_registry_main_unused
#include "test_session_registry.cpp"
#undef main

int main() {
    TestWaitForRuntimeReadyAgentSessionByIdentityRequiresRuntimeStageAndIdentity();
    return 0;
}
