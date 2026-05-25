#include <cassert>
#include <string>

#include "server/session_registry.h"
#include "server/zygote_control_rpc.h"

using namespace nook::server;

namespace {

void TestUninstallZygoteForkHookClearsOwnedTargetWhenOwnedSessionIsMissing() {
    SessionRegistry registry;
    registry.MarkOwnedZygoteControlProcess(14535, "zygote64");

    assert(registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(registry.IsOwnedZygoteControlTarget(14535, "zygote64"));

    std::string error_message;
    assert(!UninstallZygoteForkHookWithSendersForTest(&registry,
                                                      14535,
                                                      "zygote64",
                                                      &error_message,
                                                      {},
                                                      {}));

    assert(error_message == "zygote control-ready agent session not found pid=14535 process=zygote64");
    assert(!registry.IsOwnedZygoteControlProcess("zygote64"));
    assert(!registry.IsOwnedZygoteControlTarget(14535, "zygote64"));
}

}  // namespace

int main() {
    TestUninstallZygoteForkHookClearsOwnedTargetWhenOwnedSessionIsMissing();
    return 0;
}
