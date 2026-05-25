#include "server_runtime.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>

#if !defined(_WIN32)
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <process.h>
#endif

namespace nook {
namespace server {

namespace {

namespace fs = std::filesystem;

constexpr const char* kLegacyDefaultRuntimeDirectory = "/data/local/tmp/nook";
constexpr const char* kLegacyDefaultAgentPath = "/data/local/tmp/nook/libnook-agent.so";

std::string DirnameOf(const std::string& path) {
    const std::string::size_type slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return std::string();
    }
    if (slash == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash);
}

#if !defined(_WIN32)
bool IsAbsolutePath(const std::string& path) {
    return !path.empty() && path[0] == '/';
}

std::string NormalizeAbsolutePath(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    std::error_code ec;
    const fs::path normalized = fs::path(path).lexically_normal();
    if (ec) {
        return path;
    }
    return normalized.string();
}

std::string MakeAbsolutePath(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    if (IsAbsolutePath(path)) {
        return NormalizeAbsolutePath(path);
    }

    char cwd[PATH_MAX] = {};
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        return path;
    }

    std::string absolute(cwd);
    if (!absolute.empty() && absolute.back() != '/') {
        absolute.push_back('/');
    }
    absolute += path;
    return NormalizeAbsolutePath(absolute);
}
#endif

std::vector<uint8_t> ReadAllBytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
}

std::string MakeTemporarySiblingPath(const std::string& output_path) {
#if defined(_WIN32)
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    std::ostringstream stream;
    stream << output_path << ".tmp." << pid;
    return stream.str();
}

bool WriteAllBytes(const std::string& path, const uint8_t* data, size_t size, std::string* error) {
    std::error_code ec;
    const fs::path output_path(path);
    const fs::path parent = output_path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            if (error != nullptr) {
                *error = "create_directories failed: " + parent.string();
            }
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error != nullptr) {
            *error = "open failed: " + path;
        }
        return false;
    }

    if (size > 0) {
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!output) {
            if (error != nullptr) {
                *error = "write failed: " + path;
            }
            return false;
        }
    }

    output.close();
    if (!output) {
        if (error != nullptr) {
            *error = "close failed: " + path;
        }
        return false;
    }

    return true;
}

bool ReplaceFileWithTemporary(const std::string& temporary_path,
                              const std::string& output_path,
                              std::string* error) {
#if defined(_WIN32)
    std::remove(output_path.c_str());
#endif
    if (std::rename(temporary_path.c_str(), output_path.c_str()) != 0) {
        if (error != nullptr) {
            *error = "rename failed: " + temporary_path + " -> " + output_path;
        }
        std::remove(temporary_path.c_str());
        return false;
    }
    return true;
}

#if !defined(_WIN32)
bool SetEmbeddedSharedObjectMode(const std::string& path, std::string* error) {
    if (::chmod(path.c_str(), 0755) != 0) {
        if (error != nullptr) {
            *error = "chmod failed: " + path;
        }
        return false;
    }
    return true;
}
#endif

std::string BuildDefaultAbstractSocketNameForRuntimeDirectory(const std::string& runtime_dir) {
#if defined(__ANDROID__)
    const std::string& seed = runtime_dir.empty()
        ? std::string(kLegacyDefaultRuntimeDirectory)
        : runtime_dir;
    uint32_t hash = 2166136261u;
    for (unsigned char ch : seed) {
        hash ^= ch;
        hash *= 16777619u;
    }
    char name[64] = {};
    std::snprintf(name, sizeof(name), "@nook-%08x.sock", hash);
    return std::string(name);
#else
    return "/tmp/nook.sock";
#endif
}

}  // namespace

std::string DetectCurrentExecutablePath() {
#if defined(_WIN32)
    return std::string();
#else
    char buffer[4096] = {};
    const ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (size <= 0) {
        return std::string();
    }
    buffer[size] = '\0';
    return std::string(buffer, static_cast<size_t>(size));
#endif
}

std::string ResolveRuntimeDirectoryFromEnvironmentAndExecutable(const char* env_runtime_dir,
                                                                const char* executable_path) {
    if (env_runtime_dir != nullptr && env_runtime_dir[0] != '\0') {
        return std::string(env_runtime_dir);
    }

    if (executable_path != nullptr && executable_path[0] != '\0') {
        std::string executable(executable_path);
#if !defined(_WIN32)
        executable = MakeAbsolutePath(executable);
#endif
        const std::string directory = DirnameOf(executable);
        if (!directory.empty()) {
            return directory;
        }
    }

    return std::string(kLegacyDefaultRuntimeDirectory);
}

std::string ResolveRuntimeDirectoryFromEnvironmentAgentAndExecutable(const char* env_runtime_dir,
                                                                     const char* env_agent_path,
                                                                     const char* executable_path) {
    if (env_runtime_dir != nullptr && env_runtime_dir[0] != '\0') {
        return std::string(env_runtime_dir);
    }

    if (env_agent_path != nullptr && env_agent_path[0] != '\0') {
        const std::string agent_path(env_agent_path);
        const std::string directory = DirnameOf(agent_path);
        if (!directory.empty()) {
            return directory;
        }
    }

    return ResolveRuntimeDirectoryFromEnvironmentAndExecutable(nullptr, executable_path);
}

std::string BuildSocketPathFromRuntimeDirectory(const std::string& runtime_dir) {
    return BuildDefaultAbstractSocketNameForRuntimeDirectory(runtime_dir);
}

std::string ResolveNcorePathFromRuntimeDirectory(const std::string& runtime_dir) {
    if (runtime_dir.empty()) {
        return "/data/local/tmp/nook/libncore.so";
    }
    return runtime_dir + "/libncore.so";
}

std::string ResolveNcorePathFromEnvironmentAndRuntimeDirectory(const char* env_ncore_path,
                                                               const std::string& runtime_dir) {
    if (env_ncore_path != nullptr && env_ncore_path[0] != '\0') {
        return std::string(env_ncore_path);
    }

    return ResolveNcorePathFromRuntimeDirectory(runtime_dir);
}

std::string ResolveAgentPathFromEnvironmentAndExecutable(const char* env_agent_path,
                                                         const char* executable_path) {
    if (env_agent_path != nullptr && env_agent_path[0] != '\0') {
        return std::string(env_agent_path);
    }

    const std::string runtime_dir =
        ResolveRuntimeDirectoryFromEnvironmentAndExecutable(nullptr, executable_path);
    if (!runtime_dir.empty()) {
        return runtime_dir + "/libnook-agent.so";
    }

    return std::string(kLegacyDefaultAgentPath);
}

std::string BuildEmbeddedSharedObjectPathForRuntimeDirectory(const std::string& runtime_dir,
                                                             const std::string& file_name) {
    const std::string base_dir = runtime_dir.empty()
        ? std::string(kLegacyDefaultRuntimeDirectory)
        : runtime_dir;
    return base_dir + "/" + file_name;
}

std::string BuildEmbeddedAgentPathForRuntimeDirectory(const std::string& runtime_dir,
                                                      const uint8_t* data,
                                                      size_t size) {
    (void)data;
    (void)size;

    return BuildEmbeddedSharedObjectPathForRuntimeDirectory(runtime_dir, "libnook-agent.so");
}

std::string BuildEmbeddedNcorePathForRuntimeDirectory(const std::string& runtime_dir,
                                                      const uint8_t* data,
                                                      size_t size) {
    (void)data;
    (void)size;

    return BuildEmbeddedSharedObjectPathForRuntimeDirectory(runtime_dir, "libncore.so");
}

comm::Session* ResolveSpawnGateAgentSession(SessionRegistry* registry, int pid) {
    if (registry == nullptr || pid <= 0) {
        return nullptr;
    }

    SpawnSuspendedEntry entry;
    if (registry->GetSpawnSuspendedEntry(pid, &entry) && entry.suspended) {
        if (entry.authoritative_ready_stage == PendingSpawnReadyStage::kRuntimeReady) {
            // Gate release happens after script-create/load has already succeeded through the
            // currently authoritative agent session. Prefer that live session directly instead of
            // re-resolving through stricter runtime-identity matching, which can lag on the
            // legacy early-spawn path and cause a false resume timeout.
            comm::Session* authoritative = registry->FindAuthoritativeAgentSessionByPid(pid);
            if (authoritative != nullptr) {
                return authoritative;
            }
        }

        if (!entry.authoritative_process_name.empty()) {
            comm::Session* control_by_identity =
                registry->FindControlReadyAgentSessionByIdentity(
                    pid,
                    entry.authoritative_process_name);
            if (control_by_identity != nullptr) {
                return control_by_identity;
            }
        }

        comm::Session* authoritative = registry->FindAuthoritativeAgentSessionByPid(pid);
        if (authoritative != nullptr) {
            return authoritative;
        }

        return registry->FindControlReadyAgentSessionByPid(pid);
    }

    comm::Session* agent = registry->FindAuthoritativeAgentSessionByPid(pid);
    if (agent != nullptr) {
        return agent;
    }

    return registry->FindControlReadyAgentSessionByPid(pid);
}

namespace {

bool CleanupEmbeddedArtifactsByPrefix(const std::string& runtime_dir,
                                      const std::string& keep_path,
                                      const char* exact_name,
                                      const char* prefix,
                                      std::string* error) {
    if (error != nullptr) {
        error->clear();
    }

    if (runtime_dir.empty()) {
        return true;
    }

    std::error_code ec;
    if (!fs::exists(runtime_dir, ec)) {
        if (!ec) {
            return true;
        }
        if (error != nullptr) {
            *error = "exists failed: " + runtime_dir;
        }
        return false;
    }

    const std::string keep_name = keep_path.substr(keep_path.find_last_of("/\\") + 1);
    bool ok = true;
    for (const fs::directory_entry& entry : fs::directory_iterator(runtime_dir, ec)) {
        if (ec) {
            ok = false;
            if (error != nullptr && error->empty()) {
                *error = "directory iteration failed: " + runtime_dir;
            }
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name == keep_name) {
            continue;
        }
        if (name == exact_name ||
            name.rfind(prefix, 0) == 0) {
            const std::string full_path = entry.path().string();
            if (!RemoveFileIfExists(full_path, error)) {
                ok = false;
            }
        }
    }

    return ok;
}

}  // namespace

bool CleanupEmbeddedNcoreArtifacts(const std::string& runtime_dir,
                                   const std::string& keep_ncore_path,
                                   std::string* error) {
    return CleanupEmbeddedArtifactsByPrefix(runtime_dir,
                                            keep_ncore_path,
                                            "libncore.so",
                                            "libncore-",
                                            error);
}

bool CleanupEmbeddedAgentArtifacts(const std::string& runtime_dir,
                                   const std::string& keep_agent_path,
                                   std::string* error) {
#if defined(_WIN32)
    (void)runtime_dir;
    (void)keep_agent_path;
    if (error != nullptr) {
        error->clear();
    }
    return true;
#else
    return CleanupEmbeddedArtifactsByPrefix(runtime_dir,
                                            keep_agent_path,
                                            "libnook-agent.so",
                                            "libnook-agent-",
                                            error);
#endif
}

bool RemoveFileIfExists(const std::string& path, std::string* error) {
#if defined(_WIN32)
    const int rc = std::remove(path.c_str());
    if (rc != 0 && errno != ENOENT) {
        if (error != nullptr) {
            *error = "remove failed: " + path;
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
#else
    if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
        if (error != nullptr) {
            *error = "remove failed: " + path;
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
#endif
}

bool EnsureEmbeddedFileAtPath(const char* output_path,
                              const uint8_t* data,
                              size_t size,
                              EmbeddedFileMaterializationResult* result,
                              std::string* error) {
    if (result != nullptr) {
        *result = EmbeddedFileMaterializationResult::kError;
    }
    if (error != nullptr) {
        error->clear();
    }

    if (output_path == nullptr || output_path[0] == '\0') {
        if (error != nullptr) {
            *error = "output path is empty";
        }
        return false;
    }

    if (data == nullptr && size != 0) {
        if (error != nullptr) {
            *error = "embedded data is null";
        }
        return false;
    }

    const std::string output(output_path);
    const std::vector<uint8_t> current = ReadAllBytes(output);
    const bool existed = !current.empty() || std::ifstream(output, std::ios::binary).good();
    if (current.size() == size &&
        (size == 0 || std::equal(current.begin(), current.end(), data))) {
        if (result != nullptr) {
            *result = EmbeddedFileMaterializationResult::kReused;
        }
        return true;
    }

    const std::string temporary_path = MakeTemporarySiblingPath(output);
    if (!WriteAllBytes(temporary_path, data, size, error)) {
        return false;
    }

    if (!ReplaceFileWithTemporary(temporary_path, output, error)) {
        return false;
    }

#if !defined(_WIN32)
    const std::string::size_type slash = output.find_last_of("/\\");
    const std::string file_name = (slash == std::string::npos) ? output : output.substr(slash + 1);
    if (file_name.size() >= 3 &&
        file_name.rfind(".so") == file_name.size() - 3 &&
        !SetEmbeddedSharedObjectMode(output, error)) {
        return false;
    }
#endif

    if (result != nullptr) {
        *result = existed ? EmbeddedFileMaterializationResult::kReplaced
                          : EmbeddedFileMaterializationResult::kCreated;
    }
    return true;
}

bool EnsureEmbeddedFileAtPathIfMissing(const char* output_path,
                                       const uint8_t* data,
                                       size_t size,
                                       EmbeddedFileMaterializationResult* result,
                                       std::string* error) {
    if (result != nullptr) {
        *result = EmbeddedFileMaterializationResult::kError;
    }
    if (error != nullptr) {
        error->clear();
    }

    if (output_path == nullptr || output_path[0] == '\0') {
        if (error != nullptr) {
            *error = "output path is empty";
        }
        return false;
    }

    if (data == nullptr && size != 0) {
        if (error != nullptr) {
            *error = "embedded data is null";
        }
        return false;
    }

    const std::string output(output_path);
    if (std::ifstream(output, std::ios::binary).good()) {
        if (result != nullptr) {
            *result = EmbeddedFileMaterializationResult::kPreserved;
        }
        return true;
    }

    return EnsureEmbeddedFileAtPath(output_path, data, size, result, error);
}

}  // namespace server
}  // namespace nook
