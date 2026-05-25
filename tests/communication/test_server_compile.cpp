#include "communication/handler/message_dispatcher.h"
#include "communication/io/io_loop.h"
#include "server/injector.h"
#include "server/ninjector_spawn_injector.h"
#include "server/process_manager.h"
#include "server/server_handlers.h"
#include "server/session_registry.h"

int main() {
    nook::comm::IoLoop loop;
    nook::comm::MessageDispatcher dispatcher;
    nook::server::SessionRegistry registry;
    nook::server::StubInjector injector;
    nook::server::NinjectorSpawnInjector ninjector_injector;
    nook::server::ProcessManager process_manager;
    nook::server::ServerHandlerConfig config;

    (void)loop;
    (void)dispatcher;
    (void)registry;
    (void)injector;
    (void)ninjector_injector;
    (void)process_manager;
    (void)config;
    return 0;
}
