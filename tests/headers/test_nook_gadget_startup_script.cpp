#include "gadget/nook_gadget_runtime.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_asset_read_call_count = 0;
int g_script_load_call_count = 0;
bool g_asset_read_success = true;
NookStatus g_script_load_status = NOOK_STATUS_OK;
std::string g_last_asset_path;
std::string g_last_script_name;
std::string g_last_script_source;
std::vector<std::string> g_debug_logs;

bool FakeAssetReader(const char* asset_path, std::string* contents) {
    ++g_asset_read_call_count;
    g_last_asset_path = asset_path != nullptr ? asset_path : "";
    if (!g_asset_read_success || contents == nullptr) {
        return false;
    }
    *contents = "send('hello-from-configured-startup');\n";
    return true;
}

NookStatus FakeStartupScriptLoader(const char* script_name,
                                   const char* script_source,
                                   uint32_t* script_id) {
    ++g_script_load_call_count;
    g_last_script_name = script_name != nullptr ? script_name : "";
    g_last_script_source = script_source != nullptr ? script_source : "";
    if (script_id != nullptr) {
        *script_id = 7;
    }
    return g_script_load_status;
}

void FakeDebugLogger(const char* message) {
    g_debug_logs.emplace_back(message != nullptr ? message : "");
}

void ResetState() {
    g_asset_read_call_count = 0;
    g_script_load_call_count = 0;
    g_asset_read_success = true;
    g_script_load_status = NOOK_STATUS_OK;
    g_last_asset_path.clear();
    g_last_script_name.clear();
    g_last_script_source.clear();
    g_debug_logs.clear();
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    nook::gadget::GadgetConfig manual_config;
    manual_config.startup_mode = "manual";
    manual_config.startup_script.enabled = true;
    manual_config.startup_script.mode = "asset";
    manual_config.startup_script.path = "assets/nook-gadget/startup.js";

    ResetState();
    Require(
        nook::gadget::LoadConfiguredStartupScriptForTesting(
            manual_config,
            &FakeAssetReader,
            &FakeStartupScriptLoader,
            &FakeDebugLogger) == NOOK_STATUS_OK,
        "manual startup mode must skip auto-load cleanly");
    Require(g_asset_read_call_count == 0,
            "manual startup mode must not read startup assets during auto-load");
    Require(g_script_load_call_count == 0,
            "manual startup mode must not load startup scripts during auto-load");

    nook::gadget::GadgetConfig manual_debug_config = manual_config;
    manual_debug_config.debug_logging = true;
    ResetState();
    Require(
        nook::gadget::LoadConfiguredStartupScriptForTesting(
            manual_debug_config,
            &FakeAssetReader,
            &FakeStartupScriptLoader,
            &FakeDebugLogger) == NOOK_STATUS_OK,
        "manual startup mode with debug logging must still skip auto-load cleanly");
    Require(g_debug_logs.size() == 1,
            "manual startup mode debug path must emit exactly one skip log");
    Require(g_debug_logs[0].find("startup_mode=manual") != std::string::npos,
            "manual startup mode debug log must mention startup_mode=manual");

    ResetState();
    Require(
        nook::gadget::LoadConfiguredStartupScriptManuallyForTesting(
            manual_debug_config,
            &FakeAssetReader,
            &FakeStartupScriptLoader,
            &FakeDebugLogger) == NOOK_STATUS_OK,
        "manual startup mode must support an explicit later script load");
    Require(g_asset_read_call_count == 1,
            "manual later load must read the asset once");
    Require(g_script_load_call_count == 1,
            "manual later load must call the script loader once");
    Require(g_last_script_name == "startup.js",
            "manual later load must use the canonical script name");
    Require(g_last_script_source == "send('hello-from-configured-startup');\n",
            "manual later load must preserve the original startup script source");
    Require(g_debug_logs.size() == 2,
            "manual later load debug path must emit request and success logs");

    nook::gadget::GadgetConfig optional_config;
    optional_config.startup_mode = "auto-start";
    optional_config.debug_logging = true;
    optional_config.startup_script.enabled = true;
    optional_config.startup_script.mode = "asset";
    optional_config.startup_script.path = "assets/nook-gadget/startup.js";
    optional_config.startup_script.required = false;

    ResetState();
    g_asset_read_success = false;
    Require(
        nook::gadget::LoadConfiguredStartupScriptForTesting(
            optional_config,
            &FakeAssetReader,
            &FakeStartupScriptLoader,
            &FakeDebugLogger) == NOOK_STATUS_OK,
        "optional startup script read failure must not fail runtime init");
    Require(g_asset_read_call_count == 1,
            "optional startup script read failure must attempt one asset read");
    Require(g_script_load_call_count == 0,
            "optional startup script read failure must not call the loader");
    Require(g_debug_logs.size() == 1,
            "optional startup script read failure must emit one debug log");

    nook::gadget::GadgetConfig required_config = optional_config;
    required_config.startup_script.required = true;
    ResetState();
    g_asset_read_success = false;
    Require(
        nook::gadget::LoadConfiguredStartupScriptForTesting(
            required_config,
            &FakeAssetReader,
            &FakeStartupScriptLoader,
            &FakeDebugLogger) == NOOK_STATUS_INTERNAL_ERROR,
        "required startup script read failure must surface as runtime failure");
    Require(g_debug_logs.size() == 1,
            "required startup script read failure must emit one debug log");
    Require(g_debug_logs[0].find("required=true") != std::string::npos,
            "required startup script debug log must preserve required=true context");

    ResetState();
    Require(
        nook::gadget::LoadConfiguredStartupScriptForTesting(
            required_config,
            &FakeAssetReader,
            &FakeStartupScriptLoader,
            &FakeDebugLogger) == NOOK_STATUS_OK,
        "required startup script must succeed when asset reader and loader succeed");
    Require(g_asset_read_call_count == 1,
            "successful startup script auto-load must read the configured asset once");
    Require(g_script_load_call_count == 1,
            "successful startup script auto-load must load the startup script once");
    Require(g_last_asset_path == "assets/nook-gadget/startup.js",
            "successful startup script auto-load must use the configured asset path");
    Require(g_last_script_source.find("hello-from-configured-startup") != std::string::npos,
            "successful startup script auto-load must preserve the asset script payload");
    Require(g_last_script_source.find("__nookLifecycleReady") != std::string::npos,
            "successful startup script auto-load must wrap the startup script with lifecycle gating");
    Require(g_last_script_source != "send('hello-from-configured-startup');\n",
            "successful startup script auto-load must not execute the raw startup script immediately");
    Require(g_debug_logs.size() == 1,
            "successful startup script auto-load must emit one debug success log");

    return 0;
}
