#pragma once

// Shared definitions of the Pico Cheat Parasite: its arm7 part, its arm9 part and the installer.
//
// The parasite takes the place of Pico Loader (picoLoader7.bin and picoLoader9.bin in /_pico/), so the launcher boots
// it as if it were Pico Loader. The parasite loads the real Pico Loader from /_pico/loader/ and boots it with the
// parameters of the launcher. On the way, it prepares the in-game cheat menu of the game and adds it to the cheat
// list that Pico Loader receives (see CheatMenuDefinitions.h).

#include "CheatMenuDefinitions.h"

#define PARASITE_REAL_LOADER9_PATH      "/_pico/loader/picoLoader9.bin"
#define PARASITE_REAL_LOADER7_PATH      "/_pico/loader/picoLoader7.bin"
#define PARASITE_USRCHEAT_PATH          "/_pico/usrcheat.dat"

/// @brief While this file exists, the parasite boots the real Pico Loader without the cheat menu.
#define PARASITE_TRANSPARENT_FLAG_PATH  "/_pico/parasite-transparent"

/// @brief Name of the boot file of the DSpico (usually Pico Launcher). Pico Loader boots it, for example, when an
///        application returns to the launcher; the parasite then starts the real Pico Loader right away, without the
///        status screen and without the cheat menu. The name is compared without regard to case.
#define PARASITE_BOOT_FILE_NAME         "_picoboot.nds"

/// @brief Main memory where the arm7 part passes data to the arm9 part (\see parasite_mailbox_t), followed by memory
///        for the data that it prepares. Main memory is free at this point, since the launcher no longer runs.
#define PARASITE_MAILBOX_ADDRESS        0x02000000
#define PARASITE_MAILBOX_SIZE           0x00100000

/// @brief Main memory where the arm7 part decompresses the arm9 binary of the game.
#define PARASITE_SCRATCH_ADDRESS        0x02100000
#define PARASITE_SCRATCH_SIZE           0x00280000

/// @brief Part of the arm9 ITCM for the installer. Pico Loader uses the start of the ITCM and never clears the rest.
#define PARASITE_STASH_ADDRESS          0x01002000
#define PARASITE_STASH_END              0x01008000

/// @brief Upper limit of the room in the cheat list for the cheats that are turned on in the menu. The room is as
///        large as all cheats of the game together, up to this size, but never smaller than the launcher cheats. The
///        cheat list is in the small arm7 memory of the game.
#define PARASITE_MAX_ACTIVE_CHEATS_SIZE 4096

/// @brief Values exchanged through the IPC sync register between the two processors, in this order.
#define PARASITE_SYNC_LAUNCHER_DATA_COPIED  1 // arm7: the launcher data is in the mailbox
#define PARASITE_SYNC_CARD_TO_ARM7          2 // arm9: the arm7 has access to the DS card slot
#define PARASITE_SYNC_PREPARED              3 // arm7: the real Pico Loader is loaded and the cheat menu is prepared
#define PARASITE_SYNC_ARM9_DECIDED          6 // arm9: the arm7 can hand over to the real Pico Loader
#define PARASITE_SYNC_ARM7_IN_WRAM          4 // arm7: it waits outside of VRAM (handoff.s)
#define PARASITE_SYNC_BOOT_REAL_LOADER      5 // arm9: the real Pico Loader is in VRAM (handoff.s)

/// @brief Steps of the parasite, shown on the status screen. The arm7 part sets the steps up to the cheat menu; the
///        arm9 part sets \see PARASITE_STEP_DONE once it has decided whether to install the cheat menu.
#define PARASITE_STEP_SD_CARD               0
#define PARASITE_STEP_REAL_LOADER           1
#define PARASITE_STEP_CHEAT_MENU            2
#define PARASITE_STEP_DONE                  3

/// @brief Why the parasite stopped at the sd card or real loader step (fatal), or why the cheat menu is off.
#define PARASITE_ERROR_NONE                 0
#define PARASITE_ERROR_NO_DLDI              1
#define PARASITE_ERROR_SD_CARD              2
#define PARASITE_ERROR_REAL_LOADER          3
#define PARASITE_ERROR_TRANSPARENT          4
#define PARASITE_ERROR_GAME_UNSUPPORTED     5
#define PARASITE_ERROR_NO_USRCHEAT          6
#define PARASITE_ERROR_GAME_NOT_FOUND       7
#define PARASITE_ERROR_OUT_OF_MEMORY        8
#define PARASITE_ERROR_MENU_FILE            9
#define PARASITE_ERROR_LOADER_UNSUPPORTED   10

/// @brief The IPC sync register (REG_IPC_SYNC). Bits 0-3 hold the value sent by the other processor, bits 8-11 the
///        value sent to it.
#define PARASITE_REG_IPC_SYNC               (*(vu16*)0x04000180)

/// @brief Sends \p value to the other processor.
static inline void parasite_sendSync(u32 value)
{
    PARASITE_REG_IPC_SYNC = value << 8;
}

/// @brief Returns the last value sent by the other processor.
static inline u32 parasite_receivedSync()
{
    return PARASITE_REG_IPC_SYNC & 0xF;
}

/// @brief Waits until the other processor sends \p value.
static inline void parasite_waitSync(u32 value)
{
    while (parasite_receivedSync() != value);
}

/// @brief Data passed from the arm7 part to the arm9 part, at \see PARASITE_MAILBOX_ADDRESS.
typedef struct
{
    /// @brief Copy of the header of the parasite as filled in by the launcher (a pload_header7_t).
    u8 launcherHeader[1088];

    /// @brief True if the launcher boots the DSpico boot file (\see PARASITE_BOOT_FILE_NAME): the real Pico Loader is
    ///        booted without the status screen and without the cheat menu.
    u32 passThrough;

    /// @brief The binaries of the real Pico Loader.
    const void* realLoader9;
    u32 realLoader9Size;
    const void* realLoader7;
    u32 realLoader7Size;

    /// @brief Copy of the DLDI driver of the launcher, still relocated for its original address.
    const void* dldiDriver;
    u32 dldiDriverSize;

    /// @brief Copy of the launcher cheats (pload_cheats_t, not preprocessed), or \c nullptr.
    const void* launcherCheats;

    /// @brief True if the cheat menu is prepared. The arm9 part clears it if the real Pico Loader cannot run the menu;
    ///        the launcher cheats are then passed to the real Pico Loader instead of \see cheatList.
    u32 installCheatMenu;

    /// @brief The cheat list with the cheat menu (cheat_menu_list_header_t), when \see installCheatMenu.
    const cheat_menu_list_header_t* cheatList;

    /// @brief The current step (\see PARASITE_STEP_SD_CARD) and the error (\see PARASITE_ERROR_NONE), shown on the
    ///        status screen while the arm7 part works.
    volatile u32 step;
    volatile u32 error;

    /// @brief Number of cheats in the cheat menu, when \see installCheatMenu.
    u32 cheatCount;

    /// @brief The unit code in the header of the game, to show whether the game starts in DS or DSi mode.
    u32 romUnitCode;
} parasite_mailbox_t;

/// @brief Values of parasite_installer_header_t::status, which tell how far the installer got (for debugging).
#define PARASITE_INSTALLER_STATUS_PENDING           0
#define PARASITE_INSTALLER_STATUS_INSTALLED         1
#define PARASITE_INSTALLER_STATUS_INVALID_SDK_DATA  2
#define PARASITE_INSTALLER_STATUS_NO_IRQ_HANDLER    3
#define PARASITE_INSTALLER_STATUS_NO_CHEAT_LIST     4
#define PARASITE_INSTALLER_STATUS_NO_MENU_MEMORY    5

/// @brief Header of the installer, at \see PARASITE_STASH_ADDRESS.
typedef struct
{
    /// @brief Entry point of the installer (read-only). The real Pico Loader jumps there instead of to the game, with
    ///        r0 = the arm9 entry point of the game.
    u32 entryPoint;

    /// @brief Size of the installer (read-only).
    u32 size;

    /// @brief Length of the cheat list, to recognize it in the arm7 memory of the game.
    u32 listLength;

    /// @brief How far the installer got, \see PARASITE_INSTALLER_STATUS_PENDING.
    volatile u32 status;
} parasite_installer_header_t;
