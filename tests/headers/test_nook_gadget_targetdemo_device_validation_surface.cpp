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
    const std::string startup_script = ReadFile("host/nook-py/java_perform_startup_login.js");
    Require(!startup_script.empty(),
            "failed to read host/nook-py/java_perform_startup_login.js");
    Require(Contains(startup_script, "startup-login-hook-installed"),
            "startup login script must emit an installed marker");
    Require(Contains(startup_script, "verifyPasswordNative"),
            "startup login script must target verifyPasswordNative");

    const std::string wrapper = ReadFile("tools/nook_gadget_targetdemo_device_validation.ps1");
    Require(!wrapper.empty(),
            "failed to read tools/nook_gadget_targetdemo_device_validation.ps1");
    Require(Contains(wrapper, "nook_gadget_targetdemo_validation.ps1"),
            "targetdemo device wrapper must support the listen validation path");
    Require(Contains(wrapper, "nook_gadget_connect_validation.ps1"),
            "targetdemo device wrapper must support the connect validation path");
    Require(Contains(wrapper, "java_perform_startup_login.js"),
            "targetdemo device wrapper must default to the packaged startup login script");
    Require(Contains(wrapper, "listen-auto"),
            "targetdemo device wrapper must support a short listen-auto preset");
    Require(Contains(wrapper, "connect-manual-proxy"),
            "targetdemo device wrapper must support a short connect-manual-proxy preset");
    Require(Contains(wrapper, "ensure_nook_debug_keystore.ps1"),
            "targetdemo device wrapper must know how to provision a default debug keystore");
    Require(Contains(wrapper, "NOOK_GADGET_KEYSTORE"),
            "targetdemo device wrapper must support keystore defaults through environment variables");
    Require(Contains(wrapper, "build_nook_gadget.ps1"),
            "targetdemo device wrapper must point users at and invoke the gadget build step");
    Require(Contains(wrapper, "PrintOnly"),
            "targetdemo device wrapper must support a print-only planning mode");
    Require(Contains(wrapper, "[nook-gadget-targetdemo-device-validation] ok"),
            "targetdemo device wrapper must emit a stable success marker");
    return 0;
}
