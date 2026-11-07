#pragma once

// Interface between the parasite, the installer, the resident part and the in-game cheat menu.
//
// The cheat list that the parasite passes to the real Pico Loader contains the enabled cheats and the resident part
// of the cheat menu. Pico Loader places the list in the arm7 memory of the game, and its arm7 cheat engine runs the
// cheats every frame. The resident part stays in the list while the game runs: when the button combination is
// pressed, it loads the menu image from the cheat menu file into memory that holds the game's code, runs the menu and
// then restores the game's code.
//
// Layout of the cheat list:
//   - cheat_menu_list_header_t, which ends with the marker cheat;
//   - the enabled cheats (pload_cheat_t), followed by the room for cheats that are turned on later;
//   - the resident part (cheat_menu_resident_t followed by its code), at the offset in marker[5];
//   - a copy of the launcher's DLDI driver, at cheat_menu_resident_t::dldiOffset from the start of the resident part.
//
// Layout of the cheat menu file (CHEAT_MENU_FILE_PATH):
//   - the menu image: the menu code (starting with cheat_menu_image_header_t) and, from the next sector boundary,
//     the cheat menu data of the game (cheat_menu_data_t);
//   - the same number of sectors again, where the resident part saves the game's code while the menu is open.
//
// Layout of the cheat search file (CHEAT_SEARCH_FILE_PATH), see menu/cheatSearch.h:
//   - the state of the search (cheat_search_state_t), which the parasite clears on every start;
//   - one sector of result bits for every group of CHEAT_SEARCH_GROUP_SIZE bytes of the scanned memory;
//   - the snapshot of the scanned memory at the last search.

/// @brief Path of the cheat menu file on the sd card.
#define CHEAT_MENU_FILE_PATH            "/_pico/cheatmenu.bin"

/// @brief Maximum number of sector runs that describe the location of the game's block in usrcheat.dat.
#define CHEAT_MENU_MAX_SECTOR_RUNS      32

/// @brief Maximum number of sector runs of the cheat menu file.
#define CHEAT_MENU_MAX_FILE_RUNS        8

/// @brief Size of an sd card sector in bytes.
#define CHEAT_MENU_SECTOR_SIZE          512

/// @brief Magic values in the marker cheat, used by the installer to find the cheat list in the arm7 memory.
#define CHEAT_MENU_LIST_MAGIC0          0x50434D30
#define CHEAT_MENU_LIST_MAGIC1          0x4E554D45

/// @brief Opcode of each instruction of the marker cheat. D2 (next and flush) has no effect outside of a loop, so the
///        marker cheat does nothing when the cheat engine runs it.
#define CHEAT_MENU_MARKER_OPCODE        0xD2000000

/// @brief Path of the cheat search file on the sd card. The parasite creates it once and clears the search of the
///        previous game on every start; the menu stores the state of the cheat search in it.
#define CHEAT_SEARCH_FILE_PATH          "/_pico/cheatsearch.bin"

/// @brief The memory that the cheat search scans, as the game sees it: at most the 4 MB of main memory in DS mode, and
///        in DSi mode the main memory up to the cheat list and the arm7 code (see the arena in installer.cpp), since
///        DSi-enhanced games keep their data far above the first 4 MB. The installer stores the size in the resident
///        part, see cheatSearch_getMemorySize.
#define CHEAT_SEARCH_MEMORY_START       0x02000000
#define CHEAT_SEARCH_MEMORY_SIZE_DS     0x00400000
#define CHEAT_SEARCH_MEMORY_SIZE_DSI    0x00F80000

/// @brief The memory is scanned in groups of this many bytes. Each group has one sector of result bits (one bit per
///        byte) and its own sectors in the snapshot of the values.
#define CHEAT_SEARCH_GROUP_SIZE         4096
#define CHEAT_SEARCH_GROUP_COUNT        (CHEAT_SEARCH_MEMORY_SIZE_DSI / CHEAT_SEARCH_GROUP_SIZE)

/// @brief A search goes through the memory in blocks of 8 groups, so the size of the scanned memory is a multiple of
///        this size.
#define CHEAT_SEARCH_BLOCK_SIZE         (8 * CHEAT_SEARCH_GROUP_SIZE)

/// @brief Layout of the cheat search file, in sectors: the state of the search, the result bits of every group and
///        the snapshot of the memory at the last search.
#define CHEAT_SEARCH_STATE_SECTORS      3
#define CHEAT_SEARCH_BITS_SECTOR        CHEAT_SEARCH_STATE_SECTORS
#define CHEAT_SEARCH_SNAPSHOT_SECTOR    (CHEAT_SEARCH_BITS_SECTOR + CHEAT_SEARCH_GROUP_COUNT)
#define CHEAT_SEARCH_FILE_SECTORS       (CHEAT_SEARCH_SNAPSHOT_SECTOR + \
                                         CHEAT_SEARCH_MEMORY_SIZE_DSI / CHEAT_MENU_SECTOR_SIZE)

/// @brief Maximum number of values that the cheat search keeps locked, each with a cheat in the cheat list.
#define CHEAT_SEARCH_MAX_LOCKS          16

/// @brief Size of the cheat of a locked value in the cheat list: the length and three opcodes (offset, write, end).
#define CHEAT_SEARCH_LOCK_CHEAT_SIZE    (4 + 3 * 8)

/// @brief A run of consecutive sd card sectors.
typedef struct
{
    u32 sector;
    u32 count;
} cheat_menu_sector_run_t;

/// @brief Start of the cheat list passed to the real Pico Loader, compatible with pload_cheats_t. The first cheat is
///        the marker, which has no effect; the enabled cheats follow it.
typedef struct
{
    /// @brief pload_cheats_t::length: the size of the whole list, including the resident part and the DLDI driver.
    u32 length;

    /// @brief pload_cheats_t::numberOfCheats: the marker cheat and the enabled cheats.
    u32 numberOfCheats;

    /// @brief pload_cheat_t::length of the marker cheat, always sizeof(marker).
    u32 markerLength;

    /// @brief The marker cheat: D2000000 MAGIC0, D2000000 MAGIC1, D2000000 residentOffset, D2000000 residentSize.
    ///        residentOffset is relative to the start of the list.
    u32 marker[8];
} cheat_menu_list_header_t;

/// @brief Header at the start of the resident part. The resident part is position independent.
typedef struct
{
    /// @brief Offset of the OS_IrqHandler hook from the start of the resident part (read-only).
    u32 irqHookOffset;

    /// @brief Size of the resident part in memory, including its zero-initialized data (read-only).
    u32 totalSize;

    /// @brief Offset of the copy of the DLDI driver from the start of the resident part (set by the parasite).
    u32 dldiOffset;

    /// @brief Number of sectors of the menu image. The cheat menu file has the same number of sectors after the image
    ///        to save the game's code (set by the parasite).
    u32 imageSectors;

    /// @brief The sectors of the cheat menu file (set by the parasite).
    u32 fileRunCount;
    cheat_menu_sector_run_t fileRuns[CHEAT_MENU_MAX_FILE_RUNS];

    /// @brief Address in OS_IrqHandler where execution continues after the hook (set by the installer).
    u32 irqHandlerContinueAddress;

    /// @brief Address of the SDK lock that protects the DS card slot, an OSLockWord (set by the installer).
    u32 cardLockAddress;

    /// @brief The DLDI driver (dldi_header_t), relocated for its address in the resident part (set by the installer).
    const void* dldiDriver;

    /// @brief The cheat list, at the address the game uses (set by the installer).
    cheat_menu_list_header_t* list;

    /// @brief Address of the game's code that the menu image replaces while the menu is open, imageSectors sectors
    ///        long (set by the installer).
    u32 menuAddress;

    /// @brief KEYINPUT bits of the combination that opens the menu (set by the parasite).
    u32 hotkey;

    /// @brief Size of the memory that the cheat search scans (set by the installer). The installer can tell DS mode
    ///        from DSi mode: the game may have turned off the registers for that when the menu runs.
    u32 searchMemorySize;

    /// @brief Number of cheats of locked values at the start of the enabled cheats (set by the menu). They end with the
    ///        cheat search of the game, so the parasite does not keep them after a soft reset.
    u32 lockCheatCount;
} cheat_menu_resident_t;

/// @brief Header at the start of the menu image. The menu code is position independent.
typedef struct
{
    /// @brief Offset of the entry point from the start of the image (read-only). The resident part calls it as
    ///        u32 entry(cheat_menu_resident_t* resident, u32 numberOfCheats), which returns the new number of cheats
    ///        of the cheat list.
    u32 entryOffset;

    /// @brief Size of the menu code in memory, including its zero-initialized data (read-only).
    u32 totalSize;

    /// @brief Offset of the cheat menu data from the start of the image, a multiple of the sector size (set by the
    ///        parasite).
    u32 dataOffset;
} cheat_menu_image_header_t;

/// @brief The cheat menu data of the game, at the end of the menu image. Written by the parasite; all offsets are
///        relative to the start of this struct.
typedef struct
{
    /// @brief Size of the persistent part at the start of the data (this struct and the enable bits), which the menu
    ///        writes back to the cheat menu file when it is closed.
    u32 persistentSize;

    /// @brief The position in the root list when the menu was last closed.
    u32 rootCursor;
    u32 rootTop;

    /// @brief Offset of the enable bits: one bit per item of the database, followed by one bit per extra cheat.
    u32 enabledBitsOffset;

    /// @brief Size of the game's block in usrcheat.dat in bytes, or 0 if the game is not in the database.
    u32 blockSize;

    /// @brief Offset of the start of the block in the first sector of the first sector run.
    u32 blockSectorOffset;

    /// @brief Number of valid entries in \see sectorRuns.
    u32 sectorRunCount;

    /// @brief The sd card sectors that contain the block.
    cheat_menu_sector_run_t sectorRuns[CHEAT_MENU_MAX_SECTOR_RUNS];

    /// @brief Number of database items (folders and cheats), in database order.
    u32 itemCount;

    /// @brief 2 if the item offsets are u16 entries holding the offset divided by 4, or 0 if they are u32 entries
    ///        holding the offset.
    u32 itemOffsetShift;

    /// @brief Offset of the table with the offset of every item in the block.
    u32 itemOffsetsOffset;

    /// @brief Indices (u16) of the items at the root level of the database: folders and cheats outside of folders.
    u32 rootCount;
    u32 rootItemsOffset;

    /// @brief Launcher cheats that are not in the database (pload_cheat_t, already preprocessed, stored one after the
    ///        other). The menu shows them after the root items.
    u32 extraCheatCount;
    u32 extraCheatsOffset;

    /// @brief The sectors of the cheat search file (CHEAT_SEARCH_FILE_PATH), or no runs if the parasite could not
    ///        create it. The menu then has no cheat search.
    u32 searchRunCount;
    cheat_menu_sector_run_t searchRuns[CHEAT_MENU_MAX_FILE_RUNS];
} cheat_menu_data_t;

/// @brief Returns the size of the memory that the cheat search scans. In DS mode the cheat list is inside the main
///        memory: the search stops before it, so that the cheat list, the resident part and the DLDI driver are never
///        results that can be changed or locked.
/// @param listAddress The address of the cheat list, as the game sees it.
/// @param maxSize The size for the mode of the game, CHEAT_SEARCH_MEMORY_SIZE_DS or CHEAT_SEARCH_MEMORY_SIZE_DSI.
/// @return The size in bytes, a multiple of CHEAT_SEARCH_BLOCK_SIZE.
static inline u32 cheatSearch_getMemorySize(u32 listAddress, u32 maxSize)
{
    u32 listOffset = (listAddress - CHEAT_SEARCH_MEMORY_START) & ~(CHEAT_SEARCH_BLOCK_SIZE - 1);
    return listOffset < maxSize ? listOffset : maxSize;
}

/// @brief Returns the number of cheats of locked values at the start of the enabled cheats of a cheat list. The count
///        is in memory that the game could have overwritten, so it is ignored if it does not fit the list.
/// @param list The cheat list. marker[5] must be the offset of a resident part inside the list.
/// @return The number of cheats, or 0 if the count is not valid.
static inline u32 cheatMenuList_getLockCheatCount(const cheat_menu_list_header_t* list)
{
    u32 count = ((const cheat_menu_resident_t*)((const u8*)list + list->marker[5]))->lockCheatCount;
    u32 room = list->marker[5] - sizeof(*list);
    bool fits = count <= CHEAT_SEARCH_MAX_LOCKS && count < list->numberOfCheats &&
        count * CHEAT_SEARCH_LOCK_CHEAT_SIZE <= room;
    return fits ? count : 0;
}
