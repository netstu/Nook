#include "gadget/nook_gadget_config.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    nook::gadget::GadgetConfig config;

    Require(nook::gadget::ParseGadgetConfigJson("{}", &config),
            "empty gadget config json must parse");
    Require(config.gadget_version.empty(),
            "empty gadget config must leave gadget_version unset");
    Require(config.startup_mode == "auto-start",
            "empty gadget config must default startup_mode to auto-start");
    Require(config.transport_mode == "default",
            "empty gadget config must default transport_mode to default");
    Require(!config.debug_logging,
            "empty gadget config must default debug_logging to false");
    Require(config.interaction.type == "listen",
            "empty gadget config must default interaction.type to listen");
    Require(config.interaction.transport == "default",
            "empty gadget config must default interaction.transport to default");
    Require(config.interaction.host.empty(),
            "empty gadget config must default interaction.host to empty");
    Require(config.interaction.address.empty(),
            "empty gadget config must default interaction.address to empty");
    Require(config.interaction.port == 0,
            "empty gadget config must default interaction.port to 0");
    Require(config.interaction.on_load == "resume",
            "empty gadget config must default interaction.on_load to resume");
    Require(!config.startup_script.enabled,
            "empty gadget config must not enable startup script");
    Require(config.startup_script.on_load == "auto",
            "empty gadget config must default startup script on_load to auto");

    const std::string configured_json =
        "{"
        "\"gadget_version\":\"0.2\","
        "\"startup_mode\":\"manual\","
        "\"transport_mode\":\"default\","
        "\"debug_logging\":true,"
        "\"interaction\":{"
        "\"type\":\"connect\","
        "\"transport\":\"default\","
        "\"on_load\":\"wait\","
        "\"host\":\"127.0.0.1\","
        "\"address\":\"127.0.0.1\","
        "\"port\":27042"
        "},"
        "\"startup_script\":{"
        "\"mode\":\"asset\","
        "\"path\":\"assets/nook-gadget/startup.js\","
        "\"required\":true,"
        "\"on_load\":\"manual\""
        "}"
        "}";

    Require(nook::gadget::ParseGadgetConfigJson(configured_json, &config),
            "configured gadget config json must parse");
    Require(config.gadget_version == "0.2",
            "configured gadget config must parse gadget_version");
    Require(config.startup_mode == "manual",
            "configured gadget config must parse startup_mode");
    Require(config.transport_mode == "default",
            "configured gadget config must parse transport_mode");
    Require(config.debug_logging,
            "configured gadget config must parse debug_logging");
    Require(config.interaction.type == "connect",
            "configured gadget config must parse interaction.type");
    Require(config.interaction.transport == "default",
            "configured gadget config must parse interaction.transport");
    Require(config.interaction.on_load == "wait",
            "configured gadget config must parse interaction.on_load");
    Require(config.interaction.host == "127.0.0.1",
            "configured gadget config must parse interaction.host");
    Require(config.interaction.address == "127.0.0.1",
            "configured gadget config must parse interaction.address");
    Require(config.interaction.port == 27042,
            "configured gadget config must parse interaction.port");
    Require(config.startup_script.enabled,
            "configured gadget config must enable startup script");
    Require(config.startup_script.mode == "asset",
            "configured gadget config must parse startup script mode");
    Require(config.startup_script.path == "assets/nook-gadget/startup.js",
            "configured gadget config must parse startup script path");
    Require(config.startup_script.required,
            "configured gadget config must parse startup script required flag");
    Require(config.startup_script.on_load == "manual",
            "configured gadget config must parse startup script on_load");

    nook::gadget::GadgetConfig roundtrip_config;
    Require(nook::gadget::ParseGadgetConfigJson(
                nook::gadget::SerializeGadgetConfigJson(config),
                &roundtrip_config),
            "serialized gadget config json must round-trip parse");
    Require(roundtrip_config.interaction.type == "connect",
            "serialized gadget config must preserve interaction.type");
    Require(roundtrip_config.interaction.on_load == "wait",
            "serialized gadget config must preserve interaction.on_load");
    Require(roundtrip_config.interaction.host == "127.0.0.1",
            "serialized gadget config must preserve interaction.host");
    Require(roundtrip_config.interaction.address == "127.0.0.1",
            "serialized gadget config must preserve interaction.address");
    Require(roundtrip_config.interaction.port == 27042,
            "serialized gadget config must preserve interaction.port");
    Require(roundtrip_config.startup_script.on_load == "manual",
            "serialized gadget config must preserve startup script on_load");

    nook::gadget::GadgetConfig noncanonical_config;
    noncanonical_config.startup_mode = "auto-start";
    noncanonical_config.transport_mode = "default";
    noncanonical_config.interaction.type = "connect";
    noncanonical_config.interaction.transport = "tcp";
    noncanonical_config.interaction.host = "127.0.0.1";
    noncanonical_config.interaction.address = "10.0.2.2";
    noncanonical_config.interaction.port = 31337;
    noncanonical_config.startup_script.enabled = true;
    noncanonical_config.startup_script.mode = "asset";
    noncanonical_config.startup_script.path = "assets/nook-gadget/startup.js";
    noncanonical_config.startup_script.on_load = "manual";

    const std::string canonical_json =
        nook::gadget::SerializeGadgetConfigJson(noncanonical_config);
    Require(canonical_json.find("\"startup_mode\":\"manual\"") != std::string::npos,
            "serialized gadget config must canonicalize startup_mode from startup_script.on_load");
    Require(canonical_json.find("\"transport_mode\":\"tcp\"") != std::string::npos,
            "serialized gadget config must canonicalize transport_mode from interaction.transport");

    Require(nook::gadget::ParseGadgetConfigJson(canonical_json, &roundtrip_config),
            "canonical serialized gadget config json must parse");
    Require(roundtrip_config.startup_mode == "manual",
            "canonical serialized gadget config must preserve derived startup_mode");
    Require(roundtrip_config.transport_mode == "tcp",
            "canonical serialized gadget config must preserve derived transport_mode");

    nook::gadget::GadgetConfig escaped_config;
    escaped_config.gadget_version = "0.2\\\"beta";
    escaped_config.interaction.type = "connect";
    escaped_config.interaction.transport = "tcp";
    escaped_config.interaction.host = "host\\\\name\\\"quoted";
    escaped_config.interaction.address = "unix-abstract:\\\\tmp\\\"sock";
    escaped_config.interaction.port = 27042;
    escaped_config.startup_script.enabled = true;
    escaped_config.startup_script.mode = "asset";
    escaped_config.startup_script.path = "assets/nook-gadget/sta\\\\rt\\\"up.js";
    escaped_config.startup_script.on_load = "manual";

    const std::string escaped_json =
        nook::gadget::SerializeGadgetConfigJson(escaped_config);
    Require(nook::gadget::ParseGadgetConfigJson(escaped_json, &roundtrip_config),
            "escaped serialized gadget config json must parse");
    Require(roundtrip_config.gadget_version == escaped_config.gadget_version,
            "serialized gadget config must round-trip escaped gadget_version");
    Require(roundtrip_config.interaction.host == escaped_config.interaction.host,
            "serialized gadget config must round-trip escaped interaction.host");
    Require(roundtrip_config.interaction.address == escaped_config.interaction.address,
            "serialized gadget config must round-trip escaped interaction.address");
    Require(roundtrip_config.startup_script.path == escaped_config.startup_script.path,
            "serialized gadget config must round-trip escaped startup script path");

    nook::gadget::GadgetConfig trailing_backslash_config;
    trailing_backslash_config.interaction.type = "connect";
    trailing_backslash_config.interaction.transport = "tcp";
    trailing_backslash_config.interaction.host = "host-ending-with-backslash\\";
    trailing_backslash_config.interaction.address = "addr-ending-with-backslash\\";
    trailing_backslash_config.interaction.port = 31337;
    trailing_backslash_config.startup_script.enabled = true;
    trailing_backslash_config.startup_script.mode = "asset";
    trailing_backslash_config.startup_script.path = "assets/nook-gadget/trailing-backslash\\";
    trailing_backslash_config.startup_script.on_load = "manual\\\\";

    const std::string trailing_backslash_json =
        nook::gadget::SerializeGadgetConfigJson(trailing_backslash_config);
    Require(nook::gadget::ParseGadgetConfigJson(trailing_backslash_json, &roundtrip_config),
            "serialized gadget config with trailing backslashes in nested strings must parse");
    Require(roundtrip_config.interaction.host == trailing_backslash_config.interaction.host,
            "serialized gadget config must round-trip nested interaction.host ending with backslash");
    Require(roundtrip_config.interaction.address == trailing_backslash_config.interaction.address,
            "serialized gadget config must round-trip nested interaction.address ending with backslash");
    Require(roundtrip_config.startup_script.path == trailing_backslash_config.startup_script.path,
            "serialized gadget config must round-trip nested startup script path ending with backslash");
    Require(roundtrip_config.startup_script.on_load == trailing_backslash_config.startup_script.on_load,
            "serialized gadget config must round-trip nested startup script on_load ending with backslash");

    const std::string explicit_even_backslash_json =
        "{\"startup_script\":{\"mode\":\"asset\",\"path\":\"assets/nook-gadget/startup.js\","
        "\"required\":false,\"on_load\":\"manual\\\\\\\\\"}}";
    Require(nook::gadget::ParseGadgetConfigJson(explicit_even_backslash_json, &roundtrip_config),
            "explicit gadget config json with even backslash run before nested closing quote must parse");
    Require(roundtrip_config.startup_script.enabled,
            "explicit gadget config json with even backslash run must still parse startup_script object");
    Require(roundtrip_config.startup_script.on_load == "manual\\\\",
            "explicit gadget config json must round-trip startup_script.on_load ending with two backslashes");

    const std::string legacy_manual_json =
        "{"
        "\"startup_mode\":\"manual\","
        "\"startup_script\":{"
        "\"mode\":\"asset\","
        "\"path\":\"assets/nook-gadget/startup.js\""
        "}"
        "}";

    Require(nook::gadget::ParseGadgetConfigJson(legacy_manual_json, &config),
            "legacy manual gadget config json must parse");
    Require(config.interaction.type == "listen",
            "legacy manual gadget config must preserve listen interaction default");
    Require(config.startup_script.enabled,
            "legacy manual gadget config must enable startup script");
    Require(config.startup_script.on_load == "manual",
            "legacy manual gadget config must derive startup script on_load from startup_mode");

    return 0;
}
