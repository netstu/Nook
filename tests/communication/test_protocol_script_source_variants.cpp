#include <cassert>
#include <string>

#include "communication/protocol/messages.h"

using namespace nook::comm;

int main() {
    ScriptCreate create;
    create.session_id = 0;
    create.name = "smoke.js";
    create.source = "send({ type: 'send', payload: 'script-loaded' })";

    const std::vector<uint8_t> encoded = EncodeScriptCreate(create);

    ScriptCreate decoded;
    assert(DecodeScriptCreate(encoded.data(), encoded.size(), &decoded));
    assert(decoded.name == create.name);
    assert(decoded.source == create.source);
    return 0;
}
