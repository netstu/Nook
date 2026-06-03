#pragma once

#include <string>

namespace nook {
namespace gadget {

struct StartupScriptConfig {
    bool enabled = false;
    bool required = false;
    std::string mode;
    std::string path;
    std::string on_load = "auto";
};

struct InteractionConfig {
    std::string type = "listen";
    std::string transport = "default";
    std::string on_load = "resume";
    std::string host;
    std::string address;
    int port = 0;
};

struct GadgetConfig {
    std::string gadget_version;
    std::string startup_mode = "auto-start";
    std::string transport_mode = "default";
    bool debug_logging = false;
    InteractionConfig interaction;
    StartupScriptConfig startup_script;
};

bool ParseGadgetConfigJson(const std::string& json, GadgetConfig* config);
std::string SerializeGadgetConfigJson(const GadgetConfig& config);
bool ReadGadgetAssetFile(const char* asset_path, std::string* contents);

}  // namespace gadget
}  // namespace nook
