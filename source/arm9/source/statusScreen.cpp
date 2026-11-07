// Status screen of the parasite on the top screen.
//
// It shows the game, the progress of the parasite and whether the cheat menu is available, so that the screen does not
// stay blank while the arm7 part works. The screen has the style of the cheat menu: a band with the title at the top, a
// band with the buttons at the bottom, and rows with room around their text between them. The text uses the font and
// the icons of the cheat menu and is drawn into a bitmap in VRAM B, which the top screen shows in VRAM display mode;
// the real Pico Loader clears VRAM B when it starts.
//
// The screen is composed as a grid of text cells: the first row is the band at the top and the last row the band at the
// bottom. Only the cells that changed are drawn, right after the start of vblank, so that updates do not flicker. The
// variables of the arm9 part are in VRAM, which does not support 8-bit writes, so each cell is a 16-bit value: the
// color in the high byte and the character in the low byte.

#include <nds/ndstypes.h>
#include <string.h>
#include "core/mini-printf.h"
#include "../../include/picoLoader7.h"
#include "../../menu/font.h"
#include "../../menu/icons.h"
#include "ndsHeader.h"
#include "statusScreen.h"
#include "hotkeyEditor.h"

#define REG_DISPCNT             (*(vu32*)0x04000000)
#define REG_VCOUNT              (*(vu16*)0x04000006)
#define REG_KEYINPUT            (*(vu16*)0x04000130)
#define REG_MASTER_BRIGHT       (*(vu16*)0x0400006C)
#define REG_VRAMCNT_B           (*(vu8*)0x04000241)
#define REG_POWCNT              (*(vu16*)0x04000304)
#define REG_DISPCNT_SUB         (*(vu32*)0x04001000)
#define REG_MASTER_BRIGHT_SUB   (*(vu16*)0x0400106C)
#define REG_SCFG_ROM            (*(vu16*)0x04004000)
#define PALETTE_BG_SUB          ((vu16*)0x05000400)

#define VRAMCNT_B_LCDC          0x80
#define DISPCNT_VRAM_B_DISPLAY  ((2 << 16) | (1 << 18)) // VRAM display mode, showing VRAM B
#define DISPCNT_BACKDROP_ONLY   (1 << 16) // normal display mode without layers
#define POWCNT_LCD              (1 << 0)
#define POWCNT_2D_MAIN          (1 << 1)
#define POWCNT_2D_SUB           (1 << 9)
#define POWCNT_MAIN_ON_TOP      (1 << 15)

// VRAM B in LCDC mode, a 256x192 bitmap with 16-bit pixels.
#define FRAMEBUFFER             ((vu16*)0x06820000)
#define SCREEN_WIDTH            256
#define SCREEN_HEIGHT           192

#define TEXT_COLUMNS            (SCREEN_WIDTH / 8)

// The bands, with their text in the middle, and the rows between them. The first row of the grid is the band at the
// top.
#define BAND_HEIGHT             16
#define HEADER_TEXT_Y           4
#define STATUS_TEXT_Y           (SCREEN_HEIGHT - BAND_HEIGHT + 4)
#define ROW_HEIGHT              12
#define FIRST_ROW_TEXT_Y        (BAND_HEIGHT + 4)
#define HEADER_ROW              0
#define STATUS_ROW              14
#define TEXT_ROWS               (STATUS_ROW + 1)

// The rows of the progress: the game, the steps (one row each), the reason why something went wrong (two rows) and the
// current combination (two rows).
#define GAME_ROW                1
#define GAME_ROWS               2
#define FIRST_LINE_ROW          3
#define STEP_ROW                4
#define SECOND_LINE_ROW         7
#define MESSAGE_ROW             8
#define STARTING_ROW            10
#define HOTKEY_LABEL_ROW        11
#define HOTKEY_ROW              12

// The rows of the hotkey editor.
#define EDITOR_LABEL_ROW        1
#define EDITOR_HOTKEY_ROW       2
#define EDITOR_FIRST_LINE_ROW   4
#define EDITOR_MESSAGE_ROW      5
#define EDITOR_SECOND_LINE_ROW  7
#define EDITOR_HELP_ROW         8
#define EDITOR_EXIT_ROW         10
#define EDITOR_RESET_ROW        11

// The colors of the cheat menu, and red for errors.
enum
{
    COLOR_NONE,
    COLOR_WHITE,
    COLOR_YELLOW,
    COLOR_GREEN,
    COLOR_RED,
    COLOR_CYAN,
    COLOR_LIGHT,
    COLOR_LINE,
    COLOR_SIGNATURE
};

static const u16 sColors[] = { 0x0000, 0x7FFF, 0x13BF, 0x13E4, 0x109F, 0x7EE0, 0x6AF5, 0x4A29, 0x51CC };

#define BACKDROP_COLOR          0x2083 // navy
#define BAND_COLOR              0x3D06 // dark blue

/// @brief An empty cell. Spaces have no color, so that all empty cells are equal.
#define EMPTY_CELL              ' '

static u32 sShown;
static u32 sComposedStep;
static u32 sComposedError;
static u32 sComposedHotkey;
static u32 sComposedState;
static u32 sComposedCandidate;
static u32 sComposedResetSeconds;
static u32 sComposedExitReady;
static HotkeyEditor sEditor;
static u16 sCells[TEXT_ROWS * TEXT_COLUMNS];
static u16 sDrawnCells[TEXT_ROWS * TEXT_COLUMNS];

static void clearCells()
{
    for (u32 i = 0; i < TEXT_ROWS * TEXT_COLUMNS; i++)
        sCells[i] = EMPTY_CELL;
}

static void putChar(u32 column, u32 row, char c, u32 color)
{
    if (column < TEXT_COLUMNS && row < TEXT_ROWS)
        sCells[row * TEXT_COLUMNS + column] = c == ' ' ? EMPTY_CELL : (color << 8) | (u8)c;
}

/// @brief Puts text at the given cell. Text that reaches the end of the row is cut; '\n' continues at column 1 of the
///        next row. Returns the column after the text.
static u32 putText(u32 column, u32 row, const char* text, u32 color)
{
    for (; *text != 0; text++)
    {
        if (*text == '\n')
        {
            row++;
            column = 1;
        }
        else if (column < TEXT_COLUMNS)
        {
            putChar(column++, row, *text, color);
        }
    }
    return column;
}

/// @brief Puts a text with the icons of buttons: the icons are white and the text is light gray.
static void putKeyHint(u32 column, u32 row, const char* hint)
{
    for (; *hint != 0; hint++)
        putChar(column++, row, *hint, (u8)*hint >= ICON_FIRST_CHAR ? COLOR_WHITE : COLOR_LIGHT);
}

/// @brief Fills a row with a horizontal line.
static void putLine(u32 row)
{
    for (u32 column = 1; column < TEXT_COLUMNS - 1; column++)
        putChar(column, row, ICON_LINE[0], COLOR_LINE);
}

/// @brief Shows the whole combination, wrapping between buttons if it needs a second row.
static void putHotkey(u32 row, u32 hotkey, u32 color)
{
    static const struct { u32 key; const char* name; } buttons[] =
    {
        { CHEAT_MENU_KEY_L, "L" }, { CHEAT_MENU_KEY_R, "R" },
        { CHEAT_MENU_KEY_UP, "Up" }, { CHEAT_MENU_KEY_DOWN, "Down" },
        { CHEAT_MENU_KEY_LEFT, "Left" }, { CHEAT_MENU_KEY_RIGHT, "Right" },
        { CHEAT_MENU_KEY_A, "A" }, { CHEAT_MENU_KEY_B, "B" },
        { CHEAT_MENU_KEY_START, "Start" }, { CHEAT_MENU_KEY_SELECT, "Select" }
    };
    u32 column = 1;
    for (const auto& button : buttons)
    {
        if (!(hotkey & button.key))
            continue;
        if (column != 1)
            column = putText(column, row, "+", color);
        if (column + strlen(button.name) > TEXT_COLUMNS - 1)
        {
            row++;
            column = 1;
        }
        column = putText(column, row, button.name, color);
    }
    if (!hotkey)
        putText(1, row, "...", color);
}

static void composeHotkeyEditor()
{
    clearCells();
    putText(1, HEADER_ROW, "Change menu hotkey", COLOR_WHITE);
    putText(1, EDITOR_LABEL_ROW, "New combination:", COLOR_LIGHT);
    putHotkey(EDITOR_HOTKEY_ROW, sEditor.candidate, COLOR_CYAN);
    putLine(EDITOR_FIRST_LINE_ROW);
    putLine(EDITOR_SECOND_LINE_ROW);
    switch (sEditor.state)
    {
        case HotkeyEditor::RELEASE:
            putText(1, EDITOR_MESSAGE_ROW, "Release all buttons", COLOR_WHITE);
            break;
        case HotkeyEditor::CAPTURE:
        case HotkeyEditor::FAILED:
            if (sEditor.state == HotkeyEditor::FAILED)
            {
                putText(1, EDITOR_MESSAGE_ROW, "Can't save the hotkey to SD.\nPrevious hotkey is still active",
                    COLOR_RED);
            }
            else if (sEditor.candidate && !cheatMenuHotkey_isValid(sEditor.candidate))
            {
                putText(1, EDITOR_MESSAGE_ROW, "Invalid combination", COLOR_RED);
            }
            putText(1, EDITOR_HELP_ROW, "Press a button to add/remove", COLOR_LIGHT);
            putText(1, EDITOR_HELP_ROW + 1, "A B L R Start Select + D-pad", COLOR_LIGHT);
            if (cheatMenuHotkey_isValid(sEditor.candidate))
                putKeyHint(1, STATUS_ROW, "Hold " ICON_KEY_START " 1s: Save to SD");
            break;
        case HotkeyEditor::SAVING:
            putText(1, EDITOR_MESSAGE_ROW, "Saving to SD...", COLOR_WHITE);
            break;
        case HotkeyEditor::SAVED:
            putText(1, EDITOR_MESSAGE_ROW, "Hotkey saved", COLOR_GREEN);
            putKeyHint(1, STATUS_ROW, ICON_KEY_A " Continue");
            break;
        default:
            putText(1, EDITOR_MESSAGE_ROW, "Release all buttons", COLOR_WHITE);
            break;
    }
    bool showHelp = sEditor.state != HotkeyEditor::SAVING && sEditor.state != HotkeyEditor::CLOSING;
    if (sEditor.ExitReady())
    {
        // After a save, leaving the editor keeps the saved combination.
        putText(1, EDITOR_EXIT_ROW, sEditor.state == HotkeyEditor::SAVED ? "Release B: Continue" :
            "Release B: Exit without saving", COLOR_YELLOW);
    }
    else if (showHelp)
    {
        putText(1, EDITOR_EXIT_ROW, "Hold B 2s, release: Exit", COLOR_LIGHT);
    }
    if (sEditor.ResetSeconds())
    {
        char countdown[32];
        mini_snprintf(countdown, sizeof(countdown), "Reset to default in %u...", sEditor.ResetSeconds());
        putText(1, EDITOR_RESET_ROW, countdown, COLOR_YELLOW);
    }
    else if (showHelp)
    {
        putText(1, EDITOR_RESET_ROW, "Hold B 5s: Reset to default", COLOR_LIGHT);
    }
}

/// @brief Puts the file name of the game in at most two rows. Latin-1 letters lose their accents like in the cheat
///        menu, other non-ASCII characters become '?', and a name that does not fit ends with "...".
static void putGameName(const char* path)
{
    const char* slash = strrchr(path, '/');
    auto name = (const u8*)(slash ? slash + 1 : path);
    u32 column = 1;
    u32 row = GAME_ROW;
    while (*name != 0)
    {
        if (row == GAME_ROW + GAME_ROWS)
        {
            for (u32 i = 0; i < 3; i++)
                putChar(TEXT_COLUMNS - 4 + i, row - 1, '.', COLOR_CYAN);
            break;
        }
        u8 c = *name++;
        if (c >= 0x80)
        {
            u32 codePoint = c >= 0xC0 && c < 0xE0 && (*name & 0xC0) == 0x80 ? ((c & 0x1F) << 6) | (*name & 0x3F) : 0;
            while ((*name & 0xC0) == 0x80)
                name++;
            c = codePoint >= 0xC0 && codePoint <= 0xFF ? sLatin1ToAscii[codePoint - 0xC0] : '?';
        }
        putChar(column++, row, c, COLOR_CYAN);
        if (column == TEXT_COLUMNS - 1)
        {
            column = 1;
            row++;
        }
    }
}

/// @brief Puts the label of the given step on its row with the result at the end of the row, or "Label..." while the
///        step is in progress (\p result is \c nullptr).
static void putStep(u32 step, const char* label, const char* result, u32 color)
{
    u32 row = STEP_ROW + step;
    u32 column = putText(1, row, label, COLOR_LIGHT);
    if (!result)
    {
        putText(column, row, "...", COLOR_LIGHT);
        return;
    }
    putText(TEXT_COLUMNS - 1 - strlen(result), row, result, color);
}

static const char* getErrorText(u32 error)
{
    switch (error)
    {
        case PARASITE_ERROR_NO_DLDI:            return "No DLDI driver from the\nlauncher";
        case PARASITE_ERROR_SD_CARD:            return "Can't read the SD card";
        case PARASITE_ERROR_REAL_LOADER:        return "Pico Loader not found in\n/_pico/loader/";
        case PARASITE_ERROR_TRANSPARENT:        return "Turned off by\n/_pico/parasite-transparent";
        case PARASITE_ERROR_GAME_UNSUPPORTED:   return "Game not supported by the menu";
        case PARASITE_ERROR_NO_SEARCH_FILE:     return "Can't create\n/_pico/cheatsearch.bin";
        case PARASITE_ERROR_OUT_OF_MEMORY:      return "Too many cheats for the menu";
        case PARASITE_ERROR_MENU_FILE:          return "Can't write\n/_pico/cheatmenu.bin";
        case PARASITE_ERROR_LOADER_UNSUPPORTED: return "This Pico Loader version is\nnot supported by the menu";
        default:                                return "";
    }
}

/// @brief Composes the screen for the given step and error.
static void compose(const parasite_mailbox_t* box, u32 step, u32 error)
{
    if (sEditor.state != HotkeyEditor::IDLE && sEditor.state != HotkeyEditor::RETURNING)
    {
        composeHotkeyEditor();
        return;
    }
    clearCells();
    putText(1, HEADER_ROW, "Pico Cheat Menu", COLOR_WHITE);
    putText(TEXT_COLUMNS - 13, HEADER_ROW, "By Esentasse", COLOR_SIGNATURE);
    putGameName(((const pload_header7_t*)box->launcherHeader)->loadParams.romPath);
    putLine(FIRST_LINE_ROW);
    putLine(SECOND_LINE_ROW);

    // An error before the cheat menu step stops the parasite. The reason why the cheat menu is off is only shown once
    // the arm9 part has decided, since it may still change.
    bool fatal = error != PARASITE_ERROR_NONE && step < PARASITE_STEP_CHEAT_MENU;
    bool showError = error != PARASITE_ERROR_NONE && step != PARASITE_STEP_CHEAT_MENU;
    static const char* const sLabels[] = { "SD card", "Pico Loader" };
    for (u32 i = PARASITE_STEP_SD_CARD; i < PARASITE_STEP_CHEAT_MENU && i <= step; i++)
        putStep(i, sLabels[i], i < step ? "OK" : (fatal ? "ERROR" : nullptr), i < step ? COLOR_GREEN : COLOR_RED);
    if (step == PARASITE_STEP_CHEAT_MENU)
    {
        putStep(step, "Cheat menu", nullptr, COLOR_WHITE);
    }
    else if (step == PARASITE_STEP_DONE)
    {
        char count[16];
        // Without cheats, the menu still offers the cheat search.
        if (box->cheatCount == 0)
            strcpy(count, "search only");
        else
            mini_snprintf(count, sizeof(count), box->cheatCount == 1 ? "%u cheat" : "%u cheats", box->cheatCount);
        putStep(PARASITE_STEP_CHEAT_MENU, "Cheat menu", error ? "OFF" : count, error ? COLOR_YELLOW : COLOR_GREEN);
        // Like the real Pico Loader: DSi mode if the console runs in DSi mode and the game supports it.
        bool dsiMode = (REG_SCFG_ROM & 3) == 1 && (box->romUnitCode & NDS_HEADER_UNIT_CODE_SUPPORTS_DSI_MODE);
        u32 column = putText(1, STARTING_ROW, "Starting the game in ", COLOR_WHITE);
        putText(column, STARTING_ROW, dsiMode ? "DSi mode" : "DS mode", COLOR_CYAN);
    }

    if (showError)
        putText(1, MESSAGE_ROW, getErrorText(error), fatal ? COLOR_RED : COLOR_YELLOW);
    // The arm7 part loads the saved combination before the cheat menu step; until then box->hotkey is the default.
    if (step >= PARASITE_STEP_CHEAT_MENU && (!showError || box->installCheatMenu))
    {
        putText(1, HOTKEY_LABEL_ROW, "In-game menu hotkey:", COLOR_LIGHT);
        putHotkey(HOTKEY_ROW, box->hotkey, COLOR_CYAN);
        putText(1, STATUS_ROW, "Hold Down to change the hotkey", COLOR_LIGHT);
    }
}

/// @brief Returns the height of the text of the given row.
static u32 getRowTextY(u32 row)
{
    if (row == HEADER_ROW)
        return HEADER_TEXT_Y;
    if (row == STATUS_ROW)
        return STATUS_TEXT_Y;
    return FIRST_ROW_TEXT_Y + (row - 1) * ROW_HEIGHT;
}

static void drawCell(u32 index, u16 cell)
{
    u32 c = cell & 0xFF;
    const u8* glyph;
    if (c >= ICON_FIRST_CHAR && c < ICON_FIRST_CHAR + ICON_COUNT)
    {
        glyph = &sIcons[(c - ICON_FIRST_CHAR) * 8];
    }
    else
    {
        if (c < 0x20 || c > 0x7F)
            c = '?';
        glyph = &sFont[(c - 0x20) * 8];
    }
    u32 row = index / TEXT_COLUMNS;
    u16 color = sColors[cell >> 8];
    u16 background = row == HEADER_ROW || row == STATUS_ROW ? BAND_COLOR : BACKDROP_COLOR;
    vu16* line = FRAMEBUFFER + getRowTextY(row) * SCREEN_WIDTH + (index % TEXT_COLUMNS) * 8;
    for (u32 y = 0; y < 8; y++, line += SCREEN_WIDTH)
    {
        for (u32 x = 0; x < 8; x++)
            line[x] = (glyph[y] >> x) & 1 ? color : background;
    }
}

static void drawChangedCells()
{
    for (u32 i = 0; i < TEXT_ROWS * TEXT_COLUMNS; i++)
    {
        if (sCells[i] != sDrawnCells[i])
        {
            drawCell(i, sCells[i]);
            sDrawnCells[i] = sCells[i];
        }
    }
}

void statusScreen_show()
{
    if (sShown)
        return;
    sShown = true;
    REG_VRAMCNT_B = VRAMCNT_B_LCDC;
    for (u32 y = 0; y < SCREEN_HEIGHT; y++)
    {
        u16 color = y < BAND_HEIGHT || y >= SCREEN_HEIGHT - BAND_HEIGHT ? BAND_COLOR : BACKDROP_COLOR;
        for (u32 x = 0; x < SCREEN_WIDTH; x++)
            FRAMEBUFFER[y * SCREEN_WIDTH + x] = color;
    }
    for (u32 i = 0; i < TEXT_ROWS * TEXT_COLUMNS; i++)
        sDrawnCells[i] = EMPTY_CELL;
    // The next update composes the screen.
    sComposedStep = ~0u;
    REG_MASTER_BRIGHT = 0;
    REG_DISPCNT = DISPCNT_VRAM_B_DISPLAY;
    // The bottom screen shows only the backdrop.
    PALETTE_BG_SUB[0] = BACKDROP_COLOR;
    REG_MASTER_BRIGHT_SUB = 0;
    REG_DISPCNT_SUB = DISPCNT_BACKDROP_ONLY;
    REG_POWCNT = POWCNT_LCD | POWCNT_2D_MAIN | POWCNT_2D_SUB | POWCNT_MAIN_ON_TOP;
}

void statusScreen_finish()
{
    if (sShown)
        PALETTE_BG_SUB[0] = 0;
}

bool statusScreen_hotkeyActive()
{
    return sEditor.state != HotkeyEditor::IDLE;
}

void statusScreen_update(parasite_mailbox_t* box)
{
    while (REG_VCOUNT == 192);
    while (REG_VCOUNT != 192);
    if (!sShown)
        return;
    u32 step = box->step;
    u32 error = box->error;
    sEditor.Update(~REG_KEYINPUT & CHEAT_MENU_KEY_MASK, box);
    u32 hotkey = box->hotkey;
    u32 resetSeconds = sEditor.ResetSeconds();
    u32 exitReady = sEditor.ExitReady();
    if (step != sComposedStep || error != sComposedError || hotkey != sComposedHotkey ||
        (u32)sEditor.state != sComposedState || sEditor.candidate != sComposedCandidate ||
        resetSeconds != sComposedResetSeconds || exitReady != sComposedExitReady)
    {
        sComposedExitReady = exitReady;
        sComposedStep = step;
        sComposedError = error;
        sComposedHotkey = hotkey;
        sComposedState = sEditor.state;
        sComposedCandidate = sEditor.candidate;
        sComposedResetSeconds = resetSeconds;
        compose(box, step, error);
    }
    drawChangedCells();
}
