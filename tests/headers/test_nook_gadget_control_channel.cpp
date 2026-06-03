#include "gadget/nook_gadget_runtime.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_control_init_call_count = 0;
int g_connect_init_call_count = 0;
std::string g_last_connect_host;
int g_last_connect_port = 0;
NookStatus g_control_init_status = NOOK_STATUS_OK;
NookStatus g_connect_init_status = NOOK_STATUS_OK;

NookStatus FakeControlInitializer() {
    ++g_control_init_call_count;
    return g_control_init_status;
}

NookStatus FakeConnectInitializer(const nook::gadget::GadgetConfig& config) {
    ++g_connect_init_call_count;
    g_last_connect_host = config.interaction.host;
    g_last_connect_port = config.interaction.port;
    return g_connect_init_status;
}

void ResetState() {
    g_control_init_call_count = 0;
    g_connect_init_call_count = 0;
    g_last_connect_host.clear();
    g_last_connect_port = 0;
    g_control_init_status = NOOK_STATUS_OK;
    g_connect_init_status = NOOK_STATUS_OK;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    nook::gadget::SetConnectInitializerForTesting(&FakeConnectInitializer);

    nook::gadget::GadgetConfig default_config;
    ResetState();
    Require(
        nook::gadget::InitializeConfiguredControlChannelForTesting(
            default_config,
            &FakeControlInitializer) == NOOK_STATUS_OK,
        "default gadget config must initialize listen control successfully");
    Require(g_control_init_call_count == 0,
            "default gadget config must not eagerly initialize a listen control channel");
    Require(g_connect_init_call_count == 0,
            "default gadget config must not call the connect initializer");

    nook::gadget::GadgetConfig explicit_listen_config;
    explicit_listen_config.interaction.type = "listen";
    ResetState();
    Require(
        nook::gadget::InitializeConfiguredControlChannelForTesting(
            explicit_listen_config,
            &FakeControlInitializer) == NOOK_STATUS_OK,
        "explicit listen gadget config must initialize listen control successfully");
    Require(g_control_init_call_count == 0,
            "explicit listen gadget config must stay passive until a host attaches");

    nook::gadget::GadgetConfig unsupported_transport_config;
    unsupported_transport_config.transport_mode = "tcp";
    unsupported_transport_config.interaction.transport = "tcp";
    ResetState();
    Require(
        nook::gadget::InitializeConfiguredControlChannelForTesting(
            unsupported_transport_config,
            &FakeControlInitializer) == NOOK_STATUS_INVALID_ARGUMENT,
        "unsupported transport mode must fail gadget control initialization");
    Require(g_control_init_call_count == 0,
            "unsupported transport mode must fail before listen control init");

    nook::gadget::GadgetConfig unsupported_interaction_config;
    unsupported_interaction_config.interaction.type = "pipe";
    ResetState();
    Require(
        nook::gadget::InitializeConfiguredControlChannelForTesting(
            unsupported_interaction_config,
            &FakeControlInitializer) == NOOK_STATUS_INVALID_ARGUMENT,
        "unsupported interaction type must fail gadget control initialization");
    Require(g_control_init_call_count == 0,
            "unsupported interaction type must fail before listen control init");
    Require(g_connect_init_call_count == 0,
            "unsupported interaction type must fail before connect init");

    nook::gadget::GadgetConfig connect_config;
    connect_config.interaction.type = "connect";
    connect_config.interaction.host = "127.0.0.1";
    connect_config.interaction.port = 27042;
    ResetState();
    Require(
        nook::gadget::InitializeConfiguredControlChannelForTesting(
            connect_config,
            &FakeControlInitializer) == NOOK_STATUS_OK,
        "connect gadget config must initialize outbound control successfully");
    Require(g_control_init_call_count == 0,
            "connect gadget config must bypass the listen control initializer");
    Require(g_connect_init_call_count == 1,
            "connect gadget config must call the connect initializer once");
    Require(g_last_connect_host == "127.0.0.1" && g_last_connect_port == 27042,
            "connect gadget config must forward the configured endpoint");

    nook::gadget::GadgetConfig invalid_connect_config;
    invalid_connect_config.interaction.type = "connect";
    invalid_connect_config.interaction.port = 27042;
    ResetState();
    Require(
        nook::gadget::InitializeConfiguredControlChannelForTesting(
            invalid_connect_config,
            &FakeControlInitializer) == NOOK_STATUS_INVALID_ARGUMENT,
        "connect gadget config without host must fail");
    Require(g_connect_init_call_count == 0,
            "invalid connect gadget config must fail before connect init");

    nook::gadget::GadgetConfig fallback_connect_host_config;
    fallback_connect_host_config.interaction.type = "connect";
    fallback_connect_host_config.interaction.address = "10.0.2.2";
    fallback_connect_host_config.interaction.port = 31337;
    ResetState();
    Require(
        nook::gadget::InitializeConfiguredControlChannelForTesting(
            fallback_connect_host_config,
            &FakeControlInitializer) == NOOK_STATUS_OK,
        "connect gadget config must accept address as host fallback");
    Require(g_connect_init_call_count == 1,
            "address fallback connect gadget config must call the connect initializer once");

    nook::gadget::ResetConnectInitializerForTesting();
    return 0;
}
