#include "native_hook/inline_hook/trampoline_allocator.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace NookInlineHookInternal {

bool AllocateExecutableTrampoline(size_t size, TrampolineAllocation* allocation) {
    if (size == 0u || allocation == nullptr) {
        return false;
    }

#if defined(_WIN32)
    void* address = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (address == nullptr) {
        return false;
    }

    allocation->address = address;
    allocation->size = size;
    return true;
#else
    const long page_size = sysconf(_SC_PAGESIZE);
    const size_t rounded_size =
            ((size + static_cast<size_t>(page_size) - 1u) / static_cast<size_t>(page_size)) *
            static_cast<size_t>(page_size);
    void* address = mmap(nullptr,
                         rounded_size,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         -1,
                         0);
    if (address == MAP_FAILED) {
        return false;
    }

    allocation->address = address;
    allocation->size = rounded_size;
    return true;
#endif
}

void FreeExecutableTrampoline(TrampolineAllocation* allocation) {
    if (allocation == nullptr || allocation->address == nullptr) {
        return;
    }

#if defined(_WIN32)
    VirtualFree(allocation->address, 0, MEM_RELEASE);
#else
    munmap(allocation->address, allocation->size);
#endif

    allocation->address = nullptr;
    allocation->size = 0u;
}

}  // namespace NookInlineHookInternal
