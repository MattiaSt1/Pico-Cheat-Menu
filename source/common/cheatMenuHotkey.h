#pragma once

// The button combination that opens the in-game cheat menu, shared by the status screen, the arm7 part of the parasite
// and the resident part. A combination is a set of KEYINPUT bits. X and Y are not part of KEYINPUT (only the arm7 can
// read them), so the resident part, which runs on the arm9, cannot use them.

#define CHEAT_MENU_KEY_A          (1u << 0)
#define CHEAT_MENU_KEY_B          (1u << 1)
#define CHEAT_MENU_KEY_SELECT     (1u << 2)
#define CHEAT_MENU_KEY_START      (1u << 3)
#define CHEAT_MENU_KEY_RIGHT      (1u << 4)
#define CHEAT_MENU_KEY_LEFT       (1u << 5)
#define CHEAT_MENU_KEY_UP         (1u << 6)
#define CHEAT_MENU_KEY_DOWN       (1u << 7)
#define CHEAT_MENU_KEY_R          (1u << 8)
#define CHEAT_MENU_KEY_L          (1u << 9)

/// @brief All buttons that a combination may contain.
#define CHEAT_MENU_KEY_MASK       0x3FFu

/// @brief The combination used when none is saved, or when the saved one is invalid.
#define CHEAT_MENU_DEFAULT_HOTKEY (CHEAT_MENU_KEY_L | CHEAT_MENU_KEY_R | CHEAT_MENU_KEY_DOWN | CHEAT_MENU_KEY_SELECT)

/// @brief Returns whether \p keys is a combination that can be pressed: at least one supported button, and neither
///        Up and Down nor Left and Right together, since the D-pad cannot press opposite directions at once.
static inline bool cheatMenuHotkey_isValid(u32 keys)
{
    return keys != 0 && !(keys & ~CHEAT_MENU_KEY_MASK) &&
        (keys & (CHEAT_MENU_KEY_UP | CHEAT_MENU_KEY_DOWN)) != (CHEAT_MENU_KEY_UP | CHEAT_MENU_KEY_DOWN) &&
        (keys & (CHEAT_MENU_KEY_LEFT | CHEAT_MENU_KEY_RIGHT)) != (CHEAT_MENU_KEY_LEFT | CHEAT_MENU_KEY_RIGHT);
}
