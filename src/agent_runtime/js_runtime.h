#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "agent_runtime/nook_native_js_bridge.h"

namespace nook {
namespace agent_runtime {

class JsRuntime {
public:
    using SendCallback = std::function<bool(const std::string&, const std::vector<uint8_t>&)>;

    static bool Initialize(std::string* error_message = nullptr);
    static void Shutdown();
    static bool IsInitialized();

    static void SetSendCallback(SendCallback callback);
    static bool DispatchMessage(uint32_t script_id,
                                const std::string& message_json,
                                const std::vector<uint8_t>& data = {},
                                std::string* error_message = nullptr);
    static bool DispatchPendingNativeHookEvents(std::string* error_message = nullptr);
    static bool InvokeNativeHookCallbackSync(const HookEvent& event,
                                             HookInvocationMutationResult* mutation_result,
                                             std::string* error_message = nullptr);
    static bool NotifyModuleLoaded(const char* module_path, std::string* error_message = nullptr);
    static bool RemoveMessageHandler(uint32_t script_id, std::string* error_message = nullptr);

    static bool ValidateScript(const std::string& source,
                               const std::string& filename,
                               std::string* error_message = nullptr);
    static bool Evaluate(const std::string& source,
                         const std::string& filename,
                         uint32_t script_id = 0,
                         std::string* error_message = nullptr);
    static bool DispatchJavaReadyCallbacks(std::string* error_message = nullptr);
    static bool PumpPendingTasks(std::string* error_message = nullptr);
    static bool CallRpc(uint32_t script_id,
                        const std::string& method,
                        const std::string& args_json,
                        std::string* result_json,
                        std::string* error_message = nullptr);
};

}  // namespace agent_runtime
}  // namespace nook
