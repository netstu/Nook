#include "nook/NookJavaHookMacros.h"

NOOK_PAYLOAD_CONFIG("J", 3, 50);
NOOK_JAVA_REPLACE_INT("cn/n1ng/javatest/JavaHookTest", "get_num_from_java_method", "()I", 0, 999);

int main() {
    const NookJavaHookPayloadConfig* config = NookPayloadGetConfig();
    return (config != nullptr && config->retry_count == 3 && config->retry_interval_ms == 50) ? 0 : 1;
}
