#pragma once
#include <nds/ndstypes.h>
#include "ParasiteDefinitions.h"

// Hotkey editor of the status screen: the input state machine that changes the combination that opens the in-game
// cheat menu. Holding Down while the game starts opens it; the arm7 part saves the chosen combination (see saveHotkey
// in arm7/source/main.cpp). The status screen draws the editor from its state, and tests/hotkeyEditor.cpp tests it on
// the host.

/// @brief State of the hotkey editor, updated once per frame with \see Update. All fields are words, since the
///        variables of the arm9 part are in VRAM, which ignores 8-bit writes.
struct HotkeyEditor
{
    /// @brief The editor states. The underlying type is u32, since the ARM toolchain uses short enums, which would be
    ///        stored with 8-bit writes.
    enum State : u32
    {
        IDLE,       ///< The editor is closed.
        RELEASE,    ///< The editor waits until all buttons are released.
        CAPTURE,    ///< Buttons are added to and removed from \see candidate.
        SAVING,     ///< The arm7 part saves \see candidate.
        SAVED,      ///< The combination is saved; A closes the editor.
        FAILED,     ///< The save failed; the combination can be changed and saved again.
        CLOSING,    ///< The editor closes once all buttons are released.
        RETURNING   ///< The default was restored; the editor closes once all buttons are released.
    };
    static_assert(sizeof(State) == sizeof(u32), "Editor state must support word writes to VRAM");

    /// @brief Frames that Start must be held alone to save.
    static constexpr u32 SAVE_HOLD_FRAMES = 60;

    /// @brief Frames that B must be held alone before releasing it closes the editor without saving.
    static constexpr u32 EXIT_HOLD_FRAMES = 120;

    /// @brief Frames that B must be held alone to restore the default combination.
    static constexpr u32 RESET_HOLD_FRAMES = 300;

    State state;
    u32 candidate;      ///< The combination being edited.
    u32 previousKeys;   ///< The buttons of the previous frame.
    u32 downFrames;     ///< Frames that Down has been held alone while the editor is closed.
    u32 requested;      ///< Nonzero once Down was held long enough; the editor opens when the menu is available.
    u32 startFrames;    ///< Frames that Start has been held alone.
    u32 startPending;   ///< Nonzero if a release of Start toggles Start in \see candidate.
    u32 resetFrames;    ///< Frames that B has been held alone, for the exit and reset gestures.
    u32 resetAfterSave; ///< Nonzero if the save in progress restores the default combination.

    /// @brief Returns the whole seconds left until the default combination is restored, or 0 if B is not held.
    u32 ResetSeconds() const
    {
        return resetFrames ? (RESET_HOLD_FRAMES - resetFrames + 59) / 60 : 0;
    }

    /// @brief Returns whether B has been held long enough that releasing it closes the editor.
    bool ExitReady() const
    {
        return resetFrames >= EXIT_HOLD_FRAMES;
    }

    /// @brief Closes the editor. The saved combination stays as it is.
    void Close()
    {
        state = IDLE;
        downFrames = 0;
        requested = 0;
        resetAfterSave = 0;
        startFrames = 0;
        startPending = 0;
        resetFrames = 0;
    }

    /// @brief Asks the arm7 part to save \see candidate.
    void Save(parasite_mailbox_t* box)
    {
        box->requestedHotkey = candidate;
        box->hotkeyStatus = PARASITE_HOTKEY_SAVE;
        startFrames = 0;
        startPending = 0;
        resetFrames = 0;
        state = SAVING;
    }

    /// @brief Advances the editor by one frame.
    /// @param keys The buttons that are held (KEYINPUT bits, set when pressed).
    /// @param box The mailbox, for the progress of the parasite and the save request.
    void Update(u32 keys, parasite_mailbox_t* box)
    {
        u32 pressed = keys & ~previousKeys;
        // Only B on its own is the exit and reset gesture, so a held combination containing B remains selectable.
        // Releasing B after EXIT_HOLD_FRAMES closes the editor; holding it on restores the default combination.
        bool gestures = state != IDLE && state != SAVING && state != CLOSING && state != RETURNING;
        if (gestures && keys == CHEAT_MENU_KEY_B)
        {
            if (++resetFrames == RESET_HOLD_FRAMES)
            {
                candidate = CHEAT_MENU_DEFAULT_HOTKEY;
                resetAfterSave = 1;
                Save(box);
                previousKeys = keys;
                return;
            }
        }
        else
        {
            // Only releasing all buttons exits; pressing another button with B cancels the gesture.
            if (gestures && keys == 0 && ExitReady())
            {
                Close();
                previousKeys = keys;
                return;
            }
            resetFrames = 0;
        }
        switch (state)
        {
            case IDLE:
                if (keys == CHEAT_MENU_KEY_DOWN)
                {
                    if (++downFrames >= 20)
                        requested = 1;
                }
                else
                    downFrames = 0;
                if (requested && box->step == PARASITE_STEP_DONE && box->installCheatMenu)
                {
                    candidate = 0;
                    resetAfterSave = 0;
                    state = RELEASE;
                }
                break;
            case RELEASE:
                if (keys == 0)
                    state = CAPTURE;
                break;
            case CAPTURE:
            case FAILED:
                // Every ordinary key toggles its selection. Releasing a chord never turns A or B into UI controls.
                if (pressed & ~CHEAT_MENU_KEY_START)
                {
                    candidate ^= pressed & ~CHEAT_MENU_KEY_START;
                    resetAfterSave = 0;
                    state = CAPTURE;
                }
                // A short Start press selects Start. Holding it alone saves without adding it to the combination.
                if (pressed & CHEAT_MENU_KEY_START)
                    startPending = 1;
                if (!(keys & CHEAT_MENU_KEY_START) && (previousKeys & CHEAT_MENU_KEY_START) && startPending)
                {
                    candidate ^= CHEAT_MENU_KEY_START;
                    startPending = 0;
                    resetAfterSave = 0;
                    state = CAPTURE;
                }
                if (keys == CHEAT_MENU_KEY_START)
                {
                    if (startFrames < SAVE_HOLD_FRAMES)
                        startFrames++;
                    if (startFrames == SAVE_HOLD_FRAMES && cheatMenuHotkey_isValid(candidate))
                        Save(box);
                }
                else
                    startFrames = 0;
                break;
            case SAVING:
                if (box->hotkeyStatus == PARASITE_HOTKEY_SAVED)
                    state = resetAfterSave ? RETURNING : SAVED;
                else if (box->hotkeyStatus == PARASITE_HOTKEY_FAILED)
                    state = FAILED;
                break;
            case SAVED:
                if (pressed & CHEAT_MENU_KEY_A)
                    state = CLOSING;
                break;
            case CLOSING:
            case RETURNING:
                if (keys == 0)
                    Close();
                break;
        }
        previousKeys = keys;
    }
};
