// Host tests of how the in-game cheat menu writes the cheat list (updateActiveCheats in source/menu/cheatMenu.c).
//
// The test includes the menu, so that it can set up the state of the menu directly, and keeps the cheat list, the
// resident part and the cheat menu data in host memory. It never reaches the hardware or the DLDI driver. See
// tests/README.md for how to build and run the test.

#include <stdio.h>
#include "cheatMenu.c"

#define LOCK_WORDS      (CHEAT_SEARCH_LOCK_CHEAT_SIZE / 4)
#define EXTRA_MARK      0x12345678

cheat_menu_image_header_t cheatmenu_header;

void cheatmenu_cleanDataCache(const void* start, u32 size)
{
    (void)start;
    (void)size;
}

void cheatmenu_invalidateDataCache(const void* start, u32 size)
{
    (void)start;
    (void)size;
}

static struct
{
    cheat_menu_list_header_t header;
    u32 cheats[64];
} sList;

// The cheat menu data with one enable bit and room for one extra cheat.
static struct
{
    cheat_menu_data_t data;
    u32 enabledBits;
    u32 extraCheat[3];
} sTestData;

static cheat_menu_resident_t sTestResident;
static int sFailures;

#define CHECK(condition) \
    do { if (!(condition)) { printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); sFailures++; } } \
        while (0)

// Starts with an empty list, a state of the search that is loaded, and one extra cheat that is turned on.
static void setUp(void)
{
    memset(&sList, 0, sizeof(sList));
    memset(&sTestData, 0, sizeof(sTestData));
    memset(&sTestResident, 0, sizeof(sTestResident));
    memset(&sSearch, 0, sizeof(sSearch));
    sList.header.marker[5] = sizeof(sList);
    sTestResident.list = &sList.header;
    sTestData.data.extraCheatCount = 1;
    sTestData.data.enabledBitsOffset = offsetof(__typeof__(sTestData), enabledBits);
    sTestData.data.extraCheatsOffset = offsetof(__typeof__(sTestData), extraCheat);
    sTestData.enabledBits = 1;
    sTestData.extraCheat[0] = 8; // pload_cheat_t::length
    sTestData.extraCheat[1] = EXTRA_MARK;
    sTestData.extraCheat[2] = 0;
    // A cheat search file whose sectors cannot be reached: the state can only be read if it is loaded already.
    sTestData.data.searchRunCount = 1;
    sResident = &sTestResident;
    sData = &sTestData.data;
    sSearchLoaded = true;
}

static void testLocksComeFirst(void)
{
    setUp();
    CHECK(cheatSearch_lock(&sSearch, 0x02100000, 1, 4));
    CHECK(cheatSearch_lock(&sSearch, 0x02200000, 2, 4));
    CHECK(updateActiveCheats());
    CHECK(sTestResident.lockCheatCount == 2);
    CHECK(sNumberOfCheats == 4); // the marker, two locked values and the extra cheat
    CHECK(sList.cheats[0] == CHEAT_SEARCH_LOCK_CHEAT_SIZE - 4);
    CHECK(sList.cheats[3] == 0x02100000);
    CHECK(sList.cheats[LOCK_WORDS + 3] == 0x02200000);
    CHECK(sList.cheats[2 * LOCK_WORDS + 1] == EXTRA_MARK);
}

// Unlocking the last value leaves no cheats of locked values for the parasite to skip.
static void testNoLocks(void)
{
    setUp();
    sTestResident.lockCheatCount = 2;
    CHECK(updateActiveCheats());
    CHECK(sTestResident.lockCheatCount == 0);
    CHECK(sNumberOfCheats == 2);
    CHECK(sList.cheats[1] == EXTRA_MARK);
}

// If the cheat search was not opened, its state is read when the menu closes. If that fails, the values stay locked:
// the state on the sd card still has them.
static void testLocksStayWhenStateCannotBeRead(void)
{
    setUp();
    CHECK(cheatSearch_lock(&sSearch, 0x02100000, 1, 4));
    CHECK(cheatSearch_lock(&sSearch, 0x02200000, 2, 4));
    CHECK(updateActiveCheats());

    sSearchLoaded = false;
    CHECK(updateActiveCheats());
    CHECK(!sSearchLoaded);
    CHECK(sTestResident.lockCheatCount == 2);
    CHECK(sNumberOfCheats == 4);
    CHECK(sList.cheats[3] == 0x02100000);
    CHECK(sList.cheats[LOCK_WORDS + 3] == 0x02200000);
    CHECK(sList.cheats[2 * LOCK_WORDS + 1] == EXTRA_MARK);
}

// Without a cheat search file there are never cheats of locked values.
static void testNoCheatSearch(void)
{
    setUp();
    sTestData.data.searchRunCount = 0;
    sSearchLoaded = false;
    CHECK(updateActiveCheats());
    CHECK(sTestResident.lockCheatCount == 0);
    CHECK(sNumberOfCheats == 2);
    CHECK(sList.cheats[1] == EXTRA_MARK);
}

int main(void)
{
    testLocksComeFirst();
    testNoLocks();
    testLocksStayWhenStateCannotBeRead();
    testNoCheatSearch();
    if (sFailures != 0)
    {
        printf("%d check(s) failed\n", sFailures);
        return 1;
    }
    printf("All cheat list tests passed\n");
    return 0;
}
