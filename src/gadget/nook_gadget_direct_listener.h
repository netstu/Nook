#pragma once

#include "gadget/nook_gadget_config.h"
#include "nook/Nook.h"

namespace nook {
namespace gadget {

NookStatus EnsureDirectAttachListenerForCurrentProcess(const GadgetConfig& config);

}  // namespace gadget
}  // namespace nook
