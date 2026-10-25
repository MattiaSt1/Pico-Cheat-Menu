#include <nds/ndstypes.h>
#include "ParasiteDefinitions.h"
#include "GameScanner.h"
#include "ndsHeader.h"
#include "moduleParams.h"
#include "../arm7/source/loader/dldiHeader.h"
extern "C" parasite_installer_header_t installer_header;
extern "C" void installer_flush();
static_assert(sizeof(cheat_menu_resident_t) == 104, "resident/entry.s header layout");
static_assert(sizeof(parasite_installer_header_t) == 16, "installer/entry.s header layout");
static void relocateRange(u32* begin, u32* end, u32 oldStart, u32 oldEnd, u32 delta)
{
    while (begin < end) { if (*begin >= oldStart && *begin < oldEnd) *begin += delta; ++begin; }
}
// Relocates the driver at d, which the game will see at address.
static void relocateDldi(dldi_header_t* d, u32 address)
{
    u32 old = d->driverStartAddress, end = d->driverEndAddress;
    u32 delta = address - old;
    u8* bytes = (u8*)d;
    if (d->fixFlags & DLDI_FIX_ALL)
        relocateRange((u32*)(bytes + 128), (u32*)(bytes + end - old), old, end, delta);
    else
    {
        if (d->fixFlags & DLDI_FIX_GLUE)
            relocateRange((u32*)(bytes + d->glueStartAddress - old), (u32*)(bytes + d->glueEndAddress - old), old, end, delta);
        if (d->fixFlags & DLDI_FIX_GOT)
            relocateRange((u32*)(bytes + d->gotStartAddress - old), (u32*)(bytes + d->gotEndAddress - old), old, end, delta);
    }
    // Pointer fields, excluding the IO type and feature flags. Preserve BSS from the launcher.
    u32* fields = &d->driverStartAddress;
    for (u32 i = 0; i < 16; i++)
        if (i != 8 && i != 9 && fields[i]) fields[i] += delta;
}
static u32 finalAddress(u32 initial, const game_scanner_module_params_t* p)
{
    u32 src = p->autoloadStart;
    u32 stride = p->sdkVersion >= 0x05000000 ? 16 : 12;
    for (u32 ptr = p->autoloadListStart; ptr < p->autoloadListEnd; ptr += stride)
    {
        const u32* entry = (const u32*)ptr;
        if (initial >= src && initial - src < entry[1]) return entry[0] + initial - src;
        src += entry[1];
    }
    return initial;
}
// Pico Loader places the cheat list at the start of the arm7 main memory arena of the game.
static cheat_menu_list_header_t* findList(u32 start, u32 end, u32 length)
{
    for (u32 address = start; address + sizeof(cheat_menu_list_header_t) <= end; address += 4)
    {
        auto list = (cheat_menu_list_header_t*)address;
        if (list->length != length || list->markerLength != sizeof(list->marker) ||
            list->marker[0] != CHEAT_MENU_MARKER_OPCODE || list->marker[1] != CHEAT_MENU_LIST_MAGIC0 ||
            list->marker[3] != CHEAT_MENU_LIST_MAGIC1 || list->marker[5] + list->marker[7] != length ||
            length > end - address)
            continue;
        return list;
    }
    return nullptr;
}
extern "C" void installer_run()
{
    auto& h = installer_header;
    h.status = 2;
    auto rom = (const nds_header_ntr_t*)0x02FFFE00;
    bool dsi = ((*(vu32*)0x04004008 >> 14) & 3) >= 2;
    u32 arenaEnd = dsi ? 0x02F80000 : 0x023E0000;
    if (rom->arm9LoadAddress < 0x02000000 || rom->arm9LoadAddress >= arenaEnd ||
        rom->arm9Size > arenaEnd - rom->arm9LoadAddress) return;
    u32* code = (u32*)rom->arm9LoadAddress;
    int mod = gameScanner_findModuleParams(code, rom->arm9Size / 4);
    if (mod < 0) return;
    auto p = (const game_scanner_module_params_t*)(code + mod);
    u32 stride = p->sdkVersion >= 0x05000000 ? 16 : 12;
    if (p->autoloadListStart < rom->arm9LoadAddress || p->autoloadListEnd < p->autoloadListStart ||
        p->autoloadListEnd >= arenaEnd || (p->autoloadListEnd - p->autoloadListStart) % stride) return;
    u32 scanSize = p->autoloadListEnd - rom->arm9LoadAddress;
    if (scanSize < rom->arm9Size) scanSize = rom->arm9Size;
    u32 continueWords;
    int irq = gameScanner_findIrqHandler(code, scanSize / 4, &continueWords);
    h.status = 3;
    if (irq < 0) return;
    u32 irqAddress = finalAddress((u32)(code + irq), p);

    h.status = 4;
    // The arm7 dedicated main memory. In DS mode it is mirrored every 4MB and Pico Loader may write it through
    // another mirror; the game, and so the resident part, use the 0x023xxxxx addresses.
    cheat_menu_list_header_t* list = dsi ? findList(0x02F80000, 0x02FFC000, h.listLength) : nullptr;
    if (!list) list = findList(0x02380000, 0x02400000, h.listLength);
    if (!list && !dsi) list = findList(0x02B80000, 0x02C00000, h.listLength);
    if (!list) return;
    u32 written = (u32)list;
    u32 seen = dsi ? written : 0x02000000 | (written & 0x003FFFFF);
    // w: address to write now, a: address the game sees
    auto a = [&](const void* w) { return (u32)w - written + seen; };
    auto resident = (cheat_menu_resident_t*)((u8*)list + list->marker[5]);

    // While the menu is open, its image borrows memory with the game's code: after the secure area, where Pico
    // Loader puts its own patches, and in the first half of the static arm9 binary, before its data. The game is
    // paused meanwhile, and the arm7 and dma do not access code.
    h.status = 5;
    u32 menuAddress = (rom->arm9LoadAddress + 0x4000 + 31) & ~31u;
    u32 menuSize = resident->imageSectors * CHEAT_MENU_SECTOR_SIZE;
    u32 staticEnd = p->autoloadStart;
    if (staticEnd <= rom->arm9LoadAddress || staticEnd > arenaEnd) return;
    u32 limit = rom->arm9LoadAddress + (staticEnd - rom->arm9LoadAddress) / 2;
    if (menuSize == 0 || menuAddress >= limit || menuSize > limit - menuAddress ||
        (irqAddress < menuAddress + menuSize && irqAddress + continueWords * 4 > menuAddress)) return;

    auto driver = (dldi_header_t*)((u8*)resident + resident->dldiOffset);
    relocateDldi(driver, a(driver));
    resident->dldiDriver = (const void*)a(driver);
    resident->list = (cheat_menu_list_header_t*)a(list);
    resident->menuAddress = menuAddress;
    resident->irqHandlerContinueAddress = irqAddress + continueWords * 4;
    resident->cardLockAddress = p->sdkVersion >= 0x05000000 ? 0x02FFFFE0 : 0x027FFFE0;

    // The arm7 memory is not accessible to the arm9 of the game (protection region without access). The first
    // words of OS_IrqHandler are replaced by code that saves the access permissions, allows read/write access in
    // every protection region for data and instructions, and jumps to the resident hook. The hook restores the data
    // permissions and also does what the replaced instructions did.
    u32* hook = code + irq;
    hook[0] = 0xEE150F50; // mrc p15, 0, r0, c5, c0, 2 (data access permissions)
    hook[1] = 0xEE151F70; // mrc p15, 0, r1, c5, c0, 3 (instruction access permissions)
    hook[2] = 0xE3A02033; // mov r2, #0x33
    hook[3] = 0xE1822402; // orr r2, r2, r2, lsl #8
    hook[4] = 0xE1822802; // orr r2, r2, r2, lsl #16
    hook[5] = 0xEE052F50; // mcr p15, 0, r2, c5, c0, 2
    hook[6] = 0xEE052F70; // mcr p15, 0, r2, c5, c0, 3
    hook[7] = 0xE51FF004; // ldr pc, [pc, #-4]
    hook[8] = a(resident) + resident->irqHookOffset;
    h.status = 1;
    installer_flush();
}
