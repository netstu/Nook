#include "../../src/native_hook/inline_hook/arm64_instruction_relocator.h"

#include <cstdint>

int main() {
    using NookInlineHookInternal::GetArm64RelocatedInstructionLength;
    using NookInlineHookInternal::RelocateArm64InstructionSequence;
    using NookInlineHookInternal::RewriteArm64Instruction;

    if (GetArm64RelocatedInstructionLength(0x10000000u) != 16u) {  // ADR
        return 1;
    }
    if (GetArm64RelocatedInstructionLength(0x90000000u) != 16u) {  // ADRP
        return 1;
    }
    if (GetArm64RelocatedInstructionLength(0x14000000u) != 20u) {  // B
        return 1;
    }
    if (GetArm64RelocatedInstructionLength(0x94000000u) != 20u) {  // BL
        return 1;
    }
    if (GetArm64RelocatedInstructionLength(0x54000000u) != 24u) {  // B.cond
        return 1;
    }
    if (GetArm64RelocatedInstructionLength(0x34000000u) != 24u) {  // CBZ
        return 1;
    }
    if (GetArm64RelocatedInstructionLength(0x36000000u) != 24u) {  // TBZ
        return 1;
    }
    if (GetArm64RelocatedInstructionLength(0x58000000u) != 20u) {  // LDR literal 64
        return 1;
    }

    uint32_t output[16] = {};
    size_t output_words = 0u;
    if (!RewriteArm64Instruction(0x14000002u,
                                 0x1000u,
                                 0x2000u,
                                 0x2008u,
                                 nullptr,
                                 0u,
                                 output,
                                 16u,
                                 &output_words)) {
        return 1;
    }
    if (output_words != 5u ||
        output[0] != 0x58000051u ||
        output[1] != 0x14000003u ||
        output[4] != 0xD61F0220u) {
        return 1;
    }

    for (size_t i = 0; i < 16u; ++i) {
        output[i] = 0u;
    }
    output_words = 0u;
    if (!RewriteArm64Instruction(0x94000002u,
                                 0x1000u,
                                 0x2000u,
                                 0x2008u,
                                 nullptr,
                                 0u,
                                 output,
                                 16u,
                                 &output_words)) {
        return 1;
    }
    if (output_words != 5u || output[4] != 0xD63F0220u) {
        return 1;
    }

    const uint32_t sequence[] = {
        0x14000001u,  // B to next instruction in original block
        0xD503201Fu,  // NOP
    };
    for (size_t i = 0; i < 16u; ++i) {
        output[i] = 0u;
    }
    output_words = 0u;
    if (!RelocateArm64InstructionSequence(
            sequence, 2u, 0x1000u, 0x2000u, output, 16u, &output_words)) {
        return 1;
    }
    const uintptr_t translated_target = 0x2000u + 20u;
    if (output_words != 6u ||
        output[0] != 0x58000051u ||
        static_cast<uintptr_t>(output[2]) != (translated_target & 0xffffffffu) ||
        output[5] != 0xD503201Fu) {
        return 1;
    }

    output_words = 123u;
    if (RewriteArm64Instruction(0x14000000u,
                                0x1000u,
                                0x2000u,
                                0x2008u,
                                nullptr,
                                0u,
                                nullptr,
                                16u,
                                &output_words)) {
        return 1;
    }

    return 0;
}
