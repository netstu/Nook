#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    const std::string smoke = ReadFile("tools/nook_gadget_patch_smoke.ps1");
    Require(!smoke.empty(), "failed to read tools/nook_gadget_patch_smoke.ps1");
    Require(Contains(smoke, "nook_patchapk.py"),
            "nook gadget smoke harness must invoke the patch tool");
    Require(Contains(smoke, "libnook-gadget.so"),
            "nook gadget smoke harness must validate the gadget library artifact");
    Require(Contains(smoke, "--startup-script"),
            "nook gadget smoke harness must forward the startup-script option");
    Require(Contains(smoke, "--startup-mode"),
            "nook gadget smoke harness must forward the startup-mode option");
    Require(Contains(smoke, "assets/nook-gadget/startup.js"),
            "nook gadget smoke harness must document packaged startup script placement");
    Require(Contains(smoke, "startup_mode=manual disables packaged startup-script auto-load on cold start"),
            "nook gadget smoke harness must explain manual startup-mode behavior");
    Require(Contains(smoke, "nook.gadget.load-configured-startup"),
            "nook gadget smoke harness must document the manual startup-script RPC");
    Require(Contains(smoke, "nook_gadget_trigger_packaged_startup.ps1"),
            "nook gadget smoke harness must document the packaged-startup helper script");
    Require(Contains(smoke, "nook-cli attach"),
            "nook gadget smoke harness must document the host attach command");

    const std::string apk_validation = ReadFile("tools/nook_gadget_apk_validation.ps1");
    Require(!apk_validation.empty(), "failed to read tools/nook_gadget_apk_validation.ps1");
    Require(Contains(apk_validation, "manual packaged startup trigger completed"),
            "apk validation harness must report manual packaged startup trigger completion");
    Require(Contains(apk_validation, "manual packaged startup matches:"),
            "apk validation harness must validate post-trigger startup logs for manual mode");
    Require(Contains(apk_validation, "No manual startup validation logs matched pattern"),
            "apk validation harness must fail when post-trigger startup logs are missing");

    const std::string status = ReadFile("docs/plans/2026-05-18-nook-gadget-validation-status.md");
    Require(!status.empty(), "failed to read nook gadget validation status doc");
    Require(Contains(status, "Supported v1 scope"),
            "validation status doc must include the supported v1 scope");
    Require(Contains(status, "Known gaps"),
            "validation status doc must include known gaps");
    Require(Contains(status, "nook_gadget_patch_smoke.ps1"),
            "validation status doc must reference the smoke harness");
    Require(Contains(status, "startup script"),
            "validation status doc must mention startup-script scope");
    Require(Contains(status, "startup_mode=manual"),
            "validation status doc must document manual startup-mode behavior");
    Require(Contains(status, "transport_mode"),
            "validation status doc must document transport-mode semantics");
    Require(Contains(status, "nook.gadget.load-configured-startup"),
            "validation status doc must document the manual startup-script RPC");
    Require(Contains(status, "nook_gadget_trigger_packaged_startup.ps1"),
            "validation status doc must document the packaged-startup helper script");
    Require(Contains(status, "manual packaged startup trigger"),
            "validation status doc must document manual packaged-startup validation");
    return 0;
}
