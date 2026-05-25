#include "nook/NookJavaHookMacros.h"

static void test_helpers(JNIEnv* env, jobject thiz, NookJavaHookValue* args) {
    jobject self = NOOK_JAVA_THIS_OBJECT(env, thiz);
    jobject arg0 = NOOK_JAVA_ARG_OBJECT(env, args, 0);
    (void)self;
    (void)arg0;
}

int main() {
    return 0;
}
