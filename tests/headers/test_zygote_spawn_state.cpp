#include <cassert>
#include <iostream>
#include <string>

#include "framework/NookZygoteSpawn.h"

using namespace nook::framework;

namespace {

void TestInitialStateIsIdle() {
    ZygoteSpawnController controller;
    assert(controller.state() == ZygoteSpawnState::kIdle);
    assert(controller.target_package().empty());
    assert(controller.spawn_token().empty());
}

void TestInstallTransitionsIdleToArmed() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-1", &error_message));
    assert(error_message.empty());
    assert(controller.state() == ZygoteSpawnState::kArmed);
    assert(controller.target_package() == "com.demo.target");
    assert(controller.spawn_token() == "spawn-token-1");
}

void TestInstallWhileArmedFails() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-1", &error_message));

    error_message.clear();
    assert(!controller.Install("com.demo.other", "spawn-token-2", &error_message));
    assert(error_message == "zygote spawn controller already armed");
    assert(controller.state() == ZygoteSpawnState::kArmed);
    assert(controller.target_package() == "com.demo.target");
    assert(controller.spawn_token() == "spawn-token-1");
}

void TestConsumeMatchingTokenTransitionsToConsumed() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-1", &error_message));

    error_message.clear();
    assert(controller.Consume("spawn-token-1", &error_message));
    assert(error_message.empty());
    assert(controller.state() == ZygoteSpawnState::kConsumed);
    assert(controller.target_package() == "com.demo.target");
    assert(controller.spawn_token() == "spawn-token-1");
}

void TestConsumeWrongTokenFails() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-1", &error_message));

    error_message.clear();
    assert(!controller.Consume("spawn-token-2", &error_message));
    assert(error_message == "spawn token mismatch");
    assert(controller.state() == ZygoteSpawnState::kArmed);
}

void TestUninstallRestoresIdle() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-1", &error_message));
    assert(controller.Consume("spawn-token-1", &error_message));

    controller.Uninstall();
    assert(controller.state() == ZygoteSpawnState::kIdle);
    assert(controller.target_package().empty());
    assert(controller.spawn_token().empty());
}

void TestParseSpawnInstallArgsJson() {
    std::string target_package;
    std::string spawn_token;
    std::string mode;
    std::string error_message;
    assert(ParseSpawnInstallArgsJson(
        "{\"target_package\":\"com.demo.target\",\"spawn_token\":\"spawn-token-7\",\"mode\":\"stable\"}",
        &target_package,
        &spawn_token,
        &mode,
        &error_message));
    assert(error_message.empty());
    assert(target_package == "com.demo.target");
    assert(spawn_token == "spawn-token-7");
    assert(mode == "stable");
}

void TestParseSpawnInstallArgsJsonRejectsMissingField() {
    std::string target_package;
    std::string spawn_token;
    std::string mode;
    std::string error_message;
    assert(!ParseSpawnInstallArgsJson(
        "{\"target_package\":\"com.demo.target\",\"mode\":\"stable\"}",
        &target_package,
        &spawn_token,
        &mode,
        &error_message));
    assert(error_message == "invalid spawn install args json");
}

void TestBuildZygoteSpawnStatusJson() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-9", &error_message));
    const std::string json = BuildZygoteSpawnStatusJson(true, controller);
    assert(json == "{\"ready\":true,\"state\":\"armed\",\"target_package\":\"com.demo.target\",\"spawn_token\":\"spawn-token-9\"}");
}

void TestTryConsumeForNiceNameConsumesArmedController() {
    ZygoteSpawnController controller;
    std::string error_message;
    std::string spawn_token;
    assert(controller.Install("com.demo.target", "spawn-token-10", &error_message));
    assert(TryConsumeForNiceName(&controller, "com.demo.target", &spawn_token, &error_message));
    assert(error_message.empty());
    assert(spawn_token == "spawn-token-10");
    assert(controller.state() == ZygoteSpawnState::kConsumed);
}

void TestConsumedControllerRejectsSecondConsumeForSameNiceName() {
    ZygoteSpawnController controller;
    std::string error_message;
    std::string spawn_token;
    assert(controller.Install("com.demo.target", "spawn-token-10", &error_message));
    assert(TryConsumeForNiceName(&controller, "com.demo.target", &spawn_token, &error_message));
    spawn_token.clear();
    error_message.clear();
    assert(!TryConsumeForNiceName(&controller, "com.demo.target", &spawn_token, &error_message));
    assert(error_message == "zygote spawn controller is not armed");
    assert(spawn_token.empty());
    assert(controller.state() == ZygoteSpawnState::kConsumed);
}

void TestTryConsumeForNiceNameRejectsMismatch() {
    ZygoteSpawnController controller;
    std::string error_message;
    std::string spawn_token;
    assert(controller.Install("com.demo.target", "spawn-token-10", &error_message));
    assert(!TryConsumeForNiceName(&controller, "com.demo.other", &spawn_token, &error_message));
    assert(error_message == "nice name does not match target package");
    assert(controller.state() == ZygoteSpawnState::kArmed);
}

void TestMatchesArmedTargetPackageAcceptsExactMatchWithoutConsuming() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-11", &error_message));
    assert(MatchesArmedTargetPackage(&controller, "com.demo.target", &error_message));
    assert(error_message.empty());
    assert(controller.state() == ZygoteSpawnState::kArmed);
    assert(controller.spawn_token() == "spawn-token-11");
}

void TestMatchesArmedTargetPackageRejectsMismatchWithoutConsuming() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-11", &error_message));
    assert(!MatchesArmedTargetPackage(&controller, "com.demo.other", &error_message));
    assert(error_message == "nice name does not match target package");
    assert(controller.state() == ZygoteSpawnState::kArmed);
}

void TestMatchesArmedTargetPackageRejectsConsumedController() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-11", &error_message));
    assert(controller.Consume("spawn-token-11", &error_message));
    assert(!MatchesArmedTargetPackage(&controller, "com.demo.target", &error_message));
    assert(error_message == "zygote spawn controller is not armed");
}

void TestGetArmedSpawnTokenForNiceNameReturnsTokenWithoutConsuming() {
    ZygoteSpawnController controller;
    std::string error_message;
    std::string spawn_token;
    assert(controller.Install("com.demo.target", "spawn-token-12", &error_message));
    assert(GetArmedSpawnTokenForNiceName(&controller, "com.demo.target", &spawn_token, &error_message));
    assert(error_message.empty());
    assert(spawn_token == "spawn-token-12");
    assert(controller.state() == ZygoteSpawnState::kArmed);
}

void TestGetArmedSpawnTokenForNiceNameRejectsMismatch() {
    ZygoteSpawnController controller;
    std::string error_message;
    std::string spawn_token;
    assert(controller.Install("com.demo.target", "spawn-token-12", &error_message));
    assert(!GetArmedSpawnTokenForNiceName(&controller, "com.demo.other", &spawn_token, &error_message));
    assert(error_message == "nice name does not match target package");
    assert(controller.state() == ZygoteSpawnState::kArmed);
}

void TestCompatibilitySpawnFallbackAllowedWhenControllerIdle() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(IsCompatibilitySpawnFallbackAllowed(&controller, &error_message));
    assert(error_message.empty());
}

void TestCompatibilitySpawnFallbackBlockedWhenControllerArmed() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-12", &error_message));
    assert(!IsCompatibilitySpawnFallbackAllowed(&controller, &error_message));
    assert(error_message == "zygote spawn controller owns active transaction");
}

void TestCompatibilitySpawnFallbackBlockedWhenControllerConsumed() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-12", &error_message));
    assert(controller.Consume("spawn-token-12", &error_message));
    assert(!IsCompatibilitySpawnFallbackAllowed(&controller, &error_message));
    assert(error_message == "zygote spawn controller owns active transaction");
}

void TestGetActiveSpawnSnapshotRejectsIdleController() {
    ZygoteSpawnController controller;
    std::string target_package;
    std::string spawn_token;
    std::string error_message;
    assert(!GetActiveSpawnSnapshot(&controller, &target_package, &spawn_token, &error_message));
    assert(error_message == "zygote spawn controller is idle");
}

void TestGetActiveSpawnSnapshotReturnsArmedControllerData() {
    ZygoteSpawnController controller;
    std::string target_package;
    std::string spawn_token;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-14", &error_message));
    assert(GetActiveSpawnSnapshot(&controller, &target_package, &spawn_token, &error_message));
    assert(error_message.empty());
    assert(target_package == "com.demo.target");
    assert(spawn_token == "spawn-token-14");
}

void TestGetActiveSpawnSnapshotReturnsConsumedControllerData() {
    ZygoteSpawnController controller;
    std::string target_package;
    std::string spawn_token;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-15", &error_message));
    assert(controller.Consume("spawn-token-15", &error_message));
    assert(GetActiveSpawnSnapshot(&controller, &target_package, &spawn_token, &error_message));
    assert(error_message.empty());
    assert(target_package == "com.demo.target");
    assert(spawn_token == "spawn-token-15");
}

void TestMatchSpawnTargetAndTokenAcceptsExactInputs() {
    std::string error_message;
    assert(MatchSpawnTargetAndToken("com.demo.target",
                                    "com.demo.target",
                                    "spawn-token-13",
                                    &error_message));
    assert(error_message.empty());
}

void TestMatchSpawnTargetAndTokenRejectsMissingToken() {
    std::string error_message;
    assert(!MatchSpawnTargetAndToken("com.demo.target",
                                     "com.demo.target",
                                     "",
                                     &error_message));
    assert(error_message == "spawn token is empty");
}

void TestMatchSpawnTargetAndTokenRejectsMismatch() {
    std::string error_message;
    assert(!MatchSpawnTargetAndToken("com.demo.target",
                                     "com.demo.other",
                                     "spawn-token-13",
                                     &error_message));
    assert(error_message == "nice name does not match target package");
}

}  // namespace

int main() {
    TestInitialStateIsIdle();
    TestInstallTransitionsIdleToArmed();
    TestInstallWhileArmedFails();
    TestConsumeMatchingTokenTransitionsToConsumed();
    TestConsumeWrongTokenFails();
    TestUninstallRestoresIdle();
    TestParseSpawnInstallArgsJson();
    TestParseSpawnInstallArgsJsonRejectsMissingField();
    TestBuildZygoteSpawnStatusJson();
    TestTryConsumeForNiceNameConsumesArmedController();
    TestConsumedControllerRejectsSecondConsumeForSameNiceName();
    TestTryConsumeForNiceNameRejectsMismatch();
    TestMatchesArmedTargetPackageAcceptsExactMatchWithoutConsuming();
    TestMatchesArmedTargetPackageRejectsMismatchWithoutConsuming();
    TestMatchesArmedTargetPackageRejectsConsumedController();
    TestGetArmedSpawnTokenForNiceNameReturnsTokenWithoutConsuming();
    TestGetArmedSpawnTokenForNiceNameRejectsMismatch();
    TestCompatibilitySpawnFallbackAllowedWhenControllerIdle();
    TestCompatibilitySpawnFallbackBlockedWhenControllerArmed();
    TestCompatibilitySpawnFallbackBlockedWhenControllerConsumed();
    TestGetActiveSpawnSnapshotRejectsIdleController();
    TestGetActiveSpawnSnapshotReturnsArmedControllerData();
    TestGetActiveSpawnSnapshotReturnsConsumedControllerData();
    TestMatchSpawnTargetAndTokenAcceptsExactInputs();
    TestMatchSpawnTargetAndTokenRejectsMissingToken();
    TestMatchSpawnTargetAndTokenRejectsMismatch();

    std::cout << "Zygote spawn state tests passed!" << std::endl;
    return 0;
}
