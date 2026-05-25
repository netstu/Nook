#pragma once

#include <string>

namespace nook {
namespace framework {

enum class ZygoteSpawnState {
    kIdle = 0,
    kArmed,
    kConsumed,
};

class ZygoteSpawnController {
public:
    ZygoteSpawnController();

    ZygoteSpawnState state() const;
    const std::string& target_package() const;
    const std::string& spawn_token() const;

    bool Install(const std::string& target_package,
                 const std::string& spawn_token,
                 std::string* error_message);
    bool Consume(const std::string& spawn_token, std::string* error_message);
    void Uninstall();

private:
    ZygoteSpawnState state_;
    std::string target_package_;
    std::string spawn_token_;
};

const char* ToString(ZygoteSpawnState state);

bool ParseSpawnInstallArgsJson(const std::string& args_json,
                               std::string* target_package,
                               std::string* spawn_token,
                               std::string* mode,
                               std::string* error_message);

std::string BuildZygoteSpawnStatusJson(bool ready, const ZygoteSpawnController& controller);

bool MatchSpawnTargetAndToken(const std::string& target_package,
                              const std::string& nice_name,
                              const std::string& spawn_token,
                              std::string* error_message);

bool MatchesArmedTargetPackage(const ZygoteSpawnController* controller,
                               const std::string& nice_name,
                               std::string* error_message);

bool GetArmedSpawnTokenForNiceName(const ZygoteSpawnController* controller,
                                   const std::string& nice_name,
                                   std::string* spawn_token,
                                   std::string* error_message);

bool GetActiveSpawnSnapshot(const ZygoteSpawnController* controller,
                            std::string* target_package,
                            std::string* spawn_token,
                            std::string* error_message);

bool IsCompatibilitySpawnFallbackAllowed(const ZygoteSpawnController* controller,
                                         std::string* error_message);

bool TryConsumeForNiceName(ZygoteSpawnController* controller,
                           const std::string& nice_name,
                           std::string* spawn_token,
                           std::string* error_message);

}  // namespace framework
}  // namespace nook
