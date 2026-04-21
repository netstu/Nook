#ifndef INJECTDEMO_DEF_H
#define INJECTDEMO_DEF_H

#include <elf.h>
#include <android/api-level.h>

// 修复宏定义冲突，只在旧API级别定义
#if __ANDROID_API__ <= 14
#define PT_TLS 7
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK (PT_LOOS + 0x474e551)
#endif

// 只在未定义时定义这些宏
#ifndef PT_GNU_RELRO
#define PT_GNU_RELRO 0x6474e552
#endif

#ifndef PT_ARM_EXIDX
#define PT_ARM_EXIDX (PT_LOPROC + 1)
#endif

#define SO_NAME_LEN 128

// 只在旧API级别定义动态段相关常量
#if __ANDROID_API__ < 21
#ifndef DT_LOOS
#define DT_LOOS 0x6000000d
#endif
#ifndef DT_HIOS
#define DT_HIOS 0x6ffff000
#endif
#ifndef DT_VALRNGLO
#define DT_VALRNGLO 0x6ffffd00
#endif
#ifndef DT_VALRNGHI
#define DT_VALRNGHI 0x6ffffdff
#endif
#ifndef DT_ADDRRNGLO
#define DT_ADDRRNGLO 0x6ffffe00
#endif
#ifndef DT_ADDRRNGHI
#define DT_ADDRRNGHI 0x6ffffeff
#endif
#ifndef DT_VERSYM
#define DT_VERSYM 0x6ffffff0
#endif
#ifndef DT_RELACOUNT
#define DT_RELACOUNT 0x6ffffff9
#endif
#ifndef DT_RELCOUNT
#define DT_RELCOUNT 0x6ffffffa
#endif
#ifndef DT_FLAGS_1
#define DT_FLAGS_1 0x6ffffffb
#endif
#ifndef DT_VERDEF
#define DT_VERDEF 0x6ffffffc
#endif
#ifndef DT_VERDEFNUM
#define DT_VERDEFNUM 0x6ffffffd
#endif
#ifndef DT_VERNEED
#define DT_VERNEED 0x6ffffffe
#endif
#ifndef DT_VERNEEDNUM
#define DT_VERNEEDNUM 0x6fffffff
#endif
#endif

// 定义其他DT常量，避免冲突
#ifndef DT_INIT_ARRAY
#define DT_INIT_ARRAY 25
#endif
#ifndef DT_FINI_ARRAY
#define DT_FINI_ARRAY 26
#endif
#ifndef DT_INIT_ARRAYSZ
#define DT_INIT_ARRAYSZ 27
#endif
#ifndef DT_FINI_ARRAYSZ
#define DT_FINI_ARRAYSZ 28
#endif
#ifndef DT_RUNPATH
#define DT_RUNPATH 29
#endif
#ifndef DT_FLAGS
#define DT_FLAGS 30
#endif
#ifndef DT_PREINIT_ARRAY
#define DT_PREINIT_ARRAY 32
#endif
#ifndef DT_PREINIT_ARRAYSZ
#define DT_PREINIT_ARRAYSZ 33
#endif

// 只在未定义时定义Android特定的DT类型
#ifndef DT_ANDROID_REL
#define DT_ANDROID_REL (DT_LOOS + 2)
#endif
#ifndef DT_ANDROID_RELSZ
#define DT_ANDROID_RELSZ (DT_LOOS + 3)
#endif
#ifndef DT_ANDROID_RELA
#define DT_ANDROID_RELA (DT_LOOS + 4)
#endif
#ifndef DT_ANDROID_RELASZ
#define DT_ANDROID_RELASZ (DT_LOOS + 5)
#endif

#ifndef DT_GNU_HASH
#define DT_GNU_HASH 0x6ffffef5
#endif

#define powerof2(x)     ((((x)-1)&(x))==0)

// Returns the address of the page containing address 'x'.
#define PAGE_START(x) ((x) & PAGE_MASK)

// Returns the address of the next page after address 'x', unless 'x' is
// itself at the start of a page.
#define PAGE_END(x) PAGE_START((x) + (PAGE_SIZE-1))

#define MAYBE_MAP_FLAG(x, from, to)  (((x) & (from)) ? (to) : 0)
#define PFLAGS_TO_PROT(x)            (MAYBE_MAP_FLAG((x), PF_X, PROT_EXEC) | \
                                      MAYBE_MAP_FLAG((x), PF_R, PROT_READ) | \
                                      MAYBE_MAP_FLAG((x), PF_W, PROT_WRITE))

#if defined(__aarch64__) || defined(__x86_64__)
#define USE_RELA 1
#endif

#if defined(__LP64__)
#define ElfW(what) Elf64_ ## what
#define ELF_R_SYM(i) ELF64_R_SYM(i)
#define ELF_R_TYPE(i) ELF64_R_TYPE(i)
#else
// 移除Elf32_Xword的重复定义，直接使用系统定义
#define ElfW(what) Elf32_ ## what
#if __ANDROID_API__ > 19
#define ELF_R_SYM(i) ELF32_R_SYM(i)
#define ELF_R_TYPE(i) ELF32_R_TYPE(i)
#endif
#endif

#if defined(__arm__)
#include "elf_arm.h"

#define R_GENERIC_JUMP_SLOT R_ARM_JUMP_SLOT
#define R_GENERIC_GLOB_DAT  R_ARM_GLOB_DAT
#define R_GENERIC_RELATIVE  R_ARM_RELATIVE
#define R_GENERIC_IRELATIVE R_ARM_IRELATIVE
#define R_GENERIC_ABS       R_ARM_ABS32
#elif defined(__aarch64__)
#define R_AARCH64_IRELATIVE             1032

#define R_GENERIC_JUMP_SLOT R_AARCH64_JUMP_SLOT
#define R_GENERIC_GLOB_DAT  R_AARCH64_GLOB_DAT
#define R_GENERIC_RELATIVE  R_AARCH64_RELATIVE
#define R_GENERIC_IRELATIVE R_AARCH64_IRELATIVE
#define R_GENERIC_ABS       R_AARCH64_ABS64
#endif

#endif //INJECTDEMO_DEF_H
