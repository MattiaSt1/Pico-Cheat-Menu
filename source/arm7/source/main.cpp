// Arm7 part of the Pico Cheat Parasite.
//
// The launcher boots the parasite as if it were Pico Loader. The arm7 part copies what the launcher passed (the load
// parameters, the DLDI driver and the cheats) to the mailbox, mounts the sd card with the DLDI driver and loads the
// real Pico Loader from /_pico/loader/. Then it prepares the in-game cheat menu of the game:
//   1. It checks that the arm9 binary of the game contains the SDK function that the menu hooks (OS_IrqHandler).
//   2. It indexes the cheats of the game in usrcheat.dat (CheatDatabase).
//   3. It writes the menu image (the menu code and the cheat menu data) to the cheat menu file.
//   4. It builds the cheat list for the real Pico Loader: the marker, the launcher cheats, room for more cheats, and
//      the resident part of the menu with a copy of the DLDI driver.
// Meanwhile the arm9 part shows the progress on the status screen. It then decides whether the real Pico Loader can
// run the menu and boots it (see arm9/source/main.cpp).

#include "common.h"
#include <string.h>
#include "globalHeap.h"
#include "loader/DldiDriver.h"
#include "loader/CheatDatabase.h"
#include "loader/CheatPreprocessor.h"
#include "ParasiteDefinitions.h"
#include "GameScanner.h"
#include "ndsHeader.h"
#include "cheatMenu_bin.h"
#include "resident_bin.h"

/// @brief Address in arm7 WRAM of the stub that hands over to the real Pico Loader (handoff.s).
#define HANDOFF_STUB_ADDRESS    0x0380F000

/// @brief Maximum sizes of the binaries of the real Pico Loader.
#define REAL_LOADER7_MAX_SIZE   0x40000
#define REAL_LOADER9_MAX_SIZE   0x20000

FATFS gFatFs;
DldiDriver* gDiskDriver;

extern "C" void __libc_init_array();
extern "C" u8 arm7HandoffStart[], arm7HandoffEnd[];

/// @brief The next free address after the mailbox, for \see reserve.
static u32 sNextFree = PARASITE_MAILBOX_ADDRESS + sizeof(parasite_mailbox_t);

static u32 align32(u32 value)
{
    return (value + 31) & ~31u;
}

static u32 alignToSector(u32 value)
{
    return (value + CHEAT_MENU_SECTOR_SIZE - 1) & ~(CHEAT_MENU_SECTOR_SIZE - 1);
}

/// @brief Allocates memory after the mailbox, 32-byte aligned, for data that the arm9 part or the real Pico Loader
///        uses. This memory is never freed. Returns \c nullptr if there is not enough room.
static void* reserve(u32 size)
{
    u32 address = align32(sNextFree);
    if (size > PARASITE_MAILBOX_ADDRESS + PARASITE_MAILBOX_SIZE - address)
        return nullptr;
    sNextFree = address + size;
    return (void*)address;
}

/// @brief Reports a fatal error to the arm9 part, which shows it on the status screen, and stops.
[[noreturn]] static void fail(u32 error)
{
    ((parasite_mailbox_t*)PARASITE_MAILBOX_ADDRESS)->error = error;
    while (true);
}

static bool readAt(FIL* file, u32 offset, void* buffer, u32 size)
{
    UINT bytesRead;
    return f_lseek(file, offset) == FR_OK && f_read(file, buffer, size, &bytesRead) == FR_OK && bytesRead == size;
}

static bool fileExists(const char* path)
{
    FILINFO info;
    return f_stat(path, &info) == FR_OK;
}

/// @brief Loads the given file into reserved memory. Returns \c nullptr if it is missing, empty, larger than
///        \p maximumSize or cannot be read.
static void* loadFile(const char* path, u32 maximumSize, u32& size)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK)
        return nullptr;
    size = f_size(&file);
    void* data = size != 0 && size <= maximumSize ? reserve((size + 3) & ~3) : nullptr;
    if (data && !readAt(&file, 0, data, size))
        data = nullptr;
    f_close(&file);
    return data;
}

/// @brief Returns whether the given rom path names the boot file of the DSpico (\see PARASITE_BOOT_FILE_NAME).
static bool isBootFile(const char* romPath, u32 maximumLength)
{
    u32 length = strnlen(romPath, maximumLength);
    if (length == maximumLength)
        return false;
    u32 nameStart = length;
    while (nameStart != 0 && romPath[nameStart - 1] != '/' && romPath[nameStart - 1] != ':')
        nameStart--;
    const char* name = romPath + nameStart;
    const char* bootFileName = PARASITE_BOOT_FILE_NAME;
    u32 i = 0;
    for (; bootFileName[i] != 0; i++)
    {
        char c = name[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != bootFileName[i])
            return false;
    }
    return name[i] == 0;
}

/// @brief Checks the structure of a cheat list passed by the launcher.
static bool isValidCheatList(const pload_cheats_t* cheats)
{
    if (cheats->length < 8 || cheats->length > 0x10000 || (cheats->length & 3) ||
        cheats->numberOfCheats > (cheats->length - 8) / 4)
        return false;
    u32 position = 8;
    for (u32 i = 0; i < cheats->numberOfCheats; i++)
    {
        if (cheats->length - position < 4)
            return false;
        u32 size = *(const u32*)((const u8*)cheats + position);
        if ((size & 7) || size > cheats->length - position - 4)
            return false;
        position += 4 + size;
    }
    return position == cheats->length;
}

/// @brief Copies the DLDI driver of the launcher to the mailbox. It stays relocated for its address in the launcher.
static void copyDldiDriver(parasite_mailbox_t* box)
{
    auto driver = (const dldi_header_t*)gLoaderHeader.dldiDriver;
    if (!driver || driver->dldiMagic != DLDI_MAGIC || driver->driverSize < 7 || driver->driverSize > 15)
        return;
    u32 size = 1u << driver->driverSize;
    void* copy = reserve(size);
    if (!copy)
        return;
    memcpy(copy, driver, size);
    box->dldiDriver = copy;
    box->dldiDriverSize = size;
}

/// @brief Copies the cheats passed by the launcher to the mailbox. After a soft reset of the game, Pico Loader passes
///        back the cheat list of the parasite: then only the enabled cheats after the marker are kept.
static void copyLauncherCheats(parasite_mailbox_t* box)
{
    auto cheats = gLoaderHeader.v3.cheats;
    if (!cheats)
        return;
    auto list = (const cheat_menu_list_header_t*)cheats;
    if (cheats->length >= sizeof(*list) && cheats->numberOfCheats >= 1 &&
        list->markerLength == sizeof(list->marker) && list->marker[1] == CHEAT_MENU_LIST_MAGIC0 &&
        list->marker[3] == CHEAT_MENU_LIST_MAGIC1 && list->marker[5] >= sizeof(*list) &&
        list->marker[5] <= list->length)
    {
        auto first = (const u8*)(list + 1);
        u32 room = list->marker[5] - sizeof(*list);
        u32 size = 0;
        u32 count = 0;
        while (count < list->numberOfCheats - 1 && room - size >= 4)
        {
            u32 cheatLength = *(const u32*)(first + size);
            if ((cheatLength & 7) || cheatLength > room - size - 4)
                break;
            size += 4 + cheatLength;
            count++;
        }
        auto copy = count != 0 ? (pload_cheats_t*)reserve(8 + size) : nullptr;
        if (copy)
        {
            copy->length = 8 + size;
            copy->numberOfCheats = count;
            memcpy(&copy->firstCheat, first, size);
            box->launcherCheats = copy;
        }
    }
    else if (isValidCheatList(cheats))
    {
        void* copy = reserve(cheats->length);
        if (copy)
        {
            memcpy(copy, cheats, cheats->length);
            box->launcherCheats = copy;
        }
    }
}

/// @brief Decompresses the BLZ-compressed arm9 binary at \p data in place. The output must fit in the scratch area,
///        so it never overwrites the load addresses of the game.
/// @param data The arm9 binary, at \see PARASITE_SCRATCH_ADDRESS.
/// @param inputSize The size of the arm9 binary.
/// @param endOffset The offset of the end of the compressed data, from the module params.
/// @param outputSize Receives the size of the decompressed binary.
/// @return True on success, false if the compressed data is invalid.
static bool decompress(u8* data, u32 inputSize, u32 endOffset, u32& outputSize)
{
    outputSize = inputSize;
    if (endOffset == 0)
        return true;
    if (endOffset < 8 || endOffset > inputSize)
        return false;
    u32 footer = *(u32*)(data + endOffset - 8);
    u32 extraSize = *(u32*)(data + endOffset - 4);
    if (extraSize == 0)
        return true; // a decompressed rom hack
    u32 headerSize = footer >> 24;
    u32 packedSize = footer & 0xFFFFFF;
    if (extraSize > PARASITE_SCRATCH_SIZE - endOffset || headerSize < 8 || headerSize > packedSize ||
        packedSize > endOffset)
        return false;
    u32 source = endOffset - headerSize;
    u32 stop = endOffset - packedSize;
    u32 destination = endOffset + extraSize;
    outputSize = destination;
    while (source > stop)
    {
        u8 flags = data[--source];
        for (u32 mask = 0x80; mask != 0 && source > stop; mask >>= 1)
        {
            if (flags & mask)
            {
                // A copy of 3-18 bytes from 3-4098 bytes higher in the output, which is decompressed from the end.
                if (source - stop < 2)
                    return false;
                u32 high = data[--source];
                u32 low = data[--source];
                u32 distance = (((high & 0xF) << 8) | low) + 3;
                u32 length = (high >> 4) + 3;
                if (destination < stop + length || destination + distance > outputSize)
                    return false;
                while (length--)
                {
                    destination--;
                    data[destination] = data[destination + distance];
                }
            }
            else
            {
                if (destination <= stop)
                    return false;
                data[--destination] = data[--source];
            }
        }
    }
    return destination == stop;
}

/// @brief Reads the first 512 bytes of the rom header of the game.
static bool readRomHeader(void* header)
{
    FIL file;
    if (f_open(&file, gLoaderHeader.loadParams.romPath, FA_READ) != FR_OK)
        return false;
    bool ok = readAt(&file, 0, header, 512);
    f_close(&file);
    return ok;
}

/// @brief Returns whether the cheat menu can hook the game: its arm9 binary, decompressed if needed, contains
///        OS_IrqHandler in one of the known variants.
static bool isGameSupported(const nds_header_ntr_t* romHeader)
{
    FIL file;
    if (f_open(&file, gLoaderHeader.loadParams.romPath, FA_READ) != FR_OK)
        return false;
    auto data = (u32*)PARASITE_SCRATCH_ADDRESS;
    u32 size = romHeader->arm9Size;
    bool loaded = size >= 36 && size <= PARASITE_SCRATCH_SIZE && readAt(&file, romHeader->arm9RomOffset, data, size);
    f_close(&file);
    int moduleParams = loaded ? gameScanner_findModuleParams(data, size / 4) : -1;
    if (moduleParams < 0)
        return false;
    u32 compressedEnd = ((const game_scanner_module_params_t*)(data + moduleParams))->compressedEnd;
    if (compressedEnd != 0 && (compressedEnd < romHeader->arm9LoadAddress ||
        !decompress((u8*)data, size, compressedEnd - romHeader->arm9LoadAddress, size)))
        return false;
    u32 continueWords;
    return gameScanner_findIrqHandler(data, size / 4, &continueWords) >= 0;
}

/// @brief Builds the menu image: the menu code, followed by the cheat menu data of the game at a sector boundary.
static const u8* buildMenuImage(const CheatDatabase& database, const pload_cheats_t* processedCheats, u32& imageSize)
{
    auto menu = (const cheat_menu_image_header_t*)cheatMenu_bin;
    u32 dataOffset = alignToSector(menu->totalSize);
    imageSize = alignToSector(dataOffset + database.GetImageDataSize());
    auto image = (u8*)reserve(imageSize);
    if (!image)
        return nullptr;
    // The zero-initialized data of the menu is part of the image, so it is cleared every time the image is loaded.
    memset(image, 0, imageSize);
    memcpy(image, cheatMenu_bin, cheatMenu_bin_size);
    ((cheat_menu_image_header_t*)image)->dataOffset = dataOffset;
    database.WriteImageData((cheat_menu_data_t*)(image + dataOffset), processedCheats);
    return image;
}

/// @brief Builds the cheat list for the real Pico Loader: the marker, the launcher cheats as they are (Pico Loader
///        preprocesses them), room to turn on more cheats, and the resident part with the DLDI driver.
static cheat_menu_list_header_t* buildCheatList(const parasite_mailbox_t* box, u32 activeCheatsSize, u32 imageSize)
{
    auto launcherCheats = (const pload_cheats_t*)box->launcherCheats;
    auto driver = (const dldi_header_t*)box->dldiDriver;
    // Only the used part of the driver is copied, with its bss as the launcher left it.
    u32 driverEnd = driver->driverEndAddress > driver->bssEndAddress ? driver->driverEndAddress :
        driver->bssEndAddress;
    u32 driverSize = driverEnd > driver->driverStartAddress &&
        driverEnd - driver->driverStartAddress <= box->dldiDriverSize ? driverEnd - driver->driverStartAddress :
        box->dldiDriverSize;
    auto blob = (const cheat_menu_resident_t*)resident_bin;
    u32 residentOffset = align32(sizeof(cheat_menu_list_header_t) + activeCheatsSize);
    u32 dldiOffset = align32(blob->totalSize);
    u32 residentSize = dldiOffset + align32(driverSize);
    u32 length = residentOffset + residentSize;
    auto list = (cheat_menu_list_header_t*)reserve(length);
    if (!list)
        return nullptr;
    memset(list, 0, length);
    list->length = length;
    list->numberOfCheats = 1;
    list->markerLength = sizeof(list->marker);
    const u32 marker[8] =
    {
        CHEAT_MENU_MARKER_OPCODE, CHEAT_MENU_LIST_MAGIC0, CHEAT_MENU_MARKER_OPCODE, CHEAT_MENU_LIST_MAGIC1,
        CHEAT_MENU_MARKER_OPCODE, residentOffset, CHEAT_MENU_MARKER_OPCODE, residentSize
    };
    memcpy(list->marker, marker, sizeof(marker));
    if (launcherCheats)
    {
        memcpy(list + 1, &launcherCheats->firstCheat, launcherCheats->length - 8);
        list->numberOfCheats += launcherCheats->numberOfCheats;
    }
    u8* resident = (u8*)list + residentOffset;
    memcpy(resident, resident_bin, resident_bin_size);
    auto header = (cheat_menu_resident_t*)resident;
    header->dldiOffset = dldiOffset;
    header->imageSectors = imageSize / CHEAT_MENU_SECTOR_SIZE;
    memcpy(resident + dldiOffset, driver, driverSize);
    return list;
}

/// @brief Stores the sector runs of the first \p sectors sectors of the open file in the resident part, from the
///        cluster map of the file. Returns false if the file is too fragmented or too short.
static bool getFileRuns(FIL* file, u32 sectors, cheat_menu_resident_t* resident)
{
    DWORD clusterMap[1 + 2 * CHEAT_MENU_MAX_FILE_RUNS + 1];
    clusterMap[0] = sizeof(clusterMap) / sizeof(clusterMap[0]);
    file->cltbl = clusterMap;
    FRESULT result = f_lseek(file, CREATE_LINKMAP);
    file->cltbl = nullptr;
    if (result != FR_OK)
        return false;
    const FATFS* fs = file->obj.fs;
    resident->fileRunCount = 0;
    for (const DWORD* fragment = &clusterMap[1]; fragment[0] != 0 && sectors != 0; fragment += 2)
    {
        u32 count = fragment[0] * fs->csize;
        if (count > sectors)
            count = sectors;
        auto& run = resident->fileRuns[resident->fileRunCount++];
        run.sector = fs->database + (fragment[1] - 2) * fs->csize;
        run.count = count;
        sectors -= count;
    }
    return sectors == 0;
}

/// @brief Writes the menu image to the cheat menu file, which is extended by as many sectors for the game's code that
///        the image replaces while the menu is open, and stores the sector runs of the file in the resident part.
static bool writeMenuFile(const u8* image, u32 imageSize, cheat_menu_resident_t* resident)
{
    FIL file;
    if (f_open(&file, CHEAT_MENU_FILE_PATH, FA_OPEN_ALWAYS | FA_READ | FA_WRITE) != FR_OK)
        return false;
    UINT written;
    u32 fileSize = 2 * imageSize;
    bool ok = (f_size(&file) >= fileSize || (f_lseek(&file, fileSize) == FR_OK && f_tell(&file) == fileSize)) &&
        f_lseek(&file, 0) == FR_OK && f_write(&file, image, imageSize, &written) == FR_OK && written == imageSize &&
        f_sync(&file) == FR_OK && getFileRuns(&file, fileSize / CHEAT_MENU_SECTOR_SIZE, resident);
    f_close(&file);
    return ok;
}

/// @brief Prepares the cheat menu of the game and stores the cheat list in the mailbox. Returns why the cheat menu
///        cannot be installed, or \see PARASITE_ERROR_NONE.
static u32 prepareCheatMenu(parasite_mailbox_t* box, const u8* romHeader)
{
    if (!isGameSupported((const nds_header_ntr_t*)romHeader))
        return PARASITE_ERROR_GAME_UNSUPPORTED;

    CheatDatabase database;
    auto launcherCheats = (const pload_cheats_t*)box->launcherCheats;
    database.Load(romHeader, launcherCheats, PARASITE_MAX_ACTIVE_CHEATS_SIZE);
    if (!database.HasCheats())
        return fileExists(PARASITE_USRCHEAT_PATH) ? PARASITE_ERROR_GAME_NOT_FOUND : PARASITE_ERROR_NO_USRCHEAT;

    // The launcher cheats that are not in the database are stored in the menu image, preprocessed like Pico Loader
    // does, so that the menu can copy them to the cheat list as they are.
    pload_cheats_t* processedCheats = nullptr;
    if (launcherCheats)
    {
        processedCheats = (pload_cheats_t*)malloc(launcherCheats->length);
        if (!processedCheats)
            return PARASITE_ERROR_OUT_OF_MEMORY;
        memcpy(processedCheats, launcherCheats, launcherCheats->length);
        auto cheat = &processedCheats->firstCheat;
        for (u32 i = 0; i < processedCheats->numberOfCheats; i++)
        {
            CheatPreprocessor().PreprocessCheat(cheat);
            cheat = (pload_cheat_t*)((u8*)cheat + 4 + cheat->length);
        }
    }
    u32 imageSize;
    const u8* image = buildMenuImage(database, processedCheats, imageSize);
    free(processedCheats);
    if (!image)
        return PARASITE_ERROR_OUT_OF_MEMORY;
    auto list = buildCheatList(box, database.GetActiveCheatsSize(), imageSize);
    if (!list)
        return PARASITE_ERROR_OUT_OF_MEMORY;
    auto resident = (cheat_menu_resident_t*)((u8*)list + list->marker[5]);
    if (!writeMenuFile(image, imageSize, resident))
        return PARASITE_ERROR_MENU_FILE;
    box->cheatList = list;
    box->cheatCount = database.GetCheatCount();
    return PARASITE_ERROR_NONE;
}

extern "C" void loaderMain()
{
    // Copy what the launcher passed before the arm9 part may change any mappings or memory.
    auto box = (parasite_mailbox_t*)PARASITE_MAILBOX_ADDRESS;
    memset(box, 0, sizeof(*box));
    static_assert(sizeof(gLoaderHeader) <= sizeof(box->launcherHeader));
    memcpy(box->launcherHeader, &gLoaderHeader, sizeof(gLoaderHeader));
    box->passThrough = isBootFile(gLoaderHeader.loadParams.romPath, sizeof(gLoaderHeader.loadParams.romPath));
    copyDldiDriver(box);
    copyLauncherCheats(box);
    parasite_sendSync(PARASITE_SYNC_LAUNCHER_DATA_COPIED);
    parasite_waitSync(PARASITE_SYNC_CARD_TO_ARM7);
    heap_init();
    __libc_init_array();

    // The sd card, with a working copy of the DLDI driver that is relocated for its address in the heap.
    if (gLoaderHeader.bootDrive != PLOAD_BOOT_DRIVE_DLDI || !box->dldiDriver)
        fail(PARASITE_ERROR_NO_DLDI);
    auto workDriver = (dldi_header_t*)malloc(box->dldiDriverSize);
    if (!workDriver)
        fail(PARASITE_ERROR_NO_DLDI);
    memcpy(workDriver, box->dldiDriver, box->dldiDriverSize);
    DldiDriver disk(workDriver);
    disk.Relocate();
    disk.PrepareForUse();
    gDiskDriver = &disk;
    if (!disk.Startup() || f_mount(&gFatFs, "fat:", 1) != FR_OK)
        fail(PARASITE_ERROR_SD_CARD);
    f_chdrive("fat:");

    // The real Pico Loader.
    box->step = PARASITE_STEP_REAL_LOADER;
    box->realLoader7 = loadFile(PARASITE_REAL_LOADER7_PATH, REAL_LOADER7_MAX_SIZE, box->realLoader7Size);
    box->realLoader9 = loadFile(PARASITE_REAL_LOADER9_PATH, REAL_LOADER9_MAX_SIZE, box->realLoader9Size);
    if (!box->realLoader7 || !box->realLoader9 || box->realLoader7Size < sizeof(pload_header7_t))
        fail(PARASITE_ERROR_REAL_LOADER);

    // The cheat menu, except for the boot file of the DSpico.
    box->step = PARASITE_STEP_CHEAT_MENU;
    if (!box->passThrough)
    {
        alignas(4) u8 romHeader[512];
        bool headerRead = readRomHeader(romHeader);
        if (headerRead)
            box->romUnitCode = ((const nds_header_ntr_t*)romHeader)->unitCode;
        if (fileExists(PARASITE_TRANSPARENT_FLAG_PATH))
            box->error = PARASITE_ERROR_TRANSPARENT;
        else if (!headerRead)
            box->error = PARASITE_ERROR_GAME_UNSUPPORTED;
        else
            box->error = prepareCheatMenu(box, romHeader);
        box->installCheatMenu = box->error == PARASITE_ERROR_NONE;
    }
    parasite_sendSync(PARASITE_SYNC_PREPARED);
    parasite_waitSync(PARASITE_SYNC_ARM9_DECIDED);

    // The arm9 part now maps VRAM C and D, where this code runs, to itself to copy the real Pico Loader there. A small
    // stub in arm7 WRAM waits for it and jumps to the real Pico Loader (handoff.s). From here on, no code in VRAM may
    // run.
    memcpy((void*)HANDOFF_STUB_ADDRESS, arm7HandoffStart, arm7HandoffEnd - arm7HandoffStart);
    auto realHeader = (const pload_header7_t*)box->realLoader7;
    ((void (*)(void*))HANDOFF_STUB_ADDRESS)(realHeader->entryPoint);
    while (true);
}
