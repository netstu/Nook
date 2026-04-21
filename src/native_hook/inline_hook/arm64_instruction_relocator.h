#pragma once

#include <cstddef>
#include <cstdint>

namespace NookInlineHookInternal {

size_t GetArm64RelocatedInstructionLength(uint32_t instruction);

bool RewriteArm64Instruction(uint32_t instruction,
                             uintptr_t instruction_address,
                             uintptr_t relocated_block_start,
                             uintptr_t relocated_block_end,
                             const size_t* relocated_instruction_lengths,
                             size_t relocated_instruction_count,
                             uint32_t* output,
                             size_t output_capacity_words,
                             size_t* output_word_count);

bool RelocateArm64InstructionSequence(const uint32_t* instructions,
                                      size_t instruction_count,
                                      uintptr_t source_address,
                                      uintptr_t relocated_address,
                                      uint32_t* output,
                                      size_t output_capacity_words,
                                      size_t* output_word_count);

}  // namespace NookInlineHookInternal
