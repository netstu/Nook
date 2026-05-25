#include "nook/NookJavaHookMacros.h"

NOOK_PAYLOAD_CONFIG("T", 1, 1);
NOOK_JAVA_BLOCK("A", "m", "()V", 0);

int main() {
    const NookJavaHookPayloadConfig* config = NookPayloadGetConfig();
    return (config != nullptr && config->retry_count == 1) ? 0 : 1;
}
