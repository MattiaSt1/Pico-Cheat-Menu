#pragma once

// Cheat search of the in-game cheat menu.
//
// The cheat search finds the address of a value of the game, such as the money or the lives, by scanning the memory
// of the game several times: first for a known value or for every value, then, after the value changed in the game,
// for the values that are equal to a new value, that changed, that stayed the same, that increased or that decreased.
// The addresses that match every search so far are the results. Once only a few are left, they can be listed, written
// and locked: a locked value gets a cheat in the cheat list that keeps writing it.
//
// The menu has very little memory of its own, so the search lives in the cheat search file on the sd card (see
// CheatMenuDefinitions.h). The scanned memory is split into groups of CHEAT_SEARCH_GROUP_SIZE bytes. Every group has
// one sector of result bits, with one bit per byte that is set where a result starts, and its sectors in the snapshot,
// which holds the values of the last search for the comparisons with the previous value. The state keeps one bit per
// group that tells whether the group has results, so that groups without results are skipped without reading their
// sectors. Groups that the menu image overlaps are never scanned, since the game's code is not there meanwhile.
//
// The search accesses the file through cheatSearch_transferSectors and reports its progress through
// cheatSearch_reportProgress, which the menu implements. Both are separate functions so that the search can be tested
// on the host.

#include <nds/ndstypes.h>
#include "CheatMenuDefinitions.h"

/// @brief Value of cheat_search_state_t::magic when the state is valid.
#define CHEAT_SEARCH_MAGIC              0x48435253

/// @brief Maximum number of results whose addresses are kept in the state, so that they can be listed.
#define CHEAT_SEARCH_MAX_LISTED         128

/// @brief The comparisons of a search.
enum
{
    /// @brief Every value: the first search when the value is not known.
    CHEAT_SEARCH_UNKNOWN,
    /// @brief The values that are equal to the given value.
    CHEAT_SEARCH_EQUAL,
    /// @brief The values that differ from the previous search.
    CHEAT_SEARCH_CHANGED,
    /// @brief The values that are the same as in the previous search.
    CHEAT_SEARCH_UNCHANGED,
    /// @brief The values that are greater than in the previous search.
    CHEAT_SEARCH_INCREASED,
    /// @brief The values that are smaller than in the previous search.
    CHEAT_SEARCH_DECREASED,
    /// @brief The number of comparisons.
    CHEAT_SEARCH_COMPARE_COUNT
};

/// @brief A value that is kept locked by a cheat.
typedef struct
{
    /// @brief The address of the value, as the game sees it.
    u32 address;

    /// @brief The value that the cheat writes.
    u32 value;

    /// @brief The size of the value in bytes: 1, 2 or 4.
    u32 size;
} cheat_search_lock_t;

/// @brief The state of the cheat search, stored in the first sectors of the cheat search file.
typedef struct
{
    /// @brief CHEAT_SEARCH_MAGIC when the state is valid. The parasite clears it on every start.
    u32 magic;

    /// @brief Size in bytes of the values of the current search (1, 2 or 4), or 0 before the first search.
    u32 valueSize;

    /// @brief Number of searches done since the current search was started.
    u32 searchCount;

    /// @brief Number of results.
    u32 resultCount;

    /// @brief Nonzero when every aligned value of the groups with results is a result. The result bits of the groups
    ///        are then not written.
    u32 allResults;

    /// @brief The settings of the next search, chosen in the menu.
    u32 settingSize;
    u32 settingCompare;
    u32 settingValue;

    /// @brief The locked values.
    u32 lockCount;
    cheat_search_lock_t locks[CHEAT_SEARCH_MAX_LOCKS];

    /// @brief One bit per group of the scanned memory, set if the group has results.
    u32 groupBits[CHEAT_SEARCH_GROUP_COUNT / 32];

    /// @brief Unused, so that the state fills its sectors.
    u32 reserved[75];

    /// @brief The addresses of the first CHEAT_SEARCH_MAX_LISTED results in ascending order. The list is complete when
    ///        there are at most that many results.
    u32 results[CHEAT_SEARCH_MAX_LISTED];
} cheat_search_state_t;

_Static_assert(sizeof(cheat_search_state_t) == CHEAT_SEARCH_STATE_SECTORS * CHEAT_MENU_SECTOR_SIZE,
    "The state must fill its sectors");

/// @brief Reads or writes sectors of the cheat search file. Implemented by the menu.
/// @param sector The first sector, relative to the start of the file.
/// @param count The number of sectors.
/// @param buffer The data to write, or the destination of the data that is read.
/// @param write True to write the sectors, false to read them.
/// @return True on success.
bool cheatSearch_transferSectors(u32 sector, u32 count, void* buffer, bool write);

/// @brief Called while a search runs, after each block of CHEAT_SEARCH_BLOCK_SIZE bytes. Implemented by the menu.
/// @param done The number of blocks done so far.
/// @param total The number of blocks of the search.
void cheatSearch_reportProgress(u32 done, u32 total);

/// @brief Reads the state from the cheat search file. If it cannot be read or is not valid, for example after the
///        parasite cleared it, a new state without a search is set up.
/// @return False if the state cannot be read.
bool cheatSearch_load(cheat_search_state_t* state);

/// @brief Writes the state to the cheat search file.
/// @return False if the state cannot be written.
bool cheatSearch_save(cheat_search_state_t* state);

/// @brief Ends the current search, so that the next search starts from the whole memory again. The locked values and
///        the settings stay, except for a comparison that a first search cannot use, which becomes CHEAT_SEARCH_EQUAL.
void cheatSearch_reset(cheat_search_state_t* state);

/// @brief Returns whether the given comparison can be used for the next search: only CHEAT_SEARCH_UNKNOWN and
///        CHEAT_SEARCH_EQUAL for the first search, and every comparison except CHEAT_SEARCH_UNKNOWN afterwards.
bool cheatSearch_isCompareAllowed(const cheat_search_state_t* state, u32 compare);

/// @brief Returns whether the given comparison uses the value of the settings.
bool cheatSearch_usesValue(u32 compare);

/// @brief Returns the largest value of the given size in bytes.
u32 cheatSearch_getMaxValue(u32 size);

/// @brief Reads the value of the given size at the given offset of the scanned memory.
u32 cheatSearch_readValue(const u8* memory, u32 offset, u32 size);

/// @brief Runs a search with the comparison and the value of the settings. The first search uses the size of the
///        settings. The state is not saved.
/// @param state The state of the search.
/// @param memory The scanned memory, which the game sees at CHEAT_SEARCH_MEMORY_START.
/// @param memorySize The size of the scanned memory, a multiple of CHEAT_SEARCH_BLOCK_SIZE.
/// @param excludeStart The game address of the start of the memory that must not be scanned (the menu image).
/// @param excludeEnd The game address of the end of that memory.
/// @return False if the comparison cannot be used or the cheat search file cannot be accessed. After a file error,
///         the search is reset.
bool cheatSearch_run(cheat_search_state_t* state, const u8* memory, u32 memorySize, u32 excludeStart, u32 excludeEnd);

/// @brief Returns the index of the lock of the given address, or -1.
int cheatSearch_findLock(const cheat_search_state_t* state, u32 address);

/// @brief Locks the value of the given size at the given address, or changes the value if it is locked already.
/// @return False if CHEAT_SEARCH_MAX_LOCKS values are locked already.
bool cheatSearch_lock(cheat_search_state_t* state, u32 address, u32 value, u32 size);

/// @brief Unlocks the value at the given address, if it is locked.
void cheatSearch_unlock(cheat_search_state_t* state, u32 address);

/// @brief Writes the cheats of the locked values in the format of the cheat list (pload_cheat_t).
/// @param state The state of the search.
/// @param dst The destination in the cheat list.
/// @param capacity The room at dst in bytes.
/// @param count Receives the number of cheats written.
/// @return The number of bytes written. Locks that do not fit are skipped.
u32 cheatSearch_writeLockCheats(const cheat_search_state_t* state, u8* dst, u32 capacity, u32* count);
