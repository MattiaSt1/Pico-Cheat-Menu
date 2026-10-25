#pragma once

#include "CheatMenuDefinitions.h"

// Pico Cheat Parasite: boots the real Pico Loader with a cheat list that contains the in-game cheat menu.

#define PARASITE_REAL_LOADER9_PATH      "/_pico/loader/picoLoader9.bin"
#define PARASITE_REAL_LOADER7_PATH      "/_pico/loader/picoLoader7.bin"
#define PARASITE_USRCHEAT_PATH          "/_pico/usrcheat.dat"

/// @brief Main memory used for passing data from the arm7 to the arm9 part of the parasite.
///        Main memory is free at this point, since the launcher is no longer running.
#define PARASITE_MAILBOX_ADDRESS        0x02000000
#define PARASITE_MAILBOX_SIZE           0x00100000

/// @brief Main memory used by the arm7 for decompressing the arm9 binary of the game.
#define PARASITE_SCRATCH_ADDRESS        0x02100000
#define PARASITE_SCRATCH_SIZE           0x00280000

/// @brief Part of the arm9 ITCM for the installer. The start of the ITCM is used by Pico Loader.
#define PARASITE_STASH_ADDRESS          0x01002000
#define PARASITE_STASH_END              0x01008000

/// @brief Room in the cheat list for the cheats that are turned on in the menu: as large as all cheats of the game
///        together, but at most this size (more if the launcher cheats need it). The list is in the arm7 arena of the
///        game, which is small.
#define PARASITE_MAX_ACTIVE_CHEATS_SIZE 4096

/// @brief IPC sync values used between the arm7 and arm9 parts of the parasite.
#define PARASITE_SYNC_LAUNCHER_DATA_COPIED  1
#define PARASITE_SYNC_CARD_TO_ARM7          2
#define PARASITE_SYNC_PREPARED              3
#define PARASITE_SYNC_ARM7_IN_WRAM          4
#define PARASITE_SYNC_BOOT_REAL_LOADER      5
#define PARASITE_SYNC_ARM9_DECIDED          6

/// @brief Data passed from the arm7 part to the arm9 part of the parasite, at \see PARASITE_MAILBOX_ADDRESS.
typedef struct
{
    /// @brief The header of the parasite as filled in by the launcher (pload_header7_t, without the read-only fields).
    u8 launcherHeader[1088];
    u32 launcherHeaderSize;

    /// @brief True if the real Pico Loader binaries were loaded.
    u32 realLoaderLoaded;
    const void* realLoader9;
    u32 realLoader9Size;
    const void* realLoader7;
    u32 realLoader7Size;

    /// @brief Copy of the dldi driver of the launcher (still relocated for its original address).
    const void* dldiDriver;
    u32 dldiDriverSize;

    /// @brief Copy of the launcher cheats (pload_cheats_t, not preprocessed), or \c nullptr.
    const void* launcherCheats;

    /// @brief True if the in-game cheat menu should be installed. Otherwise the launcher cheats are passed to the
    ///        real Pico Loader.
    u32 installCheatMenu;

    /// @brief The cheat list (cheat_menu_list_header_t) to pass to the real Pico Loader, when \see installCheatMenu.
    const cheat_menu_list_header_t* cheatList;
} parasite_mailbox_t;

/// @brief Header of the installer, at \see PARASITE_STASH_ADDRESS.
typedef struct
{
    /// @brief The installer entry point, jumped to instead of the game by the real Pico Loader, with r0 = the arm9
    ///        entry point of the game (read-only).
    u32 entryPoint;

    /// @brief Size of the installer (read-only).
    u32 size;

    /// @brief Length of the cheat list, to recognize it in the arm7 arena of the game.
    u32 listLength;

    // 0 pending, 1 installed, 2 invalid SDK data, 3 hook missing, 4 cheat list not found, 5 no room for the menu.
    volatile u32 status;
} parasite_installer_header_t;
