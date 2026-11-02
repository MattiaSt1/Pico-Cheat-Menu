// Host test of the hotkey editor of the status screen (source/arm9/source/hotkeyEditor.h). See README.md in this
// directory for how to build and run it.

#include <cassert>
#include <cstdio>
#include "../source/arm9/source/hotkeyEditor.h"

// Compile with -fshort-enums as well: byte writes to the editor's state would be ignored in arm9 VRAM.
static_assert(sizeof(HotkeyEditor::State) == sizeof(u32), "Editor state must support word writes to VRAM");

static void hold(HotkeyEditor& editor, parasite_mailbox_t& box, u32 keys, u32 frames)
{
    while (frames--)
        editor.Update(keys, &box);
}

static void tap(HotkeyEditor& editor, parasite_mailbox_t& box, u32 keys)
{
    editor.Update(keys, &box);
    editor.Update(0, &box);
}

static parasite_mailbox_t readyBox()
{
    parasite_mailbox_t box = {};
    box.step = PARASITE_STEP_DONE;
    box.installCheatMenu = 1;
    box.hotkey = CHEAT_MENU_KEY_L | CHEAT_MENU_KEY_R;
    return box;
}

static void enter(HotkeyEditor& editor, parasite_mailbox_t& box)
{
    hold(editor, box, CHEAT_MENU_KEY_DOWN, 20);
    assert(editor.state == HotkeyEditor::RELEASE);
    hold(editor, box, CHEAT_MENU_KEY_DOWN, 60);
    assert(editor.state == HotkeyEditor::RELEASE && editor.candidate == 0);
    editor.Update(0, &box);
    hold(editor, box, 0, 600);
    assert(editor.state == HotkeyEditor::CAPTURE);
}

static void testEntry()
{
    auto box = readyBox();
    box.step = PARASITE_STEP_CHEAT_MENU;
    box.installCheatMenu = 0;
    HotkeyEditor editor = {};
    hold(editor, box, CHEAT_MENU_KEY_DOWN, 19);
    editor.Update(0, &box);
    assert(!editor.requested && editor.state == HotkeyEditor::IDLE);
    hold(editor, box, CHEAT_MENU_KEY_DOWN, 20);
    editor.Update(0, &box);
    assert(editor.requested && editor.state == HotkeyEditor::IDLE);
    box.step = PARASITE_STEP_DONE;
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::IDLE); // an unavailable menu never blocks boot
    box.installCheatMenu = 1;
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::RELEASE);
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::CAPTURE);
}

static void testLRAB()
{
    auto box = readyBox();
    HotkeyEditor editor = {};
    enter(editor, box);
    const u32 lr = CHEAT_MENU_KEY_L | CHEAT_MENU_KEY_R;
    const u32 ab = CHEAT_MENU_KEY_A | CHEAT_MENU_KEY_B;
    // Adding A and B while L and R are still down preserves all four buttons.
    editor.Update(lr, &box);
    editor.Update(lr | ab, &box);
    editor.Update(CHEAT_MENU_KEY_B, &box);
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::CAPTURE && editor.candidate == (lr | ab));
    assert(box.hotkeyStatus == PARASITE_HOTKEY_IDLE);
    // The same choice can be entered across separate taps, even after a long pause.
    tap(editor, box, lr | ab); // remove all
    tap(editor, box, lr);
    hold(editor, box, 0, 600);
    tap(editor, box, CHEAT_MENU_KEY_A);
    tap(editor, box, CHEAT_MENU_KEY_B);
    assert(editor.candidate == (lr | ab) && editor.state == HotkeyEditor::CAPTURE);
    hold(editor, box, CHEAT_MENU_KEY_START, 59);
    assert(editor.state == HotkeyEditor::CAPTURE && box.hotkeyStatus == PARASITE_HOTKEY_IDLE);
    editor.Update(CHEAT_MENU_KEY_START, &box);
    assert(editor.state == HotkeyEditor::SAVING && box.requestedHotkey == (lr | ab));
    hold(editor, box, CHEAT_MENU_KEY_START, 600);
    assert(editor.state == HotkeyEditor::SAVING); // arm7 must acknowledge the SD write
    box.hotkey = box.requestedHotkey;
    box.hotkeyStatus = PARASITE_HOTKEY_SAVED;
    editor.Update(CHEAT_MENU_KEY_START, &box);
    assert(editor.state == HotkeyEditor::SAVED);
    editor.Update(0, &box);
    tap(editor, box, CHEAT_MENU_KEY_A);
    assert(editor.state == HotkeyEditor::IDLE && !editor.requested);
}

static void testEveryCombination()
{
    for (u32 mask = 1; mask <= CHEAT_MENU_KEY_MASK; mask++)
    {
        if (!cheatMenuHotkey_isValid(mask))
            continue;
        auto box = readyBox();
        HotkeyEditor editor = {};
        enter(editor, box);
        // Select every supported button separately, including A, B and a short Start press.
        for (u32 bit = 1; bit <= CHEAT_MENU_KEY_L; bit <<= 1)
            if (mask & bit)
                tap(editor, box, bit);
        assert(editor.candidate == mask && editor.state == HotkeyEditor::CAPTURE);
        hold(editor, box, CHEAT_MENU_KEY_START, 60);
        assert(editor.state == HotkeyEditor::SAVING && box.requestedHotkey == mask);
    }
}

static void testReset()
{
    auto box = readyBox();
    const u32 oldHotkey = box.hotkey;
    HotkeyEditor editor = {};
    enter(editor, box);
    hold(editor, box, CHEAT_MENU_KEY_B, 299);
    assert(editor.state == HotkeyEditor::CAPTURE && editor.ResetSeconds() == 1);
    assert(box.hotkey == oldHotkey && box.hotkeyStatus == PARASITE_HOTKEY_IDLE);
    editor.Update(CHEAT_MENU_KEY_B, &box);
    assert(editor.state == HotkeyEditor::SAVING && editor.resetAfterSave);
    assert(box.requestedHotkey == CHEAT_MENU_DEFAULT_HOTKEY && box.hotkey == oldHotkey);
    box.hotkey = box.requestedHotkey;
    box.hotkeyStatus = PARASITE_HOTKEY_SAVED;
    editor.Update(CHEAT_MENU_KEY_B, &box);
    assert(editor.state == HotkeyEditor::RETURNING); // the main status screen is now shown
    hold(editor, box, CHEAT_MENU_KEY_B, 600);
    assert(editor.state == HotkeyEditor::RETURNING); // do not boot until B is released
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::IDLE && !editor.requested && !editor.resetAfterSave);
}

static void testInterruptedReset()
{
    auto box = readyBox();
    HotkeyEditor editor = {};
    enter(editor, box);
    // Releasing B before the exit delay only cancels the countdown.
    hold(editor, box, CHEAT_MENU_KEY_B, HotkeyEditor::EXIT_HOLD_FRAMES - 1);
    editor.Update(0, &box);
    assert(editor.resetFrames == 0 && editor.state == HotkeyEditor::CAPTURE);
    hold(editor, box, CHEAT_MENU_KEY_B, 299);
    assert(editor.state == HotkeyEditor::CAPTURE && box.hotkeyStatus == PARASITE_HOTKEY_IDLE);
    // Other buttons cancel the countdown; a long held chord containing B remains a legitimate selection.
    hold(editor, box, CHEAT_MENU_KEY_B | CHEAT_MENU_KEY_R, 600);
    assert(editor.resetFrames == 0 && box.hotkeyStatus == PARASITE_HOTKEY_IDLE);
    editor.Update(0, &box);
    hold(editor, box, CHEAT_MENU_KEY_B, 300);
    assert(editor.state == HotkeyEditor::SAVING && box.requestedHotkey == CHEAT_MENU_DEFAULT_HOTKEY);
}

static void testExitWithoutSaving()
{
    auto box = readyBox();
    const u32 oldHotkey = box.hotkey;
    HotkeyEditor editor = {};
    enter(editor, box);
    tap(editor, box, CHEAT_MENU_KEY_A);
    hold(editor, box, CHEAT_MENU_KEY_B, HotkeyEditor::EXIT_HOLD_FRAMES);
    assert(editor.ExitReady() && editor.state == HotkeyEditor::CAPTURE);
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::IDLE && !editor.requested && editor.resetFrames == 0);
    assert(box.hotkey == oldHotkey && box.hotkeyStatus == PARASITE_HOTKEY_IDLE);

    // Pressing another button instead of releasing B does not exit.
    enter(editor, box);
    hold(editor, box, CHEAT_MENU_KEY_B, 200);
    editor.Update(CHEAT_MENU_KEY_B | CHEAT_MENU_KEY_L, &box);
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::CAPTURE && box.hotkeyStatus == PARASITE_HOTKEY_IDLE);

    // After a failed save, the exit keeps the previous combination.
    tap(editor, box, CHEAT_MENU_KEY_R);
    hold(editor, box, CHEAT_MENU_KEY_START, 60);
    box.hotkeyStatus = PARASITE_HOTKEY_FAILED;
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::FAILED);
    hold(editor, box, CHEAT_MENU_KEY_B, HotkeyEditor::EXIT_HOLD_FRAMES);
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::IDLE && box.hotkey == oldHotkey);

    // After a save, the exit continues like A.
    box.hotkeyStatus = PARASITE_HOTKEY_IDLE;
    enter(editor, box);
    tap(editor, box, CHEAT_MENU_KEY_L);
    hold(editor, box, CHEAT_MENU_KEY_START, 60);
    box.hotkey = box.requestedHotkey;
    box.hotkeyStatus = PARASITE_HOTKEY_SAVED;
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::SAVED);
    hold(editor, box, CHEAT_MENU_KEY_B, HotkeyEditor::EXIT_HOLD_FRAMES);
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::IDLE && box.hotkey == CHEAT_MENU_KEY_L);
}

static void testResetFailureAndRetry()
{
    auto box = readyBox();
    const u32 oldHotkey = box.hotkey;
    HotkeyEditor editor = {};
    enter(editor, box);
    hold(editor, box, CHEAT_MENU_KEY_B, 300);
    box.hotkeyStatus = PARASITE_HOTKEY_FAILED;
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::FAILED && box.hotkey == oldHotkey);
    hold(editor, box, CHEAT_MENU_KEY_START, 60);
    assert(editor.state == HotkeyEditor::SAVING && editor.resetAfterSave);
    assert(box.requestedHotkey == CHEAT_MENU_DEFAULT_HOTKEY);
    box.hotkey = box.requestedHotkey;
    box.hotkeyStatus = PARASITE_HOTKEY_SAVED;
    editor.Update(CHEAT_MENU_KEY_START, &box);
    assert(editor.state == HotkeyEditor::RETURNING);
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::IDLE);
}

static void testResetAfterSaving()
{
    auto box = readyBox();
    HotkeyEditor editor = {};
    enter(editor, box);
    tap(editor, box, CHEAT_MENU_KEY_A);
    hold(editor, box, CHEAT_MENU_KEY_START, 60);
    box.hotkey = box.requestedHotkey;
    box.hotkeyStatus = PARASITE_HOTKEY_SAVED;
    editor.Update(0, &box);
    assert(editor.state == HotkeyEditor::SAVED);
    hold(editor, box, CHEAT_MENU_KEY_B, 300);
    assert(editor.state == HotkeyEditor::SAVING && editor.resetAfterSave);
    assert(box.requestedHotkey == CHEAT_MENU_DEFAULT_HOTKEY);
}

static void testInvalidSelection()
{
    for (u32 keys = 0; keys < 0x2000; keys++)
    {
        bool expected = keys > 0 && keys <= CHEAT_MENU_KEY_MASK &&
            (keys & 0xC0) != 0xC0 && (keys & 0x30) != 0x30;
        assert(cheatMenuHotkey_isValid(keys) == expected);
    }
    auto box = readyBox();
    HotkeyEditor editor = {};
    enter(editor, box);
    tap(editor, box, CHEAT_MENU_KEY_UP);
    tap(editor, box, CHEAT_MENU_KEY_DOWN);
    hold(editor, box, CHEAT_MENU_KEY_START, 60);
    assert(editor.state == HotkeyEditor::CAPTURE && box.hotkeyStatus == PARASITE_HOTKEY_IDLE);
}

int main()
{
    testEntry();
    testLRAB();
    testEveryCombination();
    testReset();
    testInterruptedReset();
    testExitWithoutSaving();
    testResetFailureAndRetry();
    testResetAfterSaving();
    testInvalidSelection();
    std::puts("Hotkey editor: all combinations, save, exit and five-second reset scenarios passed");
}
