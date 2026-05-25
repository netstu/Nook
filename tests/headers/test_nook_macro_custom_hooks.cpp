#include "nook/NookJavaHookMacros.h"

NOOK_PAYLOAD_CONFIG("T", 2, 5);

NOOK_JAVA_HOOK(TestVoidHook, "A", "m", "(II)V", 0) {
    (void)env;
    (void)thiz;
    (void)args;
    (void)arg_count;
    (void)result;
    return 0;
}

NOOK_JAVA_HOOK(TestIntHook, "A", "n", "()I", 0) {
    (void)env;
    (void)thiz;
    (void)args;
    (void)arg_count;
    result->i = 123;
    return 0;
}

int main() {
    const NookJavaHookPayloadConfig* config = NookPayloadGetConfig();
    return (config != nullptr && config->retry_count == 2 && config->retry_interval_ms == 5) ? 0 : 1;
}
