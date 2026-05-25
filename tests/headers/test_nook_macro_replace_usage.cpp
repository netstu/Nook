#include "nook/NookJavaHookMacros.h"

NOOK_JAVA_REPLACE_INT("A", "m", "()I", 0, 7);
NOOK_JAVA_REPLACE_BOOL("A", "b", "()Z", 0, 1);
NOOK_JAVA_REPLACE_LONG("A", "l", "()J", 0, 9);

int main() {
    return 0;
}
