#include <nds/ndstypes.h>
#include <string.h>
#include "ParasiteDefinitions.h"
#include "GameScanner.h"
#include "../../include/picoLoader7.h"
#include "installer_bin.h"
extern "C" void bootRealLoader(const void*, u32, const void*, u32);
static void waitSync(u32 value) { while ((*(vu16*)0x04000180 & 15) != value); }
extern "C" void loaderMain()
{
    waitSync(PARASITE_SYNC_LAUNCHER_DATA_COPIED);
    // ARM9 starts with D-cache disabled. ARM7 owns the cartridge during filesystem access.
    *(vu16*)0x04000204 |= 1 << 11;
    *(vu16*)0x04000180 = PARASITE_SYNC_CARD_TO_ARM7 << 8;
    waitSync(PARASITE_SYNC_PREPARED);
    auto box = (parasite_mailbox_t*)PARASITE_MAILBOX_ADDRESS;
    if (!box->realLoaderLoaded) while (true);
    auto realHeader = (pload_header7_t*)box->realLoader7;
    auto launcher = (const pload_header7_t*)box->launcherHeader;
    int jump = gameScanner_findPicoLoaderJumpToArm9((const u32*)box->realLoader9, box->realLoader9Size / 4);
    u32 itcmEnd = gameScanner_picoItcmEnd((const u32*)box->realLoader9, box->realLoader9Size);
    // The menu needs a fork that takes cheats (api v3): the list with the resident part is passed as the cheats.
    bool install = box->installCheatMenu && box->cheatList && jump >= 0 && realHeader->apiVersion >= 3 &&
        itcmEnd >= 0x01000000 && itcmEnd <= PARASITE_STASH_ADDRESS &&
        installer_bin_size <= PARASITE_STASH_END - PARASITE_STASH_ADDRESS;
    if (install)
    {
        memcpy((void*)PARASITE_STASH_ADDRESS, installer_bin, installer_bin_size);
        auto header = (parasite_installer_header_t*)PARASITE_STASH_ADDRESS;
        header->listLength = box->cheatList->length;
        u32* patch = (u32*)box->realLoader9 + jump;
        patch[0] = 0xE51FF004;
        patch[1] = header->entryPoint;
    }
    realHeader->dldiDriver = (void*)box->dldiDriver;
    realHeader->bootDrive = launcher->bootDrive;
    realHeader->loadParams = launcher->loadParams;
    if (realHeader->apiVersion >= 2) realHeader->v2 = launcher->v2;
    if (realHeader->apiVersion >= 3)
        realHeader->v3.cheats = install ? (const pload_cheats_t*)box->cheatList :
            (const pload_cheats_t*)box->launcherCheats;
    box->installCheatMenu = install;
    *(vu16*)0x04000180 = PARASITE_SYNC_ARM9_DECIDED << 8;
    waitSync(PARASITE_SYNC_ARM7_IN_WRAM);
    bootRealLoader(box->realLoader9, (box->realLoader9Size + 1) & ~1,
        box->realLoader7, (box->realLoader7Size + 1) & ~1);
    while (true);
}
