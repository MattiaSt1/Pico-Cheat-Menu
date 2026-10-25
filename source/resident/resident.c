// Resident part of the cheat menu: checks the button combination on vblank and swaps the menu in and out.

#include <stdbool.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef volatile unsigned short vu16;
typedef volatile unsigned int vu32;

#include "CheatMenuDefinitions.h"
#include "../arm7/source/loader/dldiHeader.h"

#define REG_VCOUNT              (*(vu16*)0x04000006)
#define REG_KEYINPUT            (*(vu16*)0x04000130)
#define REG_ROMCTRL             (*(vu32*)0x040001A4)
#define REG_EXMEMCNT            (*(vu16*)0x04000204)
#define REG_IE                  (*(vu32*)0x04000210)
#define REG_IF                  (*(vu32*)0x04000214)

#define EXMEMCNT_CARD_ARM7      (1 << 11)
#define ROMCTRL_BUSY            (1u << 31)

#define KEY_SELECT              (1 << 2)
#define KEY_DOWN                (1 << 7)
#define KEY_R                   (1 << 8)
#define KEY_L                   (1 << 9)
#define KEY_MASK                0x3FF

#define MENU_COMBO              (KEY_L | KEY_R | KEY_DOWN | KEY_SELECT)

#define IRQ_VBLANK              1

// SDK lock id used for the card lock, outside of the range allocated by OS_GetLockID.
#define CARD_LOCK_ID            0x7F
// While the game uses the card, opening the menu is retried on the next vblanks.
#define CARD_LOCK_RETRY_FRAMES  60

// The game cannot continue without its code, so reading it back is retried.
#define RESTORE_ATTEMPTS        8

typedef u32 (*menu_entry_t)(cheat_menu_resident_t* resident, u32 numberOfCheats);

// defined in entry.s; hidden to get pc-relative instead of GOT access
extern cheat_menu_resident_t resident_header __attribute__((visibility("hidden")));
void resident_flushCaches(const void* start, u32 size) __attribute__((visibility("hidden")));
u32 resident_swapWord(u32 value, vu32* address) __attribute__((visibility("hidden")));

void resident_tick(void);

static u32 sPreviousKeys;
static u32 sOpenFrames;
static bool sDldiStarted;
static u16 sSavedExmemcnt;

static void waitVBlank(void)
{
    while (REG_VCOUNT == 192);
    while (REG_VCOUNT != 192);
}

static bool tryLockCard(void)
{
    vu32* lockWord = (vu32*)resident_header.cardLockAddress;
    if (resident_swapWord(1, lockWord) != 0)
        return false;
    ((vu16*)lockWord)[2] = CARD_LOCK_ID; // OSLockWord::ownerID
    sSavedExmemcnt = REG_EXMEMCNT;
    REG_EXMEMCNT = sSavedExmemcnt & ~EXMEMCNT_CARD_ARM7;
    while (REG_ROMCTRL & ROMCTRL_BUSY);
    return true;
}

static void unlockCard(void)
{
    vu32* lockWord = (vu32*)resident_header.cardLockAddress;
    ((vu16*)lockWord)[2] = 0;
    REG_EXMEMCNT = sSavedExmemcnt;
    lockWord[0] = 0;
}

// Reads or writes the given sectors of the cheat menu file.
static bool transferFileSectors(u32 sector, u32 count, u8* buffer, bool write)
{
    const cheat_menu_resident_t* resident = &resident_header;
    const dldi_header_t* driver = (const dldi_header_t*)resident->dldiDriver;
    for (u32 i = 0; i < resident->fileRunCount && count != 0; i++)
    {
        const cheat_menu_sector_run_t* run = &resident->fileRuns[i];
        if (sector >= run->count)
        {
            sector -= run->count;
            continue;
        }
        u32 n = run->count - sector < count ? run->count - sector : count;
        bool ok = write ?
            ((dldi_write_func_t)driver->writeSectorsFuncAddress)(run->sector + sector, n, buffer) :
            ((dldi_read_func_t)driver->readSectorsFuncAddress)(run->sector + sector, n, buffer);
        if (!ok)
            return false;
        buffer += n * CHEAT_MENU_SECTOR_SIZE;
        count -= n;
        sector = 0;
    }
    return count == 0;
}

static void openMenu(void)
{
    cheat_menu_resident_t* resident = &resident_header;
    vu32* numberOfCheats = &resident->list->numberOfCheats;
    u32 cheats = *numberOfCheats;
    // The engine reads the number of cheats once per vblank: a run that started before has ended after a vblank.
    *numberOfCheats = 0;
    const dldi_header_t* driver = (const dldi_header_t*)resident->dldiDriver;
    if (!sDldiStarted)
        sDldiStarted = ((dldi_startup_func_t)driver->startupFuncAddress)();
    waitVBlank();
    waitVBlank();

    u8* menu = (u8*)resident->menuAddress;
    u32 sectors = resident->imageSectors;
    u32 size = sectors * CHEAT_MENU_SECTOR_SIZE;
    resident_flushCaches(menu, size);
    // the borrowed memory goes to the sectors after the menu image
    if (sDldiStarted && transferFileSectors(sectors, sectors, menu, true))
    {
        bool loaded = transferFileSectors(0, sectors, menu, false);
        resident_flushCaches(menu, size);
        const cheat_menu_image_header_t* image = (const cheat_menu_image_header_t*)menu;
        if (loaded && image->entryOffset < image->totalSize && image->totalSize <= image->dataOffset &&
            image->dataOffset < size)
        {
            cheats = ((menu_entry_t)(menu + image->entryOffset))(resident, cheats);
        }
        resident_flushCaches(menu, size);
        for (u32 i = 0; i < RESTORE_ATTEMPTS && !transferFileSectors(sectors, sectors, menu, false); i++);
        resident_flushCaches(menu, size);
    }
    *numberOfCheats = cheats;
}

void resident_tick(void)
{
    if (!(REG_IE & REG_IF & IRQ_VBLANK))
        return;
    u32 keys = ~REG_KEYINPUT & KEY_MASK;
    if ((keys & MENU_COMBO) == MENU_COMBO && (sPreviousKeys & MENU_COMBO) != MENU_COMBO)
        sOpenFrames = CARD_LOCK_RETRY_FRAMES;
    sPreviousKeys = keys;
    if (sOpenFrames == 0)
        return;
    sOpenFrames--;
    if (!tryLockCard())
        return;
    sOpenFrames = 0;
    openMenu();
    unlockCard();
    sPreviousKeys = 0;
}
