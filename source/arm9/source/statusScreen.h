#pragma once
#include <nds/ndstypes.h>
#include "ParasiteDefinitions.h"

// Status screen of the parasite on the top screen: the game, the progress of the parasite and whether the cheat menu
// is available.

/// @brief Shows the status screen: takes VRAM B for the top screen and turns both screens on. Calls after the first one
///        have no effect.
void statusScreen_show();

/// @brief Turns the backdrop of the bottom screen back to black, the color that the real Pico Loader and the game
///        expect.
void statusScreen_finish();

/// @brief Waits for the start of the next vblank. If the status screen is shown, it then draws what changed in
///        \p box.
void statusScreen_update(parasite_mailbox_t* box);

/// @brief Returns whether the hotkey editor is open. The game does not start until it is closed.
bool statusScreen_hotkeyActive();
