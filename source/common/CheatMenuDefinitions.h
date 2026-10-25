#pragma once

// The cheat list passed to Pico Loader holds the enabled cheats and the resident part, which opens the menu.
// The menu image lives in the cheat menu file and is swapped in over the game's code only while it is open.

/// @brief The cheat menu file: the menu image followed by the same number of sectors to save the borrowed memory.
#define CHEAT_MENU_FILE_PATH            "/_pico/cheatmenu.bin"

/// @brief Maximum number of sector runs describing the location of the game's block in usrcheat.dat.
#define CHEAT_MENU_MAX_SECTOR_RUNS      32

/// @brief Maximum number of sector runs of the cheat menu file.
#define CHEAT_MENU_MAX_FILE_RUNS        8

#define CHEAT_MENU_SECTOR_SIZE          512

/// @brief Magic values of the marker cheat, used by the installer to find the list in the arm7 arena.
#define CHEAT_MENU_LIST_MAGIC0          0x50434D30
#define CHEAT_MENU_LIST_MAGIC1          0x4E554D45
/// @brief D2 (NEXT + FLUSH) does nothing outside of a loop, so the marker cheat has no effect.
#define CHEAT_MENU_MARKER_OPCODE        0xD2000000

/// @brief A run of consecutive sd sectors.
typedef struct
{
    u32 sector;
    u32 count;
} cheat_menu_sector_run_t;

/// @brief Start of the cheat list passed to the real Pico Loader (a pload_cheats_t). The first cheat is a marker
///        without effect, followed by the enabled cheats. The resident part starts at marker[5].
typedef struct
{
    u32 length;          // pload_cheats_t::length, the whole list including the resident part
    u32 numberOfCheats;  // pload_cheats_t::numberOfCheats, the marker and the enabled cheats
    u32 markerLength;    // pload_cheat_t::length of the marker, 32
    u32 marker[8];       // D2000000 MAGIC0, D2000000 MAGIC1, D2000000 residentOffset, D2000000 residentSize
} cheat_menu_list_header_t;

/// @brief Header of the resident part, at the start of the resident blob. The blob is position independent.
typedef struct
{
    /// @brief Offset of the OS_IrqHandler hook from the start of the blob (read-only).
    u32 irqHookOffset;

    /// @brief Size of the blob in memory, including zero-initialized data (read-only).
    u32 totalSize;

    /// @brief Offset of the dldi driver from the start of the blob (set by the parasite).
    u32 dldiOffset;

    /// @brief Number of sectors of the menu image. The file contains as many sectors after it, for the borrowed
    ///        memory (set by the parasite).
    u32 imageSectors;

    /// @brief The sectors of the cheat menu file (set by the parasite).
    u32 fileRunCount;
    cheat_menu_sector_run_t fileRuns[CHEAT_MENU_MAX_FILE_RUNS];

    /// @brief Address to continue at in the original OS_IrqHandler after the hook (set by the installer).
    u32 irqHandlerContinueAddress;

    /// @brief Address of the SDK card lock (OSLockWord, set by the installer).
    u32 cardLockAddress;

    /// @brief The dldi driver (dldi_header_t), relocated to its location in the resident part (set by the installer).
    const void* dldiDriver;

    /// @brief The cheat list, as seen by the game (set by the installer).
    cheat_menu_list_header_t* list;

    /// @brief Memory with the game's code that the menu image borrows while the menu is open, imageSectors sectors
    ///        (set by the installer).
    u32 menuAddress;
} cheat_menu_resident_t;

/// @brief Header at the start of the menu image. The image is position independent.
typedef struct
{
    /// @brief Offset of u32 cheatmenu_entry(cheat_menu_resident_t* resident, u32 numberOfCheats) from the start of
    ///        the image (read-only). Returns the new number of cheats of the list.
    u32 entryOffset;

    /// @brief Size of the menu code in memory, including zero-initialized data (read-only).
    u32 totalSize;

    /// @brief Offset of the cheat menu data from the start of the image, a multiple of the sector size
    ///        (set by the parasite).
    u32 dataOffset;
} cheat_menu_image_header_t;

/// @brief The cheat menu data in the menu image, written by the parasite. Offsets are relative to this struct.
typedef struct
{
    /// @brief Size of the part at the start of this struct that the menu writes back to the file when it is closed:
    ///        this struct and the enable bits.
    u32 persistentSize;

    /// @brief The position in the root list when the menu was closed.
    u32 rootCursor;
    u32 rootTop;

    /// @brief Enable bits: one bit per item, followed by one bit per extra cheat.
    u32 enabledBitsOffset;

    /// @brief Size of the game's block in usrcheat.dat in bytes, or 0 if the game is not in the database.
    u32 blockSize;

    /// @brief Byte offset of the start of the block in the first sector of the first sector run.
    u32 blockSectorOffset;

    /// @brief Number of valid entries in \see sectorRuns.
    u32 sectorRunCount;

    /// @brief The sd sectors containing the block.
    cheat_menu_sector_run_t sectorRuns[CHEAT_MENU_MAX_SECTOR_RUNS];

    /// @brief Number of database items (folders and cheats), in database order.
    u32 itemCount;

    /// @brief 2 if the item offsets are u16 entries with the item offset divided by 4,
    ///        or 0 if they are u32 entries with the item offset.
    u32 itemOffsetShift;

    /// @brief Offset of every item in the block.
    u32 itemOffsetsOffset;

    /// @brief Indices (u16) of the items at the root level of the database (folders and cheats outside of folders).
    u32 rootCount;
    u32 rootItemsOffset;

    /// @brief Launcher cheats that are not in the database (pload_cheat_t, preprocessed, stored consecutively).
    ///        They are shown after the root items.
    u32 extraCheatCount;
    u32 extraCheatsOffset;
} cheat_menu_data_t;
