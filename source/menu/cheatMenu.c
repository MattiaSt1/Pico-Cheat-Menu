// In-game cheat menu.
//
// The menu is a position-independent blob that the parasite stores in the cheat menu file, followed by the cheat menu
// data of the game (see CheatMenuDefinitions.h). When the button combination is pressed, the resident part loads the
// menu image into memory that holds the game's code and calls cheatmenu_entry (entry.s). The menu runs inside the
// interrupt handler of the game, so the game is paused while the menu is open. The image is loaded from the file each
// time, so all static variables start at zero.
//
// The menu lists all cheats of the game from usrcheat.dat, with their folders, followed by the launcher cheats that
// are not in the database. The database stays on the sd card: the menu reads the game's block through its sector runs
// with the DLDI driver of the resident part, which holds the SDK card lock while the menu is open. Only the rows that
// can be visible are kept in memory.
//
// When the menu is closed after changes, the enabled cheats are written to the cheat list, which the arm7 cheat engine
// of Pico Loader runs every vblank, and the enable bits and the position in the list are written back to the cheat
// menu file for the next time.
//
// The menu is drawn on the sub engine. Everything it changes there is saved before and restored afterwards, so the
// game's graphics are not affected; only registers that can be read back are changed. To use little VRAM, every text
// cell is an 8x8 sprite with a glyph of the font or an icon (see icons.h). A colored band at the top holds the title
// and a band at the bottom the buttons of the open screen. Since 128 sprites are not enough for a screen of text, the
// rows between the bands are drawn with two sets of 32 sprites that are rewritten while the screen is displayed: each
// row is written as soon as the row that used the same set before it has been displayed.
//
// R opens the cheat search (see cheatSearch.h): a screen with the settings of the next search, and a screen with the
// results once few are left, where a value can be written or locked. The state of the search is stored in the cheat
// search file, so it is kept between the times the menu is opened. The cheats of the locked values come first in the
// cheat list.

#include <stddef.h>
#include <nds/ndstypes.h>
#include "../include/picoLoader7.h"
#include "cheatMenuFile.h"
#include "cheatSearch.h"
#include "font.h"
#include "icons.h"

// ---- Hardware ----

#define REG_DISPCNT_SUB         (*(vu32*)0x04001000)
#define REG_VCOUNT              (*(vu16*)0x04000006)
#define REG_BLDCNT_SUB          (*(vu16*)0x04001050)
#define REG_MASTER_BRIGHT_SUB   (*(vu16*)0x0400106C)
#define REG_KEYINPUT            (*(vu16*)0x04000130)
#define REG_VRAMCNT_D           (*(vu8*)0x04000243)
#define REG_VRAMCNT_I           (*(vu8*)0x04000249)
#define REG_POWCNT              (*(vu16*)0x04000304)

#define PALETTE_BG_SUB          ((vu16*)0x05000400)
#define PALETTE_OBJ_SUB         ((vu16*)0x05000600)
#define OAM_SUB                 ((vu16*)0x07000400)
#define VRAM_SUB_OBJ            ((vu32*)0x06600000)
#define VRAM_LCDC_I             ((vu32*)0x068A0000)

#define OAM_ENTRY_COUNT         128

#define VRAMCNT_ENABLE          0x80
#define VRAMCNT_D_SUB_OBJ       (VRAMCNT_ENABLE | 4)
#define VRAMCNT_I_SUB_OBJ       (VRAMCNT_ENABLE | 2)
#define VRAMCNT_LCDC            VRAMCNT_ENABLE

#define POWCNT_LCD              (1 << 0)
#define POWCNT_2D_SUB           (1 << 9)

#define DISPCNT_FORCED_BLANK    (1 << 7)
// Normal display mode, sprites on, 1D sprite tile mapping with a 32 byte boundary, OAM accessible during hblank.
#define DISPCNT_MENU            ((1 << 23) | (1 << 16) | (1 << 12) | (1 << 4))

#define OBJ_DISABLED            (1 << 9)
#define OBJ_WIDE                (1 << 14) // attribute 0 shape of 32x16 and 64x32 sprites
#define OBJ_TALL                (1 << 15) // attribute 0 shape of 8x16 and 8x32 sprites
#define OBJ_SIZE_8X32           (1 << 14) // attribute 1 size of an 8x32 sprite
#define OBJ_SIZE_32X16          (2 << 14) // attribute 1 size of a 32x16 sprite
#define OBJ_SIZE_64X32          (3 << 14) // attribute 1 size of a 64x32 sprite

#define KEY_A                   (1 << 0)
#define KEY_B                   (1 << 1)
#define KEY_SELECT              (1 << 2)
#define KEY_START               (1 << 3)
#define KEY_RIGHT               (1 << 4)
#define KEY_LEFT                (1 << 5)
#define KEY_UP                  (1 << 6)
#define KEY_DOWN                (1 << 7)
#define KEY_R                   (1 << 8)
#define KEY_L                   (1 << 9)
#define KEY_MASK                0x3FF
#define KEY_DIRECTIONS          (KEY_RIGHT | KEY_LEFT | KEY_UP | KEY_DOWN)

// ---- Layout ----

#define TEXT_COLUMNS            32
// Text stays one column away from the edges of the screen.
#define LAST_TEXT_COLUMN        (TEXT_COLUMNS - 2)

// The bands at the top and at the bottom of the screen, with their text in the middle.
#define BAND_HEIGHT             16
#define HEADER_TEXT_Y           4
#define STATUS_BAND_Y           (192 - BAND_HEIGHT)
#define STATUS_TEXT_Y           (STATUS_BAND_Y + 4)

// The rows between the bands. A row is ROW_HEIGHT pixels high, which leaves room around its 8 pixels of text.
#define ROW_HEIGHT              12
#define CONTENT_ROWS            13
#define FIRST_ROW_Y             (BAND_HEIGHT + 2)
#define ROW_TEXT_OFFSET         ((ROW_HEIGHT - 8) / 2)

#define LIST_VISIBLE_ROWS       CONTENT_ROWS
#define EMPTY_LIST_ROW          5 // the first row of the text that a game without cheats shows instead of the list
#define CURSOR_COLUMN           0
#define STATE_COLUMN            1 // the icon that shows whether a cheat is on, or the icon of a folder
#define NAME_COLUMN             4
#define NAME_VISIBLE_LENGTH     (LAST_TEXT_COLUMN + 1 - NAME_COLUMN)
#define SELECTED_NAME_LENGTH    96

// The description view: the name of the entry, a line and the rows of the note, which are closer together than the rows
// of the list so that more of the note fits. All rows are centered. A long name takes more rows, and leaves as many
// fewer rows for the note.
#define DESCRIPTION_ROW_HEIGHT  9
#define DESCRIPTION_LINE_GAP    10 // the distance from the line to the rows before and after it
#define DESCRIPTION_NAME_ROWS   3
#define DESCRIPTION_COLUMNS     (LAST_TEXT_COLUMN)
#define DESCRIPTION_ROWS        15

// The signature in the header of the list stays in this column, whatever the length of the position next to it.
#define SIGNATURE_COLUMN        12

// The selected row moves this many pixels to the right, so its name shows one character less.
#define FOCUS_SHIFT             6

// The rows of the list that can be visible: the visible rows and, while the list scrolls, the row on each side.
#define LIST_ROW_CACHE_SIZE     (LIST_VISIBLE_ROWS + 2)

// When the list does not fit on the screen, a scrollbar in the last column shows the position of the cursor.
#define SCROLLBAR_X             ((TEXT_COLUMNS - 1) * 8)
#define SCROLLBAR_TRACK_SPRITES 5 // 8x32 sprites between the bands
#define THUMB_HEIGHT            16

// ---- Tiles and sprites ----

#define FIRST_GLYPH             0x20
#define GLYPH_COUNT             96
// After the glyphs and the icons: the solid tiles of the 64x32 sprites of the bands, the tiles of the 32x16 sprites of
// the selection bar, 4 tiles for the 8x32 sprites of the scrollbar track and 2 tiles for the 8x16 thumb.
#define ICON_TILE               GLYPH_COUNT
#define BAND_TILE               (ICON_TILE + ICON_COUNT)
#define BAND_TILE_COUNT         32
#define BAR_TILE                (BAND_TILE + BAND_TILE_COUNT)
#define BAR_TILE_COUNT          8
#define TRACK_TILE              (BAR_TILE + BAR_TILE_COUNT)
#define THUMB_TILE              (TRACK_TILE + 4)
#define TILE_VRAM_SIZE          ((THUMB_TILE + 2) * 32)

// Sprites, from the front to the back: the text of the bands, the bands, the scrollbar (thumb, then track), the two
// sets of sprites for the rows between the bands, and the selection bar. The bands hide the list rows that slide behind
// them while the list scrolls. The header and the status row share their sprites: the status row is written once the
// header has been displayed.
#define FRONT_SPRITES           0
#define BAND_SPRITES_PER_BAND   (TEXT_COLUMNS / 8)
#define BAND_SPRITES            (FRONT_SPRITES + TEXT_COLUMNS)
#define THUMB_SPRITE            (BAND_SPRITES + 2 * BAND_SPRITES_PER_BAND)
#define TRACK_SPRITES           (THUMB_SPRITE + 1)
#define TEXT_SPRITES            (TRACK_SPRITES + SCROLLBAR_TRACK_SPRITES)
#define BAR_SPRITES             (TEXT_SPRITES + 2 * TEXT_COLUMNS)
#define BAR_SPRITE_COUNT        (TEXT_COLUMNS / 4)

// Each sprite palette uses only color 1, for the pixels of the tiles; color 0 is transparent.
enum
{
    PALETTE_WHITE,
    PALETTE_LIGHT,
    PALETTE_GRAY,
    PALETTE_YELLOW,
    PALETTE_GREEN,
    PALETTE_CYAN,
    PALETTE_SIGNATURE,
    PALETTE_LINE,
    PALETTE_BAR,
    PALETTE_BAND,
    PALETTE_COUNT
};

static const u16 sPaletteColors[PALETTE_COUNT] =
{
    0x7FFF, // white: titles, cheats that are on and the selected row
    0x6AF5, // light gray: cheats that are off, explanations and keys
    0x4E73, // gray: what cannot be used at the moment
    0x13BF, // yellow
    0x13E4, // green
    0x7EE0, // cyan
    0x51CC, // signature, subtle on the band
    0x4A29, // lines and the scrollbar track
    0x5D87, // selection bar, blue
    0x3D06  // bands, dark blue
};

#define BACKDROP_COLOR          0x2083 // navy

// ---- Timing ----

// Holding a direction key repeats it after REPEAT_DELAY frames, then every REPEAT_RATE frames.
#define REPEAT_DELAY            20
#define REPEAT_RATE             4
// Pixels per frame of the animations, so that moving by one row takes as long as one key repeat.
#define ANIMATION_SPEED         (ROW_HEIGHT / REPEAT_RATE)
// Number of vblank lines that drawFrame needs at least before the first line is displayed.
#define VBLANK_DRAW_LINES       10
// Names that do not fit scroll by one character every MARQUEE_SPEED frames, pausing MARQUEE_PAUSE frames at the ends.
#define MARQUEE_PAUSE           60
#define MARQUEE_SPEED           6
#define MESSAGE_FRAMES          120

// ---- Cheat search ----

// The rows of the screen with the settings of the search: the settings, a line, the actions, a line and a few rows that
// tell what to do next.
#define OPTION_FIRST_LINE_ROW   3
#define OPTION_SECOND_LINE_ROW  8
#define OPTION_CHOICE_COLUMN    14
#define OPTION_VALUE_COLUMN     16
#define SEARCH_HINT_FIRST_ROW   9
// The rows of the progress while a search runs, and the columns of its bar.
#define PROGRESS_TEXT_ROW       5
#define PROGRESS_BAR_ROW        6
#define PROGRESS_BAR_COLUMN     2
#define PROGRESS_BAR_CELLS      (TEXT_COLUMNS - 2 * PROGRESS_BAR_COLUMN)
// The screen with the results has a row with the names of its columns.
#define RESULT_FIRST_ROW        1
#define RESULT_VISIBLE_ROWS     (CONTENT_ROWS - RESULT_FIRST_ROW)
// The columns of a row of the results: the address, the value right-aligned in MAX_DIGITS columns, and the lock.
#define RESULT_ADDRESS_COLUMN   2
#define RESULT_VALUE_COLUMN     12
#define RESULT_LOCK_COLUMN      23 // the icon, followed by a space and the text
// Digits of the largest number that the menu shows, 4294967295.
#define MAX_DIGITS              10

// ---- Database ----

#define SECTOR_SIZE             CHEAT_MENU_SECTOR_SIZE
#define SECTOR_CACHE_SIZE       4

#define ITEM_FLAG_FOLDER        (1u << 28)
#define ITEM_SIZE_MASK          0x00FFFFFF
// The top byte of the flags of a folder in which at most one cheat can be on.
#define ITEM_ONE_ONLY_FOLDER    0x11

// Returned by readCheatOpcodes when the opcodes do not fit in the room left in the cheat list.
#define OPCODES_DO_NOT_FIT      0xFFFFFFFF

// ---- Interface ----

// Defined in entry.s. Hidden visibility makes the compiler use pc-relative instead of GOT-based access.
extern cheat_menu_image_header_t cheatmenu_header __attribute__((visibility("hidden")));
void cheatmenu_cleanDataCache(const void* start, u32 size) __attribute__((visibility("hidden")));
void cheatmenu_invalidateDataCache(const void* start, u32 size) __attribute__((visibility("hidden")));

// Called by cheatmenu_entry in entry.s.
u32 cheatmenu_main(cheat_menu_resident_t* resident, u32 numberOfCheats);

// The menu is linked without a C library, but the compiler may emit calls to these.
void* memset(void* dst, int value, size_t count);
void* memcpy(void* dst, const void* src, size_t count);

// ---- State ----

// The screens of the cheat search.
enum
{
    SEARCH_CLOSED,
    SEARCH_OPTIONS,
    SEARCH_RESULTS
};

// The options on the screen with the settings of the search, from the top.
enum
{
    OPTION_SIZE,
    OPTION_COMPARE,
    OPTION_VALUE,
    OPTION_SEARCH,
    OPTION_RESULTS,
    OPTION_NEW,
    OPTION_UNLOCK,
    OPTION_COUNT
};

// What handleEditKeys did with the keys.
enum
{
    EDIT_CONTINUE,
    EDIT_CONFIRMED,
    EDIT_CANCELLED
};

typedef enum
{
    ENTRY_CHEAT,
    ENTRY_FOLDER,
    ENTRY_EXTRA_CHEAT
} EntryType;

// A row of the list, as read from the database.
typedef struct
{
    u32 item;
    u8 type;
    u8 enabledInFolder;
    char name[NAME_VISIBLE_LENGTH + 1];
} row_t;

static cheat_menu_resident_t* sResident;
static cheat_menu_data_t* sData;
// Number of cheats in the cheat list, including the marker.
static u32 sNumberOfCheats;

// Navigation. An entry is a position in the current list: the root list or the cheats of the open folder.
static bool sInFolder;
static u32 sFolderItem;
static u32 sFolderChildCount;
static bool sFolderOneOnly;
static char sFolderName[SELECTED_NAME_LENGTH + 1];
static u32 sFolderNameLength;
static u32 sFolderVisibleLength; // the characters of the name that fit next to the longest position in the folder
static u32 sFolderMarqueeTimer;
static u32 sFolderMarqueeOffset;
static u32 sRootCursor;
static u32 sRootTop;
static u32 sCursor;
static u32 sListTop;
static bool sChanged;
static u32 sRepeatTimer;

// The rows that can be visible, read from the database when the list scrolls. sRows[0] is the entry before sListTop.
static row_t sRows[LIST_ROW_CACHE_SIZE];
static bool sRowsValid;
static int sRowsFirst;

// The full name of the selected entry, which scrolls when it does not fit.
static char sSelectedName[SELECTED_NAME_LENGTH + 1];
static u32 sSelectedNameLength;
static u32 sSelectedNameCursor; // sCursor + 1 of the entry in sSelectedName, or 0 if none
static u32 sMarqueeTimer;
static u32 sMarqueeOffset;

// Selection animation: the selected entry slides to the right and the entry that was selected before slides back.
// The list is drawn sScrollOffset pixels away from its place and slides to it.
static u32 sFocusEntry;
static int sFocusShift;
static u32 sUnfocusEntry;
static int sUnfocusShift;
static int sBarY;
static int sScrollOffset;
static int sThumbY;

// The description view, with the rows of the note of the selected entry.
static bool sDescriptionOpen;
static u32 sDescriptionStart;
static u32 sDescriptionEnd;
static char sDescriptionNameRows[DESCRIPTION_NAME_ROWS][TEXT_COLUMNS + 1];
static u32 sDescriptionNameRowCount;
static char sDescriptionRows[DESCRIPTION_ROWS][TEXT_COLUMNS + 1];
static u32 sDescriptionRowCount; // the rows that the note can use next to the name

// The message on the status row, if any, and the number of frames until it disappears (0 to keep it).
static const char* sMessage;
static u32 sMessageTimer;

// Set when the database cannot be read; the main loop reports it.
static bool sDatabaseError;

// A message that is composed at runtime.
static char sMessageBuffer[TEXT_COLUMNS + 1];

// The cheat search: the open screen, its state as read from the cheat search file, the selected option and the
// selected result.
static u32 sSearchScreen;
static cheat_search_state_t sSearch __attribute__((aligned(32)));
static bool sSearchLoaded;
static u32 sOptionCursor;
static u32 sResultCursor;
static u32 sResultTop;
static u32 sProgressPercent;

// The number that is edited, the search value or the value of a result, one decimal digit per entry from the most
// significant one.
static bool sEditing;
static u8 sEditDigits[MAX_DIGITS];
static u32 sEditDigitCount;
static u32 sEditPosition;

static u8 sSectorCache[SECTOR_CACHE_SIZE][SECTOR_SIZE] __attribute__((aligned(32)));
static u32 sSectorCacheTags[SECTOR_CACHE_SIZE]; // block sector + 1, or 0 if the slot is empty
static u32 sSectorCacheNext;

// The state of the sub engine that the menu changes.
static struct
{
    u32 dispCnt;
    u16 powCnt;
    u16 bldCnt;
    u16 masterBright;
    u16 backdrop;
    u8 vramCntD;
    u8 vramCntI;
    u16 objPalette[PALETTE_COUNT]; // color 1 of each palette
    u16 oam[OAM_ENTRY_COUNT][3]; // attributes 0-2; attribute 3 is not changed
    u32 tileVram[TILE_VRAM_SIZE / 4];
} sSaved;

// ---- Utilities ----

void* memset(void* dst, int value, size_t count)
{
    u8* d = (u8*)dst;
    while (count--)
        *d++ = (u8)value;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t count)
{
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    while (count--)
        *d++ = *s++;
    return dst;
}

static u32 stringLength(const char* string)
{
    u32 length = 0;
    while (string[length] != 0)
        length++;
    return length;
}

// Unsigned division, since the menu is linked without the division routines of the compiler.
static u32 divide(u32 numerator, u32 denominator)
{
    u32 quotient = 0;
    for (int bit = 31; bit >= 0; bit--)
    {
        if ((numerator >> bit) >= denominator)
        {
            numerator -= denominator << bit;
            quotient |= 1u << bit;
        }
    }
    return quotient;
}

static const u32 sPowersOfTen[MAX_DIGITS] =
{
    1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1
};

// Formats a decimal number without division. Returns its length.
static u32 formatNumber(char* dst, u32 value)
{
    u32 length = 0;
    for (u32 i = 0; i < MAX_DIGITS; i++)
    {
        u32 digit = 0;
        while (value >= sPowersOfTen[i])
        {
            value -= sPowersOfTen[i];
            digit++;
        }
        if (digit != 0 || length != 0 || i == MAX_DIGITS - 1)
            dst[length++] = '0' + digit;
    }
    dst[length] = 0;
    return length;
}

// Formats a number as 8 hexadecimal digits.
static void formatHex(char* dst, u32 value)
{
    for (u32 i = 0; i < 8; i++)
    {
        u32 digit = (value >> (28 - i * 4)) & 0xF;
        dst[i] = digit < 10 ? '0' + digit : 'A' + digit - 10;
    }
    dst[8] = 0;
}

// Formats "<count> result" or "<count> results". Returns its length.
static u32 formatResultCount(char* dst, u32 count)
{
    u32 length = formatNumber(dst, count);
    const char* unit = count == 1 ? " result" : " results";
    u32 unitLength = stringLength(unit);
    memcpy(&dst[length], unit, unitLength + 1);
    return length + unitLength;
}

// Shows a message on the status row for MESSAGE_FRAMES frames.
static void showMessage(const char* message)
{
    sMessage = message;
    sMessageTimer = MESSAGE_FRAMES;
}

// Returns the pressed keys as set bits.
static inline u32 readKeys(void)
{
    return ~REG_KEYINPUT & KEY_MASK;
}

// Waits for the start of the next vblank.
static void waitVBlank(void)
{
    while (REG_VCOUNT == 192);
    while (REG_VCOUNT != 192);
}

static void copyWords(vu32* dst, const vu32* src, u32 byteCount)
{
    for (u32 i = 0; i < byteCount / 4; i++)
        dst[i] = src[i];
}

// Returns the part of the cheat menu data at the given offset.
static inline void* getDataPart(u32 offset)
{
    return (u8*)sData + offset;
}

// ---- Cheat menu file ----

// Writes the persistent part of the cheat menu data (the enable bits and the position in the list) back to the menu
// image in the cheat menu file, for the next time the menu is opened. The data starts at a sector boundary of the
// image.
static bool savePersistentData(void)
{
    u32 bytes = (sData->persistentSize + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1);
    cheatmenu_cleanDataCache(sData, bytes);
    return cheatMenuFile_transfer(sResident, cheatmenu_header.dataOffset / SECTOR_SIZE, bytes / SECTOR_SIZE, sData,
        true);
}

// ---- Cheat search file ----

static bool hasCheatSearch(void)
{
    return sData->searchRunCount != 0;
}

bool cheatSearch_transferSectors(u32 sector, u32 count, void* buffer, bool write)
{
    // The same cache maintenance as in getBlockSector, since the driver may use DMA. The buffer is either a buffer of
    // the menu or the memory of the game, whose values are written to the snapshot.
    u32 size = count * SECTOR_SIZE;
    cheatmenu_cleanDataCache(buffer, size);
    bool ok = sectorRuns_transfer((const dldi_header_t*)sResident->dldiDriver, sData->searchRuns,
        sData->searchRunCount, sector, count, buffer, write);
    if (!write)
    {
        cheatmenu_cleanDataCache(buffer, size);
        cheatmenu_invalidateDataCache(buffer, size);
    }
    return ok;
}

// Reads the state of the cheat search the first time it is needed after the menu was opened. Returns false if there
// is no cheat search or its state cannot be read.
static bool loadSearch(void)
{
    if (!sSearchLoaded && hasCheatSearch() && cheatSearch_load(&sSearch))
        sSearchLoaded = true;
    return sSearchLoaded;
}

// Writes the state of the cheat search back to the cheat search file. Returns false and shows an error on failure.
static bool saveSearch(void)
{
    if (cheatSearch_save(&sSearch))
        return true;
    showMessage("Error writing to the sd card");
    return false;
}

// ---- Database ----

// Returns the given sector of the game's block in usrcheat.dat, counted from the first sector of the block, through
// the sector cache. Returns NULL and sets sDatabaseError if the sector cannot be read.
static const u8* getBlockSector(u32 blockSector)
{
    for (u32 i = 0; i < SECTOR_CACHE_SIZE; i++)
    {
        if (sSectorCacheTags[i] == blockSector + 1)
            return sSectorCache[i];
    }

    u32 sector = blockSector;
    for (u32 i = 0; i < sData->sectorRunCount; i++)
    {
        const cheat_menu_sector_run_t* run = &sData->sectorRuns[i];
        if (sector >= run->count)
        {
            sector -= run->count;
            continue;
        }
        u32 slot = sSectorCacheNext;
        sSectorCacheNext = (slot + 1) % SECTOR_CACHE_SIZE;
        sSectorCacheTags[slot] = 0;
        // The driver may copy the data with the cpu or with DMA. Cleaning the cache before the transfer prevents dirty
        // lines from being written back over the data later; cleaning and invalidating it afterwards keeps the data
        // that the cpu wrote and makes the data written by DMA visible.
        const dldi_header_t* driver = (const dldi_header_t*)sResident->dldiDriver;
        cheatmenu_cleanDataCache(sSectorCache[slot], SECTOR_SIZE);
        if (!((dldi_read_func_t)driver->readSectorsFuncAddress)(run->sector + sector, 1, sSectorCache[slot]))
            break;
        cheatmenu_cleanDataCache(sSectorCache[slot], SECTOR_SIZE);
        cheatmenu_invalidateDataCache(sSectorCache[slot], SECTOR_SIZE);
        sSectorCacheTags[slot] = blockSector + 1;
        return sSectorCache[slot];
    }
    sDatabaseError = true;
    return NULL;
}

// Reads the byte at the given offset of the game's block. Returns false and sets sDatabaseError on failure.
static bool readByte(u32 offset, u8* value)
{
    if (offset >= sData->blockSize)
    {
        sDatabaseError = true;
        return false;
    }
    u32 position = offset + sData->blockSectorOffset;
    const u8* sector = getBlockSector(position / SECTOR_SIZE);
    if (!sector)
        return false;
    *value = sector[position % SECTOR_SIZE];
    return true;
}

// Reads the little-endian word at the given offset of the game's block. Returns false on failure.
static bool readWord(u32 offset, u32* value)
{
    u32 result = 0;
    for (u32 i = 0; i < 4; i++)
    {
        u8 byte;
        if (!readByte(offset + i, &byte))
            return false;
        result |= (u32)byte << (i * 8);
    }
    *value = result;
    return true;
}

// Advances *offset past the zero-terminated string at *offset, which must end before end. Returns false and sets
// sDatabaseError if the string cannot be read.
static bool skipString(u32* offset, u32 end)
{
    u8 c;
    do
    {
        if (*offset >= end)
        {
            sDatabaseError = true;
            return false;
        }
        if (!readByte((*offset)++, &c))
            return false;
    } while (c != 0);
    return true;
}

// Decodes the UTF-8 character at *offset, which must be before end, and advances *offset past it. Returns the code
// point; '?' for an invalid sequence, which never includes the byte that ends it, so a terminator is never skipped;
// or 0 at the terminator, at end and on a read error.
static u32 readUtf8(u32* offset, u32 end)
{
    u8 c;
    if (*offset >= end || !readByte(*offset, &c))
        return 0;
    (*offset)++;
    if (c < 0x80)
        return c;
    u32 count = c >= 0xC2 && c <= 0xDF ? 1 : (c >= 0xE0 && c <= 0xEF ? 2 : (c >= 0xF0 && c <= 0xF4 ? 3 : 0));
    if (count == 0)
        return '?';
    u32 codePoint = c & (0x3F >> count);
    for (u32 i = 0; i < count; i++)
    {
        if (*offset >= end || !readByte(*offset, &c) || (c & 0xC0) != 0x80)
            return '?';
        (*offset)++;
        codePoint = (codePoint << 6) | (c & 0x3F);
    }
    // Overlong encodings, surrogates and values beyond Unicode are invalid.
    static const u32 sMinimum[] = { 0x80, 0x800, 0x10000 };
    if (codePoint < sMinimum[count - 1] || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
        return '?';
    return codePoint;
}

// Returns the character that shows the given code point: the font has the ASCII characters, Latin-1 letters lose
// their accents and other characters become '?'.
static char toDisplayChar(u32 codePoint)
{
    if (codePoint < 0x80)
        return (char)codePoint;
    if (codePoint >= 0xC0 && codePoint <= 0xFF)
        return sLatin1ToAscii[codePoint - 0xC0];
    return '?';
}

// Reads the zero-terminated UTF-8 name at the given offset for display, at most maxLength characters. Returns its
// length.
static u32 readName(u32 offset, char* dst, u32 maxLength)
{
    u32 length = 0;
    u32 c;
    while (length < maxLength && (c = readUtf8(&offset, sData->blockSize)) != 0)
        dst[length++] = toDisplayChar(c);
    dst[length] = 0;
    return length;
}

// Returns the offset of the given item in the game's block.
static u32 getItemOffset(u32 item)
{
    const void* offsets = getDataPart(sData->itemOffsetsOffset);
    if (sData->itemOffsetShift)
        return ((const u16*)offsets)[item] << sData->itemOffsetShift;
    return ((const u32*)offsets)[item];
}

// Returns the offset of the end of the given item: the start of the next item, or the end of the block.
static u32 getItemEnd(u32 item)
{
    return item + 1 < sData->itemCount ? getItemOffset(item + 1) : sData->blockSize;
}

// Returns the flags of the given item, or 0 if they cannot be read.
static u32 getItemFlags(u32 item)
{
    u32 flags = 0;
    readWord(getItemOffset(item), &flags);
    return flags;
}

static u32 readItemName(u32 item, char* dst, u32 maxLength)
{
    return readName(getItemOffset(item) + 4, dst, maxLength);
}

// Reads the opcodes of the given cheat item into dst, which has room for maxWords words. Returns the number of opcode
// words, OPCODES_DO_NOT_FIT if they do not fit, or 0 if the cheat has no opcodes or cannot be read (which sets
// sDatabaseError).
static u32 readCheatOpcodes(u32 item, u32* dst, u32 maxWords)
{
    u32 end = getItemEnd(item);
    u32 offset = getItemOffset(item) + 4;
    // The name and the note are followed by the number of opcode words, aligned to 4 bytes from the start of the block.
    if (!skipString(&offset, end) || !skipString(&offset, end))
        return 0;
    offset = (offset + 3) & ~3;
    u32 wordCount;
    if (!readWord(offset, &wordCount))
        return 0;
    offset += 4;
    if ((wordCount & 1) || offset > end || wordCount > (end - offset) / 4)
    {
        sDatabaseError = true;
        return 0;
    }
    if (wordCount > maxWords)
        return OPCODES_DO_NOT_FIT;
    for (u32 i = 0; i < wordCount; i++)
    {
        if (!readWord(offset + i * 4, &dst[i]))
            return 0;
    }
    return wordCount;
}

// Enable bits: one per database item, followed by one per extra cheat (index itemCount + extra cheat index).
static bool isEnabled(u32 index)
{
    const u8* bits = (const u8*)getDataPart(sData->enabledBitsOffset);
    return (bits[index >> 3] >> (index & 7)) & 1;
}

static void setEnabled(u32 index, bool enabled)
{
    u8* bits = (u8*)getDataPart(sData->enabledBitsOffset) + (index >> 3);
    if (enabled)
        *bits |= 1 << (index & 7);
    else
        *bits &= ~(1 << (index & 7));
}

// ---- Cheat list ----

// Cheat preprocessing, the same as in arm7/source/loader/CheatPreprocessor.cpp: some cheats contain hacks that modify
// the cheat engine of other cheat devices. They are replaced by the equivalent features of the Pico Loader engine.

static int findOpcode(const pload_cheat_opcode_t* opcodes, int start, int count, u32 a, u32 b)
{
    for (int i = start; i < count; i++)
    {
        if (opcodes[i].a == a && opcodes[i].b == b)
            return i;
    }
    return -1;
}

static void setNop(pload_cheat_opcode_t* opcode)
{
    opcode->a = 0xD4000000;
    opcode->b = 0;
}

// Replaces the hack that starts with the opcode (startA, startB) and ends with (endA, endB) with nops, and modifies
// the first opcode in between whose first word is opcodeA.
static void patchHack(pload_cheat_opcode_t* opcodes, int count, u32 startA, u32 startB, u32 endA, u32 endB,
    u32 opcodeA, u32 orValue, u32 subtractValue)
{
    int start = findOpcode(opcodes, 0, count, startA, startB);
    if (start < 0)
        return;
    setNop(&opcodes[start++]);
    int end = findOpcode(opcodes, start, count, endA, endB);
    int searchEnd = end < 0 ? count : end;
    for (int i = start; i < searchEnd; i++)
    {
        if (opcodes[i].a == opcodeA)
        {
            opcodes[i].a = (opcodes[i].a | orValue) - subtractValue;
            break;
        }
    }
    if (end >= 0)
        setNop(&opcodes[end]);
}

static void preprocessCheat(pload_cheat_opcode_t* opcodes, int count)
{
    // Hacks that modify the 0xD4 handler, replaced by the custom data operations of the cheat engine.
    patchHack(opcodes, count, 0x023FE424, 0xE1833004, 0x023FE424, 0xE0833004, 0xD4000000, 1, 0);
    patchHack(opcodes, count, 0x023FE424, 0xE0033004, 0x023FE424, 0xE0833004, 0xD4000000, 2, 0);
    patchHack(opcodes, count, 0x023FE424, 0xE0833004, 0x023FE424, 0xE0833004, 0xD4000000, 0, 0);
    int dbFix = findOpcode(opcodes, 0, count, 0x023FE4D8, 0x0A000003);
    if (dbFix >= 0)
        setNop(&opcodes[dbFix]);
    // Assembly cheats written for the Action Replay DS: the 0x0E code becomes 0xC2 in ARM mode.
    patchHack(opcodes, count, 0x023FE074, 0x012FFF11, 0x023FE074, 0xE3520003, 0xE0000000, 0, 0x1E000000);
}

// Writes the enabled cheats to the cheat list after the marker, up to the resident part, and sets the new number of
// cheats. The cheats of the locked values come first: the parasite made room for them in any case, and it finds them
// through their number in the resident part. If the state of the cheat search cannot be read, the cheats of the locked
// values stay as they are. The resident part has paused the cheat engine while the menu is open. Returns false if not
// all enabled cheats fit in the list; read errors set sDatabaseError.
static bool updateActiveCheats(void)
{
    cheat_menu_list_header_t* list = sResident->list;
    u8* cheats = (u8*)(list + 1);
    u32 capacity = list->marker[5] - sizeof(*list);
    u32 used = 0;
    u32 count = 0;
    bool allFit = true;
    if (loadSearch())
    {
        used = cheatSearch_writeLockCheats(&sSearch, cheats, capacity, &count);
        sResident->lockCheatCount = count;
        // The parasite reads the number after a soft reset, on the arm7.
        cheatmenu_cleanDataCache(&sResident->lockCheatCount, sizeof(sResident->lockCheatCount));
    }
    else
    {
        count = sResident->lockCheatCount;
        used = count * CHEAT_SEARCH_LOCK_CHEAT_SIZE;
    }
    for (u32 item = 0; item < sData->itemCount; item++)
    {
        if (!isEnabled(item))
            continue;
        pload_cheat_t* cheat = (pload_cheat_t*)(cheats + used);
        u32 maxWords = capacity - used >= 4 ? (capacity - used - 4) / 4 : 0;
        u32 wordCount = readCheatOpcodes(item, (u32*)cheat->opcodes, maxWords);
        if (wordCount == OPCODES_DO_NOT_FIT)
        {
            allFit = false;
            continue;
        }
        // A cheat without opcodes has nothing to run.
        if (wordCount == 0)
            continue;
        cheat->length = wordCount * 4;
        preprocessCheat(cheat->opcodes, wordCount / 2);
        used += 4 + cheat->length;
        count++;
    }

    // The extra cheats are stored in the cheat menu data, already preprocessed.
    const u8* extraCheat = (const u8*)getDataPart(sData->extraCheatsOffset);
    for (u32 i = 0; i < sData->extraCheatCount; i++)
    {
        u32 size = 4 + ((const pload_cheat_t*)extraCheat)->length;
        if (isEnabled(sData->itemCount + i))
        {
            if (size <= capacity - used)
            {
                memcpy(cheats + used, extraCheat, size);
                used += size;
                count++;
            }
            else
            {
                allFit = false;
            }
        }
        extraCheat += size;
    }

    // The cheat engine runs on the arm7, which does not see the data cache of the arm9.
    cheatmenu_cleanDataCache(cheats, used);
    // The marker stays the first cheat.
    sNumberOfCheats = 1 + count;
    return allFit;
}

// ---- List ----

static u32 getEntryCount(void)
{
    return sInFolder ? sFolderChildCount : sData->rootCount + sData->extraCheatCount;
}

static bool hasScrollbar(void)
{
    return getEntryCount() > LIST_VISIBLE_ROWS;
}

// Returns how many characters of a name are visible in the list. A row that is shifted to the right shows one
// character less.
static u32 getVisibleNameLength(bool shifted)
{
    return NAME_VISIBLE_LENGTH - shifted;
}

// Returns the item of the given entry of the current list, or itemCount + its index for an extra cheat.
static u32 getEntryItem(u32 entry)
{
    if (sInFolder)
        return sFolderItem + 1 + entry;
    if (entry < sData->rootCount)
        return ((const u16*)getDataPart(sData->rootItemsOffset))[entry];
    return sData->itemCount + entry - sData->rootCount;
}

static void getExtraCheatName(u32 extraIndex, char* dst)
{
    static const char sPrefix[] = "Launcher cheat ";
    memcpy(dst, sPrefix, sizeof(sPrefix) - 1);
    formatNumber(dst + sizeof(sPrefix) - 1, extraIndex + 1);
}

static u32 countEnabledInFolder(u32 folderItem, u32 childCount)
{
    u32 count = 0;
    for (u32 i = 0; i < childCount; i++)
        count += isEnabled(folderItem + 1 + i);
    return count;
}

static void loadRow(row_t* row, u32 entry)
{
    row->item = getEntryItem(entry);
    row->enabledInFolder = 0;
    if (row->item >= sData->itemCount)
    {
        row->type = ENTRY_EXTRA_CHEAT;
        getExtraCheatName(row->item - sData->itemCount, row->name);
        return;
    }
    u32 flags = getItemFlags(row->item);
    row->type = (flags & ITEM_FLAG_FOLDER) ? ENTRY_FOLDER : ENTRY_CHEAT;
    if (row->type == ENTRY_FOLDER)
        row->enabledInFolder = countEnabledInFolder(row->item, flags & ITEM_SIZE_MASK) != 0;
    readItemName(row->item, row->name, NAME_VISIBLE_LENGTH);
}

// Makes sRows hold the rows that can be visible. When the list scrolls, the rows that stay are moved instead of being
// read again.
static void updateRows(void)
{
    int first = (int)sListTop - 1;
    if (sRowsValid && sRowsFirst == first)
        return;
    int move = sRowsValid ? first - sRowsFirst : LIST_ROW_CACHE_SIZE;
    int entryCount = getEntryCount();
    for (int n = 0; n < LIST_ROW_CACHE_SIZE; n++)
    {
        // Go through the rows in the direction in which they move, so that no row is overwritten before it is moved.
        int i = move >= 0 ? n : LIST_ROW_CACHE_SIZE - 1 - n;
        int from = i + move;
        if (from >= 0 && from < LIST_ROW_CACHE_SIZE)
            sRows[i] = sRows[from];
        else if (first + i >= 0 && first + i < entryCount)
            loadRow(&sRows[i], first + i);
    }
    sRowsFirst = first;
    sRowsValid = true;
}

static void resetMarquee(void)
{
    sMarqueeTimer = 0;
    sMarqueeOffset = 0;
}

// Reads the full name of the selected entry when the selection changed.
static void updateSelectedName(void)
{
    if (sSelectedNameCursor == sCursor + 1)
        return;
    sSelectedNameCursor = sCursor + 1;
    resetMarquee();
    sSelectedName[0] = 0;
    if (getEntryCount() != 0)
    {
        u32 item = getEntryItem(sCursor);
        if (item >= sData->itemCount)
            getExtraCheatName(item - sData->itemCount, sSelectedName);
        else
            readItemName(item, sSelectedName, SELECTED_NAME_LENGTH);
    }
    sSelectedNameLength = stringLength(sSelectedName);
}

// Advances the scrolling of a name that shows visibleLength characters: it starts after a pause and returns to its
// start after a pause at its end.
static void stepMarquee(u32* timer, u32* offset, u32 length, u32 visibleLength)
{
    if (length <= visibleLength)
        return;
    u32 maxOffset = length - visibleLength;
    (*timer)++;
    if (*offset == 0 || *offset >= maxOffset)
    {
        if (*timer < MARQUEE_PAUSE)
            return;
        if (*offset >= maxOffset)
        {
            *timer = 0;
            *offset = 0;
            return;
        }
    }
    if (*timer >= MARQUEE_SPEED)
    {
        *timer = 0;
        (*offset)++;
    }
}

// Scrolls the names of the list that do not fit: the selected name in its row, and the name of the open folder in the
// header.
static void updateMarquee(void)
{
    if (sDescriptionOpen || sSearchScreen != SEARCH_CLOSED)
        return;
    stepMarquee(&sMarqueeTimer, &sMarqueeOffset, sSelectedNameLength, getVisibleNameLength(true));
    if (sInFolder)
        stepMarquee(&sFolderMarqueeTimer, &sFolderMarqueeOffset, sFolderNameLength, sFolderVisibleLength);
}

// ---- Selection animation ----

// Moves value towards target: by half of the distance while it is long, then at the animation speed, so that holding
// a direction key moves at a constant speed.
static int approach(int value, int target)
{
    int distance = target - value;
    int length = distance < 0 ? -distance : distance;
    int step = length > 16 ? length >> 1 : ANIMATION_SPEED;
    if (step > length)
        step = length;
    return value + (distance < 0 ? -step : step);
}

// Returns the height of the top of the given row between the bands.
static inline u32 getRowY(u32 row)
{
    return FIRST_ROW_Y + row * ROW_HEIGHT;
}

static int getCursorBarY(void)
{
    return getRowY(sCursor - sListTop);
}

static int getThumbY(void)
{
    u32 entryCount = getEntryCount();
    u32 range = LIST_VISIBLE_ROWS * ROW_HEIGHT - THUMB_HEIGHT;
    return FIRST_ROW_Y + (entryCount > 1 ? divide(range * sCursor, entryCount - 1) : 0);
}

// Starts the animation of a new list, without movement of the bar.
static void resetSelectionAnimation(void)
{
    sFocusEntry = sCursor;
    sFocusShift = 0;
    sUnfocusShift = 0;
    sBarY = getCursorBarY();
    sScrollOffset = 0;
    sThumbY = getThumbY();
}

static void updateSelectionAnimation(void)
{
    if (sCursor != sFocusEntry)
    {
        // When the selection returns to the entry that is still sliding back, it continues from where it is.
        int shift = sCursor == sUnfocusEntry ? sUnfocusShift : 0;
        sUnfocusEntry = sFocusEntry;
        sUnfocusShift = sFocusShift;
        sFocusEntry = sCursor;
        sFocusShift = shift;
    }
    sFocusShift = approach(sFocusShift, FOCUS_SHIFT);
    sUnfocusShift = approach(sUnfocusShift, 0);
    sBarY = approach(sBarY, getCursorBarY());
    sScrollOffset = approach(sScrollOffset, 0);
    sThumbY = approach(sThumbY, getThumbY());
}

// Returns the horizontal shift in pixels of the row of the given entry.
static u32 getEntryShift(u32 entry)
{
    if (entry == sFocusEntry)
        return sFocusShift;
    if (entry == sUnfocusEntry)
        return sUnfocusShift;
    return 0;
}

// ---- Navigation ----

// Selects the given entry, limited to the list, and scrolls the list to show it.
static void setCursor(int cursor)
{
    u32 entryCount = getEntryCount();
    if (cursor >= (int)entryCount)
        cursor = entryCount - 1;
    if (cursor < 0)
        cursor = 0;
    sCursor = cursor;
    u32 previousTop = sListTop;
    if (sCursor < sListTop)
        sListTop = sCursor;
    else if (sCursor >= sListTop + LIST_VISIBLE_ROWS)
        sListTop = sCursor - LIST_VISIBLE_ROWS + 1;
    // The rows stay where they were and slide to their new place, by one row at most.
    sScrollOffset += ((int)sListTop - (int)previousTop) * ROW_HEIGHT;
    if (sScrollOffset > ROW_HEIGHT)
        sScrollOffset = ROW_HEIGHT;
    else if (sScrollOffset < -ROW_HEIGHT)
        sScrollOffset = -ROW_HEIGHT;
}

// Moves the cursor by the given number of entries. Single steps wrap around at the ends of the list.
static void moveCursor(int move)
{
    int cursor = (int)sCursor + move;
    int entryCount = getEntryCount();
    if (move == -1 && cursor < 0)
        cursor = entryCount - 1;
    else if (move == 1 && cursor >= entryCount)
        cursor = 0;
    setCursor(cursor);
}

static void openFolder(u32 folderItem)
{
    u32 flags = getItemFlags(folderItem);
    sRootCursor = sCursor;
    sRootTop = sListTop;
    sInFolder = true;
    sFolderItem = folderItem;
    sFolderChildCount = flags & ITEM_SIZE_MASK;
    sFolderOneOnly = (flags >> 24) == ITEM_ONE_ONLY_FOLDER;
    sFolderNameLength = readItemName(folderItem, sFolderName, SELECTED_NAME_LENGTH);
    sFolderMarqueeTimer = 0;
    sFolderMarqueeOffset = 0;
    // The room of the name does not change with the position, which is at most "<count>/<count>".
    char digits[MAX_DIGITS + 1];
    sFolderVisibleLength = LAST_TEXT_COLUMN - 2 * formatNumber(digits, sFolderChildCount) - 2;
    sCursor = 0;
    sListTop = 0;
    sRowsValid = false;
    sSelectedNameCursor = 0;
    resetSelectionAnimation();
}

static void closeFolder(void)
{
    sInFolder = false;
    sCursor = sRootCursor;
    sListTop = sRootTop;
    sRowsValid = false;
    sSelectedNameCursor = 0;
    resetSelectionAnimation();
}

// Opens the selected folder, or turns the selected cheat on or off. In a folder where at most one cheat can be on,
// turning a cheat on turns the others off.
static void toggleEntry(void)
{
    if (getEntryCount() == 0)
        return;
    u32 item = getEntryItem(sCursor);
    if (item < sData->itemCount && (getItemFlags(item) & ITEM_FLAG_FOLDER))
    {
        openFolder(item);
        return;
    }
    bool enable = !isEnabled(item);
    if (enable && sInFolder && sFolderOneOnly)
    {
        for (u32 i = 0; i < sFolderChildCount; i++)
            setEnabled(sFolderItem + 1 + i, false);
    }
    setEnabled(item, enable);
    sChanged = true;
}

// Turns off all cheats of the open folder, or all cheats when no folder is open.
static void disableAll(void)
{
    if (sInFolder)
    {
        for (u32 i = 0; i < sFolderChildCount; i++)
            setEnabled(sFolderItem + 1 + i, false);
    }
    else
    {
        for (u32 i = 0; i < sData->itemCount + sData->extraCheatCount; i++)
            setEnabled(i, false);
    }
    // The folder rows show whether a cheat in them is on, so they are read again.
    sRowsValid = false;
    sChanged = true;
    showMessage(sInFolder ? "All cheats in this folder off" : "All cheats off");
}

// ---- Description ----

// The note of the selected entry stays on the sd card: only the rows that fit on the screen are read, and a longer
// note is cut. All reads stay within the note and take place before drawing.

// Returns the next character of the note at *offset as it is drawn: '\n' for a line break (LF, CR or CR LF), a space
// for other control characters, and 0 at the end of the note.
static char readDescriptionChar(u32* offset)
{
    u32 c = readUtf8(offset, sDescriptionEnd);
    if (c == '\r')
    {
        u32 next = *offset;
        if (readUtf8(&next, sDescriptionEnd) == '\n')
            *offset = next;
        return '\n';
    }
    if (c == '\n' || c == 0)
        return (char)c;
    if (c < 0x20 || c == 0x7F)
        return ' ';
    return toDisplayChar(c);
}

// Reads the row of the note that starts at offset into row and returns the offset of the next row. Words that do not
// fit wrap to the next row; a word longer than a row is split.
static u32 readDescriptionRow(u32 offset, char* row)
{
    u32 length = 0;
    u32 spaceLength = 0;
    u32 afterSpace = 0;
    bool newline = false;
    while (offset < sDescriptionEnd && length < DESCRIPTION_COLUMNS)
    {
        char c = readDescriptionChar(&offset);
        if (c == 0)
            break;
        if (c == '\n')
        {
            newline = true;
            break;
        }
        if (c == ' ')
        {
            spaceLength = length;
            afterSpace = offset;
        }
        row[length++] = c;
    }
    if (!newline && length == DESCRIPTION_COLUMNS && offset < sDescriptionEnd)
    {
        u32 next = offset;
        char c = readDescriptionChar(&next);
        if (c == '\n')
        {
            offset = next;
        }
        else if (c == ' ')
        {
            // Spaces at a wrap do not start the next row, and neither does a line break right after them.
            do
            {
                offset = next;
                c = readDescriptionChar(&next);
            } while (c == ' ');
            if (c == '\n')
                offset = next;
        }
        else if (afterSpace != 0 && spaceLength != 0)
        {
            // The last word does not fit: it starts the next row.
            length = spaceLength;
            offset = afterSpace;
        }
    }
    while (length != 0 && row[length - 1] == ' ')
        length--;
    row[length] = 0;
    return offset;
}

// Returns whether the note has visible characters from offset on.
static bool hasDescriptionText(u32 offset)
{
    char c;
    while ((c = readDescriptionChar(&offset)) != 0)
    {
        if (c != ' ' && c != '\n')
            return true;
    }
    return false;
}

static void setDescriptionMessage(const char* message)
{
    memset(sDescriptionRows, 0, sizeof(sDescriptionRows));
    memcpy(sDescriptionRows[0], message, stringLength(message));
}

// Splits the selected name into the rows of the description view. Names wrap between words, a word longer than a row is
// split, and a name that does not fit ends with "...".
static void loadDescriptionName(void)
{
    memset(sDescriptionNameRows, 0, sizeof(sDescriptionNameRows));
    const char* name = sSelectedName;
    u32 row = 0;
    while (*name != 0 && row < DESCRIPTION_NAME_ROWS)
    {
        u32 length = stringLength(name);
        if (length > DESCRIPTION_COLUMNS)
        {
            length = DESCRIPTION_COLUMNS;
            while (length != 0 && name[length] != ' ')
                length--;
            if (length == 0)
                length = DESCRIPTION_COLUMNS;
        }
        memcpy(sDescriptionNameRows[row++], name, length);
        name += length;
        while (*name == ' ')
            name++;
    }
    if (*name != 0)
    {
        char* last = sDescriptionNameRows[DESCRIPTION_NAME_ROWS - 1];
        u32 length = stringLength(last);
        if (length > DESCRIPTION_COLUMNS - 3)
            length = DESCRIPTION_COLUMNS - 3;
        memcpy(&last[length], "...", 4);
    }
    sDescriptionNameRowCount = row != 0 ? row : 1;
    sDescriptionRowCount = DESCRIPTION_ROWS - (sDescriptionNameRowCount - 1);
}

// Reads the rows of the note between sDescriptionStart and sDescriptionEnd.
static void loadDescription(void)
{
    memset(sDescriptionRows, 0, sizeof(sDescriptionRows));
    u32 offset = sDescriptionStart;
    for (u32 row = 0; row < sDescriptionRowCount && offset < sDescriptionEnd && !sDatabaseError; row++)
        offset = readDescriptionRow(offset, sDescriptionRows[row]);
    if (!sDatabaseError && hasDescriptionText(offset))
    {
        // The note does not fit on the screen: the last row ends with "..." to show that it is cut.
        char* row = sDescriptionRows[sDescriptionRowCount - 1];
        u32 length = stringLength(row);
        if (length > DESCRIPTION_COLUMNS - 3)
            length = DESCRIPTION_COLUMNS - 3;
        memcpy(&row[length], "...", 4);
    }
    if (sDatabaseError)
    {
        // The error is shown here instead of on the status row, which the description covers.
        setDescriptionMessage("Error reading description.");
        sDatabaseError = false;
    }
    else if (!hasDescriptionText(sDescriptionStart))
    {
        setDescriptionMessage("No description available.");
    }
}

// Opens the description view with the note of the selected entry.
static void openDescription(void)
{
    if (getEntryCount() == 0)
        return;
    sDescriptionOpen = true;
    sDescriptionStart = 0;
    sDescriptionEnd = 0;
    loadDescriptionName();
    // An item starts with its flags, its name and its note. Extra cheats have no note.
    u32 item = getEntryItem(sCursor);
    if (item < sData->itemCount)
    {
        u32 offset = getItemOffset(item);
        u32 end = getItemEnd(item);
        if (offset >= end || end > sData->blockSize)
        {
            sDatabaseError = true;
        }
        else
        {
            offset += 4;
            if (skipString(&offset, end))
            {
                sDescriptionStart = offset;
                if (skipString(&offset, end))
                    sDescriptionEnd = offset - 1;
            }
        }
    }
    loadDescription();
}

static void closeDescription(void)
{
    sDescriptionOpen = false;
}

// ---- Drawing ----

// Writes a string to a composed text row from the given column, at most maxLength characters. Returns the number of
// characters written.
static u32 putString(char* chars, u8* palettes, u32 column, const char* string, u32 maxLength, u32 palette)
{
    u32 length = 0;
    while (string[length] != 0 && length < maxLength && column + length < TEXT_COLUMNS)
    {
        chars[column + length] = string[length];
        palettes[column + length] = palette;
        length++;
    }
    return length;
}

// Writes a text with the icons of buttons from the given column: the icons are white and the text is light gray.
// Returns the column after the text and a space.
static u32 putKeyHint(char* chars, u8* palettes, u32 column, const char* hint)
{
    u32 length = putString(chars, palettes, column, hint, TEXT_COLUMNS, PALETTE_LIGHT);
    for (u32 i = 0; i < length; i++)
    {
        if ((u8)hint[i] >= ICON_FIRST_CHAR)
            palettes[column + i] = PALETTE_WHITE;
    }
    return column + length + 1;
}

// Fills a composed text row with a horizontal line.
static void putLine(char* chars, u8* palettes)
{
    for (u32 i = 1; i <= LAST_TEXT_COLUMN; i++)
    {
        chars[i] = ICON_LINE[0];
        palettes[i] = PALETTE_LINE;
    }
}

static void clearRow(char* chars, u8* palettes)
{
    for (u32 i = 0; i < TEXT_COLUMNS; i++)
    {
        chars[i] = ' ';
        palettes[i] = PALETTE_WHITE;
    }
}

// The header shows the title, or the name of the open folder, and the position of the cursor in the list.
static void composeHeader(char* chars, u8* palettes)
{
    u32 titleEnd = TEXT_COLUMNS;
    u32 entryCount = getEntryCount();
    if (entryCount != 0)
    {
        char position[12];
        u32 length = formatNumber(position, sCursor + 1);
        position[length++] = '/';
        length += formatNumber(&position[length], entryCount);
        titleEnd = LAST_TEXT_COLUMN - length;
        putString(chars, palettes, titleEnd + 1, position, length, PALETTE_LIGHT);
    }
    if (sInFolder)
    {
        putString(chars, palettes, 1, &sFolderName[sFolderMarqueeOffset], sFolderVisibleLength, PALETTE_CYAN);
        return;
    }
    putString(chars, palettes, 1, "Cheat Menu", titleEnd - 1, PALETTE_WHITE);
    // A very long position takes the place of the signature, which never moves.
    static const char sSignature[] = "By Esentasse";
    if (titleEnd >= SIGNATURE_COLUMN + sizeof(sSignature) - 1)
        putString(chars, palettes, SIGNATURE_COLUMN, sSignature, sizeof(sSignature) - 1, PALETTE_SIGNATURE);
}

// ---- Cheat search screens ----

// Returns the size of the values of the current search, or the size for the next search if none is running.
static u32 getSearchSize(void)
{
    return sSearch.valueSize != 0 ? sSearch.valueSize : sSearch.settingSize;
}

static bool canListResults(void)
{
    return sSearch.valueSize != 0 && sSearch.resultCount != 0 && sSearch.resultCount <= CHEAT_SEARCH_MAX_LISTED;
}

// Returns the row of the given option: the settings come first, and the actions follow after a line.
static u32 getOptionRow(u32 option)
{
    return option < OPTION_SEARCH ? option : option + 1;
}

// Returns the current value of the given result in the memory of the game.
static u32 readResultValue(u32 address)
{
    return cheatSearch_readValue((const u8*)CHEAT_SEARCH_MEMORY_START, address - CHEAT_SEARCH_MEMORY_START,
        sSearch.valueSize);
}

static const char* getSizeName(u32 size)
{
    switch (size)
    {
        case 1:     return "1 byte";
        case 2:     return "2 bytes";
        default:    return "4 bytes";
    }
}

static const char* getCompareName(u32 compare)
{
    switch (compare)
    {
        case CHEAT_SEARCH_UNKNOWN:      return "Unknown value";
        case CHEAT_SEARCH_EQUAL:        return "Equal to value";
        case CHEAT_SEARCH_CHANGED:      return "Changed";
        case CHEAT_SEARCH_UNCHANGED:    return "Unchanged";
        case CHEAT_SEARCH_INCREASED:    return "Increased";
        default:                        return "Decreased";
    }
}

// Returns the given row of the text that tells what to do next, on the screen with the settings of the search.
static const char* getSearchHint(u32 row)
{
    if (sSearch.valueSize == 0)
    {
        switch (row)
        {
            case 0:     return "1. Search for the value shown";
            case 1:     return "   (or Unknown if not shown).";
            case 2:     return "2. Let it change in the game.";
            default:    return "3. Search until few are left.";
        }
    }
    if (sSearch.resultCount == 0)
        return row == 0 ? "No results are left." : (row == 1 ? "Start a new search." : "");
    if (sSearch.resultCount > CHEAT_SEARCH_MAX_LISTED)
    {
        // The last row matches CHEAT_SEARCH_MAX_LISTED.
        switch (row)
        {
            case 0:     return "Let the value change in the";
            case 1:     return "game, then search again.";
            case 2:     return "The results can be shown when";
            default:    return "at most 128 are left.";
        }
    }
    switch (row)
    {
        case 0:     return "Show the results to change a";
        case 1:     return "value, or to lock it so that";
        case 2:     return "it always keeps that value.";
        default:    return "";
    }
}

// Returns the keys that can be used on the open screen of the cheat search, with the icons of the buttons.
static const char* getSearchKeyHint(void)
{
    if (sSearchScreen == SEARCH_RESULTS)
    {
        if (sEditing)
            return ICON_DPAD_LEFT_RIGHT " Digit " ICON_DPAD_UP_DOWN " +/- " ICON_KEY_A " Write " ICON_KEY_B " Cancel";
        return cheatSearch_findLock(&sSearch, sSearch.results[sResultCursor]) >= 0 ?
            ICON_KEY_A " Edit " ICON_KEY_START " Unlock " ICON_KEY_B " Back" :
            ICON_KEY_A " Edit " ICON_KEY_START " Lock " ICON_KEY_B " Back";
    }
    if (sEditing)
        return ICON_DPAD_LEFT_RIGHT " Digit " ICON_DPAD_UP_DOWN " +/- " ICON_KEY_A " OK " ICON_KEY_B " Cancel";
    if (sOptionCursor == OPTION_SIZE || sOptionCursor == OPTION_COMPARE)
        return ICON_DPAD_UP_DOWN " Move " ICON_DPAD_LEFT_RIGHT " Change " ICON_KEY_B " Back";
    return ICON_DPAD_UP_DOWN " Move " ICON_KEY_A " Select " ICON_KEY_B " Back";
}

// Writes the digits of the edited number from the given column. The selected digit is yellow.
static void composeEditor(char* chars, u8* palettes, u32 column)
{
    for (u32 i = 0; i < sEditDigitCount && column + i < TEXT_COLUMNS; i++)
    {
        chars[column + i] = '0' + sEditDigits[i];
        palettes[column + i] = i == sEditPosition ? PALETTE_YELLOW : PALETTE_WHITE;
    }
}

// Writes the value of an option that Left and Right change, between arrows if it can be changed.
static void composeChoice(char* chars, u8* palettes, const char* name, bool changeable)
{
    putString(chars, palettes, OPTION_VALUE_COLUMN, name, TEXT_COLUMNS, changeable ? PALETTE_CYAN : PALETTE_GRAY);
    if (changeable)
    {
        putString(chars, palettes, OPTION_CHOICE_COLUMN, "<", 1, PALETTE_GRAY);
        putString(chars, palettes, LAST_TEXT_COLUMN, ">", 1, PALETTE_GRAY);
    }
}

// Composes the row of an option. Options that cannot be used at the moment are gray, and the search itself stands out.
static void composeOption(u32 option, char* chars, u8* palettes)
{
    if (option == sOptionCursor)
        putString(chars, palettes, 0, ICON_CURSOR, 1, PALETTE_YELLOW);
    bool started = sSearch.valueSize != 0;
    bool enabled = true;
    u32 palette = PALETTE_WHITE;
    const char* label;
    char number[MAX_DIGITS + 3];
    switch (option)
    {
        case OPTION_SIZE:
            label = "Value size";
            composeChoice(chars, palettes, getSizeName(getSearchSize()), !started);
            break;
        case OPTION_COMPARE:
            label = "Search for";
            composeChoice(chars, palettes, getCompareName(sSearch.settingCompare), true);
            break;
        case OPTION_VALUE:
            label = "Value";
            enabled = cheatSearch_usesValue(sSearch.settingCompare);
            if (sEditing)
            {
                composeEditor(chars, palettes, OPTION_VALUE_COLUMN);
            }
            else
            {
                formatNumber(number, sSearch.settingValue);
                putString(chars, palettes, OPTION_VALUE_COLUMN, enabled ? number : "-", MAX_DIGITS,
                    enabled ? PALETTE_CYAN : PALETTE_GRAY);
            }
            break;
        case OPTION_SEARCH:
            label = started ? "Search again" : "Start search";
            palette = PALETTE_YELLOW;
            break;
        case OPTION_RESULTS:
            label = "Show results";
            enabled = canListResults();
            break;
        case OPTION_NEW:
            label = "New search";
            enabled = started;
            break;
        default:
            label = "Unlock all values";
            enabled = sSearch.lockCount != 0;
            if (enabled)
            {
                number[0] = '(';
                u32 length = 1 + formatNumber(&number[1], sSearch.lockCount);
                number[length++] = ')';
                number[length] = 0;
                putString(chars, palettes, OPTION_VALUE_COLUMN + 4, number, length, PALETTE_GREEN);
            }
            break;
    }
    putString(chars, palettes, 2, label, TEXT_COLUMNS, enabled ? palette : PALETTE_GRAY);
}

// Composes the row of the given result: its address, its current value and whether it is locked.
static void composeResultRow(u32 entry, char* chars, u8* palettes)
{
    u32 address = sSearch.results[entry];
    bool selected = entry == sResultCursor;
    if (selected)
        putString(chars, palettes, 0, ICON_CURSOR, 1, PALETTE_YELLOW);
    char text[MAX_DIGITS + 1];
    formatHex(text, address);
    putString(chars, palettes, RESULT_ADDRESS_COLUMN, text, 8, PALETTE_LIGHT);
    if (selected && sEditing)
    {
        composeEditor(chars, palettes, RESULT_VALUE_COLUMN + MAX_DIGITS - sEditDigitCount);
    }
    else
    {
        u32 length = formatNumber(text, readResultValue(address));
        putString(chars, palettes, RESULT_VALUE_COLUMN + MAX_DIGITS - length, text, length, PALETTE_WHITE);
    }
    if (cheatSearch_findLock(&sSearch, address) >= 0)
    {
        putString(chars, palettes, RESULT_LOCK_COLUMN, ICON_LOCK, 1, PALETTE_GREEN);
        putString(chars, palettes, RESULT_LOCK_COLUMN + 2, "Locked", 6, PALETTE_GREEN);
    }
}

// Composes the row with the names of the columns of the results.
static void composeResultHeading(char* chars, u8* palettes)
{
    putString(chars, palettes, RESULT_ADDRESS_COLUMN, "Address", 7, PALETTE_GRAY);
    putString(chars, palettes, RESULT_VALUE_COLUMN + MAX_DIGITS - 5, "Value", 5, PALETTE_GRAY);
}

// The header shows the title and the number of results, or the position of the cursor in the results.
static void composeSearchHeader(char* chars, u8* palettes)
{
    char text[TEXT_COLUMNS];
    u32 length = 0;
    if (sSearchScreen == SEARCH_RESULTS)
    {
        length = formatNumber(text, sResultCursor + 1);
        text[length++] = '/';
        length += formatNumber(&text[length], sSearch.resultCount);
    }
    else if (sSearch.valueSize != 0)
    {
        length = formatResultCount(text, sSearch.resultCount);
    }
    putString(chars, palettes, LAST_TEXT_COLUMN + 1 - length, text, length, PALETTE_LIGHT);
    putString(chars, palettes, 1, sSearchScreen == SEARCH_RESULTS ? "Results" : "Cheat Search", TEXT_COLUMNS,
        PALETTE_WHITE);
}

// Composes the given row of the open screen of the cheat search.
static void composeSearchRow(u32 row, char* chars, u8* palettes)
{
    if (sSearchScreen == SEARCH_RESULTS)
    {
        if (row < RESULT_FIRST_ROW)
        {
            composeResultHeading(chars, palettes);
            return;
        }
        u32 entry = sResultTop + row - RESULT_FIRST_ROW;
        if (entry < sSearch.resultCount)
            composeResultRow(entry, chars, palettes);
    }
    else if (row == OPTION_FIRST_LINE_ROW || row == OPTION_SECOND_LINE_ROW)
    {
        putLine(chars, palettes);
    }
    else if (row >= SEARCH_HINT_FIRST_ROW)
    {
        putString(chars, palettes, 1, getSearchHint(row - SEARCH_HINT_FIRST_ROW), LAST_TEXT_COLUMN, PALETTE_LIGHT);
    }
    else
    {
        composeOption(row < OPTION_FIRST_LINE_ROW ? row : row - 1, chars, palettes);
    }
}

// Composes the keys of the list: what A does with the selected entry, how to leave the list, and the other screens.
static void composeListKeys(char* chars, u8* palettes)
{
    bool hasEntries = getEntryCount() != 0;
    u32 column = 1;
    if (hasEntries)
    {
        bool folder = sRows[sCursor - sRowsFirst].type == ENTRY_FOLDER;
        column = putKeyHint(chars, palettes, column, folder ? ICON_KEY_A " Open" : ICON_KEY_A " On/Off");
    }
    column = putKeyHint(chars, palettes, column, sInFolder ? ICON_KEY_B " Back" : ICON_KEY_B " Exit");
    if (hasEntries)
        column = putKeyHint(chars, palettes, column, ICON_KEY_SELECT " Info");
    if (hasCheatSearch())
        putKeyHint(chars, palettes, column, ICON_KEY_R " Find");
}

// Composes the band at the top of the open screen.
static void composeHeaderRow(char* chars, u8* palettes)
{
    if (sSearchScreen != SEARCH_CLOSED)
        composeSearchHeader(chars, palettes);
    else if (sDescriptionOpen)
        putString(chars, palettes, 1, "Description", TEXT_COLUMNS - 1, PALETTE_WHITE);
    else
        composeHeader(chars, palettes);
}

// Composes the band at the bottom of the open screen: the current message or the keys of the screen.
static void composeStatusRow(char* chars, u8* palettes)
{
    if (sMessage)
        putString(chars, palettes, 1, sMessage, LAST_TEXT_COLUMN, PALETTE_YELLOW);
    else if (sSearchScreen != SEARCH_CLOSED)
        putKeyHint(chars, palettes, 1, getSearchKeyHint());
    else if (sDescriptionOpen)
        putKeyHint(chars, palettes, 1, ICON_KEY_B " Back");
    else
        composeListKeys(chars, palettes);
}

// Composes the row of the given entry of the list, which must be in sRows. Returns the horizontal shift of the row in
// pixels.
static u32 composeListRow(u32 entry, char* chars, u8* palettes)
{
    const row_t* row = &sRows[entry - sRowsFirst];
    bool selected = entry == sCursor;
    u32 shift = getEntryShift(entry);
    const char* name = row->name;
    u32 nameLength = getVisibleNameLength(selected || shift != 0);
    if (selected)
    {
        putString(chars, palettes, CURSOR_COLUMN, ICON_CURSOR, 1, PALETTE_YELLOW);
        name = &sSelectedName[sMarqueeOffset];
    }
    if (row->type == ENTRY_FOLDER)
    {
        putString(chars, palettes, STATE_COLUMN, ICON_FOLDER, 2, row->enabledInFolder ? PALETTE_GREEN : PALETTE_CYAN);
        putString(chars, palettes, NAME_COLUMN, name, nameLength, selected ? PALETTE_WHITE : PALETTE_CYAN);
    }
    else
    {
        bool enabled = isEnabled(row->item);
        putString(chars, palettes, STATE_COLUMN, enabled ? ICON_ON : ICON_OFF, 2,
            enabled ? PALETTE_GREEN : PALETTE_GRAY);
        putString(chars, palettes, NAME_COLUMN, name, nameLength, enabled || selected ? PALETTE_WHITE : PALETTE_LIGHT);
    }
    return shift;
}

// Writes a string of at most DESCRIPTION_COLUMNS characters to the middle of a composed text row.
static void putCentered(char* chars, u8* palettes, const char* string, u32 palette)
{
    putString(chars, palettes, (TEXT_COLUMNS - stringLength(string)) >> 1, string, TEXT_COLUMNS, palette);
}

// Composes the given row of the description view: the rows of the name, the line and the rows of the note.
static void composeDescriptionRow(u32 row, char* chars, u8* palettes)
{
    if (row < sDescriptionNameRowCount)
        putCentered(chars, palettes, sDescriptionNameRows[row], PALETTE_CYAN);
    else if (row == sDescriptionNameRowCount)
        putLine(chars, palettes);
    else
        putCentered(chars, palettes, sDescriptionRows[row - sDescriptionNameRowCount - 1], PALETTE_WHITE);
}

// Writes a composed text row to the 32 sprites starting at firstSprite, at the given height and horizontal shift.
static void writeRow(const char* chars, const u8* palettes, u32 y, u32 shift, u32 firstSprite)
{
    vu16* oam = &OAM_SUB[firstSprite * 4];
    for (u32 column = 0; column < TEXT_COLUMNS; column++, oam += 4)
    {
        u32 glyph = (u8)chars[column];
        if (glyph == ' ')
        {
            oam[0] = OBJ_DISABLED;
            continue;
        }
        u32 tile;
        if (glyph >= ICON_FIRST_CHAR && glyph < ICON_FIRST_CHAR + ICON_COUNT)
            tile = ICON_TILE + glyph - ICON_FIRST_CHAR;
        else if (glyph < FIRST_GLYPH || glyph >= FIRST_GLYPH + GLYPH_COUNT)
            tile = '?' - FIRST_GLYPH;
        else
            tile = glyph - FIRST_GLYPH;
        oam[1] = column * 8 + shift; // 8x8
        oam[2] = tile | (palettes[column] << 12);
        oam[0] = y; // square, 16 colors
    }
}

static void drawHeaderRow(void)
{
    char chars[TEXT_COLUMNS];
    u8 palettes[TEXT_COLUMNS];
    clearRow(chars, palettes);
    composeHeaderRow(chars, palettes);
    writeRow(chars, palettes, HEADER_TEXT_Y, 0, FRONT_SPRITES);
}

static void drawStatusRow(void)
{
    char chars[TEXT_COLUMNS];
    u8 palettes[TEXT_COLUMNS];
    clearRow(chars, palettes);
    composeStatusRow(chars, palettes);
    writeRow(chars, palettes, STATUS_TEXT_Y, 0, FRONT_SPRITES);
}

// Draws the given row of the open screen of the cheat search or of the description view.
static void drawContentRow(u32 row, u32 y, u32 firstSprite)
{
    char chars[TEXT_COLUMNS];
    u8 palettes[TEXT_COLUMNS];
    clearRow(chars, palettes);
    if (sSearchScreen != SEARCH_CLOSED)
        composeSearchRow(row, chars, palettes);
    else
        composeDescriptionRow(row, chars, palettes);
    writeRow(chars, palettes, y, 0, firstSprite);
}

static void drawListRow(u32 entry, u32 y, u32 firstSprite)
{
    char chars[TEXT_COLUMNS];
    u8 palettes[TEXT_COLUMNS];
    clearRow(chars, palettes);
    u32 shift = composeListRow(entry, chars, palettes);
    writeRow(chars, palettes, y, shift, firstSprite);
}

static void hideSprites(u32 firstSprite, u32 count)
{
    for (u32 i = 0; i < count; i++)
        OAM_SUB[(firstSprite + i) * 4] = OBJ_DISABLED;
}

// Places the selection bar behind the selected row of the list or of the cheat search, or hides it.
static void drawBar(void)
{
    int y = sBarY;
    bool visible = !sDescriptionOpen && getEntryCount() != 0;
    if (sSearchScreen == SEARCH_OPTIONS)
    {
        y = getRowY(getOptionRow(sOptionCursor));
        visible = true;
    }
    else if (sSearchScreen == SEARCH_RESULTS)
    {
        y = getRowY(RESULT_FIRST_ROW + sResultCursor - sResultTop);
        visible = true;
    }
    for (u32 i = 0; i < BAR_SPRITE_COUNT; i++)
        OAM_SUB[(BAR_SPRITES + i) * 4] = visible ? y | OBJ_WIDE : OBJ_DISABLED;
}

static void drawScrollbar(void)
{
    bool visible = !sDescriptionOpen && sSearchScreen == SEARCH_CLOSED && hasScrollbar();
    OAM_SUB[THUMB_SPRITE * 4] = visible ? sThumbY | OBJ_TALL : OBJ_DISABLED;
    for (u32 i = 0; i < SCROLLBAR_TRACK_SPRITES; i++)
        OAM_SUB[(TRACK_SPRITES + i) * 4] = visible ? (BAND_HEIGHT + i * 32) | OBJ_TALL : OBJ_DISABLED;
}

// Shows the progress of a search as a text and a bar. The search does not present frames, so the two rows use one set
// of text sprites each and stay on the screen without being rewritten.
static void drawSearchProgress(u32 percent)
{
    static const char sPrefix[] = "Searching... ";
    char text[TEXT_COLUMNS];
    memcpy(text, sPrefix, sizeof(sPrefix) - 1);
    u32 length = sizeof(sPrefix) - 1 + formatNumber(&text[sizeof(sPrefix) - 1], percent);
    text[length++] = '%';
    text[length] = 0;
    char chars[TEXT_COLUMNS];
    u8 palettes[TEXT_COLUMNS];
    clearRow(chars, palettes);
    putString(chars, palettes, (TEXT_COLUMNS - length) >> 1, text, length, PALETTE_WHITE);
    writeRow(chars, palettes, getRowY(PROGRESS_TEXT_ROW) + ROW_TEXT_OFFSET, 0, TEXT_SPRITES);

    clearRow(chars, palettes);
    u32 pixels = divide(percent * PROGRESS_BAR_CELLS * 8, 100);
    for (u32 i = 0; i < PROGRESS_BAR_CELLS; i++)
    {
        u32 cellStart = i * 8;
        u32 filled = pixels <= cellStart ? 0 : (pixels - cellStart >= 8 ? 8 : pixels - cellStart);
        chars[PROGRESS_BAR_COLUMN + i] = ICON_PROGRESS_CHAR(filled);
        palettes[PROGRESS_BAR_COLUMN + i] = filled != 0 ? PALETTE_GREEN : PALETTE_LINE;
    }
    writeRow(chars, palettes, getRowY(PROGRESS_BAR_ROW) + ROW_TEXT_OFFSET, 0, TEXT_SPRITES + TEXT_COLUMNS);
}

static void startSearchProgress(void)
{
    hideSprites(FRONT_SPRITES, TEXT_COLUMNS);
    hideSprites(TEXT_SPRITES, 2 * TEXT_COLUMNS);
    hideSprites(BAR_SPRITES, BAR_SPRITE_COUNT);
    drawHeaderRow();
    sProgressPercent = 0;
    drawSearchProgress(0);
}

void cheatSearch_reportProgress(u32 done, u32 total)
{
    u32 percent = divide(done * 100, total);
    if (percent == sProgressPercent)
        return;
    sProgressPercent = percent;
    drawSearchProgress(percent);
}

// Waits until the given line is displayed, after the end of vblank.
static void waitForLine(u32 line)
{
    while (REG_VCOUNT >= 192);
    while (REG_VCOUNT < line);
}

// The two sets of text sprites while a frame is drawn: the set of the next row and, for each set, the line after which
// its previous row has been displayed.
typedef struct
{
    u32 set;
    u32 freeLine[2];
} row_sets_t;

// Returns the first sprite for the next row, which is displayed from line y on, once the sprites of its set are free.
static u32 takeRowSprites(row_sets_t* sets, u32 y)
{
    u32 set = sets->set;
    if (sets->freeLine[set] != 0)
        waitForLine(sets->freeLine[set]);
    sets->freeLine[set] = y + 8;
    sets->set = set ^ 1;
    return TEXT_SPRITES + set * TEXT_COLUMNS;
}

// Returns the height of the given row of the description view.
static u32 getDescriptionRowY(u32 row)
{
    u32 nameY = FIRST_ROW_Y + ROW_TEXT_OFFSET;
    if (row < sDescriptionNameRowCount)
        return nameY + row * DESCRIPTION_ROW_HEIGHT;
    u32 lineY = nameY + (sDescriptionNameRowCount - 1) * DESCRIPTION_ROW_HEIGHT + DESCRIPTION_LINE_GAP;
    if (row == sDescriptionNameRowCount)
        return lineY;
    return lineY + DESCRIPTION_LINE_GAP + (row - sDescriptionNameRowCount - 1) * DESCRIPTION_ROW_HEIGHT;
}

// Draws a text in the middle of the given row between the bands.
static void drawCenteredRow(const char* text, u32 row, row_sets_t* sets)
{
    char chars[TEXT_COLUMNS];
    u8 palettes[TEXT_COLUMNS];
    clearRow(chars, palettes);
    putCentered(chars, palettes, text, PALETTE_LIGHT);
    u32 y = getRowY(row) + ROW_TEXT_OFFSET;
    writeRow(chars, palettes, y, 0, takeRowSprites(sets, y));
}

// Draws one frame of the menu. Must be called in vblank.
//
// The header is written in vblank. The rows use the two sets of text sprites alternately: each row is written as soon
// as the row before the previous one, which used the same set, has been displayed. Writing a row takes less than 8
// lines. The header and the status row share the front sprites, so the status row is written once the header has been
// displayed.
static void drawFrame(void)
{
    drawBar();
    drawScrollbar();
    drawHeaderRow();
    // The sets may still hold rows of the previous frame.
    hideSprites(TEXT_SPRITES, 2 * TEXT_COLUMNS);
    row_sets_t sets = { 0, { 0, 0 } };
    if (sSearchScreen != SEARCH_CLOSED)
    {
        for (u32 row = 0; row < CONTENT_ROWS; row++)
        {
            u32 y = getRowY(row) + ROW_TEXT_OFFSET;
            drawContentRow(row, y, takeRowSprites(&sets, y));
        }
    }
    else if (sDescriptionOpen)
    {
        for (u32 row = 0; row < sDescriptionNameRowCount + 1 + sDescriptionRowCount; row++)
        {
            u32 y = getDescriptionRowY(row);
            drawContentRow(row, y, takeRowSprites(&sets, y));
        }
    }
    else if (!sInFolder && getEntryCount() == 0)
    {
        // The parasite only installs the menu for a game without cheats if the cheat search is available.
        drawCenteredRow("No cheats for this game", EMPTY_LIST_ROW, &sets);
        drawCenteredRow("Press R for the cheat search", EMPTY_LIST_ROW + 1, &sets);
    }
    else
    {
        int entryCount = getEntryCount();
        for (int i = -1; i <= LIST_VISIBLE_ROWS; i++)
        {
            int y = FIRST_ROW_Y + ROW_TEXT_OFFSET + i * ROW_HEIGHT + sScrollOffset;
            int entry = (int)sListTop + i;
            // Skip the rows that are completely behind the bands.
            if (y <= BAND_HEIGHT - 8 || y >= STATUS_BAND_Y || entry < 0 || entry >= entryCount)
                continue;
            drawListRow(entry, y, takeRowSprites(&sets, y));
        }
    }
    waitForLine(HEADER_TEXT_Y + 8);
    drawStatusRow();
}

// ---- Sub engine ----

// Converts 1bpp tiles, with one byte per pixel row, to 4bpp tiles.
static void uploadBitmapTiles(vu32* dst, const u8* bitmap, u32 tileCount)
{
    for (u32 i = 0; i < tileCount * 8; i++)
    {
        u32 bits = bitmap[i];
        u32 line = 0;
        for (u32 x = 0; x < 8; x++)
            line |= ((bits >> x) & 1) << (x * 4);
        dst[i] = line;
    }
}

// Converts the glyphs and the icons to 4bpp tiles and creates the other tiles of the menu.
static void uploadTiles(void)
{
    vu32* dst = VRAM_SUB_OBJ;
    uploadBitmapTiles(dst, sFont, GLYPH_COUNT);
    uploadBitmapTiles(&dst[ICON_TILE * 8], sIcons, ICON_COUNT);
    for (u32 i = 0; i < BAND_TILE_COUNT * 8; i++)
        dst[BAND_TILE * 8 + i] = 0x11111111;
    // The selection bar is as high as a row: the second row of tiles is only partly filled.
    for (u32 i = 0; i < BAR_TILE_COUNT * 8; i++)
    {
        bool filled = i < 4 * 8 || (i & 7) < ROW_HEIGHT - 8;
        dst[BAR_TILE * 8 + i] = filled ? 0x11111111 : 0;
    }
    // The track is a line in the middle of the column; the thumb is 3 pixels wide with rounded ends.
    for (u32 y = 0; y < 32; y++)
        dst[TRACK_TILE * 8 + y] = 0x00010000;
    for (u32 y = 0; y < THUMB_HEIGHT; y++)
        dst[THUMB_TILE * 8 + y] = y == 0 || y == THUMB_HEIGHT - 1 ? 0x00010000 : 0x00111000;
}

static void saveSubEngineState(void)
{
    sSaved.powCnt = REG_POWCNT;
    sSaved.dispCnt = REG_DISPCNT_SUB;
    sSaved.bldCnt = REG_BLDCNT_SUB;
    sSaved.masterBright = REG_MASTER_BRIGHT_SUB;
    sSaved.backdrop = PALETTE_BG_SUB[0];
    sSaved.vramCntD = REG_VRAMCNT_D;
    sSaved.vramCntI = REG_VRAMCNT_I;
    for (u32 i = 0; i < PALETTE_COUNT; i++)
        sSaved.objPalette[i] = PALETTE_OBJ_SUB[i * 16 + 1];
    for (u32 i = 0; i < OAM_ENTRY_COUNT; i++)
    {
        for (u32 j = 0; j < 3; j++)
            sSaved.oam[i][j] = OAM_SUB[i * 4 + j];
    }
    REG_VRAMCNT_I = VRAMCNT_LCDC;
    copyWords(sSaved.tileVram, VRAM_LCDC_I, TILE_VRAM_SIZE);
}

static void restoreSubEngineState(void)
{
    REG_DISPCNT_SUB = DISPCNT_FORCED_BLANK;
    REG_VRAMCNT_I = VRAMCNT_LCDC;
    copyWords(VRAM_LCDC_I, sSaved.tileVram, TILE_VRAM_SIZE);
    REG_VRAMCNT_I = sSaved.vramCntI;
    REG_VRAMCNT_D = sSaved.vramCntD;
    for (u32 i = 0; i < OAM_ENTRY_COUNT; i++)
    {
        for (u32 j = 0; j < 3; j++)
            OAM_SUB[i * 4 + j] = sSaved.oam[i][j];
    }
    for (u32 i = 0; i < PALETTE_COUNT; i++)
        PALETTE_OBJ_SUB[i * 16 + 1] = sSaved.objPalette[i];
    PALETTE_BG_SUB[0] = sSaved.backdrop;
    REG_MASTER_BRIGHT_SUB = sSaved.masterBright;
    REG_BLDCNT_SUB = sSaved.bldCnt;
    REG_POWCNT = sSaved.powCnt;
    REG_DISPCNT_SUB = sSaved.dispCnt;
}

// Prepares the sub engine for the menu: VRAM bank I holds the sprite tiles, and the sprites that keep their tile and
// position (bands, selection bar and scrollbar) are set up. The display stays off until the menu turns it on.
static void setupSubEngine(void)
{
    REG_DISPCNT_SUB = DISPCNT_FORCED_BLANK;
    REG_POWCNT = sSaved.powCnt | POWCNT_LCD | POWCNT_2D_SUB;
    // Bank D mapped to sub engine sprites would overlap with bank I.
    if ((sSaved.vramCntD & 0x87) == VRAMCNT_D_SUB_OBJ)
        REG_VRAMCNT_D = VRAMCNT_LCDC;
    REG_VRAMCNT_I = VRAMCNT_I_SUB_OBJ;
    REG_BLDCNT_SUB = 0;
    REG_MASTER_BRIGHT_SUB = 0;
    PALETTE_BG_SUB[0] = BACKDROP_COLOR;
    for (u32 i = 0; i < PALETTE_COUNT; i++)
        PALETTE_OBJ_SUB[i * 16 + 1] = sPaletteColors[i];
    uploadTiles();
    hideSprites(0, OAM_ENTRY_COUNT);
    for (u32 i = 0; i < BAND_SPRITES_PER_BAND; i++)
    {
        vu16* band = &OAM_SUB[(BAND_SPRITES + i) * 4];
        // The band at the top starts above the screen, which only shows its last lines.
        band[0] = ((BAND_HEIGHT - 32) & 0xFF) | OBJ_WIDE;
        band[1] = i * 64 | OBJ_SIZE_64X32;
        band[2] = BAND_TILE | (PALETTE_BAND << 12);
        band += BAND_SPRITES_PER_BAND * 4;
        band[0] = STATUS_BAND_Y | OBJ_WIDE;
        band[1] = i * 64 | OBJ_SIZE_64X32;
        band[2] = BAND_TILE | (PALETTE_BAND << 12);
    }
    for (u32 i = 0; i < BAR_SPRITE_COUNT; i++)
    {
        vu16* bar = &OAM_SUB[(BAR_SPRITES + i) * 4];
        bar[1] = i * 32 | OBJ_SIZE_32X16;
        bar[2] = BAR_TILE | (PALETTE_BAR << 12);
    }
    OAM_SUB[THUMB_SPRITE * 4 + 1] = SCROLLBAR_X; // 8x16
    OAM_SUB[THUMB_SPRITE * 4 + 2] = THUMB_TILE | (PALETTE_LIGHT << 12);
    for (u32 i = 0; i < SCROLLBAR_TRACK_SPRITES; i++)
    {
        OAM_SUB[(TRACK_SPRITES + i) * 4 + 1] = SCROLLBAR_X | OBJ_SIZE_8X32;
        OAM_SUB[(TRACK_SPRITES + i) * 4 + 2] = TRACK_TILE | (PALETTE_LINE << 12);
    }
}

// ---- Main loop ----

// Advances the animations by one frame and draws the menu in the next vblank. Also used by the loops that wait for
// something, so that the menu stays on screen.
static void presentFrame(void)
{
    updateMarquee();
    updateSelectionAnimation();
    if (sMessageTimer != 0 && --sMessageTimer == 0)
        sMessage = 0;
    // The previous frame was finished before vblank, so this is the next vblank. Not waiting for its start avoids
    // losing a frame when the work for this frame took a few lines.
    while (REG_VCOUNT < 192 || REG_VCOUNT > 262 - VBLANK_DRAW_LINES);
    drawFrame();
}

// Shows a message until it disappears. Used after the menu was closed, when there is nothing left to do.
static void showMessageAndWait(const char* message)
{
    showMessage(message);
    while (sMessageTimer != 0)
        presentFrame();
}

// Returns the direction keys to act on in this frame: the ones just pressed and, while they are held, the held ones
// after REPEAT_DELAY frames and then every REPEAT_RATE frames. Keys that were already held when the menu opened do not
// repeat until they are pressed again.
static u32 getRepeatedDirections(u32 keys, u32 pressed)
{
    if (pressed & KEY_DIRECTIONS)
    {
        sRepeatTimer = REPEAT_DELAY;
        return pressed & KEY_DIRECTIONS;
    }
    u32 held = keys & KEY_DIRECTIONS;
    if (held == 0 || sRepeatTimer == 0 || --sRepeatTimer != 0)
        return 0;
    sRepeatTimer = REPEAT_RATE;
    return held;
}

// Starts editing the given number, which must fit in the given size in bytes, with its last digit selected.
static void startEditing(u32 value, u32 size)
{
    sEditing = true;
    sEditDigitCount = size == 1 ? 3 : (size == 2 ? 5 : MAX_DIGITS);
    sEditPosition = sEditDigitCount - 1;
    for (u32 i = 0; i < sEditDigitCount; i++)
    {
        u32 power = sPowersOfTen[MAX_DIGITS - sEditDigitCount + i];
        u32 digit = 0;
        while (value >= power)
        {
            value -= power;
            digit++;
        }
        sEditDigits[i] = digit;
    }
}

// Returns the edited number, limited to the largest value of the given size in bytes.
static u32 getEditedValue(u32 size)
{
    u32 max = cheatSearch_getMaxValue(size);
    u32 value = 0;
    for (u32 i = 0; i < sEditDigitCount; i++)
    {
        // Ten digits can exceed 32 bits.
        if (value > 429496729 || (value == 429496729 && sEditDigits[i] > 5))
            return max;
        value = value * 10 + sEditDigits[i];
    }
    return value > max ? max : value;
}

// Handles the keys while a number is edited: Left and Right select a digit, Up and Down change it, A confirms the
// number and B cancels the change.
static u32 handleEditKeys(u32 keys, u32 pressed)
{
    if (pressed & (KEY_A | KEY_B))
    {
        sEditing = false;
        return (pressed & KEY_A) ? EDIT_CONFIRMED : EDIT_CANCELLED;
    }
    u32 directions = getRepeatedDirections(keys, pressed);
    u8* digit = &sEditDigits[sEditPosition];
    if ((directions & KEY_LEFT) && sEditPosition > 0)
        sEditPosition--;
    else if ((directions & KEY_RIGHT) && sEditPosition + 1 < sEditDigitCount)
        sEditPosition++;
    else if (directions & KEY_UP)
        *digit = *digit == 9 ? 0 : *digit + 1;
    else if (directions & KEY_DOWN)
        *digit = *digit == 0 ? 9 : *digit - 1;
    return EDIT_CONTINUE;
}

// Writes a value to the memory of the game. The cheat engine on the arm7 does not see the data cache of the arm9.
static void writeGameValue(u32 address, u32 value, u32 size)
{
    if (size == 1)
        *(vu8*)address = value;
    else if (size == 2)
        *(vu16*)address = value;
    else
        *(vu32*)address = value;
    cheatmenu_cleanDataCache((const void*)address, size);
}

static void openSearch(void)
{
    if (!hasCheatSearch())
    {
        showMessage("Cheat search not available");
        return;
    }
    if (!loadSearch())
    {
        showMessage("Error reading the sd card");
        return;
    }
    sSearchScreen = SEARCH_OPTIONS;
    sEditing = false;
    sOptionCursor = sSearch.valueSize != 0 ? OPTION_COMPARE : OPTION_SIZE;
}

// Returns to the list. The settings are saved for the next time.
static void closeSearch(void)
{
    sSearchScreen = SEARCH_CLOSED;
    sEditing = false;
    saveSearch();
    resetSelectionAnimation();
}

// Selects the next or the previous size of the values, before the first search.
static void changeSize(int direction)
{
    if (sSearch.valueSize != 0)
    {
        showMessage("Start a new search to change");
        return;
    }
    u32 size = sSearch.settingSize;
    if (direction > 0)
        size = size == 4 ? 1 : size << 1;
    else
        size = size == 1 ? 4 : size >> 1;
    sSearch.settingSize = size;
    u32 max = cheatSearch_getMaxValue(size);
    if (sSearch.settingValue > max)
        sSearch.settingValue = max;
}

// Selects the next or the previous comparison that the next search can use.
static void changeCompare(int direction)
{
    u32 compare = sSearch.settingCompare;
    do
    {
        if (direction > 0)
            compare = compare + 1 == CHEAT_SEARCH_COMPARE_COUNT ? 0 : compare + 1;
        else
            compare = compare == 0 ? CHEAT_SEARCH_COMPARE_COUNT - 1 : compare - 1;
    } while (!cheatSearch_isCompareAllowed(&sSearch, compare));
    sSearch.settingCompare = compare;
}

static void runSearch(void)
{
    startSearchProgress();
    u32 menuStart = sResident->menuAddress;
    u32 menuEnd = menuStart + sResident->imageSectors * SECTOR_SIZE;
    bool ok = cheatSearch_run(&sSearch, (const u8*)CHEAT_SEARCH_MEMORY_START, sResident->searchMemorySize, menuStart,
        menuEnd);
    // After a search for every value, the next search compares with the previous values.
    if (!cheatSearch_isCompareAllowed(&sSearch, sSearch.settingCompare))
        sSearch.settingCompare = CHEAT_SEARCH_CHANGED;
    if (!ok || !cheatSearch_save(&sSearch))
    {
        showMessage("Error accessing the sd card");
        return;
    }
    if (sSearch.resultCount == 0)
    {
        showMessage("No results");
        return;
    }
    formatResultCount(sMessageBuffer, sSearch.resultCount);
    showMessage(sMessageBuffer);
    if (canListResults())
        sOptionCursor = OPTION_RESULTS;
}

static void openResults(void)
{
    if (!canListResults())
    {
        if (sSearch.valueSize == 0)
            showMessage("Start a search first");
        else
            showMessage(sSearch.resultCount == 0 ? "No results" : "Too many results, search again");
        return;
    }
    sSearchScreen = SEARCH_RESULTS;
    sEditing = false;
    sResultCursor = 0;
    sResultTop = 0;
}

// Handles the keys on the screen with the settings of the search.
static void handleOptionKeys(u32 keys, u32 pressed)
{
    if (sEditing)
    {
        if (handleEditKeys(keys, pressed) == EDIT_CONFIRMED)
            sSearch.settingValue = getEditedValue(getSearchSize());
        return;
    }
    if (pressed & (KEY_B | KEY_R))
    {
        closeSearch();
        return;
    }

    u32 directions = getRepeatedDirections(keys, pressed);
    if (directions & KEY_UP)
        sOptionCursor = sOptionCursor == 0 ? OPTION_COUNT - 1 : sOptionCursor - 1;
    else if (directions & KEY_DOWN)
        sOptionCursor = sOptionCursor + 1 == OPTION_COUNT ? 0 : sOptionCursor + 1;
    else if (directions & (KEY_LEFT | KEY_RIGHT))
    {
        int direction = (directions & KEY_RIGHT) ? 1 : -1;
        if (sOptionCursor == OPTION_SIZE)
            changeSize(direction);
        else if (sOptionCursor == OPTION_COMPARE)
            changeCompare(direction);
    }

    if (!(pressed & KEY_A))
        return;
    switch (sOptionCursor)
    {
        case OPTION_SIZE:
            changeSize(1);
            break;
        case OPTION_COMPARE:
            changeCompare(1);
            break;
        case OPTION_VALUE:
            if (cheatSearch_usesValue(sSearch.settingCompare))
                startEditing(sSearch.settingValue, getSearchSize());
            else
                showMessage("This search uses no value");
            break;
        case OPTION_SEARCH:
            runSearch();
            break;
        case OPTION_RESULTS:
            openResults();
            break;
        case OPTION_NEW:
            if (sSearch.valueSize != 0)
            {
                cheatSearch_reset(&sSearch);
                sOptionCursor = OPTION_SIZE;
                if (saveSearch())
                    showMessage("New search");
            }
            break;
        default:
            if (sSearch.lockCount != 0)
            {
                sSearch.lockCount = 0;
                sChanged = true;
                if (saveSearch())
                    showMessage("All values unlocked");
            }
            break;
    }
}

// Selects the given result, limited to the results, and scrolls the results to show it.
static void setResultCursor(int cursor)
{
    int count = sSearch.resultCount;
    if (cursor >= count)
        cursor = count - 1;
    if (cursor < 0)
        cursor = 0;
    sResultCursor = cursor;
    if (sResultCursor < sResultTop)
        sResultTop = sResultCursor;
    else if (sResultCursor >= sResultTop + RESULT_VISIBLE_ROWS)
        sResultTop = sResultCursor - RESULT_VISIBLE_ROWS + 1;
}

// Locks the selected result with its current value, or unlocks it. The cheats are updated when the menu is closed.
static void toggleLock(u32 address)
{
    const char* message = "Value unlocked";
    if (cheatSearch_findLock(&sSearch, address) >= 0)
    {
        cheatSearch_unlock(&sSearch, address);
    }
    else if (cheatSearch_lock(&sSearch, address, readResultValue(address), sSearch.valueSize))
    {
        message = "Value locked";
    }
    else
    {
        showMessage("Too many locked values");
        return;
    }
    sChanged = true;
    if (saveSearch())
        showMessage(message);
}

// Handles the keys on the screen with the results.
static void handleResultKeys(u32 keys, u32 pressed)
{
    u32 address = sSearch.results[sResultCursor];
    u32 size = sSearch.valueSize;
    if (sEditing)
    {
        if (handleEditKeys(keys, pressed) != EDIT_CONFIRMED)
            return;
        u32 value = getEditedValue(size);
        writeGameValue(address, value, size);
        // A locked value keeps the new value.
        if (cheatSearch_findLock(&sSearch, address) >= 0)
        {
            cheatSearch_lock(&sSearch, address, value, size);
            sChanged = true;
            if (!saveSearch())
                return;
        }
        showMessage("Value written");
        return;
    }
    if (pressed & KEY_B)
    {
        sSearchScreen = SEARCH_OPTIONS;
        return;
    }

    u32 directions = getRepeatedDirections(keys, pressed);
    int count = sSearch.resultCount;
    if (directions & KEY_LEFT)
        setResultCursor((int)sResultCursor - RESULT_VISIBLE_ROWS);
    else if (directions & KEY_RIGHT)
        setResultCursor((int)sResultCursor + RESULT_VISIBLE_ROWS);
    else if (directions & KEY_UP)
        setResultCursor(sResultCursor == 0 ? count - 1 : (int)sResultCursor - 1);
    else if (directions & KEY_DOWN)
        setResultCursor((int)sResultCursor + 1 == count ? 0 : (int)sResultCursor + 1);
    else if (pressed & KEY_A)
        startEditing(readResultValue(address), size);
    else if (pressed & KEY_START)
        toggleLock(address);
}

// Handles the keys in the list. Returns false when the menu is to be closed.
static bool handleListKeys(u32 keys, u32 pressed)
{
    if (pressed & KEY_R)
    {
        openSearch();
        return true;
    }
    if (pressed & KEY_SELECT)
    {
        openDescription();
        return true;
    }
    if (pressed & KEY_B)
    {
        if (!sInFolder)
            return false;
        closeFolder();
        return true;
    }

    u32 directions = getRepeatedDirections(keys, pressed);
    if (directions & KEY_LEFT)
        moveCursor(-LIST_VISIBLE_ROWS);
    else if (directions & KEY_RIGHT)
        moveCursor(LIST_VISIBLE_ROWS);
    else if (directions & KEY_UP)
        moveCursor(-1);
    else if (directions & KEY_DOWN)
        moveCursor(1);

    if (pressed & KEY_A)
        toggleEntry();
    else if (pressed & KEY_START)
        disableAll();
    return true;
}

// Applies the changes and saves the persistent data. Then waits until all keys are released, so that the game does
// not react to them.
static void finishMenu(void)
{
    u32 rootCursor = sInFolder ? sRootCursor : sCursor;
    u32 rootTop = sInFolder ? sRootTop : sListTop;
    bool moved = rootCursor != sData->rootCursor || rootTop != sData->rootTop;
    sData->rootCursor = rootCursor;
    sData->rootTop = rootTop;
    if (sChanged)
    {
        sMessage = "Applying cheats...";
        sMessageTimer = 0;
        presentFrame();
        sDatabaseError = false;
        bool allFit = updateActiveCheats();
        sMessage = 0;
        if (sDatabaseError)
            showMessageAndWait("Error reading usrcheat.dat");
        else if (!allFit)
            showMessageAndWait("Too many cheats, some skipped");
    }
    if ((sChanged || moved) && !savePersistentData())
        showMessageAndWait("Error writing to the sd card");
    while (readKeys() != 0)
        presentFrame();
}

static void runMenu(void)
{
    waitVBlank();
    saveSubEngineState();
    setupSubEngine();
    REG_DISPCNT_SUB = DISPCNT_MENU;

    sCursor = sData->rootCursor;
    sListTop = sData->rootTop;
    setCursor(sCursor);
    resetSelectionAnimation();

    u32 previousKeys = readKeys();
    while (true)
    {
        if (!sDescriptionOpen && sSearchScreen == SEARCH_CLOSED)
        {
            updateRows();
            updateSelectedName();
        }
        if (sDatabaseError)
        {
            showMessage("Error reading usrcheat.dat");
            sDatabaseError = false;
        }
        presentFrame();

        u32 keys = readKeys();
        u32 pressed = keys & ~previousKeys;
        previousKeys = keys;
        if (sDescriptionOpen)
        {
            if (pressed & (KEY_B | KEY_SELECT))
                closeDescription();
        }
        else if (sSearchScreen == SEARCH_OPTIONS)
        {
            handleOptionKeys(keys, pressed);
        }
        else if (sSearchScreen == SEARCH_RESULTS)
        {
            handleResultKeys(keys, pressed);
        }
        else if (!handleListKeys(keys, pressed))
        {
            break;
        }
    }

    finishMenu();
    waitVBlank();
    restoreSubEngineState();
}

u32 cheatmenu_main(cheat_menu_resident_t* resident, u32 numberOfCheats)
{
    sResident = resident;
    sData = (cheat_menu_data_t*)((u8*)&cheatmenu_header + cheatmenu_header.dataOffset);
    sNumberOfCheats = numberOfCheats;
    runMenu();
    return sNumberOfCheats;
}
