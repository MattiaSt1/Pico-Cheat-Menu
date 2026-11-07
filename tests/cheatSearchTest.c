// Host tests of the cheat search of the in-game cheat menu (source/menu/cheatSearch.c).
//
// The cheat search file is kept in host memory and the scanned memory is an array, so that the searches, the result
// list, the exclusion of the menu image, file errors and the cheats of the locked values can be checked without a
// console. See tests/README.md for how to build and run the test.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cheatSearch.c"

#define FILE_SIZE       (CHEAT_SEARCH_FILE_SECTORS * CHEAT_MENU_SECTOR_SIZE)
#define START           CHEAT_SEARCH_MEMORY_START
#define DS_SIZE         CHEAT_SEARCH_MEMORY_SIZE_DS
#define DSI_SIZE        CHEAT_SEARCH_MEMORY_SIZE_DSI

// The test of file errors runs many searches, so it scans only two blocks, with a value in each.
#define ERROR_TEST_SIZE     (2 * CHEAT_SEARCH_BLOCK_SIZE)
#define ERROR_TEST_CHANGED  (START + 0x100)
#define ERROR_TEST_SAME     (START + CHEAT_SEARCH_BLOCK_SIZE + 0x100)

static u8* sFile;
static u8* sMemory;
static int sFailAfter = -1; // number of transfers that succeed before all others fail, or -1
static int sFailOnceAfter = -1; // number of transfers that succeed before a single one fails, or -1
static u32 sTransfers;
static u32 sProgressDone;
static u32 sProgressTotal;
static int sFailures;

bool cheatSearch_transferSectors(u32 sector, u32 count, void* buffer, bool write)
{
    sTransfers++;
    if (sFailAfter == 0)
        return false;
    if (sFailAfter > 0)
        sFailAfter--;
    if (sFailOnceAfter >= 0 && sFailOnceAfter-- == 0)
        return false;
    if (sector + count > CHEAT_SEARCH_FILE_SECTORS)
        return false;
    u8* data = sFile + sector * CHEAT_MENU_SECTOR_SIZE;
    if (write)
        memcpy(data, buffer, count * CHEAT_MENU_SECTOR_SIZE);
    else
        memcpy(buffer, data, count * CHEAT_MENU_SECTOR_SIZE);
    return true;
}

void cheatSearch_reportProgress(u32 done, u32 total)
{
    sProgressDone = done;
    sProgressTotal = total;
}

#define CHECK(condition) \
    do { if (!(condition)) { printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); sFailures++; } } \
        while (0)

static void put16(u32 address, u16 value)
{
    memcpy(sMemory + address - START, &value, 2);
}

static void put32(u32 address, u32 value)
{
    memcpy(sMemory + address - START, &value, 4);
}

// Starts each test with a file as the parasite leaves it, and with memory that holds no small values. The parasite only
// clears the first sector: the other sectors still hold what the last search wrote, so the search must not read them
// before it wrote them.
static void setUp(cheat_search_state_t* state)
{
    memset(sFile, 0xA5, FILE_SIZE);
    memset(sFile, 0, CHEAT_MENU_SECTOR_SIZE);
    memset(sMemory, 0xEE, DSI_SIZE);
    sFailAfter = -1;
    CHECK(cheatSearch_load(state));
}

static bool search(cheat_search_state_t* state, u32 compare, u32 value)
{
    state->settingCompare = compare;
    state->settingValue = value;
    return cheatSearch_run(state, sMemory, DS_SIZE, 0, 0);
}

static void testDefaults(void)
{
    cheat_search_state_t state;
    setUp(&state);
    CHECK(state.magic == CHEAT_SEARCH_MAGIC);
    CHECK(state.valueSize == 0);
    CHECK(state.settingSize == 4);
    CHECK(state.settingCompare == CHEAT_SEARCH_EQUAL);
    CHECK(cheatSearch_isCompareAllowed(&state, CHEAT_SEARCH_UNKNOWN));
    CHECK(cheatSearch_isCompareAllowed(&state, CHEAT_SEARCH_EQUAL));
    CHECK(!cheatSearch_isCompareAllowed(&state, CHEAT_SEARCH_DECREASED));

    // A relative comparison cannot start a search.
    state.settingCompare = CHEAT_SEARCH_INCREASED;
    CHECK(!cheatSearch_run(&state, sMemory, DS_SIZE, 0, 0));
    CHECK(state.valueSize == 0);
}

static void testEqualThenEqual(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 2;
    put16(0x02100000, 500);
    put16(0x02200002, 500);
    put16(0x02300004, 500);
    put16(0x02000001 + 0x10, 500); // not aligned to 2 bytes: never a result
    CHECK(search(&state, CHEAT_SEARCH_EQUAL, 500));
    CHECK(state.valueSize == 2);
    CHECK(state.resultCount == 3);
    CHECK(state.results[0] == 0x02100000 && state.results[1] == 0x02200002 && state.results[2] == 0x02300004);
    CHECK(!state.allResults);
    // The search reports that it went through all blocks.
    CHECK(sProgressTotal == DS_SIZE / CHEAT_SEARCH_BLOCK_SIZE);
    CHECK(sProgressDone == sProgressTotal);

    // The value changes in the game: only one address follows it.
    put16(0x02200002, 420);
    put16(0x02300004, 421);
    CHECK(search(&state, CHEAT_SEARCH_EQUAL, 420));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == 0x02200002);
    CHECK(state.searchCount == 2);

    // The state survives a save and a load, as when the menu is opened again.
    CHECK(cheatSearch_save(&state));
    cheat_search_state_t loaded;
    CHECK(cheatSearch_load(&loaded));
    CHECK(memcmp(&loaded, &state, sizeof(state)) == 0);
}

static void testUnknownThenRelative(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 4;
    put32(0x02123450, 1000);
    put32(0x02300000, 77);
    CHECK(search(&state, CHEAT_SEARCH_UNKNOWN, 0));
    CHECK(state.allResults);
    CHECK(state.resultCount == DS_SIZE / 4);
    CHECK(!cheatSearch_isCompareAllowed(&state, CHEAT_SEARCH_UNKNOWN));

    // Lives go down from 1000 to 999; the other value does not change.
    put32(0x02123450, 999);
    CHECK(search(&state, CHEAT_SEARCH_DECREASED, 0));
    CHECK(!state.allResults);
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == 0x02123450);

    // Nothing changes: the result stays.
    CHECK(search(&state, CHEAT_SEARCH_UNCHANGED, 0));
    CHECK(state.resultCount == 1);

    // The value goes up: it is a result of "increased" and then "changed" no longer matches.
    put32(0x02123450, 1500);
    CHECK(search(&state, CHEAT_SEARCH_INCREASED, 0));
    CHECK(state.resultCount == 1);
    CHECK(search(&state, CHEAT_SEARCH_CHANGED, 0));
    CHECK(state.resultCount == 0);
}

static void testChangedAfterUnknownBytes(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 1;
    CHECK(search(&state, CHEAT_SEARCH_UNKNOWN, 0));
    CHECK(state.resultCount == DS_SIZE);
    sMemory[0x3FFFFF] = 1;
    sMemory[5] = 2;
    CHECK(search(&state, CHEAT_SEARCH_CHANGED, 0));
    CHECK(state.resultCount == 2);
    CHECK(state.results[0] == START + 5 && state.results[1] == START + 0x3FFFFF);
}

static void testExcludedMemory(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 4;
    put32(0x02004100, 123); // inside the menu image
    put32(0x02005FFC, 123); // in a group that the menu image overlaps
    put32(0x02008000, 123);
    state.settingCompare = CHEAT_SEARCH_EQUAL;
    state.settingValue = 123;
    CHECK(cheatSearch_run(&state, sMemory, DS_SIZE, 0x02004020, 0x02005020));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == 0x02008000);
}

// The groups next to the menu image are scanned.
static void testExcludedMemoryEdges(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 4;
    put32(0x02003FFC, 123);
    put32(0x02004000, 123);
    put32(0x02005FFC, 123);
    put32(0x02006000, 123);
    state.settingCompare = CHEAT_SEARCH_EQUAL;
    state.settingValue = 123;
    CHECK(cheatSearch_run(&state, sMemory, DS_SIZE, 0x02004000, 0x02006000));
    CHECK(state.resultCount == 2);
    CHECK(state.results[0] == 0x02003FFC && state.results[1] == 0x02006000);
}

// A search for every value leaves out the menu image as well, so that a later comparison does not see it change.
static void testExcludedMemoryAfterUnknown(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 4;
    state.settingCompare = CHEAT_SEARCH_UNKNOWN;
    CHECK(cheatSearch_run(&state, sMemory, DS_SIZE, 0x02004020, 0x02005020));
    CHECK(state.resultCount == (DS_SIZE - 2 * CHEAT_SEARCH_GROUP_SIZE) / 4);
    put32(0x02004100, 1);
    put32(0x02006000, 1);
    state.settingCompare = CHEAT_SEARCH_CHANGED;
    CHECK(cheatSearch_run(&state, sMemory, DS_SIZE, 0x02004020, 0x02005020));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == 0x02006000);
}

// In DSi mode the game keeps its data far above the first 4 MB: in Pokemon Black 2 the money is at about 0x027CC644.
static void testDsiMemory(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 4;
    put32(0x02100000, 3000); // below the 4 MB of DS mode: a value that does not follow the money
    put32(0x027CC644, 3000);
    put32(0x02F7FFFC, 3000); // the last value of the memory of DSi mode
    state.settingCompare = CHEAT_SEARCH_EQUAL;
    state.settingValue = 3000;
    CHECK(cheatSearch_run(&state, sMemory, DSI_SIZE, 0, 0));
    CHECK(state.resultCount == 3);
    CHECK(state.results[1] == 0x027CC644 && state.results[2] == 0x02F7FFFC);

    // The value changes in the game. The state survives a save and a load, as when the menu is opened again.
    CHECK(cheatSearch_save(&state));
    CHECK(cheatSearch_load(&state));
    put32(0x027CC644, 2500);
    state.settingValue = 2500;
    CHECK(cheatSearch_run(&state, sMemory, DSI_SIZE, 0, 0));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == 0x027CC644);
    CHECK(state.searchCount == 2);

    // A search that compares with the previous values reads the snapshot of the high memory.
    CHECK(cheatSearch_save(&state));
    CHECK(cheatSearch_load(&state));
    put32(0x027CC644, 2400);
    state.settingCompare = CHEAT_SEARCH_DECREASED;
    CHECK(cheatSearch_run(&state, sMemory, DSI_SIZE, 0, 0));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == 0x027CC644);

    // DS mode does not look beyond its 4 MB.
    setUp(&state);
    state.settingSize = 4;
    put32(0x02400000, 3000);
    state.settingCompare = CHEAT_SEARCH_EQUAL;
    state.settingValue = 3000;
    CHECK(cheatSearch_run(&state, sMemory, DS_SIZE, 0, 0));
    CHECK(state.resultCount == 0);
}

static void testManyResults(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 2;
    for (u32 i = 0; i < 300; i++)
        put16(0x02010000 + i * 64, 7);
    CHECK(search(&state, CHEAT_SEARCH_EQUAL, 7));
    CHECK(state.resultCount == 300);
    // Only the first addresses are listed.
    CHECK(state.results[CHEAT_SEARCH_MAX_LISTED - 1] == 0x02010000 + (CHEAT_SEARCH_MAX_LISTED - 1) * 64);
    put16(0x02010000 + 299 * 64, 8);
    CHECK(search(&state, CHEAT_SEARCH_CHANGED, 0));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == 0x02010000 + 299 * 64);
}

static void testValueIsLimitedToSize(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 1;
    sMemory[0x100] = 0x34;
    CHECK(search(&state, CHEAT_SEARCH_EQUAL, 0x1234));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == START + 0x100);
}

static void testFileError(void)
{
    cheat_search_state_t state;
    setUp(&state);
    put32(0x02000100, 9);
    sFailAfter = 3;
    CHECK(!search(&state, CHEAT_SEARCH_EQUAL, 9));
    // The search is reset, so that a broken state is never used.
    CHECK(state.valueSize == 0 && state.resultCount == 0);
    sFailAfter = -1;
    CHECK(search(&state, CHEAT_SEARCH_EQUAL, 9));
    CHECK(state.resultCount == 1);

    // A state that cannot be read is replaced by a new one.
    sFailAfter = 0;
    cheat_search_state_t loaded;
    CHECK(!cheatSearch_load(&loaded));
    CHECK(loaded.magic == CHEAT_SEARCH_MAGIC && loaded.valueSize == 0);
}

// Runs a search with the given comparison, changes a value and runs a search for what changed, on two blocks of memory.
static bool runTwoSearches(cheat_search_state_t* state, u32 compare)
{
    state->settingSize = 4;
    state->settingCompare = compare;
    state->settingValue = 9;
    if (!cheatSearch_run(state, sMemory, ERROR_TEST_SIZE, 0, 0))
        return false;
    put32(ERROR_TEST_CHANGED, 10);
    state->settingCompare = CHEAT_SEARCH_CHANGED;
    return cheatSearch_run(state, sMemory, ERROR_TEST_SIZE, 0, 0);
}

static void setUpErrorTest(cheat_search_state_t* state)
{
    setUp(state);
    put32(ERROR_TEST_CHANGED, 9);
    put32(ERROR_TEST_SAME, 9);
}

// Whichever transfer fails, the search fails and is reset, and the next search works.
static void checkErrorOnEveryTransfer(u32 compare)
{
    cheat_search_state_t state;
    setUpErrorTest(&state);
    sTransfers = 0;
    CHECK(runTwoSearches(&state, compare));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == ERROR_TEST_CHANGED);
    u32 transfers = sTransfers;
    CHECK(transfers != 0);
    for (u32 i = 0; i < transfers; i++)
    {
        setUpErrorTest(&state);
        sFailOnceAfter = i;
        CHECK(!runTwoSearches(&state, compare));
        CHECK(state.valueSize == 0 && state.resultCount == 0 && !state.allResults);
        put32(ERROR_TEST_CHANGED, 10);
        state.settingCompare = CHEAT_SEARCH_EQUAL;
        state.settingValue = 9;
        CHECK(cheatSearch_run(&state, sMemory, ERROR_TEST_SIZE, 0, 0));
        CHECK(state.resultCount == 1);
        CHECK(state.results[0] == ERROR_TEST_SAME);
    }
}

static void testErrorOnEveryTransfer(void)
{
    checkErrorOnEveryTransfer(CHEAT_SEARCH_EQUAL);
    checkErrorOnEveryTransfer(CHEAT_SEARCH_UNKNOWN);
}

// Saves the state and checks that it is replaced by a new one when it is loaded.
static void checkRejected(cheat_search_state_t* state)
{
    CHECK(cheatSearch_save(state));
    CHECK(cheatSearch_load(state));
    CHECK(state->magic == CHEAT_SEARCH_MAGIC && state->valueSize == 0 && state->lockCount == 0);
    CHECK(state->settingSize == 4 && state->settingCompare == CHEAT_SEARCH_EQUAL);
}

static void testInvalidState(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 3;
    checkRejected(&state);
    state.valueSize = 3;
    checkRejected(&state);
    state.settingCompare = CHEAT_SEARCH_COMPARE_COUNT;
    checkRejected(&state);
    // All locks look valid, also the one that follows them, so that only their number tells that it is not a lock.
    state.lockCount = CHEAT_SEARCH_MAX_LOCKS + 1;
    for (u32 i = 0; i < CHEAT_SEARCH_MAX_LOCKS; i++)
        state.locks[i].size = 4;
    state.groupBits[2] = 4;
    checkRejected(&state);
    CHECK(cheatSearch_lock(&state, 0x02000000, 1, 4));
    state.locks[0].size = 3;
    checkRejected(&state);
    state.magic = 0;
    checkRejected(&state);
}

static void testResetKeepsSettingsAndLocks(void)
{
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 2;
    put16(0x02000100, 5);
    CHECK(search(&state, CHEAT_SEARCH_EQUAL, 5));
    state.settingCompare = CHEAT_SEARCH_DECREASED;
    CHECK(cheatSearch_lock(&state, 0x02000100, 99, 2));
    cheatSearch_reset(&state);
    CHECK(state.valueSize == 0 && state.resultCount == 0 && state.searchCount == 0);
    CHECK(state.settingSize == 2);
    CHECK(state.settingCompare == CHEAT_SEARCH_EQUAL);
    CHECK(state.lockCount == 1);
    // A new search starts from the whole memory: it finds a value that the old search left out.
    put16(0x02000200, 6);
    CHECK(search(&state, CHEAT_SEARCH_EQUAL, 6));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == 0x02000200);
}

static void testLocks(void)
{
    cheat_search_state_t state;
    setUp(&state);
    CHECK(cheatSearch_lock(&state, 0x02123456, 0x12345678, 4));
    CHECK(cheatSearch_lock(&state, 0x02000010, 0x1FFFF, 2));
    CHECK(cheatSearch_lock(&state, 0x02000020, 0x1FF, 1));
    CHECK(state.lockCount == 3);
    CHECK(state.locks[1].value == 0xFFFF);
    // Locking an address again changes its value.
    CHECK(cheatSearch_lock(&state, 0x02123456, 5, 4));
    CHECK(state.lockCount == 3 && state.locks[0].value == 5);
    CHECK(cheatSearch_findLock(&state, 0x02000020) == 2);

    u32 words[64];
    u32 count;
    u32 used = cheatSearch_writeLockCheats(&state, (u8*)words, sizeof(words), &count);
    CHECK(count == 3 && used == 3 * CHEAT_SEARCH_LOCK_CHEAT_SIZE);
    const u32 expected[] =
    {
        24, 0xD3000000, 0, 0x02123456, 5, 0xD2000000, 0,
        24, 0xD3000000, 0, 0x12000010, 0xFFFF, 0xD2000000, 0,
        24, 0xD3000000, 0, 0x22000020, 0xFF, 0xD2000000, 0
    };
    CHECK(memcmp(words, expected, sizeof(expected)) == 0);

    // Cheats that do not fit are skipped.
    used = cheatSearch_writeLockCheats(&state, (u8*)words, 2 * CHEAT_SEARCH_LOCK_CHEAT_SIZE + 4, &count);
    CHECK(count == 2 && used == 2 * CHEAT_SEARCH_LOCK_CHEAT_SIZE);

    cheatSearch_unlock(&state, 0x02123456);
    CHECK(state.lockCount == 2 && state.locks[0].address == 0x02000010 && state.locks[1].address == 0x02000020);
    cheatSearch_unlock(&state, 0x02999999);
    CHECK(state.lockCount == 2);

    for (u32 i = 0; i < CHEAT_SEARCH_MAX_LOCKS - 2; i++)
        CHECK(cheatSearch_lock(&state, 0x02100000 + i * 4, i, 4));
    CHECK(!cheatSearch_lock(&state, 0x02200000, 1, 4));
    CHECK(state.lockCount == CHEAT_SEARCH_MAX_LOCKS);
}

// In DS mode the cheat list is inside the 4 MB of main memory: the search stops before it, at a multiple of its blocks.
static void testMemorySize(void)
{
    CHECK(cheatSearch_getMemorySize(0x023A1234, DS_SIZE) == 0x003A0000);
    CHECK(cheatSearch_getMemorySize(0x023A0000, DS_SIZE) == 0x003A0000);

    // A cheat list beyond the scanned memory does not limit it.
    CHECK(cheatSearch_getMemorySize(0x02400000, DS_SIZE) == DS_SIZE);
    CHECK(cheatSearch_getMemorySize(0x02F80000, DSI_SIZE) == DSI_SIZE);
    CHECK(cheatSearch_getMemorySize(0x02FF0000, DSI_SIZE) == DSI_SIZE);

    // A value in the cheat list or after it is never a result.
    cheat_search_state_t state;
    setUp(&state);
    state.settingSize = 4;
    put32(0x0239FFFC, 77);
    put32(0x023A0000, 77);
    state.settingCompare = CHEAT_SEARCH_EQUAL;
    state.settingValue = 77;
    CHECK(cheatSearch_run(&state, sMemory, cheatSearch_getMemorySize(0x023A1234, DS_SIZE), 0, 0));
    CHECK(state.resultCount == 1);
    CHECK(state.results[0] == 0x0239FFFC);
}

// A cheat list with room for two cheats of locked values and the resident part after it.
typedef struct
{
    cheat_menu_list_header_t header;
    u8 room[2 * CHEAT_SEARCH_LOCK_CHEAT_SIZE];
    cheat_menu_resident_t resident;
} test_list_t;

static void setUpList(test_list_t* list, u32 numberOfCheats, u32 lockCheatCount)
{
    memset(list, 0, sizeof(*list));
    list->header.numberOfCheats = numberOfCheats;
    list->header.marker[5] = offsetof(test_list_t, resident);
    list->resident.lockCheatCount = lockCheatCount;
}

// The count comes from memory that the game can change, so that it is only used if it fits the list.
static void testLockCheatCount(void)
{
    test_list_t list;
    setUpList(&list, 4, 2);
    CHECK(cheatMenuList_getLockCheatCount(&list.header) == 2);
    setUpList(&list, 1, 0);
    CHECK(cheatMenuList_getLockCheatCount(&list.header) == 0);

    // The marker is the first cheat of the list, so that two locked values need three cheats.
    setUpList(&list, 2, 2);
    CHECK(cheatMenuList_getLockCheatCount(&list.header) == 0);

    setUpList(&list, 100, CHEAT_SEARCH_MAX_LOCKS + 1);
    CHECK(cheatMenuList_getLockCheatCount(&list.header) == 0);

    // The cheats must fit before the resident part.
    setUpList(&list, 4, 3);
    CHECK(cheatMenuList_getLockCheatCount(&list.header) == 0);
}

int main(void)
{
    sFile = malloc(FILE_SIZE);
    sMemory = aligned_alloc(32, DSI_SIZE);
    testDefaults();
    testEqualThenEqual();
    testUnknownThenRelative();
    testChangedAfterUnknownBytes();
    testExcludedMemory();
    testExcludedMemoryEdges();
    testExcludedMemoryAfterUnknown();
    testDsiMemory();
    testManyResults();
    testValueIsLimitedToSize();
    testFileError();
    testErrorOnEveryTransfer();
    testInvalidState();
    testResetKeepsSettingsAndLocks();
    testLocks();
    testMemorySize();
    testLockCheatCount();
    free(sFile);
    free(sMemory);
    if (sFailures != 0)
    {
        printf("%d check(s) failed\n", sFailures);
        return 1;
    }
    printf("All cheat search tests passed\n");
    return 0;
}
