#include "common.h"
#include <iterator>
#include <algorithm>
#include <span>
#include "CheatPreprocessor.h"

// Based on original code by edo9300: https://github.com/edo9300/nitrohax-usercheat-supercard/blob/master/arm9/source/cheat.cpp.

static constexpr u64 NOP = 0x00000000D4000000;
static constexpr u64 D4_ORR_HACK = 0xE1833004023FE424;
static constexpr u64 D4_AND_HACK = 0xE0033004023FE424;
static constexpr u64 D4_ADD_HACK = 0xE0833004023FE424;
static constexpr u64 DB_CODE_FIX_HACK = 0x0A000003023FE4D8;

// Make 0x0E handler execute the data as arm
static constexpr u64 ASM_HACK_START = 0x012FFF11023FE074;
// Restore behaviour of 0x0E handler
static constexpr u64 ASM_HACK_END = 0xE3520003023FE074;

void CheatPreprocessor::PreprocessCheat(pload_cheat_t* cheat) const
{
    PatchD4Hack(cheat, D4_ORR_HACK, 0x1);
    PatchD4Hack(cheat, D4_AND_HACK, 0x2);
    PatchD4Hack(cheat, D4_ADD_HACK, 0x0);
    PatchDBFix(cheat);
    PatchAsmHack(cheat);
}

void CheatPreprocessor::PatchD4Hack(pload_cheat_t* cheat, u64 hackPattern, u8 patchedInstructionFlag) const
{
    std::span<u64> u64Data((u64*)cheat->opcodes, cheat->length / sizeof(u64));
    auto start = std::ranges::find(u64Data, hackPattern);
    if (start != u64Data.end())
    {
        *start++ = NOP;
        auto end = std::find(start, u64Data.end(), D4_ADD_HACK);
        auto it = std::find_if(start, end, [] (auto elem) { return (elem & 0xFFFFFFFF) == 0xD4000000; });
        if (it != end)
        {
            // Replace 0xD4 code with nitrohax's custom operator code
            *it |= patchedInstructionFlag;
        }
        if (end != u64Data.end())
        {
            *end++ = NOP;
        }
    }
}

void CheatPreprocessor::PatchDBFix(pload_cheat_t* cheat) const
{
    std::span<u64> u64Data((u64*)cheat->opcodes, cheat->length / sizeof(u64));
    auto start = std::ranges::find(u64Data, DB_CODE_FIX_HACK);
    if (start != u64Data.end())
    {
        *start++ = NOP;
    }
}

void CheatPreprocessor::PatchAsmHack(pload_cheat_t* cheat) const
{
    // Make the asm cheats designed for ards work with nitrohax
    std::span<u64> u64Data((u64*)cheat->opcodes, cheat->length / sizeof(u64));
    auto start = std::ranges::find(u64Data, ASM_HACK_START);
    if (start != u64Data.end())
    {
        *start++ = NOP;
        auto end = std::find(start, u64Data.end(), ASM_HACK_END);
        auto it = std::find_if(start, end, [] (auto elem) { return (elem & 0xFFFFFFFF) == 0xE0000000; });
        if (it != end)
        {
            // Replace 0x0E code with nitrohax's 0xC2 in arm mode (0xXXXXXXXXE0000000 - 0xXXXXXXXX1E000000 = 0xXXXXXXXXC2000000)
            *it -= 0x1E000000;
        }
        if (end != u64Data.end())
        {
            *end++ = NOP;
        }
    }
}
