#pragma once

// Signature scans in the arm9 binary of the game and in the real Pico Loader. Shared by the parasite, which checks
// whether a game is supported, and the installer, which hooks the game.

#define GAME_SCANNER_MODULE_PARAMS_MAGIC_BE 0xDEC00621
#define GAME_SCANNER_MODULE_PARAMS_MAGIC_LE 0x2106C0DE

/// @brief Module params of the arm9 binary, as written by the Nitro SDK (same layout as module_params_ntr_t).
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

/// @brief Finds the first occurrence of \p pattern in \p data. Returns its word index, or -1 if it is not found.
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

/// @brief Finds the module params in the given arm9 binary. Returns their word index, or -1 if they are not found.
static inline int gameScanner_findModuleParams(const u32* data, u32 wordCount)
{
    static const u32 sMagic[] = { GAME_SCANNER_MODULE_PARAMS_MAGIC_BE, GAME_SCANNER_MODULE_PARAMS_MAGIC_LE };
    int index = gameScanner_findPattern(data, wordCount, sMagic, 2);
    return index < 7 ? -1 : index - 7;
}

/// @brief Finds OS_IrqHandler of the Nitro SDK. Returns its word index, or -1 if it is not found.
/// @param continueWords Receives the number of words at the start of the function that the cheat menu hook replaces.
///        The hook executes their equivalent itself, and execution continues after them.
static inline int gameScanner_findIrqHandler(const u32* data, u32 wordCount, u32* continueWords)
{
    // Only the known variants are accepted, because the hook executes the equivalent of the replaced instructions:
    //   stmfd sp!, {lr}; mov r12, #0x04000000; add r12, r12, #0x210; ldr r1, [r12, #-8] (IME)
    //   cmp r1, #0; return if 0; ldmia r12, {r1, r2} (IE, IF); ands r1, r1, r2; return if 0
    static const u32 sPattern[] =
    {
        0xE92D4000, 0xE3A0C301, 0xE28CCE21, 0xE51C1008, 0xE3510000, 0x08BD8000, 0xE89C0006, 0xE0111002, 0x08BD8000
    };
    // The same instructions, with each return written as beq / b / ldmfd (seen in SDK 4).
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

/// @brief Finds the jumpToArm9EntryPoint function of Pico Loader in picoLoader9.bin. Returns its word index, or -1 if
///        it is not found. The function is hand-written assembly and the same in every fork.
static inline int gameScanner_findPicoLoaderJumpToArm9(const u32* data, u32 wordCount)
{
    // push {r0}; mov r0, #0; mov r1, #0; ...; mov r12, #0; mov lr, #0; pop {pc}
    static const u32 sPattern[] =
    {
        0xE52D0004, 0xE3A00000, 0xE3A01000, 0xE3A02000, 0xE3A03000, 0xE3A04000, 0xE3A05000, 0xE3A06000,
        0xE3A07000, 0xE3A08000, 0xE3A09000, 0xE3A0A000, 0xE3A0B000, 0xE3A0C000, 0xE3A0E000, 0xE49DF004
    };
    return gameScanner_findPattern(data, wordCount, sPattern, 16);
}

/// @brief Returns the end address of the ITCM section of the real Pico Loader, or 0 if it cannot be determined. It is
///        read from the literal pool of the ITCM copy loop in the startup code of picoLoader9.bin
///        (ldr r0, =lma; ldr r2, =start; ldr r1, =end; subs r1, r1, r2). The installer is only placed in the ITCM if
///        this section ends before it.
static inline u32 gameScanner_picoItcmEnd(const u32* data, u32 size)
{
    u32 words = size / 4;
    if (words > 256)
        words = 256;
    for (u32 i = 0; i + 3 < words; i++)
    {
        if ((data[i] & 0xFFFFF000) != 0xE59F0000 || (data[i + 1] & 0xFFFFF000) != 0xE59F2000 ||
            (data[i + 2] & 0xFFFFF000) != 0xE59F1000 || data[i + 3] != 0xE0511002)
            continue;
        u32 literal = (i + 2) * 4 + 8 + (data[i + 2] & 0xFFF);
        if (!(literal & 3) && literal <= size - 4)
            return data[literal / 4];
    }
    return 0;
}
