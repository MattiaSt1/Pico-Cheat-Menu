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
// cell is an 8x8 sprite with a glyph of the font. Since 128 sprites are not enough for a screen of text, the rows
// between the header and the status row are drawn with two sets of 32 sprites that are rewritten while the screen is
// displayed: each row is written as soon as the row that used the same set before it has been displayed.

#include <stddef.h>
#include <nds/ndstypes.h>
#include "../include/picoLoader7.h"
#include "cheatMenuFile.h"
#include "font.h"

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
#define OBJ_WIDE                (1 << 14) // attribute 0 shape and attribute 1 size of a 32x8 sprite
#define OBJ_TALL                (1 << 15) // attribute 0 shape of 8x16 and 8x32 sprites
#define OBJ_SIZE_8X32           (1 << 14) // attribute 1 size of an 8x32 sprite

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
#define TEXT_ROWS               24
#define LIST_FIRST_ROW          2
#define LIST_VISIBLE_ROWS       (TEXT_ROWS - 3)
#define STATUS_ROW              (TEXT_ROWS - 1)
#define NAME_COLUMN             5
#define NAME_VISIBLE_LENGTH     (TEXT_COLUMNS - NAME_COLUMN)
#define SELECTED_NAME_LENGTH    96
#define DESCRIPTION_FIRST_ROW   3
#define DESCRIPTION_ROWS        (TEXT_ROWS - DESCRIPTION_FIRST_ROW)

// The selected row moves this many pixels to the right, so its name shows one character less.
#define FOCUS_SHIFT             6

// The rows of the list that can be visible: the visible rows and, while the list scrolls, the row on each side.
#define LIST_ROW_CACHE_SIZE     (LIST_VISIBLE_ROWS + 2)

// When the list does not fit on the screen, a scrollbar in the last column shows the position of the cursor.
#define SCROLLBAR_X             ((TEXT_COLUMNS - 1) * 8)
#define SCROLLBAR_TRACK_SPRITES 6 // 8x32 sprites; the last one is partly hidden behind the status row
#define THUMB_HEIGHT            16

// ---- Tiles and sprites ----

#define FIRST_GLYPH             0x20
#define GLYPH_COUNT             96
// After the glyphs: 4 solid tiles for the 32x8 sprites of the selection bar and of the masks, a horizontal line, 4 tiles
// for the 8x32 sprites of the scrollbar track and 2 tiles for the 8x16 thumb.
#define BAR_TILE                GLYPH_COUNT
#define LINE_TILE               (GLYPH_COUNT + 4)
#define TRACK_TILE              (GLYPH_COUNT + 5)
#define THUMB_TILE              (GLYPH_COUNT + 9)
#define TILE_VRAM_SIZE          ((GLYPH_COUNT + 11) * 32)
// A character that cannot appear in a name, drawn with LINE_TILE.
#define LINE_CHAR               0x80

// Sprites, from the front to the back: rows 1 and 23, the 32x8 masks behind them, the scrollbar (thumb, then track),
// the two sets of sprites for the other text rows, and the 32x8 sprites of the selection bar. The masks have the
// backdrop color and hide the list rows that slide behind rows 1 and 23 while the list scrolls.
#define FRONT_SPRITES           0
#define MASK_SPRITES            (FRONT_SPRITES + TEXT_COLUMNS)
#define WIDE_SPRITES_PER_ROW    (TEXT_COLUMNS / 4)
#define THUMB_SPRITE            (MASK_SPRITES + 2 * WIDE_SPRITES_PER_ROW)
#define TRACK_SPRITES           (THUMB_SPRITE + 1)
#define TEXT_SPRITES            (TRACK_SPRITES + SCROLLBAR_TRACK_SPRITES)
#define BAR_SPRITES             (TEXT_SPRITES + 2 * TEXT_COLUMNS)

// Each sprite palette uses only color 1, for the pixels of the tiles; color 0 is transparent.
enum
{
    PALETTE_WHITE,
    PALETTE_GRAY,
    PALETTE_YELLOW,
    PALETTE_GREEN,
    PALETTE_RED,
    PALETTE_CYAN,
    PALETTE_SIGNATURE,
    PALETTE_BAR,
    PALETTE_MASK,
    PALETTE_COUNT
};

static const u16 sPaletteColors[PALETTE_COUNT] =
{
    0x7FFF, // white
    0x4E73, // gray
    0x13BF, // yellow
    0x13E4, // green
    0x109F, // red
    0x7EE0, // cyan
    0x0C63, // signature, barely brighter than the backdrop
    0x38E5, // selection bar, dark blue
    0x0000  // masks, the backdrop color
};

#define BACKDROP_COLOR          0x0000

// ---- Timing ----

// Holding a direction key repeats it after REPEAT_DELAY frames, then every REPEAT_RATE frames.
#define REPEAT_DELAY            20
#define REPEAT_RATE             4
// Pixels per frame of the animations, so that moving by one row takes as long as one key repeat.
#define ANIMATION_SPEED         (8 / REPEAT_RATE)
// Number of vblank lines that drawFrame needs at least before the first line is displayed.
#define VBLANK_DRAW_LINES       10
// Names that do not fit scroll by one character every MARQUEE_SPEED frames, pausing MARQUEE_PAUSE frames at the ends.
#define MARQUEE_PAUSE           60
#define MARQUEE_SPEED           6
#define MESSAGE_FRAMES          120

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
static char sFolderName[TEXT_COLUMNS];
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
static char sDescriptionRows[DESCRIPTION_ROWS][TEXT_COLUMNS + 1];

// The message on the status row, if any, and the number of frames until it disappears (0 to keep it).
static const char* sMessage;
static u32 sMessageTimer;

// Set when the database cannot be read; the main loop reports it.
static bool sDatabaseError;

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

// Formats a decimal number of up to 5 digits without division. Returns its length.
static u32 formatNumber(char* dst, u32 value)
{
    static const u32 sPowers[] = { 10000, 1000, 100, 10, 1 };
    u32 length = 0;
    for (u32 i = 0; i < 5; i++)
    {
        u32 digit = 0;
        while (value >= sPowers[i])
        {
            value -= sPowers[i];
            digit++;
        }
        if (digit != 0 || length != 0 || i == 4)
            dst[length++] = '0' + digit;
    }
    dst[length] = 0;
    return length;
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
// cheats. The resident part has paused the cheat engine while the menu is open. Returns false if not all enabled
// cheats fit in the list; read errors set sDatabaseError.
static bool updateActiveCheats(void)
{
    cheat_menu_list_header_t* list = sResident->list;
    u8* cheats = (u8*)(list + 1);
    u32 capacity = list->marker[5] - sizeof(*list);
    u32 used = 0;
    u32 count = 0;
    bool allFit = true;
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
// character less, and the scrollbar takes the last column.
static u32 getVisibleNameLength(bool shifted)
{
    return NAME_VISIBLE_LENGTH - shifted - hasScrollbar();
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

// Scrolls the selected name when it does not fit: in its row of the list, or in its own row in the description view.
static void updateMarquee(void)
{
    u32 visibleLength = sDescriptionOpen ? TEXT_COLUMNS : getVisibleNameLength(true);
    if (sSelectedNameLength <= visibleLength)
        return;
    u32 maxOffset = sSelectedNameLength - visibleLength;
    sMarqueeTimer++;
    if (sMarqueeOffset == 0 || sMarqueeOffset >= maxOffset)
    {
        if (sMarqueeTimer < MARQUEE_PAUSE)
            return;
        if (sMarqueeOffset >= maxOffset)
        {
            resetMarquee();
            return;
        }
    }
    if (sMarqueeTimer >= MARQUEE_SPEED)
    {
        sMarqueeTimer = 0;
        sMarqueeOffset++;
    }
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

static int getCursorBarY(void)
{
    return (LIST_FIRST_ROW + sCursor - sListTop) * 8;
}

static int getThumbY(void)
{
    u32 entryCount = getEntryCount();
    u32 range = LIST_VISIBLE_ROWS * 8 - THUMB_HEIGHT;
    return LIST_FIRST_ROW * 8 + (entryCount > 1 ? divide(range * sCursor, entryCount - 1) : 0);
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

static void showMessage(const char* message)
{
    sMessage = message;
    sMessageTimer = MESSAGE_FRAMES;
}

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
    sScrollOffset += ((int)sListTop - (int)previousTop) * 8;
    if (sScrollOffset > 8)
        sScrollOffset = 8;
    else if (sScrollOffset < -8)
        sScrollOffset = -8;
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
    readItemName(folderItem, sFolderName, TEXT_COLUMNS - 8);
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
    while (offset < sDescriptionEnd && length < TEXT_COLUMNS)
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
    if (!newline && length == TEXT_COLUMNS && offset < sDescriptionEnd)
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

// Reads the rows of the note between sDescriptionStart and sDescriptionEnd.
static void loadDescription(void)
{
    memset(sDescriptionRows, 0, sizeof(sDescriptionRows));
    u32 offset = sDescriptionStart;
    for (u32 row = 0; row < DESCRIPTION_ROWS && offset < sDescriptionEnd && !sDatabaseError; row++)
        offset = readDescriptionRow(offset, sDescriptionRows[row]);
    if (!sDatabaseError && hasDescriptionText(offset))
    {
        // The note does not fit on the screen: the last row ends with "..." to show that it is cut.
        char* row = sDescriptionRows[DESCRIPTION_ROWS - 1];
        u32 length = stringLength(row);
        if (length > TEXT_COLUMNS - 3)
            length = TEXT_COLUMNS - 3;
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
    resetMarquee();
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
    resetMarquee();
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

// Fills a composed text row with a horizontal line.
static void putLine(char* chars, u8* palettes)
{
    for (u32 i = 0; i < TEXT_COLUMNS; i++)
    {
        chars[i] = LINE_CHAR;
        palettes[i] = PALETTE_GRAY;
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
        titleEnd = TEXT_COLUMNS - length - 1;
        putString(chars, palettes, titleEnd + 1, position, length, PALETTE_GRAY);
    }
    if (sInFolder)
        putString(chars, palettes, 1, sFolderName, titleEnd - 1, PALETTE_CYAN);
    else
        putString(chars, palettes, 1, "Cheat Menu", titleEnd - 1, PALETTE_YELLOW);
}

// The status row shows the current message, or else the signature.
static void composeStatus(char* chars, u8* palettes)
{
    if (sMessage)
    {
        putString(chars, palettes, 1, sMessage, TEXT_COLUMNS - 1, PALETTE_YELLOW);
        return;
    }
    static const char sSignature[] = "By Esentasse";
    putString(chars, palettes, TEXT_COLUMNS - (sizeof(sSignature) - 1), sSignature, sizeof(sSignature) - 1,
        PALETTE_SIGNATURE);
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
        putString(chars, palettes, 0, ">", 1, PALETTE_YELLOW);
        name = &sSelectedName[sMarqueeOffset];
    }
    if (row->type == ENTRY_FOLDER)
    {
        putString(chars, palettes, 1, "[+]", 3, row->enabledInFolder ? PALETTE_GREEN : PALETTE_GRAY);
        putString(chars, palettes, NAME_COLUMN, name, nameLength, PALETTE_CYAN);
    }
    else
    {
        bool enabled = isEnabled(row->item);
        putString(chars, palettes, 1, enabled ? "ON" : "OFF", 3, enabled ? PALETTE_GREEN : PALETTE_RED);
        putString(chars, palettes, NAME_COLUMN, name, nameLength, PALETTE_WHITE);
    }
    return shift;
}

// Composes the given text row, except for the rows of the list.
static void composeRow(u32 row, char* chars, u8* palettes)
{
    clearRow(chars, palettes);
    if (sDescriptionOpen)
    {
        if (row == 0)
            putString(chars, palettes, 1, "Description", TEXT_COLUMNS - 1, PALETTE_YELLOW);
        else if (row == 1)
            putString(chars, palettes, 0, &sSelectedName[sMarqueeOffset], TEXT_COLUMNS, PALETTE_CYAN);
        else if (row == 2)
            putLine(chars, palettes);
        else if (row >= DESCRIPTION_FIRST_ROW)
            putString(chars, palettes, 0, sDescriptionRows[row - DESCRIPTION_FIRST_ROW], TEXT_COLUMNS, PALETTE_WHITE);
        return;
    }
    if (row == 0)
        composeHeader(chars, palettes);
    else if (row == 1)
        putLine(chars, palettes);
    else if (row == STATUS_ROW)
        composeStatus(chars, palettes);
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
        if (glyph == LINE_CHAR)
            tile = LINE_TILE;
        else if (glyph < FIRST_GLYPH || glyph >= FIRST_GLYPH + GLYPH_COUNT)
            tile = '?' - FIRST_GLYPH;
        else
            tile = glyph - FIRST_GLYPH;
        oam[1] = column * 8 + shift; // 8x8
        oam[2] = tile | (palettes[column] << 12);
        oam[0] = y; // square, 16 colors
    }
}

static void drawTextRow(u32 row, u32 firstSprite)
{
    char chars[TEXT_COLUMNS];
    u8 palettes[TEXT_COLUMNS];
    composeRow(row, chars, palettes);
    writeRow(chars, palettes, row * 8, 0, firstSprite);
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

// Places the selection bar behind the selected row of the list, or hides it.
static void drawBar(void)
{
    bool visible = !sDescriptionOpen && getEntryCount() != 0;
    for (u32 i = 0; i < WIDE_SPRITES_PER_ROW; i++)
        OAM_SUB[(BAR_SPRITES + i) * 4] = visible ? sBarY | OBJ_WIDE : OBJ_DISABLED;
}

static void drawScrollbar(void)
{
    bool visible = !sDescriptionOpen && hasScrollbar();
    OAM_SUB[THUMB_SPRITE * 4] = visible ? sThumbY | OBJ_TALL : OBJ_DISABLED;
    for (u32 i = 0; i < SCROLLBAR_TRACK_SPRITES; i++)
        OAM_SUB[(TRACK_SPRITES + i) * 4] = visible ? (LIST_FIRST_ROW * 8 + i * 32) | OBJ_TALL : OBJ_DISABLED;
}

// Waits until the given line is displayed, after the end of vblank.
static void waitForLine(u32 line)
{
    while (REG_VCOUNT >= 192);
    while (REG_VCOUNT < line);
}

// Draws one frame of the menu. Must be called in vblank.
//
// Rows 1 and 23 use the front sprites: row 1 is written in vblank and row 23 once row 22 is displayed. Row 0 uses the
// first set of text sprites, and the rows in between use the two sets alternately, starting with the second. The first
// of them is written in vblank, each next one as soon as the row before the previous one, which used the same set, has
// been displayed. Writing a row takes less than 8 lines.
static void drawFrame(void)
{
    drawBar();
    drawScrollbar();
    drawTextRow(1, FRONT_SPRITES);
    drawTextRow(0, TEXT_SPRITES);
    // The second set may still hold a row of the previous frame.
    hideSprites(TEXT_SPRITES + TEXT_COLUMNS, TEXT_COLUMNS);
    u32 set = 1;
    bool first = true;
    if (sDescriptionOpen)
    {
        for (u32 row = 2; row < STATUS_ROW; row++, set ^= 1)
        {
            if (!first)
                waitForLine(row * 8 - 8);
            first = false;
            drawTextRow(row, TEXT_SPRITES + set * TEXT_COLUMNS);
        }
    }
    else
    {
        int entryCount = getEntryCount();
        for (int i = -1; i <= LIST_VISIBLE_ROWS; i++)
        {
            int y = (LIST_FIRST_ROW + i) * 8 + sScrollOffset;
            int entry = (int)sListTop + i;
            // Skip the rows that are completely behind rows 1 and 23.
            if (y <= (LIST_FIRST_ROW - 1) * 8 || y >= STATUS_ROW * 8 || entry < 0 || entry >= entryCount)
                continue;
            if (!first)
                waitForLine(y - 8);
            first = false;
            drawListRow(entry, y, TEXT_SPRITES + set * TEXT_COLUMNS);
            set ^= 1;
        }
    }
    waitForLine((STATUS_ROW - 1) * 8);
    drawTextRow(STATUS_ROW, FRONT_SPRITES);
}

// ---- Sub engine ----

// Converts the 1bpp glyphs to 4bpp tiles and creates the other tiles of the menu.
static void uploadTiles(void)
{
    vu32* dst = VRAM_SUB_OBJ;
    for (u32 i = 0; i < GLYPH_COUNT * 8; i++)
    {
        u32 bits = sFont[i];
        u32 line = 0;
        for (u32 x = 0; x < 8; x++)
            line |= ((bits >> x) & 1) << (x * 4);
        dst[i] = line;
    }
    for (u32 i = 0; i < 4 * 8; i++)
        dst[BAR_TILE * 8 + i] = 0x11111111;
    for (u32 y = 0; y < 8; y++)
        dst[LINE_TILE * 8 + y] = y == 3 ? 0x11111111 : 0; // at the height of '-'
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
// position (masks, selection bar and scrollbar) are set up. The display stays off until the menu turns it on.
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
    for (u32 i = 0; i < WIDE_SPRITES_PER_ROW; i++)
    {
        vu16* mask = &OAM_SUB[(MASK_SPRITES + i) * 4];
        mask[0] = 8 | OBJ_WIDE; // row 1
        mask[1] = i * 32 | OBJ_WIDE;
        mask[2] = BAR_TILE | (PALETTE_MASK << 12);
        mask += WIDE_SPRITES_PER_ROW * 4;
        mask[0] = STATUS_ROW * 8 | OBJ_WIDE;
        mask[1] = i * 32 | OBJ_WIDE;
        mask[2] = BAR_TILE | (PALETTE_MASK << 12);
        vu16* bar = &OAM_SUB[(BAR_SPRITES + i) * 4];
        bar[1] = i * 32 | OBJ_WIDE;
        bar[2] = BAR_TILE | (PALETTE_BAR << 12);
    }
    OAM_SUB[THUMB_SPRITE * 4 + 1] = SCROLLBAR_X; // 8x16
    OAM_SUB[THUMB_SPRITE * 4 + 2] = THUMB_TILE | (PALETTE_GRAY << 12);
    for (u32 i = 0; i < SCROLLBAR_TRACK_SPRITES; i++)
    {
        OAM_SUB[(TRACK_SPRITES + i) * 4 + 1] = SCROLLBAR_X | OBJ_SIZE_8X32;
        OAM_SUB[(TRACK_SPRITES + i) * 4 + 2] = TRACK_TILE | (PALETTE_SIGNATURE << 12);
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
    // The previous frame was drawn until the last lines before vblank, so this is the next vblank. Not waiting for its
    // start avoids losing a frame when the work for this frame took a few lines.
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

// Handles the keys in the list. Returns false when the menu is to be closed.
static bool handleListKeys(u32 keys, u32 pressed)
{
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
            showMessageAndWait("Too many cheats on, some skipped");
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
    if (sData->blockSize == 0)
        showMessage("Game not found in usrcheat.dat");

    u32 previousKeys = readKeys();
    while (true)
    {
        if (!sDescriptionOpen)
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
