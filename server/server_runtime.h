#pragma once

#include "../src/communication/io/io_loop.h"
#include "session_registry.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace nook {
namespace server {

enum class EmbeddedFileMaterializationResult {
    kReused,
    kCreated,
    kReplaced,
    kPreserved,
    kError,
};

inline bool RunIoLoopUntilStop(comm::IoLoop* io_loop,
                               const std::function<void()>& wait_for_shutdown) {
    if (io_loop == nullptr) {
        return false;
    }

    if (!io_loop->Start()) {
        return false;
    }

    if (wait_for_shutdown) {
        wait_for_shutdown();
    }

    io_loop->Stop();
    return true;
}
std::string DetectCurrentExecutablePath();
std::string ResolveRuntimeDirectoryFromEnvironmentAndExecutable(const char* env_runtime_dir,
                                                                const char* executable_path);
std::string ResolveRuntimeDirectoryFromEnvironmentAgentAndExecutable(const char* env_runtime_dir,
                                                                     const char* env_agent_path,
                                                                     const char* executable_path);
std::string BuildSocketPathFromRuntimeDirectory(const std::string& runtime_dir);
std::string ResolveNcorePathFromRuntimeDirectory(const std::string& runtime_dir);
std::string ResolveNcorePathFromEnvironmentAndRuntimeDirectory(const char* env_ncore_path,
                                                               const std::string& runtime_dir);
std::string ResolveAgentPathFromEnvironmentAndExecutable(const char* env_agent_path,
                                                         const char* executable_path);
std::string BuildEmbeddedSharedObjectPathForRuntimeDirectory(const std::string& runtime_dir,
                                                             const std::string& file_name);
std::string BuildEmbeddedAgentPathForRuntimeDirectory(const std::string& runtime_dir,
                                                      const uint8_t* data,
                                                      size_t size);
std::string BuildEmbeddedNcorePathForRuntimeDirectory(const std::string& runtime_dir,
                                                      const uint8_t* data,
                                                      size_t size);
comm::Session* ResolveSpawnGateAgentSession(SessionRegistry* registry, int pid);
bool CleanupEmbeddedAgentArtifacts(const std::string& runtime_dir,
                                   const std::string& keep_agent_path,
                                   std::string* error);
bool CleanupEmbeddedNcoreArtifacts(const std::string& runtime_dir,
                                   const std::string& keep_ncore_path,
                                   std::string* error);
bool RemoveFileIfExists(const std::string& path, std::string* error);
bool EnsureEmbeddedFileAtPath(const char* output_path,
                              const uint8_t* data,
                              size_t size,
                              EmbeddedFileMaterializationResult* result,
                              std::string* error);
bool EnsureEmbeddedFileAtPathIfMissing(const char* output_path,
                                       const uint8_t* data,
                                       size_t size,
                                       EmbeddedFileMaterializationResult* result,
                                       std::string* error);

}  // namespace server
}  // namespace nook
