#define main test_ninjector_spawn_injector_full_main_unused
#include "test_ninjector_spawn_injector.cpp"
#undef main

int main() {
    TestBuildSpawnExecutionPolicyLegacyPreferredWhenSymbiPreferenceDisabled();
    TestSpawnDefaultStablePathDoesNotTrySymbiWhenPreferenceDisabled();
    TestSpawnDefaultPathPrefersSymbiByDefault();
    TestSpawnDefaultPathFallsBackToLegacyWhenSymbiFails();
    return 0;
}
