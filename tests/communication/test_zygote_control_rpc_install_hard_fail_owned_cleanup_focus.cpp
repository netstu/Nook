#define main nook_test_zygote_control_rpc_main_unused
#include "test_zygote_control_rpc.cpp"
#undef main

int main() {
    TestInstallZygoteForkHookClearsOwnedOnDedicatedInstallHardFailure();
    return 0;
}
