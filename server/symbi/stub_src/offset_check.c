#include <stddef.h>
#include <stdio.h>

#include "stub.h"

int main() {
  printf("sizeof(TStub)=%zu\n", sizeof(TStub));
  printf("mark=%zu\n", offsetof(TStub, mark));
  printf("original_set_argv0=%zu\n", offsetof(TStub, original_set_argv0));
  printf("slot_addr=%zu\n", offsetof(TStub, slot_addr));
  printf("socket_name=%zu\n", offsetof(TStub, socket_name));
  printf("target_package=%zu\n", offsetof(TStub, target_package));
  printf("getpid=%zu\n", offsetof(TStub, getpid));
  printf("socket=%zu\n", offsetof(TStub, socket));
  printf("connect=%zu\n", offsetof(TStub, connect));
  printf("write=%zu\n", offsetof(TStub, write));
  printf("close=%zu\n", offsetof(TStub, close));
  printf("raise=%zu\n", offsetof(TStub, raise));
  return 0;
}
