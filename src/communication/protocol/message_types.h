#pragma once

#include <cstdint>

namespace nook {
namespace comm {

enum class MessageType : uint16_t {
    kAttachRequest      = 0x0100,
    kAttachResponse     = 0x0101,
    kDetachRequest      = 0x0102,
    kDetachResponse     = 0x0103,
    kSpawnRequest       = 0x0104,
    kSpawnResponse      = 0x0105,
    kResumeRequest      = 0x0106,
    kResumeResponse     = 0x0107,

    kScriptCreate       = 0x0200,
    kScriptCreateResp   = 0x0201,
    kScriptLoad         = 0x0202,
    kScriptLoadResp     = 0x0203,
    kScriptUnload       = 0x0204,
    kScriptUnloadResp   = 0x0205,

    kScriptMessage      = 0x0300,
    kScriptPost         = 0x0301,

    kRpcRequest         = 0x0400,
    kRpcResponse        = 0x0401,
    kSpawnInstall       = 0x0402,
    kSpawnInstallResp   = 0x0403,
    kSpawnUninstall     = 0x0404,
    kSpawnUninstallResp = 0x0405,

    kProcessListReq     = 0x0500,
    kProcessListResp    = 0x0501,
    kAppListReq         = 0x0502,
    kAppListResp        = 0x0503,

    kPing               = 0x0600,
    kPong               = 0x0601,
    kError              = 0x06FF,

    kAgentReady         = 0xFF00,
    kAgentShutdown      = 0xFF01,
};

}  // namespace comm
}  // namespace nook
