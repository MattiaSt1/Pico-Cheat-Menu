// Arm9 part of the Pico Cheat Parasite.
//
// While the arm7 part works (see arm7/source/main.cpp), the arm9 part shows its progress on the status screen. Then it
// decides whether the real Pico Loader can run the cheat menu: it must accept cheats from the launcher (API version 3),
// its jumpToArm9EntryPoint must be found, and its ITCM section must leave room for the installer. If so, the installer
// is copied to the ITCM and the real Pico Loader is patched to jump to it instead of to the game. Finally the
// parameters of the launcher are passed to the real Pico Loader, which is booted.
//
// When the launcher boots the DSpico boot file, the real Pico Loader is booted right away; the status screen only
// appears if the arm7 part fails.

#include <nds/ndstypes.h>
#include <string.h>
#include "ParasiteDefinitions.h"
#include "GameScanner.h"
#include "../../include/picoLoader7.h"
#include "installer_bin.h"
#include "statusScreen.h"

#define REG_EXMEMCNT            (*(vu16*)0x04000204)
#define EXMEMCNT_CARD_ARM7      (1 << 11)

/// @brief The real Pico Loader clears the top screen when it starts, so the result stays on the status screen this
///        many frames before; longer when the cheat menu is off for a reason that the user did not choose.
#define RESULT_FRAMES           60
#define RESULT_FRAMES_PROBLEM   180

/// @brief Copies the real Pico Loader to VRAM and boots it, together with the arm7 (handoff.s).
extern "C" void bootRealLoader(const void* loader9, u32 loader9Size, const void* loader7, u32 loader7Size);

/// @brief Copies the installer to the ITCM and patches the real Pico Loader to jump to it instead of to the game.
///        Returns false if the real Pico Loader cannot run the cheat menu.
static bool prepareInstaller(const parasite_mailbox_t* box, const pload_header7_t* realHeader)
{
    if (!box->cheatList || realHeader->apiVersion < 3)
        return false;
    int jumpToArm9 = gameScanner_findPicoLoaderJumpToArm9((const u32*)box->realLoader9, box->realLoader9Size / 4);
    u32 itcmEnd = gameScanner_picoItcmEnd((const u32*)box->realLoader9, box->realLoader9Size);
    if (jumpToArm9 < 0 || itcmEnd < 0x01000000 || itcmEnd > PARASITE_STASH_ADDRESS ||
        installer_bin_size > PARASITE_STASH_END - PARASITE_STASH_ADDRESS)
        return false;
    memcpy((void*)PARASITE_STASH_ADDRESS, installer_bin, installer_bin_size);
    auto installer = (parasite_installer_header_t*)PARASITE_STASH_ADDRESS;
    installer->listLength = box->cheatList->length;
    u32* patch = (u32*)box->realLoader9 + jumpToArm9;
    patch[0] = 0xE51FF004; // ldr pc, [pc, #-4]
    patch[1] = installer->entryPoint;
    return true;
}

extern "C" void loaderMain()
{
    parasite_waitSync(PARASITE_SYNC_LAUNCHER_DATA_COPIED);
    auto box = (parasite_mailbox_t*)PARASITE_MAILBOX_ADDRESS;
    // The arm7 part reads the sd card through the DS card slot. The data cache of the arm9 is off, so the arm9 sees
    // what the arm7 writes to the mailbox.
    REG_EXMEMCNT |= EXMEMCNT_CARD_ARM7;
    parasite_sendSync(PARASITE_SYNC_CARD_TO_ARM7);

    if (!box->passThrough)
        statusScreen_show();
    // An error stops the arm7 part before it is prepared. The error then stays on the status screen, also for the boot
    // file, since the game cannot start.
    while (parasite_receivedSync() != PARASITE_SYNC_PREPARED)
    {
        if (box->error != PARASITE_ERROR_NONE)
            statusScreen_show();
        statusScreen_update(box);
    }

    auto realHeader = (pload_header7_t*)box->realLoader7;
    auto launcherHeader = (const pload_header7_t*)box->launcherHeader;
    if (box->installCheatMenu && !prepareInstaller(box, realHeader))
    {
        box->installCheatMenu = false;
        box->error = PARASITE_ERROR_LOADER_UNSUPPORTED;
    }
    realHeader->dldiDriver = (void*)box->dldiDriver;
    realHeader->bootDrive = launcherHeader->bootDrive;
    realHeader->loadParams = launcherHeader->loadParams;
    if (realHeader->apiVersion >= 2)
        realHeader->v2 = launcherHeader->v2;
    if (realHeader->apiVersion >= 3)
    {
        realHeader->v3.cheats = box->installCheatMenu ? (const pload_cheats_t*)box->cheatList :
            (const pload_cheats_t*)box->launcherCheats;
    }

    if (!box->passThrough)
    {
        box->step = PARASITE_STEP_DONE;
        bool problem = box->error != PARASITE_ERROR_NONE && box->error != PARASITE_ERROR_TRANSPARENT;
        for (u32 i = 0; i < (problem ? RESULT_FRAMES_PROBLEM : RESULT_FRAMES); i++)
            statusScreen_update(box);
    }

    parasite_sendSync(PARASITE_SYNC_ARM9_DECIDED);
    parasite_waitSync(PARASITE_SYNC_ARM7_IN_WRAM);
    bootRealLoader(box->realLoader9, (box->realLoader9Size + 1) & ~1, box->realLoader7,
        (box->realLoader7Size + 1) & ~1);
    while (true);
}
