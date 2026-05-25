#include <cassert>
#include <string>

#include "framework/NookZygoteSpawn.h"

using namespace nook::framework;

namespace {

void TestInstallArgsJsonAndStatusJsonContract() {
    std::string target_package;
    std::string spawn_token;
    std::string mode;
    std::string error_message;
    assert(ParseSpawnInstallArgsJson(
        "{\"target_package\":\"com.demo.target\",\"spawn_token\":\"spawn-token-11\",\"mode\":\"stable\"}",
        &target_package,
        &spawn_token,
        &mode,
        &error_message));

    ZygoteSpawnController controller;
    assert(controller.Install(target_package, spawn_token, &error_message));
    const std::string status_json = BuildZygoteSpawnStatusJson(true, controller);
    assert(status_json.find("\"ready\":true") != std::string::npos);
    assert(status_json.find("\"state\":\"armed\"") != std::string::npos);
    assert(status_json.find("\"target_package\":\"com.demo.target\"") != std::string::npos);
    assert(status_json.find("\"spawn_token\":\"spawn-token-11\"") != std::string::npos);
}

void TestInstallRejectsSecondArmedTarget() {
    ZygoteSpawnController controller;
    std::string error_message;
    assert(controller.Install("com.demo.target", "spawn-token-11", &error_message));
    assert(!controller.Install("com.demo.other", "spawn-token-12", &error_message));
    assert(error_message == "zygote spawn controller already armed");
}

}  // namespace

int main() {
    TestInstallArgsJsonAndStatusJsonContract();
    TestInstallRejectsSecondArmedTarget();
    return 0;
}
