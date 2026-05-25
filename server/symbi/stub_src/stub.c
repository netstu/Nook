#include <dlfcn.h>
#include <stddef.h>

#include "stub.h"

static volatile TStub stubApi = {
    .mark = "/ningningning123123",
};

typedef struct _SymbiCallbackHeader {
    uint32_t pid;
    uint32_t load_ok;
} SymbiCallbackHeader;

static void symbi_restore_local_slot(void) {
    if (stubApi.slot_addr == 0 || stubApi.original_set_argv0 == 0) {
        return;
    }

    *((uintptr_t*) stubApi.slot_addr) = (uintptr_t) stubApi.original_set_argv0;
}

static int symbi_notify_host(void) {
    if (stubApi.socket == 0 || stubApi.connect == 0 || stubApi.write == 0 ||
        stubApi.close == 0 ||
        stubApi.getpid == 0 ||
        stubApi.socket_name[0] == '\0') {
        return 0;
    }

    int fd = stubApi.socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        return 0;
    }

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';

    unsigned int socket_name_len = 0;
    while (socket_name_len < sizeof(stubApi.socket_name) &&
           stubApi.socket_name[socket_name_len] != '\0') {
        if (1u + socket_name_len >= sizeof(addr.sun_path)) {
            break;
        }
        addr.sun_path[1u + socket_name_len] = stubApi.socket_name[socket_name_len];
        socket_name_len++;
    }

    socklen_t addr_len =
        (socklen_t) (offsetof(struct sockaddr_un, sun_path) + 1u + socket_name_len);
    if (stubApi.connect(fd, (const struct sockaddr*) &addr, addr_len) != 0) {
        stubApi.close(fd);
        return 0;
    }

    SymbiCallbackHeader header;
    header.pid = (uint32_t) stubApi.getpid();
    header.load_ok = 1;

    if (stubApi.write(fd, &header, sizeof(header)) != (ssize_t) sizeof(header)) {
        stubApi.close(fd);
        return 0;
    }
    stubApi.close(fd);
    return 1;
}

static int symbi_target_package_matches(JNIEnv* env, jstring name) {
    if (env == NULL || name == NULL || stubApi.target_package[0] == '\0') {
        return 0;
    }

    const char* actual_name = (*env)->GetStringUTFChars(env, name, NULL);
    if (actual_name == NULL) {
        return 0;
    }

    unsigned int index = 0;
    while (stubApi.target_package[index] != '\0' && actual_name[index] != '\0') {
        if (stubApi.target_package[index] != actual_name[index]) {
            (*env)->ReleaseStringUTFChars(env, name, actual_name);
            return 0;
        }
        index++;
    }

    const int matched =
        stubApi.target_package[index] == '\0' && actual_name[index] == '\0';
    (*env)->ReleaseStringUTFChars(env, name, actual_name);
    return matched;
}

__attribute__((section(".text.entrypoint")))
__attribute__((visibility("default")))
int stub_replacement_set_argv0(JNIEnv* env, jobject clazz, jstring name) {
    symbi_restore_local_slot();
    const int result = stubApi.original_set_argv0(env, clazz, name);

    if (symbi_target_package_matches(env, name)) {
        STUB_LOGI((&stubApi), "child hit, notifying host and stopping for host-side injection");
        if (symbi_notify_host()) {
            if (stubApi.raise != 0) {
                stubApi.raise(SIGSTOP);
            } else {
                STUB_LOGE((&stubApi), "raise(SIGSTOP) unavailable");
            }
        } else {
            STUB_LOGE((&stubApi), "Host notify handshake failed");
        }
    }
    return result;
}
