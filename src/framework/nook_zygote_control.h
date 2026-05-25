#pragma once

#include "../communication/protocol/messages.h"
#include "nook/Nook.h"

namespace nook {
namespace framework {

NookStatus NookZygoteMonitorInitialize();
NookStatus NookZygoteMonitorReinitialize();
comm::SpawnInstallResponse HandleSpawnInstallRequest(const comm::SpawnInstallRequest& request);
comm::SpawnUninstallResponse HandleSpawnUninstallRequest(const comm::SpawnUninstallRequest& request,
                                                         bool uninstall_hooks);

}  // namespace framework
}  // namespace nook
