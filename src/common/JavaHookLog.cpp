#include "JavaHookLog.h"

namespace Logger {
    void hex_dump_log(const void *addr, size_t size, const char *tag) {
        const unsigned char *p = (const unsigned char *)addr;
        char buf[128];
        size_t i;

        for (i = 0; i < size; i += 16) {
            snprintf(buf, sizeof(buf), "%04zx: ", i);
            int len = strlen(buf);

            for (size_t j = 0; j < 16; j++) {
                if (i + j < size) {
                    len += snprintf(buf + len, sizeof(buf) - len, "%02x ", p[i + j]);
                } else {
                    len += snprintf(buf + len, sizeof(buf) - len, "   ");
                }
            }

            len += snprintf(buf + len, sizeof(buf) - len, " |");

            for (size_t j = 0; j < 16 && i + j < size; j++) {
                unsigned char c = p[i + j];
                buf[len++] = (c >= 32 && c <= 126) ? c : '.';
            }

            buf[len++] = '|';
            buf[len] = '\0';

            LOGI("%s %s", tag, buf);
        }
    }
}
