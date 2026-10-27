// Installer of the in-game cheat menu.
//
// The arm9 part of the parasite copies the installer to the ITCM and patches the real Pico Loader so that it jumps to
// the installer instead of to the game, once the game is loaded and patched. The installer then:
//   1. finds the cheat list in the arm7 memory of the game, where Pico Loader placed it;
//   2. relocates the DLDI driver of the resident part for its final address;
//   3. chooses the memory with the game's code that the menu image replaces while the menu is open;
//   4. hooks OS_IrqHandler of the game, so that the resident part runs on every interrupt.
// Finally entry.s starts the game. installer_header.status tells how far the installer got.

#include <nds/ndstypes.h>
#include "ParasiteDefinitions.h"
#include "GameScanner.h"
#include "ndsHeader.h"
#include "dldiHeader.h"

// Defined in entry.s.
extern "C" parasite_installer_header_t installer_header;
extern "C" void installer_flush();

static_assert(sizeof(cheat_menu_resident_t) == 104, "Must match the header in resident/entry.s");
static_assert(sizeof(parasite_installer_header_t) == 16, "Must match the header in installer/entry.s");

/// @brief Adds delta to every word in [begin, end) that points into [oldStart, oldEnd).
static void relocateRange(u32* begin, u32* end, u32 oldStart, u32 oldEnd, u32 delta)
{
    for (; begin < end; begin++)
    {
        if (*begin >= oldStart && *begin < oldEnd)
            *begin += delta;
    }
}

/// @brief Relocates the DLDI driver at \p driver for the given address, where the game sees it. The bss of the driver
///        keeps the state that the launcher left.
static void relocateDldi(dldi_header_t* driver, u32 address)
{
    u32 oldStart = driver->driverStartAddress;
    u32 oldEnd = driver->driverEndAddress;
    u32 delta = address - oldStart;
    u8* bytes = (u8*)driver;
    if (driver->fixFlags & DLDI_FIX_ALL)
    {
        relocateRange((u32*)(bytes + sizeof(dldi_header_t)), (u32*)(bytes + oldEnd - oldStart), oldStart, oldEnd,
            delta);
    }
    else
    {
        if (driver->fixFlags & DLDI_FIX_GLUE)
        {
            relocateRange((u32*)(bytes + driver->glueStartAddress - oldStart),
                (u32*)(bytes + driver->glueEndAddress - oldStart), oldStart, oldEnd, delta);
        }
        if (driver->fixFlags & DLDI_FIX_GOT)
        {
            relocateRange((u32*)(bytes + driver->gotStartAddress - oldStart),
                (u32*)(bytes + driver->gotEndAddress - oldStart), oldStart, oldEnd, delta);
        }
    }
    // The address fields of the header, from driverStartAddress on, except driverMagic and featureFlags.
    u32* fields = &driver->driverStartAddress;
    for (u32 i = 0; i < 16; i++)
    {
        if (i != 8 && i != 9 && fields[i] != 0)
            fields[i] += delta;
    }
}

/// @brief Returns the address where the code at the given address of the static arm9 binary ends up after the
///        autoloads (for example code that is copied to the ITCM).
static u32 getFinalAddress(u32 address, const game_scanner_module_params_t* params)
{
    u32 source = params->autoloadStart;
    u32 stride = params->sdkVersion >= 0x05000000 ? 16 : 12;
    for (u32 entry = params->autoloadListStart; entry < params->autoloadListEnd; entry += stride)
    {
        // autoload_list_entry_t: target address, size, ...
        const u32* fields = (const u32*)entry;
        if (address >= source && address - source < fields[1])
            return fields[0] + address - source;
        source += fields[1];
    }
    return address;
}

/// @brief Finds the cheat list with the given length in [start, end). Pico Loader places it at the start of the arm7
///        main memory arena of the game.
static cheat_menu_list_header_t* findList(u32 start, u32 end, u32 length)
{
    for (u32 address = start; address + sizeof(cheat_menu_list_header_t) <= end; address += 4)
    {
        auto list = (cheat_menu_list_header_t*)address;
        if (list->length == length && list->markerLength == sizeof(list->marker) &&
            list->marker[0] == CHEAT_MENU_MARKER_OPCODE && list->marker[1] == CHEAT_MENU_LIST_MAGIC0 &&
            list->marker[3] == CHEAT_MENU_LIST_MAGIC1 && list->marker[5] + list->marker[7] == length &&
            length <= end - address)
        {
            return list;
        }
    }
    return nullptr;
}

extern "C" void installer_run()
{
    auto& header = installer_header;
    header.status = PARASITE_INSTALLER_STATUS_INVALID_SDK_DATA;
    auto rom = (const nds_header_ntr_t*)0x02FFFE00;
    bool dsiMode = ((*(vu32*)0x04004008 >> 14) & 3) >= 2; // SCFG_EXT: more than 4 MB of main memory
    u32 arenaEnd = dsiMode ? 0x02F80000 : 0x023E0000;
    if (rom->arm9LoadAddress < 0x02000000 || rom->arm9LoadAddress >= arenaEnd ||
        rom->arm9Size > arenaEnd - rom->arm9LoadAddress)
        return;
    u32* code = (u32*)rom->arm9LoadAddress;
    int moduleParams = gameScanner_findModuleParams(code, rom->arm9Size / 4);
    if (moduleParams < 0)
        return;
    auto params = (const game_scanner_module_params_t*)(code + moduleParams);
    u32 stride = params->sdkVersion >= 0x05000000 ? 16 : 12;
    if (params->autoloadListStart < rom->arm9LoadAddress || params->autoloadListEnd < params->autoloadListStart ||
        params->autoloadListEnd >= arenaEnd || (params->autoloadListEnd - params->autoloadListStart) % stride)
        return;
    // OS_IrqHandler may be in an autoload, which is still where it was loaded, before the autoload list.
    u32 scanSize = params->autoloadListEnd - rom->arm9LoadAddress;
    if (scanSize < rom->arm9Size)
        scanSize = rom->arm9Size;
    u32 continueWords;
    int irqHandler = gameScanner_findIrqHandler(code, scanSize / 4, &continueWords);
    header.status = PARASITE_INSTALLER_STATUS_NO_IRQ_HANDLER;
    if (irqHandler < 0)
        return;
    u32 irqHandlerAddress = getFinalAddress((u32)(code + irqHandler), params);

    header.status = PARASITE_INSTALLER_STATUS_NO_CHEAT_LIST;
    // The arm7 dedicated main memory. In DS mode it is mirrored every 4 MB and Pico Loader may write it through another
    // mirror; the game, and so the resident part, use the 0x023xxxxx addresses.
    cheat_menu_list_header_t* list = dsiMode ? findList(0x02F80000, 0x02FFC000, header.listLength) : nullptr;
    if (!list)
        list = findList(0x02380000, 0x02400000, header.listLength);
    if (!list && !dsiMode)
        list = findList(0x02B80000, 0x02C00000, header.listLength);
    if (!list)
        return;
    u32 writtenAddress = (u32)list;
    u32 seenAddress = dsiMode ? writtenAddress : 0x02000000 | (writtenAddress & 0x003FFFFF);
    // Converts an address where the installer writes to the address where the game sees the same memory.
    auto toGameAddress = [&](const void* address) { return (u32)address - writtenAddress + seenAddress; };
    auto resident = (cheat_menu_resident_t*)((u8*)list + list->marker[5]);

    // While the menu is open, the menu image replaces memory with the game's code: after the secure area, where Pico
    // Loader puts its own patches, and in the first half of the static arm9 binary, before its data. The game is
    // paused meanwhile, and neither the arm7 nor DMA access that code.
    header.status = PARASITE_INSTALLER_STATUS_NO_MENU_MEMORY;
    u32 menuAddress = (rom->arm9LoadAddress + 0x4000 + 31) & ~31u;
    u32 menuSize = resident->imageSectors * CHEAT_MENU_SECTOR_SIZE;
    u32 staticEnd = params->autoloadStart;
    if (staticEnd <= rom->arm9LoadAddress || staticEnd > arenaEnd)
        return;
    u32 limit = rom->arm9LoadAddress + (staticEnd - rom->arm9LoadAddress) / 2;
    if (menuSize == 0 || menuAddress >= limit || menuSize > limit - menuAddress ||
        (irqHandlerAddress < menuAddress + menuSize && irqHandlerAddress + continueWords * 4 > menuAddress))
        return;

    auto driver = (dldi_header_t*)((u8*)resident + resident->dldiOffset);
    relocateDldi(driver, toGameAddress(driver));
    resident->dldiDriver = (const void*)toGameAddress(driver);
    resident->list = (cheat_menu_list_header_t*)toGameAddress(list);
    resident->menuAddress = menuAddress;
    resident->irqHandlerContinueAddress = irqHandlerAddress + continueWords * 4;
    resident->cardLockAddress = params->sdkVersion >= 0x05000000 ? 0x02FFFFE0 : 0x027FFFE0;

    // The game protects the arm7 memory from the arm9 (a protection region without access). The first words of
    // OS_IrqHandler are therefore replaced by code that saves the access permissions, allows read/write access in
    // every protection region for data and instructions, and jumps to the hook of the resident part. The hook restores
    // the data permissions and does what the replaced instructions did.
    u32* hook = code + irqHandler;
    hook[0] = 0xEE150F50; // mrc p15, 0, r0, c5, c0, 2 (data access permissions)
    hook[1] = 0xEE151F70; // mrc p15, 0, r1, c5, c0, 3 (instruction access permissions)
    hook[2] = 0xE3A02033; // mov r2, #0x33
    hook[3] = 0xE1822402; // orr r2, r2, r2, lsl #8
    hook[4] = 0xE1822802; // orr r2, r2, r2, lsl #16
    hook[5] = 0xEE052F50; // mcr p15, 0, r2, c5, c0, 2
    hook[6] = 0xEE052F70; // mcr p15, 0, r2, c5, c0, 3
    hook[7] = 0xE51FF004; // ldr pc, [pc, #-4]
    hook[8] = toGameAddress(resident) + resident->irqHookOffset;
    header.status = PARASITE_INSTALLER_STATUS_INSTALLED;
    installer_flush();
}
