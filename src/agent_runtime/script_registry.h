#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace nook {
namespace agent_runtime {

class ScriptRegistry {
public:
    ScriptRegistry();
    bool CreateScript(const std::string& name,
                      const std::string& source,
                      uint32_t* script_id,
                      std::string* error_message);
    bool LoadScript(uint32_t script_id, std::string* error_message);
    bool UnloadScript(uint32_t script_id, std::string* error_message);
    void Clear();

private:
    struct ScriptRecord {
        uint32_t id = 0;
        std::string name;
        std::string source;
        bool loaded = false;
    };

    mutable std::mutex mutex_;
    std::vector<ScriptRecord> scripts_;
};

}  // namespace agent_runtime
}  // namespace nook
