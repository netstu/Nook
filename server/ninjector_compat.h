#pragma once

#include <string>

namespace nook {
namespace server {
namespace ninjector {

int GetPid(const char* process_name);
bool InjectSoByPid(int pid, const char* so_path, const char* ready_token = nullptr);
bool InjectSoByPidWithEntry(int pid, const char* so_path, const char* init_symbol);
bool InjectEmbeddedAgentByPid(int pid,
                              const char* runtime_dir,
                              const char* ready_token = nullptr);
bool InjectEmbeddedAgentByPidSuspended(int pid, const char* runtime_dir);
bool InjectEmbeddedAgentByPidSuspendedWithSpawnContext(int pid,
                                                       const char* runtime_dir,
                                                       const char* spawn_token);
bool InjectEmbeddedZygoteAgentByPid(int pid, const char* runtime_dir);
bool InjectEmbeddedZygoteHelperByPid(int pid, const char* runtime_dir);
bool ReinitializeEmbeddedZygoteAgentByPid(int pid, const char* runtime_dir);
bool UninstallEmbeddedZygoteControlHooksByPid(int pid);
bool IsZygoteMonitorReady(int pid);
bool HasEmbeddedZygoteControlResidue(int pid);
bool SpawnViaSymbi(int zygote_pid,
                   const char* package_name,
                   const char* so_path,
                   const char* runtime_dir,
                   const char* spawn_token,
                   int* child_pid);
bool SpawnViaSymbiEmbedded(int zygote_pid,
                           const char* package_name,
                           const char* runtime_dir,
                           const char* spawn_token,
                           int* child_pid);
bool PrepareSpawnInZygoteEmbedded(int zygote_pid,
                                  const char* package_name,
                                  const char* so_path,
                                  const char* runtime_dir,
                                  const char* spawn_token);
bool ClearSpawnInZygoteEmbedded(int zygote_pid,
                                const char* runtime_dir,
                                const char* spawn_token);
bool InjectZygoteAgentByPid(int pid, const char* so_path);
bool SetZygoteSpawnControl(int zygote_pid,
                           const char* package_name,
                           const char* spawn_token,
                           bool strict_request);
bool ClearZygoteSpawnControl(int zygote_pid,
                             const char* spawn_token,
                             bool strict_request);
std::string GetLastInjectError();
bool PrepareSpawnInZygote(int zygote_pid,
                          const char* ncore_path,
                          const char* package_name,
                          const char* so_path,
                          const char* runtime_dir,
                          const char* spawn_token);
bool ClearSpawnInZygote(int zygote_pid,
                        const char* ncore_path,
                        const char* runtime_dir,
                        const char* spawn_token);
bool StartTargetApp(const char* package_name);
int WaitForSpawnCallback(const char* result_file);
std::string GetDefaultSpawnSourceProcess();
std::string GetDefaultCallbackFile();
std::string GetDefaultNcorePath();
bool IsRemoteProcess64Bit(int pid, bool* is_64_bit);

}  // namespace ninjector
}  // namespace server
}  // namespace nook
