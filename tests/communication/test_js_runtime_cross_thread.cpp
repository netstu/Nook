#include <cassert>
#include <string>
#include <thread>

#include "agent_runtime/js_runtime.h"

using namespace nook::agent_runtime;

int main() {
    std::string error_message;
    assert(JsRuntime::Initialize(&error_message));
    assert(JsRuntime::ValidateScript("send({ type: 'send', payload: 'main-thread' })",
                                     "main.js",
                                     &error_message));

    bool worker_ok = false;
    std::string worker_error;
    std::thread worker([&]() {
        worker_ok = JsRuntime::ValidateScript("send({ type: 'send', payload: 'worker-thread' })",
                                              "worker.js",
                                              &worker_error);
    });
    worker.join();

    JsRuntime::Shutdown();
    return worker_ok ? 0 : 1;
}
