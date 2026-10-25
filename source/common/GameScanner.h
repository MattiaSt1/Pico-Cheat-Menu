#pragma once

// Finds the SDK functions that the cheat menu hooks.

#define GAME_SCANNER_MODULE_PARAMS_MAGIC_BE 0xDEC00621
#define GAME_SCANNER_MODULE_PARAMS_MAGIC_LE 0x2106C0DE

/// @brief Module params of the arm9 binary (same as module_params_ntr_t).
typedef struct
{
    u32 autoloadListStart;
    u32 autoloadListEnd;
    u32 autoloadStart;
    u32 bssStart;
    u32 bssEnd;
    u32 compressedEnd;
    u32 sdkVersion;
    u32 magicBigEndian;
    u32 magicLittleEndian;
} game_scanner_module_params_t;

static inline int gameScanner_findPattern(const u32* data, u32 wordCount, const u32* pattern, u32 patternWordCount)
{
    for (u32 i = 0; i + patternWordCount <= wordCount; i++)
    {
        u32 j = 0;
        while (j < patternWordCount && data[i + j] == pattern[j])
            j++;
        if (j == patternWordCount)
            return (int)i;
    }
    return -1;
}

/// @brief Finds the module params in the given arm9 binary. Returns the word index of the module params or -1.
static inline int gameScanner_findModuleParams(const u32* data, u32 wordCount)
{
    static const u32 sMagic[] = { GAME_SCANNER_MODULE_PARAMS_MAGIC_BE, GAME_SCANNER_MODULE_PARAMS_MAGIC_LE };
    int index = gameScanner_findPattern(data, wordCount, sMagic, 2);
    return index < 7 ? -1 : index - 7;
}

/// @brief Finds OS_IrqHandler. Returns the word index or -1.
/// @param continueWords Set to the number of words at the start of the function that the cheat menu hook replaces
///        and executes itself. Execution continues after them.
static inline int gameScanner_findIrqHandler(const u32* data, u32 wordCount, u32* continueWords)
{
    // The hook overwrites the first 9 words. Only the known variants are accepted, because the hook executes
    // the equivalent of the replaced instructions itself:
    //   stmfd sp!, {lr}; mov r12, #0x04000000; add r12, r12, #0x210; ldr r1, [r12, #-8] (IME)
    //   cmp r1, #0; return if 0; ldmia r12, {r1, r2} (IE, IF); ands r1, r1, r2; return if 0
    static const u32 sPattern[] =
    {
        0xE92D4000, 0xE3A0C301, 0xE28CCE21, 0xE51C1008, 0xE3510000, 0x08BD8000, 0xE89C0006, 0xE0111002, 0x08BD8000
    };
    // the same, with the returns written as beq/b/ldmfd (seen in sdk 4)
    static const u32 sPatternBranches[] =
    {
        0xE92D4000, 0xE3A0C301, 0xE28CCE21, 0xE51C1008, 0xE3510000, 0x0A000000, 0xEA000000, 0xE8BD8000,
        0xE89C0006, 0xE0111002, 0x0A000000, 0xEA000000, 0xE8BD8000
    };
    int index = gameScanner_findPattern(data, wordCount, sPattern, 9);
    if (index >= 0)
    {
        *continueWords = 9;
        return index;
    }
    index = gameScanner_findPattern(data, wordCount, sPatternBranches, 13);
    *continueWords = 13;
    return index;
}

/// @brief Finds the jumpToArm9EntryPoint function of Pico Loader in picoLoader9.bin. Returns the word index or -1.
static inline int gameScanner_findPicoLoaderJumpToArm9(const u32* data, u32 wordCount)
{
    // push {r0}; mov r0, #0; mov r1, #0; ... mov r12, #0; mov lr, #0; pop {pc}
    static const u32 sPattern[] =
    {
        0xE52D0004, 0xE3A00000, 0xE3A01000, 0xE3A02000, 0xE3A03000, 0xE3A04000, 0xE3A05000, 0xE3A06000,
        0xE3A07000, 0xE3A08000, 0xE3A09000, 0xE3A0A000, 0xE3A0B000, 0xE3A0C000, 0xE3A0E000, 0xE49DF004
    };
    return gameScanner_findPattern(data, wordCount, sPattern, 16);
}

// Recognize the public loader CRT's ITCM copy, including its literal end address.
// A fork that already occupies the stash is booted transparently.
static inline u32 gameScanner_picoItcmEnd(const u32* data, u32 size)
{
    u32 words = size / 4;
    if (words > 256) words = 256;
    for (u32 i = 0; i + 3 < words; i++)
    {
        if ((data[i] & 0xFFFFF000) != 0xE59F0000 ||
            (data[i+1] & 0xFFFFF000) != 0xE59F2000 ||
            (data[i+2] & 0xFFFFF000) != 0xE59F1000 || data[i+3] != 0xE0511002) continue;
        u32 literal = (i+2)*4 + 8 + (data[i+2] & 0xFFF);
        if (!(literal & 3) && literal <= size - 4) return data[literal/4];
    }
    return 0;
}
