#pragma once

#include <string>

namespace nook {
namespace comm {

std::string GetDefaultSpawnMarkerDirectory();
std::string SanitizeSpawnMarkerName(const std::string& name);
bool LooksLikeEarlySpawnProcessName(const std::string& name);
std::string BuildSpawnMarkerPath(const std::string& process_name,
                                 const std::string& base_dir = GetDefaultSpawnMarkerDirectory());
bool HasSpawnMarker(const std::string& process_name,
                    const std::string& base_dir = GetDefaultSpawnMarkerDirectory());
bool CreateSpawnMarker(const std::string& process_name,
                       const std::string& base_dir = GetDefaultSpawnMarkerDirectory());
bool ConsumeSpawnMarker(const std::string& process_name,
                        const std::string& base_dir = GetDefaultSpawnMarkerDirectory());
bool FindSinglePendingSpawnMarker(std::string* process_name,
                                  const std::string& base_dir = GetDefaultSpawnMarkerDirectory());
bool ConsumeSinglePendingSpawnMarker(std::string* process_name,
                                     const std::string& base_dir = GetDefaultSpawnMarkerDirectory());
bool ConsumeAnySingleSpawnMarker(const std::string& base_dir = GetDefaultSpawnMarkerDirectory());
bool RemoveSpawnMarker(const std::string& process_name,
                       const std::string& base_dir = GetDefaultSpawnMarkerDirectory());

}  // namespace comm
}  // namespace nook
