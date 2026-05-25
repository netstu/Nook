#define main nook_test_server_handlers_main_disabled
#include "test_server_handlers.cpp"
#undef main

int main() {
    TestControlStageAgentReadyDoesNotForwardToBoundHost();
    TestRuntimeAgentReadyReplacesEarlierControlStageConnection();
    return 0;
}
