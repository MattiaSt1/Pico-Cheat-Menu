// Resident part of the in-game cheat menu.
//
// The resident part is a position-independent blob that the parasite places in the cheat list, so that it stays in
// the arm7 memory of the game while the game runs. The installer hooks OS_IrqHandler of the game, which makes
// resident_irqHook (entry.s) run on every interrupt. On vblank, resident_tick checks the button combination; when it is
// pressed, the resident part:
//   1. pauses the arm7 cheat engine by setting the number of cheats in the cheat list to 0;
//   2. takes the SDK card lock, so that the game does not use the DS card slot in the meantime;
//   3. saves the game's code at menuAddress to the second half of the cheat menu file and loads the menu image there;
//   4. runs the menu, which returns the new number of cheats;
//   5. restores the game's code and lets the cheat engine continue with the new cheat list.
// The game is paused the whole time, since the menu runs inside its interrupt handler.

#include <nds/ndstypes.h>
#include "cheatMenuFile.h"

#define REG_VCOUNT              (*(vu16*)0x04000006)
#define REG_KEYINPUT            (*(vu16*)0x04000130)
#define REG_ROMCTRL             (*(vu32*)0x040001A4)
#define REG_EXMEMCNT            (*(vu16*)0x04000204)
#define REG_IE                  (*(vu32*)0x04000210)
#define REG_IF                  (*(vu32*)0x04000214)

#define EXMEMCNT_CARD_ARM7      (1 << 11)
#define ROMCTRL_BUSY            (1u << 31)
#define IRQ_VBLANK              (1 << 0)

#define KEY_SELECT              (1 << 2)
#define KEY_DOWN                (1 << 7)
#define KEY_R                   (1 << 8)
#define KEY_L                   (1 << 9)
#define KEY_MASK                0x3FF

/// @brief The button combination that opens the menu.
#define MENU_COMBO              (KEY_L | KEY_R | KEY_DOWN | KEY_SELECT)

/// @brief Owner ID written to the card lock while the menu holds it. It is outside of the range that OS_GetLockID
///        hands out, so it cannot be mistaken for a lock of the game.
#define CARD_LOCK_ID            0x7F

/// @brief While the game uses the DS card slot, opening the menu is retried on this many vblanks.
#define CARD_LOCK_RETRY_FRAMES  60

/// @brief Number of attempts to read the game's code back from the sd card. The game cannot continue without it.
#define RESTORE_ATTEMPTS        8

typedef u32 (*menu_entry_t)(cheat_menu_resident_t* resident, u32 numberOfCheats);

// Defined in entry.s. Hidden visibility makes the compiler use pc-relative instead of GOT-based access.
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

/// @brief Takes the SDK card lock (an OSLockWord) the same way as OS_TryLockByWord, and gives the arm9 access to the
///        DS card slot. Returns false if the game holds the lock.
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

/// @brief Releases the card lock taken by tryLockCard.
static void unlockCard(void)
{
    vu32* lockWord = (vu32*)resident_header.cardLockAddress;
    ((vu16*)lockWord)[2] = 0;
    REG_EXMEMCNT = sSavedExmemcnt;
    lockWord[0] = 0;
}

/// @brief Swaps the menu image in, runs the menu and swaps the game's code back.
static void openMenu(void)
{
    cheat_menu_resident_t* resident = &resident_header;
    vu32* numberOfCheats = &resident->list->numberOfCheats;
    u32 cheats = *numberOfCheats;
    // The cheat engine reads the number of cheats once per vblank, so after the next vblank it no longer runs the
    // cheats and the menu may rewrite them.
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
    // The game's code goes to the sectors after the menu image.
    if (sDldiStarted && cheatMenuFile_transfer(resident, sectors, sectors, menu, true))
    {
        bool loaded = cheatMenuFile_transfer(resident, 0, sectors, menu, false);
        resident_flushCaches(menu, size);
        const cheat_menu_image_header_t* image = (const cheat_menu_image_header_t*)menu;
        if (loaded && image->entryOffset < image->totalSize && image->totalSize <= image->dataOffset &&
            image->dataOffset < size)
        {
            cheats = ((menu_entry_t)(menu + image->entryOffset))(resident, cheats);
        }
        resident_flushCaches(menu, size);
        for (u32 i = 0; i < RESTORE_ATTEMPTS && !cheatMenuFile_transfer(resident, sectors, sectors, menu, false); i++);
        resident_flushCaches(menu, size);
    }
    *numberOfCheats = cheats;
}

/// @brief Called by resident_irqHook on every interrupt, before the game's handler runs.
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
    // The menu returns once all keys are released.
    sPreviousKeys = 0;
}
