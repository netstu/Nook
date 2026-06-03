#include "agent_runtime/script_registry.h"

#include "agent_runtime/js_runtime.h"

#include <vector>

namespace nook {
namespace agent_runtime {

namespace {

void SetError(std::string* error_message, const std::string& message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
}

}  // namespace

bool ScriptRegistry::CreateScript(const std::string& name,
                                  const std::string& source,
                                  uint32_t* script_id,
                                  std::string* error_message) {
    if (script_id == nullptr) {
        SetError(error_message, "script_id is null");
        return false;
    }

    if (!JsRuntime::Initialize(error_message)) {
        return false;
    }

    const std::string filename = name.empty() ? "<script>" : name;
    if (!JsRuntime::ValidateScript(source, filename, error_message)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t assigned_id = next_script_id_++;
    ScriptRecord record;
    record.id = assigned_id;
    record.name = filename;
    record.source = source;
    scripts_[assigned_id] = record;
    *script_id = assigned_id;
    return true;
}

bool ScriptRegistry::LoadScript(uint32_t script_id, std::string* error_message) {
    if (!JsRuntime::Initialize(error_message)) {
        return false;
    }

    std::string filename;
    std::string source;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = scripts_.find(script_id);
        if (it == scripts_.end()) {
            SetError(error_message, "script not found");
            return false;
        }
        if (it->second.loaded) {
            return true;
        }
        filename = it->second.name;
        source = it->second.source;
    }

    if (!JsRuntime::Evaluate(source, filename, script_id, error_message)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = scripts_.find(script_id);
    if (it == scripts_.end()) {
        SetError(error_message, "script not found after execution");
        return false;
    }
    it->second.loaded = true;
    return true;
}

bool ScriptRegistry::UnloadScript(uint32_t script_id, std::string* error_message) {
    if (!JsRuntime::RemoveMessageHandler(script_id, error_message)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t erased = scripts_.erase(script_id);
    if (erased == 0) {
        SetError(error_message, "script not found");
        return false;
    }
    return true;
}

void ScriptRegistry::Clear() {
    std::vector<uint32_t> script_ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        script_ids.reserve(scripts_.size());
        for (const auto& entry : scripts_) {
            script_ids.push_back(entry.first);
        }
        scripts_.clear();
    }

    for (uint32_t script_id : script_ids) {
        std::string ignored_error;
        (void)JsRuntime::RemoveMessageHandler(script_id, &ignored_error);
    }
}

}  // namespace agent_runtime
}  // namespace nook
