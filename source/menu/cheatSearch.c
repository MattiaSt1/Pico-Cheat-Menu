// Cheat search of the in-game cheat menu, see cheatSearch.h.
//
// A search goes through the scanned memory in blocks of GROUPS_PER_BLOCK groups, so that the result bits of a block
// are read and written with one transfer. For every group with results it reads the snapshot if the comparison needs
// the previous values, clears the result bits of the values that do not match, and writes the current values of the
// group to the snapshot if any result is left. The snapshot of a group is therefore valid whenever the group has
// results.

#include <stddef.h>
#include "cheatSearch.h"

#define GROUPS_PER_BLOCK        (CHEAT_SEARCH_BLOCK_SIZE / CHEAT_SEARCH_GROUP_SIZE)
#define SECTORS_PER_GROUP       (CHEAT_SEARCH_GROUP_SIZE / CHEAT_MENU_SECTOR_SIZE)
#define BIT_WORDS_PER_GROUP     (CHEAT_SEARCH_GROUP_SIZE / 32)

_Static_assert(BIT_WORDS_PER_GROUP * 4 == CHEAT_MENU_SECTOR_SIZE, "The result bits of a group must fill one sector");

#define DEFAULT_SIZE            4

// The cheat engine of Pico Loader runs Action Replay codes. A locked value is written by a code of type 0, 1 or 2
// (32, 16 or 8 bits), after a D3 code that sets the offset to 0 and before a D2 code that ends the cheat.
#define OPCODE_SET_OFFSET       0xD3000000
#define OPCODE_END              0xD2000000
#define OPCODE_WRITE_32         0x00000000
#define OPCODE_WRITE_16         0x10000000
#define OPCODE_WRITE_8          0x20000000
#define OPCODE_ADDRESS_MASK     0x0FFFFFFF

// The menu is linked without a C library; cheatMenu.c defines memset.
void* memset(void* dst, int value, size_t count);

// The result bits of a block and the snapshot of a group. The buffers are aligned to cache lines, since the DLDI
// driver may transfer them with DMA.
static u32 sBits[GROUPS_PER_BLOCK * BIT_WORDS_PER_GROUP] __attribute__((aligned(32)));
static u8 sSnapshot[CHEAT_SEARCH_GROUP_SIZE] __attribute__((aligned(32)));

static bool isValidSize(u32 size)
{
    return size == 1 || size == 2 || size == 4;
}

static bool hasGroupResults(const cheat_search_state_t* state, u32 group)
{
    return (state->groupBits[group >> 5] >> (group & 31)) & 1;
}

static void setGroupResults(cheat_search_state_t* state, u32 group, bool hasResults)
{
    if (hasResults)
        state->groupBits[group >> 5] |= 1u << (group & 31);
    else
        state->groupBits[group >> 5] &= ~(1u << (group & 31));
}

// Sets up a new state without a search, with the default settings.
static void initState(cheat_search_state_t* state)
{
    memset(state, 0, sizeof(*state));
    state->magic = CHEAT_SEARCH_MAGIC;
    state->settingSize = DEFAULT_SIZE;
    state->settingCompare = CHEAT_SEARCH_EQUAL;
}

// Returns whether the state read from the file can be used.
static bool isValidState(const cheat_search_state_t* state)
{
    if (state->magic != CHEAT_SEARCH_MAGIC || (state->valueSize != 0 && !isValidSize(state->valueSize)) ||
        !isValidSize(state->settingSize) || state->settingCompare >= CHEAT_SEARCH_COMPARE_COUNT ||
        state->lockCount > CHEAT_SEARCH_MAX_LOCKS)
        return false;
    for (u32 i = 0; i < state->lockCount; i++)
    {
        if (!isValidSize(state->locks[i].size))
            return false;
    }
    return true;
}

bool cheatSearch_load(cheat_search_state_t* state)
{
    bool ok = cheatSearch_transferSectors(0, CHEAT_SEARCH_STATE_SECTORS, state, false);
    if (!ok || !isValidState(state))
        initState(state);
    return ok;
}

bool cheatSearch_save(cheat_search_state_t* state)
{
    return cheatSearch_transferSectors(0, CHEAT_SEARCH_STATE_SECTORS, state, true);
}

void cheatSearch_reset(cheat_search_state_t* state)
{
    state->valueSize = 0;
    state->searchCount = 0;
    state->resultCount = 0;
    state->allResults = 0;
    memset(state->groupBits, 0, sizeof(state->groupBits));
    if (!cheatSearch_isCompareAllowed(state, state->settingCompare))
        state->settingCompare = CHEAT_SEARCH_EQUAL;
}

bool cheatSearch_isCompareAllowed(const cheat_search_state_t* state, u32 compare)
{
    if (state->valueSize == 0)
        return compare == CHEAT_SEARCH_UNKNOWN || compare == CHEAT_SEARCH_EQUAL;
    return compare != CHEAT_SEARCH_UNKNOWN && compare < CHEAT_SEARCH_COMPARE_COUNT;
}

bool cheatSearch_usesValue(u32 compare)
{
    return compare == CHEAT_SEARCH_EQUAL;
}

u32 cheatSearch_getMaxValue(u32 size)
{
    return size == 1 ? 0xFF : (size == 2 ? 0xFFFF : 0xFFFFFFFF);
}

u32 cheatSearch_readValue(const u8* memory, u32 offset, u32 size)
{
    if (size == 1)
        return memory[offset];
    if (size == 2)
        return *(const u16*)(memory + offset);
    return *(const u32*)(memory + offset);
}

static bool matches(u32 compare, u32 current, u32 previous, u32 value)
{
    switch (compare)
    {
        case CHEAT_SEARCH_EQUAL:        return current == value;
        case CHEAT_SEARCH_CHANGED:      return current != previous;
        case CHEAT_SEARCH_UNCHANGED:    return current == previous;
        case CHEAT_SEARCH_INCREASED:    return current > previous;
        case CHEAT_SEARCH_DECREASED:    return current < previous;
        default:                        return true;
    }
}

// Clears the result bits of the values of a group that do not match. previous is the snapshot of the group, or NULL
// if the comparison does not need it. found is the number of results found before this group; the addresses of the
// first CHEAT_SEARCH_MAX_LISTED results are stored in the state. Returns the new number of results found.
static u32 filterGroup(cheat_search_state_t* state, u32* bits, const u8* values, const u8* previous, u32 groupAddress,
    u32 compare, u32 value, u32 found)
{
    u32 size = state->valueSize;
    for (u32 word = 0; word < BIT_WORDS_PER_GROUP; word++)
    {
        u32 candidates = bits[word];
        if (candidates == 0)
            continue;
        u32 kept = candidates;
        for (u32 bit = 0; bit < 32; bit++)
        {
            if (!((candidates >> bit) & 1))
                continue;
            u32 offset = word * 32 + bit;
            u32 current = cheatSearch_readValue(values, offset, size);
            u32 before = previous ? cheatSearch_readValue(previous, offset, size) : 0;
            if (!matches(compare, current, before, value))
            {
                kept &= ~(1u << bit);
                continue;
            }
            if (found < CHEAT_SEARCH_MAX_LISTED)
                state->results[found] = groupAddress + offset;
            found++;
        }
        bits[word] = kept;
    }
    return found;
}

bool cheatSearch_run(cheat_search_state_t* state, const u8* memory, u32 memorySize, u32 excludeStart, u32 excludeEnd)
{
    u32 compare = state->settingCompare;
    if (!cheatSearch_isCompareAllowed(state, compare))
        return false;
    if (state->valueSize == 0)
    {
        // The first search starts with every aligned value of the memory as a result.
        state->valueSize = state->settingSize;
        state->allResults = 1;
        memset(state->groupBits, 0xFF, sizeof(state->groupBits));
    }
    u32 size = state->valueSize;
    u32 value = state->settingValue & cheatSearch_getMaxValue(size);
    bool allBefore = state->allResults != 0;
    // A search for every value keeps every value as a result, so its result bits are not needed.
    bool allAfter = compare == CHEAT_SEARCH_UNKNOWN;
    bool usesPrevious = compare != CHEAT_SEARCH_UNKNOWN && compare != CHEAT_SEARCH_EQUAL;
    // The result bits of every aligned value.
    u32 allBits = size == 1 ? 0xFFFFFFFF : (size == 2 ? 0x55555555 : 0x11111111);
    u32 found = 0;
    u32 blockCount = memorySize / CHEAT_SEARCH_BLOCK_SIZE;

    for (u32 block = 0; block < blockCount; block++)
    {
        u32 firstGroup = block * GROUPS_PER_BLOCK;
        bool blockHasResults = false;
        for (u32 i = 0; i < GROUPS_PER_BLOCK; i++)
            blockHasResults |= hasGroupResults(state, firstGroup + i);
        if (blockHasResults)
        {
            if (!allBefore && !cheatSearch_transferSectors(CHEAT_SEARCH_BITS_SECTOR + firstGroup, GROUPS_PER_BLOCK,
                sBits, false))
                goto fail;
            for (u32 i = 0; i < GROUPS_PER_BLOCK; i++)
            {
                u32 group = firstGroup + i;
                u32* bits = &sBits[i * BIT_WORDS_PER_GROUP];
                u32 groupOffset = group * CHEAT_SEARCH_GROUP_SIZE;
                u32 groupAddress = CHEAT_SEARCH_MEMORY_START + groupOffset;
                bool excluded = groupAddress < excludeEnd && groupAddress + CHEAT_SEARCH_GROUP_SIZE > excludeStart;
                if (!hasGroupResults(state, group) || excluded)
                {
                    setGroupResults(state, group, false);
                    memset(bits, 0, CHEAT_MENU_SECTOR_SIZE);
                    continue;
                }
                if (allBefore)
                {
                    for (u32 word = 0; word < BIT_WORDS_PER_GROUP; word++)
                        bits[word] = allBits;
                }
                u32 snapshotSector = CHEAT_SEARCH_SNAPSHOT_SECTOR + group * SECTORS_PER_GROUP;
                if (usesPrevious && !cheatSearch_transferSectors(snapshotSector, SECTORS_PER_GROUP, sSnapshot, false))
                    goto fail;
                u32 before = found;
                found = filterGroup(state, bits, memory + groupOffset, usesPrevious ? sSnapshot : NULL, groupAddress,
                    compare, value, found);
                setGroupResults(state, group, found != before);
                // The current values become the previous values of the next search.
                if (found != before && !cheatSearch_transferSectors(snapshotSector, SECTORS_PER_GROUP,
                    (void*)(memory + groupOffset), true))
                    goto fail;
            }
            if (!allAfter && !cheatSearch_transferSectors(CHEAT_SEARCH_BITS_SECTOR + firstGroup, GROUPS_PER_BLOCK,
                sBits, true))
                goto fail;
        }
        cheatSearch_reportProgress(block + 1, blockCount);
    }

    state->allResults = allAfter;
    state->resultCount = found;
    state->searchCount++;
    return true;

fail:
    cheatSearch_reset(state);
    return false;
}

int cheatSearch_findLock(const cheat_search_state_t* state, u32 address)
{
    for (u32 i = 0; i < state->lockCount; i++)
    {
        if (state->locks[i].address == address)
            return i;
    }
    return -1;
}

bool cheatSearch_lock(cheat_search_state_t* state, u32 address, u32 value, u32 size)
{
    int index = cheatSearch_findLock(state, address);
    if (index < 0)
    {
        if (state->lockCount >= CHEAT_SEARCH_MAX_LOCKS)
            return false;
        index = state->lockCount++;
    }
    cheat_search_lock_t* lock = &state->locks[index];
    lock->address = address;
    lock->value = value & cheatSearch_getMaxValue(size);
    lock->size = size;
    return true;
}

void cheatSearch_unlock(cheat_search_state_t* state, u32 address)
{
    int index = cheatSearch_findLock(state, address);
    if (index < 0)
        return;
    state->lockCount--;
    for (u32 i = index; i < state->lockCount; i++)
        state->locks[i] = state->locks[i + 1];
}

u32 cheatSearch_writeLockCheats(const cheat_search_state_t* state, u8* dst, u32 capacity, u32* count)
{
    u32 used = 0;
    *count = 0;
    for (u32 i = 0; i < state->lockCount && capacity - used >= CHEAT_SEARCH_LOCK_CHEAT_SIZE; i++)
    {
        const cheat_search_lock_t* lock = &state->locks[i];
        u32 type = lock->size == 1 ? OPCODE_WRITE_8 : (lock->size == 2 ? OPCODE_WRITE_16 : OPCODE_WRITE_32);
        u32* words = (u32*)(dst + used);
        words[0] = CHEAT_SEARCH_LOCK_CHEAT_SIZE - 4; // pload_cheat_t::length
        words[1] = OPCODE_SET_OFFSET;
        words[2] = 0;
        words[3] = type | (lock->address & OPCODE_ADDRESS_MASK);
        words[4] = lock->value & cheatSearch_getMaxValue(lock->size);
        words[5] = OPCODE_END;
        words[6] = 0;
        used += CHEAT_SEARCH_LOCK_CHEAT_SIZE;
        (*count)++;
    }
    return used;
}
