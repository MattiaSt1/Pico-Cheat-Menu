#pragma once
#include <nds/ndstypes.h>
#include "ParasiteDefinitions.h"

// Status screen of the parasite on the top screen: the game, the progress of the parasite and whether the cheat menu
// is available.

/// @brief Shows the status screen: takes VRAM B for the top screen and turns both screens on, black. Calls after the
///        first one have no effect.
void statusScreen_show();

/// @brief Waits for the start of the next vblank. If the status screen is shown, it then draws what changed in
///        \p box.
void statusScreen_update(const parasite_mailbox_t* box);
