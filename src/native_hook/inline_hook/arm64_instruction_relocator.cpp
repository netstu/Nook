#include "native_hook/inline_hook/arm64_instruction_relocator.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NookInlineHookInternal {

namespace {

enum class Arm64InstructionType {
    kIgnored = 0,
    kB,
    kBCond,
    kBl,
    kAdr,
    kAdrp,
    kLdrLit32,
    kLdrLit64,
    kLdrswLit,
    kPrfmLit,
    kLdrSimdLit32,
    kLdrSimdLit64,
    kLdrSimdLit128,
    kCbz,
    kCbnz,
    kTbz,
    kTbnz,
};

static uint32_t GetBits32(uint32_t value, uint32_t start, uint32_t end) {
    return (value >> end) & ((1u << (start - end + 1u)) - 1u);
}

static int64_t SignExtend64(uint64_t value, uint32_t bits) {
    return static_cast<int64_t>(value << (64u - bits)) >> (64u - bits);
}

static Arm64InstructionType GetInstructionType(uint32_t instruction) {
    if ((instruction & 0xFC000000u) == 0x14000000u) {
        return Arm64InstructionType::kB;
    }
    if ((instruction & 0xFF000010u) == 0x54000000u) {
        return Arm64InstructionType::kBCond;
    }
    if ((instruction & 0xFC000000u) == 0x94000000u) {
        return Arm64InstructionType::kBl;
    }
    if ((instruction & 0x9F000000u) == 0x10000000u) {
        return Arm64InstructionType::kAdr;
    }
    if ((instruction & 0x9F000000u) == 0x90000000u) {
        return Arm64InstructionType::kAdrp;
    }
    if ((instruction & 0xFF000000u) == 0x18000000u) {
        return Arm64InstructionType::kLdrLit32;
    }
    if ((instruction & 0xFF000000u) == 0x58000000u) {
        return Arm64InstructionType::kLdrLit64;
    }
    if ((instruction & 0xFF000000u) == 0x98000000u) {
        return Arm64InstructionType::kLdrswLit;
    }
    if ((instruction & 0xFF000000u) == 0xD8000000u) {
        return Arm64InstructionType::kPrfmLit;
    }
    if ((instruction & 0xFF000000u) == 0x1C000000u) {
        return Arm64InstructionType::kLdrSimdLit32;
    }
    if ((instruction & 0xFF000000u) == 0x5C000000u) {
        return Arm64InstructionType::kLdrSimdLit64;
    }
    if ((instruction & 0xFF000000u) == 0x9C000000u) {
        return Arm64InstructionType::kLdrSimdLit128;
    }
    if ((instruction & 0x7F000000u) == 0x34000000u) {
        return Arm64InstructionType::kCbz;
    }
    if ((instruction & 0x7F000000u) == 0x35000000u) {
        return Arm64InstructionType::kCbnz;
    }
    if ((instruction & 0x7F000000u) == 0x36000000u) {
        return Arm64InstructionType::kTbz;
    }
    if ((instruction & 0x7F000000u) == 0x37000000u) {
        return Arm64InstructionType::kTbnz;
    }
    return Arm64InstructionType::kIgnored;
}

static uintptr_t TranslateAddressIfNeeded(uintptr_t address,
                                          uintptr_t source_block_start,
                                          size_t instruction_count,
                                          uintptr_t relocated_block_start,
                                          const size_t* relocated_instruction_lengths) {
    if (relocated_instruction_lengths == nullptr) {
        return address;
    }

    const uintptr_t source_block_end = source_block_start + instruction_count * sizeof(uint32_t);
    if (address < source_block_start || address >= source_block_end) {
        return address;
    }

    const size_t target_index = static_cast<size_t>((address - source_block_start) / sizeof(uint32_t));
    size_t relocated_offset = 0u;
    for (size_t i = 0; i < target_index; ++i) {
        relocated_offset += relocated_instruction_lengths[i];
    }
    return relocated_block_start + relocated_offset;
}

static bool EmitAbsoluteBranch(uint32_t* output,
                               size_t output_capacity_words,
                               uintptr_t target_address,
                               bool link,
                               size_t* output_word_count) {
    if (output_capacity_words < 5u || output == nullptr || output_word_count == nullptr) {
        return false;
    }

    output[0] = 0x58000051u;  // LDR X17, #8
    output[1] = 0x14000003u;  // B #12
    output[2] = static_cast<uint32_t>(target_address & 0xffffffffu);
    output[3] = static_cast<uint32_t>(target_address >> 32u);
    output[4] = link ? 0xD63F0220u : 0xD61F0220u;  // BLR/BR X17
    *output_word_count = 5u;
    return true;
}

static bool RewriteWithInternalContext(uint32_t instruction,
                                       uintptr_t instruction_address,
                                       uintptr_t source_block_start,
                                       uintptr_t relocated_block_start,
                                       const size_t* relocated_instruction_lengths,
                                       size_t relocated_instruction_count,
                                       uint32_t* output,
                                       size_t output_capacity_words,
                                       size_t* output_word_count) {
    if (output == nullptr || output_word_count == nullptr) {
        return false;
    }

    const Arm64InstructionType type = GetInstructionType(instruction);
    switch (type) {
        case Arm64InstructionType::kB:
        case Arm64InstructionType::kBl: {
            const uint64_t imm26 = GetBits32(instruction, 25u, 0u);
            uintptr_t target = instruction_address + SignExtend64(imm26 << 2u, 28u);
            target = TranslateAddressIfNeeded(target,
                                             source_block_start,
                                             relocated_instruction_count,
                                             relocated_block_start,
                                             relocated_instruction_lengths);
            return EmitAbsoluteBranch(
                    output, output_capacity_words, target, type == Arm64InstructionType::kBl, output_word_count);
        }
        case Arm64InstructionType::kAdr:
        case Arm64InstructionType::kAdrp: {
            if (output_capacity_words < 4u) {
                return false;
            }
            const uint32_t rd = GetBits32(instruction, 4u, 0u);
            const uint64_t immlo = GetBits32(instruction, 30u, 29u);
            const uint64_t immhi = GetBits32(instruction, 23u, 5u);
            uintptr_t target = 0u;
            if (type == Arm64InstructionType::kAdr) {
                target = instruction_address + SignExtend64((immhi << 2u) | immlo, 21u);
            } else {
                target = (instruction_address & 0xFFFFFFFFFFFFF000ull) +
                         SignExtend64((immhi << 14u) | (immlo << 12u), 33u);
            }
            target = TranslateAddressIfNeeded(target,
                                             source_block_start,
                                             relocated_instruction_count,
                                             relocated_block_start,
                                             relocated_instruction_lengths);
            output[0] = 0x58000040u | rd;  // LDR Xd, #8
            output[1] = 0x14000003u;       // B #12
            output[2] = static_cast<uint32_t>(target & 0xffffffffu);
            output[3] = static_cast<uint32_t>(target >> 32u);
            *output_word_count = 4u;
            return true;
        }
        case Arm64InstructionType::kBCond: {
            if (output_capacity_words < 6u) {
                return false;
            }
            const uint64_t imm19 = GetBits32(instruction, 23u, 5u);
            uintptr_t target = instruction_address + SignExtend64(imm19 << 2u, 21u);
            target = TranslateAddressIfNeeded(target,
                                             source_block_start,
                                             relocated_instruction_count,
                                             relocated_block_start,
                                             relocated_instruction_lengths);
            output[0] = (instruction & 0xFF00001Fu) | 0x40u;  // B.<cond> #8
            output[1] = 0x14000005u;                          // B #20
            output[2] = 0x58000051u;                          // LDR X17, #8
            output[3] = 0xD61F0220u;                          // BR X17
            output[4] = static_cast<uint32_t>(target & 0xffffffffu);
            output[5] = static_cast<uint32_t>(target >> 32u);
            *output_word_count = 6u;
            return true;
        }
        case Arm64InstructionType::kCbz:
        case Arm64InstructionType::kCbnz: {
            if (output_capacity_words < 6u) {
                return false;
            }
            const uint64_t imm19 = GetBits32(instruction, 23u, 5u);
            uintptr_t target = instruction_address + SignExtend64(imm19 << 2u, 21u);
            target = TranslateAddressIfNeeded(target,
                                             source_block_start,
                                             relocated_instruction_count,
                                             relocated_block_start,
                                             relocated_instruction_lengths);
            output[0] = (instruction & 0xFF00001Fu) | 0x40u;  // CB(N)Z Rt, #8
            output[1] = 0x14000005u;                          // B #20
            output[2] = 0x58000051u;                          // LDR X17, #8
            output[3] = 0xD61F0220u;                          // BR X17
            output[4] = static_cast<uint32_t>(target & 0xffffffffu);
            output[5] = static_cast<uint32_t>(target >> 32u);
            *output_word_count = 6u;
            return true;
        }
        case Arm64InstructionType::kTbz:
        case Arm64InstructionType::kTbnz: {
            if (output_capacity_words < 6u) {
                return false;
            }
            const uint64_t imm14 = GetBits32(instruction, 18u, 5u);
            uintptr_t target = instruction_address + SignExtend64(imm14 << 2u, 16u);
            target = TranslateAddressIfNeeded(target,
                                             source_block_start,
                                             relocated_instruction_count,
                                             relocated_block_start,
                                             relocated_instruction_lengths);
            output[0] = (instruction & 0xFFF8001Fu) | 0x40u;  // TB(N)Z Rt, #imm, #8
            output[1] = 0x14000005u;                          // B #20
            output[2] = 0x58000051u;                          // LDR X17, #8
            output[3] = 0xD61F0220u;                          // BR X17
            output[4] = static_cast<uint32_t>(target & 0xffffffffu);
            output[5] = static_cast<uint32_t>(target >> 32u);
            *output_word_count = 6u;
            return true;
        }
        case Arm64InstructionType::kLdrLit32:
        case Arm64InstructionType::kLdrLit64:
        case Arm64InstructionType::kLdrswLit: {
            if (output_capacity_words < 5u) {
                return false;
            }
            const uint32_t rt = GetBits32(instruction, 4u, 0u);
            const uint64_t imm19 = GetBits32(instruction, 23u, 5u);
            const uintptr_t target = instruction_address + SignExtend64(imm19 << 2u, 21u);
            output[0] = 0x58000060u | rt;  // LDR Xt, #12
            if (type == Arm64InstructionType::kLdrLit32) {
                output[1] = 0xB9400000u | rt | (rt << 5u);  // LDR Wt, [Xt]
            } else if (type == Arm64InstructionType::kLdrLit64) {
                output[1] = 0xF9400000u | rt | (rt << 5u);  // LDR Xt, [Xt]
            } else {
                output[1] = 0xB9800000u | rt | (rt << 5u);  // LDRSW Xt, [Xt]
            }
            output[2] = 0x14000003u;  // B #12
            output[3] = static_cast<uint32_t>(target & 0xffffffffu);
            output[4] = static_cast<uint32_t>(target >> 32u);
            *output_word_count = 5u;
            return true;
        }
        case Arm64InstructionType::kPrfmLit:
        case Arm64InstructionType::kLdrSimdLit32:
        case Arm64InstructionType::kLdrSimdLit64:
        case Arm64InstructionType::kLdrSimdLit128: {
            if (output_capacity_words < 7u) {
                return false;
            }
            const uint32_t rt = GetBits32(instruction, 4u, 0u);
            const uint64_t imm19 = GetBits32(instruction, 23u, 5u);
            const uintptr_t target = instruction_address + SignExtend64(imm19 << 2u, 21u);
            output[0] = 0xA93F47F0u;  // STP X16, X17, [SP, #-0x10]
            output[1] = 0x58000091u;  // LDR X17, #16
            if (type == Arm64InstructionType::kPrfmLit) {
                output[2] = 0xF9800220u | rt;
            } else if (type == Arm64InstructionType::kLdrSimdLit32) {
                output[2] = 0xBD400220u | rt;
            } else if (type == Arm64InstructionType::kLdrSimdLit64) {
                output[2] = 0xFD400220u | rt;
            } else {
                output[2] = 0x3DC00220u | rt;
            }
            output[3] = 0xF85F83F1u;  // LDR X17, [SP, #-0x8]
            output[4] = 0x14000003u;  // B #12
            output[5] = static_cast<uint32_t>(target & 0xffffffffu);
            output[6] = static_cast<uint32_t>(target >> 32u);
            *output_word_count = 7u;
            return true;
        }
        case Arm64InstructionType::kIgnored:
        default:
            if (output_capacity_words < 1u) {
                return false;
            }
            output[0] = instruction;
            *output_word_count = 1u;
            return true;
    }
}

}  // namespace

size_t GetArm64RelocatedInstructionLength(uint32_t instruction) {
    switch (GetInstructionType(instruction)) {
        case Arm64InstructionType::kB:
        case Arm64InstructionType::kBl:
            return 20u;
        case Arm64InstructionType::kAdr:
        case Arm64InstructionType::kAdrp:
            return 16u;
        case Arm64InstructionType::kBCond:
        case Arm64InstructionType::kCbz:
        case Arm64InstructionType::kCbnz:
        case Arm64InstructionType::kTbz:
        case Arm64InstructionType::kTbnz:
            return 24u;
        case Arm64InstructionType::kLdrLit32:
        case Arm64InstructionType::kLdrLit64:
        case Arm64InstructionType::kLdrswLit:
            return 20u;
        case Arm64InstructionType::kPrfmLit:
        case Arm64InstructionType::kLdrSimdLit32:
        case Arm64InstructionType::kLdrSimdLit64:
        case Arm64InstructionType::kLdrSimdLit128:
            return 28u;
        case Arm64InstructionType::kIgnored:
        default:
            return 4u;
    }
}

bool RewriteArm64Instruction(uint32_t instruction,
                             uintptr_t instruction_address,
                             uintptr_t relocated_block_start,
                             uintptr_t /*relocated_block_end*/,
                             const size_t* relocated_instruction_lengths,
                             size_t relocated_instruction_count,
                             uint32_t* output,
                             size_t output_capacity_words,
                             size_t* output_word_count) {
    return RewriteWithInternalContext(instruction,
                                      instruction_address,
                                      instruction_address,
                                      relocated_block_start,
                                      relocated_instruction_lengths,
                                      relocated_instruction_count,
                                      output,
                                      output_capacity_words,
                                      output_word_count);
}

bool RelocateArm64InstructionSequence(const uint32_t* instructions,
                                      size_t instruction_count,
                                      uintptr_t source_address,
                                      uintptr_t relocated_address,
                                      uint32_t* output,
                                      size_t output_capacity_words,
                                      size_t* output_word_count) {
    if (instructions == nullptr || instruction_count == 0u || output == nullptr ||
        output_word_count == nullptr) {
        return false;
    }

    std::vector<size_t> rewritten_lengths(instruction_count, 0u);
    size_t total_output_words = 0u;
    for (size_t i = 0; i < instruction_count; ++i) {
        rewritten_lengths[i] = GetArm64RelocatedInstructionLength(instructions[i]);
        total_output_words += rewritten_lengths[i] / sizeof(uint32_t);
    }
    if (total_output_words > output_capacity_words) {
        return false;
    }

    size_t output_offset = 0u;
    for (size_t i = 0; i < instruction_count; ++i) {
        size_t rewritten_words = 0u;
        if (!RewriteWithInternalContext(instructions[i],
                                        source_address + i * sizeof(uint32_t),
                                        source_address,
                                        relocated_address,
                                        rewritten_lengths.data(),
                                        instruction_count,
                                        output + output_offset,
                                        output_capacity_words - output_offset,
                                        &rewritten_words)) {
            return false;
        }
        output_offset += rewritten_words;
    }

    *output_word_count = output_offset;
    return true;
}

}  // namespace NookInlineHookInternal
