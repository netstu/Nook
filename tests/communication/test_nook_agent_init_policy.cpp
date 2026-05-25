#include <cassert>
#include <string>

#include "framework/nook_agent_init_policy.h"

using namespace nook::framework;

namespace {

void TestShouldAutoInitializeNookAgentRejectsEarlySpawnProcesses() {
    assert(!ShouldAutoInitializeNookAgent(""));
    assert(!ShouldAutoInitializeNookAgent("zygote"));
    assert(!ShouldAutoInitializeNookAgent("zygote64"));
    assert(!ShouldAutoInitializeNookAgent("usap64"));
}

void TestShouldAutoInitializeNookAgentAcceptsRealAppProcess() {
    assert(ShouldAutoInitializeNookAgent("com.ad2001.frida0x8"));
}

void TestShouldActivateInheritedNookAgentRejectsEarlySpawnProcesses() {
    assert(!ShouldActivateInheritedNookAgent("zygote64", true));
    assert(!ShouldActivateInheritedNookAgent("usap64", true));
}

void TestShouldActivateInheritedNookAgentRequiresRealProcessAndMarker() {
    assert(ShouldActivateInheritedNookAgent("com.ad2001.frida0x8", false));
    assert(ShouldActivateInheritedNookAgent("com.ad2001.frida0x8", true));
}

}  // namespace

int main() {
    TestShouldAutoInitializeNookAgentRejectsEarlySpawnProcesses();
    TestShouldAutoInitializeNookAgentAcceptsRealAppProcess();
    TestShouldActivateInheritedNookAgentRejectsEarlySpawnProcesses();
    TestShouldActivateInheritedNookAgentRequiresRealProcessAndMarker();
    return 0;
}
