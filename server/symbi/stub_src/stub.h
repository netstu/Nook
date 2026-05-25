#ifndef NINJECTOR_SYMBI_STUB_SRC_H
#define NINJECTOR_SYMBI_STUB_SRC_H

#include <jni.h>
#include <stdint.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

struct _TStub {
    char mark[20];

    int (*original_set_argv0)(JNIEnv* env, jobject clazz, jstring name);

    uintptr_t slot_addr;

    char socket_name[64];
    char target_package[256];

    int (*socket)(int domain, int type, int protocol);
    int (*connect)(int fd, const struct sockaddr* addr, socklen_t len);
    ssize_t (*write)(int fd, const void* buf, size_t count);
    int (*close)(int fd);
    pid_t (*getpid)();
    int (*raise)(int sig);
};

typedef struct _TStub TStub;

#define STUB_LOGI(stub, ...) ((void) 0)
#define STUB_LOGE(stub, ...) ((void) 0)

#endif // NINJECTOR_SYMBI_STUB_SRC_H
