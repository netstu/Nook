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
    const std::string source = ReadFile("tools/nook_patchapk.py");
    Require(!source.empty(), "failed to read tools/nook_patchapk.py");
    Require(Contains(source, "def build_parser("),
            "nook_patchapk.py must define a parser builder");
    Require(Contains(source, "def build_patch_plan("),
            "nook_patchapk.py must define a patch plan builder");
    Require(Contains(source, "def validate_port_arg("),
            "nook_patchapk.py must validate port ranges explicitly");
    Require(Contains(source, "def validate_interaction_args("),
            "nook_patchapk.py must validate interaction-mode-specific CLI inputs");
    Require(Contains(source, "def get_config_asset_path("),
            "nook_patchapk.py must define a config asset path helper");
    Require(Contains(source, "def build_default_config("),
            "nook_patchapk.py must define default patch metadata");
    Require(Contains(source, "def detect_supported_lib_dir("),
            "nook_patchapk.py must define ABI layout detection");
    Require(Contains(source, "def place_gadget_library("),
            "nook_patchapk.py must define gadget library placement");
    Require(Contains(source, "def inject_bootstrap_into_decoded_smali_dir("),
            "nook_patchapk.py must define bootstrap injection for decoded smali fixtures");
    Require(Contains(source, "def rewrite_manifest_for_bootstrap("),
            "nook_patchapk.py must define manifest rewrite for bootstrap markers");
    Require(Contains(source, "def unpack_apk_to_dir("),
            "nook_patchapk.py must define APK unpacking for the patch flow");
    Require(Contains(source, "def rebuild_dir_to_apk("),
            "nook_patchapk.py must define APK rebuild for the patch flow");
    Require(Contains(source, "def patch_apk("),
            "nook_patchapk.py must define an integrated patch flow");
    Require(Contains(source, "def main("),
            "nook_patchapk.py must define a standalone main entrypoint");
    Require(Contains(source, "--input-apk"),
            "nook_patchapk.py must expose --input-apk");
    Require(Contains(source, "--output-apk"),
            "nook_patchapk.py must expose --output-apk");
    Require(Contains(source, "--gadget-lib"),
            "nook_patchapk.py must expose --gadget-lib");
    Require(Contains(source, "--startup-script"),
            "nook_patchapk.py must expose --startup-script");
    Require(Contains(source, "--startup-script-on-load"),
            "nook_patchapk.py must expose --startup-script-on-load");
    Require(Contains(source, "--startup-script-required"),
            "nook_patchapk.py must expose --startup-script-required");
    Require(Contains(source, "--startup-mode"),
            "nook_patchapk.py must expose --startup-mode");
    Require(Contains(source, "--interaction-type"),
            "nook_patchapk.py must expose --interaction-type");
    Require(Contains(source, "--connect-host"),
            "nook_patchapk.py must expose --connect-host");
    Require(Contains(source, "--connect-port"),
            "nook_patchapk.py must expose --connect-port");
    Require(Contains(source, "--listen-address"),
            "nook_patchapk.py must expose --listen-address");
    Require(Contains(source, "--listen-port"),
            "nook_patchapk.py must expose --listen-port");
    Require(Contains(source, "--transport-mode"),
            "nook_patchapk.py must expose --transport-mode");
    Require(Contains(source, "--bootstrap-mode"),
            "nook_patchapk.py must expose --bootstrap-mode");
    Require(Contains(source, "proxy-loader"),
            "nook_patchapk.py bootstrap surface must mention proxy-loader mode");
    Require(Contains(source, "--debug-logging"),
            "nook_patchapk.py must expose --debug-logging");
    Require(Contains(source, "--use-aapt2"),
            "nook_patchapk.py must expose --use-aapt2");
    Require(Contains(source, "--print-plan"),
            "nook_patchapk.py must expose --print-plan");
    Require(Contains(source, "assets/nook-gadget/config.json"),
            "nook_patchapk.py must emit config to a stable asset path");
    Require(Contains(source, "assets/nook-gadget/startup.js"),
            "nook_patchapk.py must support a stable startup script asset path");
    Require(Contains(source, "startup_script"),
            "nook_patchapk.py must support startup script config metadata");
    Require(Contains(source, "\"interaction\""),
            "nook_patchapk.py must emit v2.1 interaction config metadata");
    Require(Contains(source, "\"type\""),
            "nook_patchapk.py interaction config must encode its type");
    Require(Contains(source, "\"host\""),
            "nook_patchapk.py interaction config must encode its host");
    Require(Contains(source, "\"address\""),
            "nook_patchapk.py interaction config must encode its address");
    Require(Contains(source, "\"port\""),
            "nook_patchapk.py interaction config must encode its port");
    Require(Contains(source, "\"on_load\""),
            "nook_patchapk.py startup script config must encode on_load policy");
    Require(Contains(source, "\"startup_mode\""),
            "nook_patchapk.py must emit startup_mode config metadata");
    Require(Contains(source, "\"transport_mode\""),
            "nook_patchapk.py must emit transport_mode config metadata");
    Require(Contains(source, "\"bootstrap_mode\""),
            "nook_patchapk.py must emit bootstrap_mode metadata");
    Require(Contains(source, "\"debug_logging\""),
            "nook_patchapk.py must emit debug_logging config metadata");
    Require(Contains(source, "input_apk=args.input_apk"),
            "nook_patchapk.py main must dispatch patch_apk with keyword arguments");
    Require(Contains(source, "interaction_type=args.interaction_type"),
            "nook_patchapk.py main must forward interaction_type by keyword");
    Require(Contains(source, "startup_script_on_load=args.startup_script_on_load"),
            "nook_patchapk.py main must forward startup_script_on_load by keyword");
    Require(Contains(source, "System;->loadLibrary(Ljava/lang/String;)V"),
            "nook_patchapk.py bootstrap helper must inject System.loadLibrary");
    Require(Contains(source, "\"nook-gadget\""),
            "nook_patchapk.py bootstrap helper must inject the nook-gadget library name");
    Require(Contains(source, "nook.gadget.bootstrap"),
            "nook_patchapk.py manifest rewrite must leave a stable bootstrap marker");
    return 0;
}
