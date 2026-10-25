#include "common.h"
#include <string.h>
#include <stdio.h>
#include "globalHeap.h"
#include "loader/DldiDriver.h"
#include "loader/CheatDatabase.h"
#include "loader/CheatPreprocessor.h"
#include "ParasiteDefinitions.h"
#include "GameScanner.h"
#include "ndsHeader.h"
#include "cheatMenu_bin.h"
#include "resident_bin.h"

FATFS gFatFs;
DldiDriver* gDiskDriver;
extern "C" void __libc_init_array();
extern "C" u8 arm7HandoffStart[], arm7HandoffEnd[];
static u32 sNext = PARASITE_MAILBOX_ADDRESS + sizeof(parasite_mailbox_t);

static void sync(u32 value) { *(vu16*)0x04000180 = value << 8; }
static void waitSync(u32 value) { while ((*(vu16*)0x04000180 & 15) != value); }
static void* reserve(u32 size)
{
    u32 address = (sNext + 31) & ~31;
    if (size > PARASITE_MAILBOX_ADDRESS + PARASITE_MAILBOX_SIZE - address) return nullptr;
    sNext = address + size;
    return (void*)address;
}
static void fail()
{
    while (true);
}
static u32 align32(u32 n) { return (n + 31) & ~31u; }
static u32 alignSector(u32 n) { return (n + CHEAT_MENU_SECTOR_SIZE - 1) & ~(CHEAT_MENU_SECTOR_SIZE - 1); }
static bool validCheats(const pload_cheats_t* cheats)
{
    if (cheats->length < 8 || cheats->length > 0x10000 || (cheats->length & 3) ||
        cheats->numberOfCheats > (cheats->length - 8) / 4) return false;
    u32 position = 8;
    for (u32 i = 0; i < cheats->numberOfCheats; i++)
    {
        if (cheats->length - position < 4) return false;
        u32 size = *(const u32*)((const u8*)cheats + position);
        if ((size & 7) || size > cheats->length - position - 4) return false;
        position += 4 + size;
    }
    return position == cheats->length;
}
static bool readAt(FIL* file, u32 offset, void* buffer, u32 size)
{
    UINT got;
    return f_lseek(file, offset) == FR_OK && f_read(file, buffer, size, &got) == FR_OK && got == size;
}
static void* loadFile(const char* path, u32 maximum, u32& size)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) return nullptr;
    size = f_size(&file);
    void* data = size && size <= maximum ? reserve((size + 3) & ~3) : nullptr;
    if (data && !readAt(&file, 0, data, size)) data = nullptr;
    f_close(&file);
    return data;
}
// BLZ backward decompression, bounded to the scratch area, never to the game's load addresses.
static bool decompress(u8* data, u32 inputSize, u32 endOffset, u32& outputSize)
{
    outputSize = inputSize;
    if (!endOffset) return true;
    if (endOffset < 8 || endOffset > inputSize) return false;
    u32 footer = *(u32*)(data + endOffset - 8);
    u32 extra = *(u32*)(data + endOffset - 4);
    if (!extra) return true; // decompressed ROM hack
    u32 headerSize = footer >> 24, packedSize = footer & 0xFFFFFF;
    if (extra > PARASITE_SCRATCH_SIZE - endOffset || headerSize < 8 || headerSize > packedSize || packedSize > endOffset)
        return false;
    u32 src = endOffset - headerSize, stop = endOffset - packedSize, dst = endOffset + extra;
    outputSize = dst;
    while (src > stop)
    {
        u8 flags = data[--src];
        for (u32 mask = 128; mask && src > stop; mask >>= 1)
        {
            if (flags & mask)
            {
                if (src - stop < 2) return false;
                u32 hi = data[--src], lo = data[--src];
                u32 distance = (((hi & 15) << 8) | lo) + 3;
                u32 length = (hi >> 4) + 3;
                if (dst < stop + length || dst + distance > outputSize) return false;
                while (length--) { --dst; data[dst] = data[dst + distance]; }
            }
            else { if (dst <= stop) return false; data[--dst] = data[--src]; }
        }
    }
    return dst == stop;
}
// The menu image: the menu code, followed by the cheat menu data of the game at a sector boundary.
static const u8* buildMenuImage(const CheatDatabase& database, const pload_cheats_t* processed, u32& imageSize)
{
    auto menu = (const cheat_menu_image_header_t*)cheatMenu_bin;
    u32 dataOffset = alignSector(menu->totalSize);
    imageSize = alignSector(dataOffset + database.GetImageDataSize());
    auto image = (u8*)reserve(imageSize);
    if (!image) return nullptr;
    memset(image, 0, imageSize);
    memcpy(image, cheatMenu_bin, cheatMenu_bin_size);
    ((cheat_menu_image_header_t*)image)->dataOffset = dataOffset;
    database.WriteImageData((cheat_menu_data_t*)(image + dataOffset), processed);
    return image;
}
// The cheat list for the real Pico Loader: the marker, the launcher cheats as they are (Pico Loader preprocesses
// them), room to turn on more cheats, then the resident part with the dldi driver.
static cheat_menu_list_header_t* buildCheatList(const parasite_mailbox_t* box, u32 activeCheatsSize, u32 imageSize)
{
    auto raw = (const pload_cheats_t*)box->launcherCheats;
    auto driver = (const dldi_header_t*)box->dldiDriver;
    // only the used part of the driver, with its bss as the launcher left it
    u32 driverEnd = driver->driverEndAddress > driver->bssEndAddress ? driver->driverEndAddress : driver->bssEndAddress;
    u32 driverSize = driverEnd > driver->driverStartAddress && driverEnd - driver->driverStartAddress <=
        box->dldiDriverSize ? driverEnd - driver->driverStartAddress : box->dldiDriverSize;
    auto blob = (const cheat_menu_resident_t*)resident_bin;
    u32 residentOffset = align32(sizeof(cheat_menu_list_header_t) + activeCheatsSize);
    u32 dldiOffset = align32(blob->totalSize);
    u32 residentSize = dldiOffset + align32(driverSize);
    u32 length = residentOffset + residentSize;
    auto list = (cheat_menu_list_header_t*)reserve(length);
    if (!list) return nullptr;
    memset(list, 0, length);
    list->length = length;
    list->numberOfCheats = 1;
    list->markerLength = sizeof(list->marker);
    const u32 marker[8] = { CHEAT_MENU_MARKER_OPCODE, CHEAT_MENU_LIST_MAGIC0, CHEAT_MENU_MARKER_OPCODE,
        CHEAT_MENU_LIST_MAGIC1, CHEAT_MENU_MARKER_OPCODE, residentOffset, CHEAT_MENU_MARKER_OPCODE, residentSize };
    memcpy(list->marker, marker, sizeof(marker));
    if (raw)
    {
        memcpy(list + 1, &raw->firstCheat, raw->length - 8);
        list->numberOfCheats += raw->numberOfCheats;
    }
    u8* resident = (u8*)list + residentOffset;
    memcpy(resident, resident_bin, resident_bin_size);
    auto header = (cheat_menu_resident_t*)resident;
    header->dldiOffset = dldiOffset;
    header->imageSectors = imageSize / CHEAT_MENU_SECTOR_SIZE;
    memcpy(resident + dldiOffset, driver, driverSize);
    return list;
}
// The sectors of the cheat menu file, from its cluster map.
static bool getFileRuns(FIL* file, u32 sectors, cheat_menu_resident_t* resident)
{
    DWORD clusterMap[1 + 2 * CHEAT_MENU_MAX_FILE_RUNS + 1];
    clusterMap[0] = sizeof(clusterMap) / sizeof(clusterMap[0]);
    file->cltbl = clusterMap;
    FRESULT result = f_lseek(file, CREATE_LINKMAP);
    file->cltbl = nullptr;
    if (result != FR_OK) return false; // too fragmented
    const FATFS* fs = file->obj.fs;
    resident->fileRunCount = 0;
    for (const DWORD* fragment = &clusterMap[1]; fragment[0] != 0 && sectors != 0; fragment += 2)
    {
        u32 count = fragment[0] * fs->csize;
        if (count > sectors) count = sectors;
        auto& run = resident->fileRuns[resident->fileRunCount++];
        run.sector = fs->database + (fragment[1] - 2) * fs->csize;
        run.count = count;
        sectors -= count;
    }
    return sectors == 0;
}
// Writes the menu image to the cheat menu file, followed by as many sectors for the memory that the menu borrows.
static bool writeMenuFile(const u8* image, u32 imageSize, cheat_menu_resident_t* resident)
{
    FIL file;
    if (f_open(&file, CHEAT_MENU_FILE_PATH, FA_OPEN_ALWAYS | FA_READ | FA_WRITE) != FR_OK) return false;
    UINT written;
    u32 fileSize = 2 * imageSize;
    bool ok = (f_size(&file) >= fileSize || (f_lseek(&file, fileSize) == FR_OK && f_tell(&file) == fileSize)) &&
        f_lseek(&file, 0) == FR_OK && f_write(&file, image, imageSize, &written) == FR_OK && written == imageSize &&
        f_sync(&file) == FR_OK && getFileRuns(&file, fileSize / CHEAT_MENU_SECTOR_SIZE, resident);
    f_close(&file);
    return ok;
}
static bool prepareGame(parasite_mailbox_t* box)
{
    FIL file;
    if (f_open(&file, gLoaderHeader.loadParams.romPath, FA_READ) != FR_OK) return false;
    alignas(4) u8 header[512];
    bool valid = readAt(&file, 0, header, sizeof(header));
    auto rom = (const nds_header_ntr_t*)header;
    auto data = (u32*)PARASITE_SCRATCH_ADDRESS;
    u32 size = rom->arm9Size;
    valid = valid && size >= 36 && size <= PARASITE_SCRATCH_SIZE &&
        readAt(&file, rom->arm9RomOffset, data, size);
    int module = valid ? gameScanner_findModuleParams(data, size / 4) : -1;
    valid = valid && module >= 0;
    if (valid)
    {
        auto params = (game_scanner_module_params_t*)(data + module);
        u32 end = params->compressedEnd;
        valid = !end || (end >= rom->arm9LoadAddress &&
            decompress((u8*)data, size, end - rom->arm9LoadAddress, size));
    }
    u32 continueWords;
    valid = valid && gameScanner_findIrqHandler(data, size / 4, &continueWords) >= 0;
    f_close(&file);
    if (!valid) return false;
    CheatDatabase database;
    auto raw = (const pload_cheats_t*)box->launcherCheats;
    database.Load(header, raw, PARASITE_MAX_ACTIVE_CHEATS_SIZE);
    if (!database.HasCheats()) return false;
    pload_cheats_t* processed = nullptr;
    if (raw)
    {
        processed = (pload_cheats_t*)malloc(raw->length);
        if (!processed) return false;
        memcpy(processed, raw, raw->length);
        auto cheat = &processed->firstCheat;
        for (u32 i = 0; i < processed->numberOfCheats; i++)
        {
            CheatPreprocessor().PreprocessCheat(cheat);
            cheat = (pload_cheat_t*)((u8*)cheat + 4 + cheat->length);
        }
    }
    u32 imageSize;
    const u8* image = buildMenuImage(database, processed, imageSize);
    free(processed);
    if (!image) return false;
    auto list = buildCheatList(box, database.GetActiveCheatsSize(), imageSize);
    if (!list) return false;
    auto resident = (cheat_menu_resident_t*)((u8*)list + list->marker[5]);
    if (!writeMenuFile(image, imageSize, resident)) return false;
    box->cheatList = list;
    return true;
}
extern "C" void loaderMain()
{
    // Copy launcher-owned objects before ARM9 is allowed to change any mappings or RAM.
    auto box = (parasite_mailbox_t*)PARASITE_MAILBOX_ADDRESS;
    memset(box, 0, sizeof(*box));
    static_assert(sizeof(gLoaderHeader) <= sizeof(box->launcherHeader));
    memcpy(box->launcherHeader, &gLoaderHeader, sizeof(gLoaderHeader));
    box->launcherHeaderSize = sizeof(gLoaderHeader);
    auto driver = (const dldi_header_t*)gLoaderHeader.dldiDriver;
    if (driver && driver->dldiMagic == DLDI_MAGIC && driver->driverSize >= 7 && driver->driverSize <= 15)
    {
        box->dldiDriverSize = 1u << driver->driverSize;
        void* copy = reserve(box->dldiDriverSize);
        memcpy(copy, driver, box->dldiDriverSize);
        box->dldiDriver = copy;
    }
    auto cheats = gLoaderHeader.v3.cheats;
    auto list = (const cheat_menu_list_header_t*)cheats;
    if (cheats && cheats->length >= sizeof(*list) && cheats->numberOfCheats >= 1 &&
        list->markerLength == sizeof(list->marker) && list->marker[1] == CHEAT_MENU_LIST_MAGIC0 &&
        list->marker[3] == CHEAT_MENU_LIST_MAGIC1 && list->marker[5] >= sizeof(*list) && list->marker[5] <= list->length)
    {
        // After a soft reset Pico Loader passes back the list of the parasite: keep the enabled cheats after the marker.
        auto first = (const u8*)(list + 1);
        u32 room = list->marker[5] - sizeof(*list), size = 0, count = 0;
        while (count < list->numberOfCheats - 1 && room - size >= 4)
        {
            u32 cheatLength = *(const u32*)(first + size);
            if ((cheatLength & 7) || cheatLength > room - size - 4) break;
            size += 4 + cheatLength;
            count++;
        }
        auto copy = count ? (pload_cheats_t*)reserve(8 + size) : nullptr;
        if (copy)
        {
            copy->length = 8 + size;
            copy->numberOfCheats = count;
            memcpy(&copy->firstCheat, first, size);
            box->launcherCheats = copy;
        }
    }
    else if (cheats && validCheats(cheats))
    {
        void* copy = reserve(cheats->length);
        memcpy(copy, cheats, cheats->length);
        box->launcherCheats = copy;
    }
    sync(PARASITE_SYNC_LAUNCHER_DATA_COPIED);
    waitSync(PARASITE_SYNC_CARD_TO_ARM7);
    heap_init();
    __libc_init_array();
    if (gLoaderHeader.bootDrive != PLOAD_BOOT_DRIVE_DLDI || !box->dldiDriver) fail();
    auto workDriver = (dldi_header_t*)malloc(box->dldiDriverSize);
    if (!workDriver) fail();
    memcpy(workDriver, box->dldiDriver, box->dldiDriverSize);
    DldiDriver disk(workDriver);
    disk.Relocate();
    disk.PrepareForUse();
    gDiskDriver = &disk;
    if (!disk.Startup() || f_mount(&gFatFs, "fat:", 1) != FR_OK) fail();
    f_chdrive("fat:");
    box->realLoader7 = loadFile(PARASITE_REAL_LOADER7_PATH, 0x40000, box->realLoader7Size);
    box->realLoader9 = loadFile(PARASITE_REAL_LOADER9_PATH, 0x20000, box->realLoader9Size);
    if (!box->realLoader7 || !box->realLoader9 || box->realLoader7Size < sizeof(pload_header7_t)) fail();
    box->realLoaderLoaded = 1;
    FIL flag;
    bool transparent = f_open(&flag, "/_pico/parasite-transparent", FA_READ) == FR_OK;
    if (transparent) f_close(&flag);
    box->installCheatMenu = !transparent && prepareGame(box);
    sync(PARASITE_SYNC_PREPARED);
    waitSync(PARASITE_SYNC_ARM9_DECIDED);
    // A tiny ARM stub survives after ARM9 reclaims VRAM C/D. No more stack or VRAM access.
    memcpy((void*)0x0380F000, arm7HandoffStart, arm7HandoffEnd - arm7HandoffStart);
    auto header = (const pload_header7_t*)box->realLoader7;
    ((void(*)(void*))0x0380F000)(header->entryPoint);
    while (true);
}
