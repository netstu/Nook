#define main test_ninjector_spawn_injector_full_main_unused
#include "test_ninjector_spawn_injector.cpp"
#undef main

int main() {
    TestSpawnStrictZygoteControlPrefersZygoteControlPath();
    TestSpawnDefaultStablePathSkipsZygoteControlWhenEnabled();
    TestSpawnServerStrictEnvDoesNotPromoteDefaultSpawnRoute();
    TestSpawnDefaultStablePathDoesNotTrySymbiWhenPreferenceDisabled();
    TestBuildSpawnExecutionPolicyExplicitSymbiRequest();
    TestSpawnReinjectsWhenOnlyPreexistingControlReadySessionExistsWithoutOwnedTarget();
    TestSpawnStrictZygoteControlAbortsWhenControlFails();
    TestSpawnExplicitSymbiPrefersSymbiBackend();
    TestSpawnExplicitSymbiFailureDoesNotFallbackToLegacyBackend();
    TestSpawnSymbiFailureStopsFlow();
    TestSpawnDefaultEmbeddedAgentStaysOnStableLegacyRouteWhenZygoteControlIsEnabled();
    TestSpawnDefaultStablePathSkipsZygoteControlEvenWhenSupportIsEnabled();
    return 0;
}
