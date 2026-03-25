#include "JVM.h"
#include "../common/JavaHookLog.h"
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include "../../third_party/elfio/elfio/elfio.hpp"
#include "../../third_party/xdl/xdl.h"

namespace tool {
    // 动态分配可执行内存
    void* allocate_exec_mem(size_t size) {
        void* mem = mmap(nullptr, size,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (mem == MAP_FAILED) {
            perror("mmap");
            return nullptr;
        }
        return mem;
    }

    // 释放可执行内存
    bool free_exec_mem(void* addr, size_t size) {
        if (addr == nullptr || size == 0) {
            return false;
        }
        if (munmap(addr, size) != 0) {
            perror("munmap");
            return false;
        }
        return true;
    }

    // 从 /proc/self/maps 查找库路径
    const char* find_path_from_maps(const char* soname) {
        __android_log_print(ANDROID_LOG_INFO, "JavaHook", "→ find_path_from_maps(%s)", soname);

        FILE* fp = fopen("/proc/self/maps", "r");
        if (fp == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, "JavaHook", "✗ Failed to open /proc/self/maps");
            return nullptr;
        }
        __android_log_print(ANDROID_LOG_INFO, "JavaHook", "✓ Opened /proc/self/maps");

        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, soname)) {
                char* start = strchr(line, '/');
                char* path = strdup(start);
                if (path) {
                    path[strlen(path) - 1] = '\0';
                }
                fclose(fp);
                __android_log_print(ANDROID_LOG_INFO, "JavaHook", "✓ Found %s at: %s", soname, path ? path : "NULL");
                return path;
            }
        }
        fclose(fp);
        __android_log_print(ANDROID_LOG_ERROR, "JavaHook", "✗ %s not found in /proc/self/maps", soname);
        return nullptr;
    }

    // 获取模块的基址和大小
    std::pair<size_t, size_t> find_info_from_maps(const char* soname) {
        FILE* fp = fopen("/proc/self/maps", "r");
        if (fp == NULL) {
            return std::make_pair(0, 0);
        }
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, soname)) {
                char* start = strtok(line, "-");
                char* end = strtok(NULL, " ");
                fclose(fp);
                return std::make_pair((size_t) strtoul(start, NULL, 16),
                                      strtoul(end, NULL, 16) - strtoul(start, NULL, 16));
            }
        }
        fclose(fp);
        return std::make_pair(0, 0);
    }

    // 从模块获取符号地址
    void* get_address_from_module(const char* module_path, const char* symbol_name, bool isFunction) {
        ELFIO::elfio elffile;
        std::string name;
        ELFIO::Elf64_Addr value;
        ELFIO::Elf_Xword size;
        unsigned char bind;
        unsigned char type;
        ELFIO::Elf_Half section_index;
        unsigned char other;
        const char* file_name = strrchr(module_path, '/');

        if (!elffile.load(module_path)) {
            LOGE("Failed to load ELF file: %s", module_path);
            return nullptr;
        }

        size_t module_base = find_info_from_maps(file_name).first;
        size_t offset = 0;

        ELFIO::section* s = elffile.sections[".dynsym"];
        if (s != nullptr) {
            ELFIO::symbol_section_accessor symbol_accessor(elffile, s);
            for (int i = 0; i < symbol_accessor.get_symbols_num(); ++i) {
                symbol_accessor.get_symbol(i, name, value, size, bind, type, section_index, other);
                if (name.find(symbol_name) != std::string::npos &&
                    ((isFunction && type == 2) || (!isFunction))) {
                    offset = value;
                    break;
                }
            }
        }

        s = elffile.sections[".symtab"];
        if (s != nullptr && offset == 0) {
            ELFIO::symbol_section_accessor symbol_accessor(elffile, s);
            for (int i = 0; i < symbol_accessor.get_symbols_num(); ++i) {
                symbol_accessor.get_symbol(i, name, value, size, bind, type, section_index, other);
                if (name.find(symbol_name) != std::string::npos) {
                    offset = value;
                    break;
                }
            }
        }

        for (const auto& segment : elffile.segments) {
            ELFIO::Elf64_Addr seg_vaddr = segment->get_virtual_address();
            ELFIO::Elf_Xword seg_memsz = segment->get_memory_size();

            ELFIO::Elf64_Addr target_vaddr = module_base + offset;

            if (target_vaddr >= module_base + seg_vaddr &&
                target_vaddr < module_base + seg_vaddr + seg_memsz) {
                return (void*)target_vaddr;
            }
        }

        return nullptr;
    }

    // 检查指针是否在模块内
    bool is_in_module(void* ptr, const char* module_name) {
        std::ifstream maps("/proc/self/maps");
        std::string line;
        while (std::getline(maps, line)) {
            if (line.find(module_name) != std::string::npos) {
                uintptr_t start, end;
                sscanf(line.c_str(), "%lx-%lx", &start, &end);
                if ((uintptr_t)ptr >= start && (uintptr_t)ptr < end)
                    return true;
            }
        }
        return false;
    }
}

static bool is_addr_executable(void* ptr) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    uintptr_t target = reinterpret_cast<uintptr_t>(ptr);
    while (std::getline(maps, line)) {
        uintptr_t start = 0, end = 0;
        char perms[5] = {0};
        if (sscanf(line.c_str(), "%lx-%lx %4s", &start, &end, perms) != 3) {
            continue;
        }
        if (target >= start && target < end) {
            return perms[2] == 'x';
        }
    }
    return false;
}

// 全局 JavaVM 指针
JavaVM* JavaEnv::g_globalJavaVM = nullptr;

// 设置全局 JavaVM
void JavaEnv::SetJavaVM(JavaVM* vm) {
    g_globalJavaVM = vm;
    __android_log_print(ANDROID_LOG_INFO, "JavaHook", "✓✓✓ Global JavaVM set: %p ✓✓✓", vm);
}

// JavaEnv 实现
JavaEnv::JavaEnv() {
    LOGI("→ JavaEnv constructor ENTRY");

    // 优先使用全局 JavaVM（从 JNI_OnLoad 设置）
    if (g_globalJavaVM) {
        javaVm = g_globalJavaVM;
        LOGI("✓ Using global JavaVM: %p", javaVm);
    } else {
        // 如果没有设置全局 JavaVM，尝试动态获取
        LOGI("⚠ Global JavaVM not set, trying dynamic retrieval...");
        javaVm = getJavaVMInternal();
        if (!javaVm) {
            LOGE("✗ JavaVM not found.");
            env = nullptr;
            return;
        }
        LOGI("✓ JavaVM from dynamic retrieval: %p", javaVm);
    }

    jint ret = javaVm->GetEnv((void**)&env, JNI_VERSION_1_6);
    LOGI("GetEnv result: %d", ret);

    if (ret == JNI_EDETACHED) {
        LOGI("Thread detached, attaching...");
        if (javaVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
            LOGI("✓ Attached thread to JVM");
        } else {
            LOGE("✗ Failed to attach thread");
            env = nullptr;
        }
    } else if (ret != JNI_OK) {
        LOGE("✗ JNI version not supported");
        env = nullptr;
    } else {
        LOGI("✓ Thread already attached");
    }

    LOGI("← JavaEnv constructor EXIT (env=%p)", env);
}

JavaEnv::~JavaEnv() {
    if (attached && javaVm) {
        javaVm->DetachCurrentThread();
        LOGI("Detached thread from JVM");
    }
}

JNIEnv* JavaEnv::get() const {
    return env;
}

JNIEnv* JavaEnv::operator->() const {
    return env;
}

JavaVM* JavaEnv::getJVM() const {
    return this->javaVm;
}

bool JavaEnv::isNull() const {
    return env == nullptr;
}

JavaVM* JavaEnv::getJavaVMInternal() {
    __android_log_print(ANDROID_LOG_INFO, "JavaHook", "→ getJavaVMInternal() ENTRY");

    using JNI_GetCreatedJavaVMs_t = jint (*)(JavaVM**, jsize, jsize*);

    const char* libart_path = tool::find_path_from_maps("libart.so");
    __android_log_print(ANDROID_LOG_INFO, "JavaHook", "libart.so path: %s", libart_path ? libart_path : "NULL");

    if (!libart_path) {
        __android_log_print(ANDROID_LOG_ERROR, "JavaHook", "✗ libart.so not found");
        return nullptr;
    }

    JNI_GetCreatedJavaVMs_t func = nullptr;

    // Prefer xdl to bypass namespace restrictions in injected context
    void* xdl_handle = xdl_open("libart.so", XDL_TRY_FORCE_LOAD);
    if (xdl_handle) {
        void* sym = xdl_sym(xdl_handle, "_ZN3art3JNI19GetCreatedJavaVMsEPNS_6JavaVMEiPi", nullptr);
        if (!sym) {
            sym = xdl_sym(xdl_handle, "JNI_GetCreatedJavaVMs", nullptr);
        }
        func = reinterpret_cast<JNI_GetCreatedJavaVMs_t>(sym);
        __android_log_print(ANDROID_LOG_INFO, "JavaHook", "xdl_sym GetCreatedJavaVMs: %p", sym);
        xdl_close(xdl_handle);
    } else {
        __android_log_print(ANDROID_LOG_WARN, "JavaHook", "⚠ xdl_open(libart.so) failed");
    }

    if (!func) {
        func = (JNI_GetCreatedJavaVMs_t)tool::get_address_from_module(
            libart_path, "_ZN3art3JNI19GetCreatedJavaVMsEPNS_6JavaVMEiPi", true);
        if (!func) {
            __android_log_print(ANDROID_LOG_WARN, "JavaHook", "⚠ First symbol not found, trying alternative...");
            func = (JNI_GetCreatedJavaVMs_t)tool::get_address_from_module(
                libart_path, "JNI_GetCreatedJavaVMs", true);
        }
    }

    if (!func) {
        __android_log_print(ANDROID_LOG_ERROR, "JavaHook", "✗ GetCreatedJavaVMs symbol not found");
        return nullptr;
    }
    __android_log_print(ANDROID_LOG_INFO, "JavaHook", "✓ GetCreatedJavaVMs symbol: %p", func);

    if (!is_addr_executable(reinterpret_cast<void*>(func))) {
        __android_log_print(ANDROID_LOG_ERROR, "JavaHook", "✗ GetCreatedJavaVMs not in executable map: %p", func);
        return nullptr;
    }

    JavaVM* vms[1];
    jsize num_vms = 0;
    jint result = func(vms, 1, &num_vms);

    if (result != JNI_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "JavaHook", "✗ GetCreatedJavaVMs failed: result=%d", result);
        return nullptr;
    }

    if (num_vms == 0) {
        __android_log_print(ANDROID_LOG_ERROR, "JavaHook", "✗ num_vms == 0");
        return nullptr;
    }

    __android_log_print(ANDROID_LOG_INFO, "JavaHook", "✓✓✓ getJavaVMInternal() SUCCESS: JavaVM=%p ✓✓✓", vms[0]);
    return vms[0];
}
