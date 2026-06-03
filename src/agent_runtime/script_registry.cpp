#include "agent_runtime/script_registry.h"

#include "agent_runtime/js_runtime.h"

#include <atomic>
#include <algorithm>
#include <vector>

namespace nook {
namespace agent_runtime {

namespace {

void SetError(std::string* error_message, const std::string& message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
}

std::atomic<uint32_t>& GlobalNextScriptId() {
    static std::atomic<uint32_t> next_script_id{1u};
    return next_script_id;
}

}  // namespace

ScriptRegistry::ScriptRegistry() {
    scripts_.reserve(64);
}

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
    const uint32_t assigned_id = GlobalNextScriptId().fetch_add(1u, std::memory_order_relaxed);
    ScriptRecord record;
    record.id = assigned_id;
    record.name = filename;
    record.source = source;
    scripts_.push_back(std::move(record));
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
        auto it = std::find_if(scripts_.begin(),
                               scripts_.end(),
                               [script_id](const ScriptRecord& record) {
                                   return record.id == script_id;
                               });
        if (it == scripts_.end()) {
            SetError(error_message, "script not found");
            return false;
        }
        if (it->loaded) {
            return true;
        }
        filename = it->name;
        source = it->source;
    }

    if (!JsRuntime::Evaluate(source, filename, script_id, error_message)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(scripts_.begin(),
                           scripts_.end(),
                           [script_id](const ScriptRecord& record) {
                               return record.id == script_id;
                           });
    if (it == scripts_.end()) {
        SetError(error_message, "script not found after execution");
        return false;
    }
    it->loaded = true;
    return true;
}

bool ScriptRegistry::UnloadScript(uint32_t script_id, std::string* error_message) {
    if (!JsRuntime::RemoveMessageHandler(script_id, error_message)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(scripts_.begin(),
                           scripts_.end(),
                           [script_id](const ScriptRecord& record) {
                               return record.id == script_id;
                           });
    if (it == scripts_.end()) {
        SetError(error_message, "script not found");
        return false;
    }
    scripts_.erase(it);
    return true;
}

void ScriptRegistry::Clear() {
    std::vector<uint32_t> script_ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        script_ids.reserve(scripts_.size());
        for (const auto& entry : scripts_) {
            script_ids.push_back(entry.id);
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
