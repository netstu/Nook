#include "framework/NookZygoteSpawn.h"

#include <cstddef>

namespace nook {
namespace framework {

namespace {

void SetError(std::string* error_message, const char* message) {
    if (error_message != nullptr) {
        *error_message = (message != nullptr) ? message : "";
    }
}

bool ExtractJsonStringField(const std::string& json,
                            const char* field_name,
                            std::string* value) {
    if (field_name == nullptr || value == nullptr) {
        return false;
    }

    const std::string needle = std::string("\"") + field_name + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }

    const std::size_t colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return false;
    }

    const std::size_t quote_start = json.find('"', colon_pos + 1);
    if (quote_start == std::string::npos) {
        return false;
    }

    const std::size_t quote_end = json.find('"', quote_start + 1);
    if (quote_end == std::string::npos) {
        return false;
    }

    *value = json.substr(quote_start + 1, quote_end - quote_start - 1);
    return true;
}

}  // namespace

ZygoteSpawnController::ZygoteSpawnController()
    : state_(ZygoteSpawnState::kIdle) {}

ZygoteSpawnState ZygoteSpawnController::state() const {
    return state_;
}

const std::string& ZygoteSpawnController::target_package() const {
    return target_package_;
}

const std::string& ZygoteSpawnController::spawn_token() const {
    return spawn_token_;
}

bool ZygoteSpawnController::Install(const std::string& target_package,
                                    const std::string& spawn_token,
                                    std::string* error_message) {
    if (state_ != ZygoteSpawnState::kIdle) {
        SetError(error_message, "zygote spawn controller already armed");
        return false;
    }
    if (target_package.empty()) {
        SetError(error_message, "target package is empty");
        return false;
    }
    if (spawn_token.empty()) {
        SetError(error_message, "spawn token is empty");
        return false;
    }

    target_package_ = target_package;
    spawn_token_ = spawn_token;
    state_ = ZygoteSpawnState::kArmed;
    SetError(error_message, "");
    return true;
}

bool ZygoteSpawnController::Consume(const std::string& spawn_token,
                                    std::string* error_message) {
    if (state_ != ZygoteSpawnState::kArmed) {
        SetError(error_message, "zygote spawn controller is not armed");
        return false;
    }
    if (spawn_token != spawn_token_) {
        SetError(error_message, "spawn token mismatch");
        return false;
    }

    state_ = ZygoteSpawnState::kConsumed;
    SetError(error_message, "");
    return true;
}

void ZygoteSpawnController::Uninstall() {
    state_ = ZygoteSpawnState::kIdle;
    target_package_.clear();
    spawn_token_.clear();
}

const char* ToString(ZygoteSpawnState state) {
    switch (state) {
        case ZygoteSpawnState::kIdle:
            return "idle";
        case ZygoteSpawnState::kArmed:
            return "armed";
        case ZygoteSpawnState::kConsumed:
            return "consumed";
        default:
            return "unknown";
    }
}

bool ParseSpawnInstallArgsJson(const std::string& args_json,
                               std::string* target_package,
                               std::string* spawn_token,
                               std::string* mode,
                               std::string* error_message) {
    if (target_package == nullptr || spawn_token == nullptr || mode == nullptr) {
        SetError(error_message, "spawn install parse outputs are null");
        return false;
    }

    target_package->clear();
    spawn_token->clear();
    mode->clear();

    if (!ExtractJsonStringField(args_json, "target_package", target_package) ||
        !ExtractJsonStringField(args_json, "spawn_token", spawn_token) ||
        !ExtractJsonStringField(args_json, "mode", mode)) {
        SetError(error_message, "invalid spawn install args json");
        return false;
    }

    SetError(error_message, "");
    return true;
}

std::string BuildZygoteSpawnStatusJson(bool ready, const ZygoteSpawnController& controller) {
    return std::string("{\"ready\":") +
           (ready ? "true" : "false") +
           ",\"state\":\"" + ToString(controller.state()) +
           "\",\"target_package\":\"" + controller.target_package() +
           "\",\"spawn_token\":\"" + controller.spawn_token() +
           "\"}";
}

bool MatchSpawnTargetAndToken(const std::string& target_package,
                              const std::string& nice_name,
                              const std::string& spawn_token,
                              std::string* error_message) {
    if (target_package.empty()) {
        SetError(error_message, "target package is empty");
        return false;
    }
    if (nice_name.empty()) {
        SetError(error_message, "nice name is empty");
        return false;
    }
    if (spawn_token.empty()) {
        SetError(error_message, "spawn token is empty");
        return false;
    }
    if (nice_name != target_package) {
        SetError(error_message, "nice name does not match target package");
        return false;
    }

    SetError(error_message, "");
    return true;
}

bool MatchesArmedTargetPackage(const ZygoteSpawnController* controller,
                               const std::string& nice_name,
                               std::string* error_message) {
    if (controller == nullptr) {
        SetError(error_message, "zygote spawn controller is null");
        return false;
    }
    if (nice_name.empty()) {
        SetError(error_message, "nice name is empty");
        return false;
    }
    if (controller->state() != ZygoteSpawnState::kArmed) {
        SetError(error_message, "zygote spawn controller is not armed");
        return false;
    }

    return MatchSpawnTargetAndToken(controller->target_package(),
                                    nice_name,
                                    controller->spawn_token(),
                                    error_message);
}

bool GetArmedSpawnTokenForNiceName(const ZygoteSpawnController* controller,
                                   const std::string& nice_name,
                                   std::string* spawn_token,
                                   std::string* error_message) {
    if (!MatchesArmedTargetPackage(controller, nice_name, error_message)) {
        return false;
    }

    if (spawn_token != nullptr) {
        *spawn_token = controller->spawn_token();
    }
    SetError(error_message, "");
    return true;
}

bool GetActiveSpawnSnapshot(const ZygoteSpawnController* controller,
                            std::string* target_package,
                            std::string* spawn_token,
                            std::string* error_message) {
    if (controller == nullptr) {
        SetError(error_message, "zygote spawn controller is null");
        return false;
    }

    if (controller->state() == ZygoteSpawnState::kIdle) {
        SetError(error_message, "zygote spawn controller is idle");
        return false;
    }

    if (target_package != nullptr) {
        *target_package = controller->target_package();
    }
    if (spawn_token != nullptr) {
        *spawn_token = controller->spawn_token();
    }
    SetError(error_message, "");
    return true;
}

bool IsCompatibilitySpawnFallbackAllowed(const ZygoteSpawnController* controller,
                                         std::string* error_message) {
    if (controller == nullptr) {
        SetError(error_message, "zygote spawn controller is null");
        return false;
    }

    if (controller->state() != ZygoteSpawnState::kIdle) {
        SetError(error_message, "zygote spawn controller owns active transaction");
        return false;
    }

    SetError(error_message, "");
    return true;
}

bool TryConsumeForNiceName(ZygoteSpawnController* controller,
                           const std::string& nice_name,
                           std::string* spawn_token,
                           std::string* error_message) {
    std::string token;
    if (!GetArmedSpawnTokenForNiceName(controller, nice_name, &token, error_message)) {
        return false;
    }

    if (!controller->Consume(token, error_message)) {
        return false;
    }
    if (spawn_token != nullptr) {
        *spawn_token = token;
    }
    SetError(error_message, "");
    return true;
}

}  // namespace framework
}  // namespace nook
