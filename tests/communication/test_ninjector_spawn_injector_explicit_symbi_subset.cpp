#define main test_ninjector_spawn_injector_full_main_unused
#include "test_ninjector_spawn_injector.cpp"
#undef main

int main() {
    TestBuildSpawnExecutionPolicyExplicitSymbiRequest();
    TestSpawnExplicitSymbiPrefersSymbiBackend();
    TestSpawnExplicitSymbiFailureDoesNotFallbackToLegacyBackend();
    TestSpawnSymbiFailureStopsFlow();
    return 0;
}
