#pragma once

#include "nook/Nook.h"

namespace nook {
namespace agent_runtime {

NookStatus NookScriptRuntimeBridgeInitialize();
void NookScriptRuntimeBridgeShutdown();

}  // namespace agent_runtime
}  // namespace nook
