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
} cheat_menu_data_t;
