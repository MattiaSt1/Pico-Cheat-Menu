// In-game cheat menu.
//
// This code is built as a separate position independent blob (see Makefile.cheatmenu). The parasite writes it, with
// the cheat menu data of the game, to the cheat menu file on the sd card (see CheatMenuDefinitions.h). When the
// cheat menu button combination is pressed, the resident part loads it into memory with the game's code, which it
// saves and restores, and calls cheatmenu_entry. The menu runs inside the irq handler (so the game is paused) and is
// drawn on the sub engine.
//
// The menu shows all cheats of the game from the usrcheat.dat cheat database, with their folders. The database
// is read from the sd card using the launcher's relocated DLDI driver; the resident part holds the SDK card lock
// while the menu is open. When the menu is closed after changes, the enabled cheats are written to the cheat list
// that the arm7 cheat engine of Pico Loader runs every vblank, and the enable bits are written back to the file.
//
// To keep the memory usage low, the menu only uploads the font glyphs to VRAM and draws every text cell as an 8x8
// sprite. Since 128 sprites are not enough for a full screen of text, two sets of 32 sprites are used alternately
// for the text rows and are rewritten while the screen is being drawn.
//
// Everything that is touched on the sub engine is saved before and restored afterwards, so the game graphics are
// not affected. Only readable registers are used, since write-only registers could not be restored.

#include <stdbool.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef volatile unsigned char vu8;
typedef volatile unsigned short vu16;
typedef volatile unsigned int vu32;

typedef struct
{
    u32 length;
    u32 opcodes[];
} pload_cheat_t;

typedef struct
{
    u32 length;
    u32 numberOfCheats;
} pload_cheats_t;

#include "CheatMenuDefinitions.h"
#include "../arm7/source/loader/dldiHeader.h"
#include "font.h"

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
// mode normal, OBJ enabled, 1D OBJ mapping with 32 byte boundary, OAM accessible during hblank
#define DISPCNT_MENU            ((1 << 23) | (1 << 16) | (1 << 12) | (1 << 4))

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

#define TEXT_COLUMNS            32
#define TEXT_ROWS               24
#define LIST_FIRST_ROW          2
#define LIST_VISIBLE_ROWS       (TEXT_ROWS - 3)
#define STATUS_ROW              (TEXT_ROWS - 1)
#define NAME_COLUMN             5
#define NAME_VISIBLE_LENGTH     (TEXT_COLUMNS - NAME_COLUMN)
#define SELECTED_NAME_LENGTH    96
#define DESCRIPTION_FIRST_ROW   3
#define DESCRIPTION_ROWS        (STATUS_ROW - DESCRIPTION_FIRST_ROW)

#define FIRST_GLYPH             0x20
#define GLYPH_COUNT             96
#define GLYPH_VRAM_SIZE         (GLYPH_COUNT * 32)

#define REPEAT_DELAY            20
#define REPEAT_RATE             4
#define MARQUEE_PAUSE           60
#define MARQUEE_SPEED           6
#define MESSAGE_FRAMES          120

#define SECTOR_SIZE             CHEAT_MENU_SECTOR_SIZE
#define SECTOR_CACHE_SIZE       4

#define ITEM_FLAG_FOLDER        (1u << 28)
#define ITEM_SIZE_MASK          0x00FFFFFF
#define ITEM_ONE_ONLY_FOLDER    0x11

enum
{
    PALETTE_WHITE,
    PALETTE_GRAY,
    PALETTE_YELLOW,
    PALETTE_GREEN,
    PALETTE_RED,
    PALETTE_CYAN,
    PALETTE_SIGNATURE,
    PALETTE_COUNT
};

// Glyph pixels use color 1 of the sprite palette, the background of a glyph uses color 2.
static const u16 sTextColors[PALETTE_COUNT] =
{
    0x7FFF, // white
    0x4E73, // gray
    0x13BF, // yellow
    0x13E4, // green
    0x109F, // red
    0x7EE0, // cyan
    0x0C63  // signature, barely brighter than the black backdrop
};

// Sprites are only drawn on top of the backdrop, so using the backdrop color looks transparent.
#define BACKDROP_COLOR          0x0000

// Ascii replacements for the latin-1 characters 0xC0-0xFF.
static const char sLatin1ToAscii[64] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";

// defined in entry.s; hidden to get pc-relative instead of GOT access
extern cheat_menu_image_header_t cheatmenu_header __attribute__((visibility("hidden")));
void cheatmenu_cleanDataCache(const void* start, u32 size) __attribute__((visibility("hidden")));
void cheatmenu_invalidateDataCache(const void* start, u32 size) __attribute__((visibility("hidden")));

u32 cheatmenu_main(cheat_menu_resident_t* resident, u32 numberOfCheats);
void cheatmenu_run(void);

// gcc may emit calls to these
void* memset(void* dst, int value, unsigned int count);
void* memcpy(void* dst, const void* src, unsigned int count);

typedef enum
{
    ENTRY_CHEAT,
    ENTRY_FOLDER,
    ENTRY_EXTRA_CHEAT
} EntryType;

typedef struct
{
    u32 item;
    u8 type;
    u8 enabledInFolder;
    char name[NAME_VISIBLE_LENGTH + 1];
} row_t;

static cheat_menu_resident_t* sResident;
static cheat_menu_data_t* sData;
// of the cheat list, including the marker
static u32 sNumberOfCheats;


// navigation
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

// visible rows, read from the database when the list scrolls
static row_t sRows[LIST_VISIBLE_ROWS];
static bool sRowsValid;
static u32 sRowsTop;

static char sSelectedName[SELECTED_NAME_LENGTH + 1];
static u32 sSelectedNameLength;
static u32 sSelectedNameCursor;
static u32 sMarqueeTimer;
static u32 sMarqueeOffset;

static bool sDescriptionOpen;
static bool sDescriptionError;
static u32 sDescriptionStart;
static u32 sDescriptionEnd;
static u32 sDescriptionPageOffset;
static u32 sDescriptionNextPage;
static u32 sDescriptionPage;
static char sDescriptionRows[DESCRIPTION_ROWS][TEXT_COLUMNS + 1];

static const char* sMessage;
static u32 sMessageTimer;

static bool sDatabaseError;

static u8 sSectorCache[SECTOR_CACHE_SIZE][SECTOR_SIZE] __attribute__((aligned(32)));
static u32 sSectorCacheTags[SECTOR_CACHE_SIZE];
static u32 sSectorCacheNext;

static struct
{
    u32 dispCnt;
    u16 powCnt;
    u16 bldCnt;
    u16 masterBright;
    u16 backdrop;
    u8 vramCntD;
    u8 vramCntI;
    u16 objPalette[PALETTE_COUNT][2]; // colors 1 and 2 of each palette
    u16 oam[OAM_ENTRY_COUNT][3]; // attr0-2, attr3 is not touched
    u32 glyphVram[GLYPH_VRAM_SIZE / 4];
} sSaved;

void* memset(void* dst, int value, unsigned int count)
{
    u8* d = (u8*)dst;
    while (count--)
        *d++ = (u8)value;
    return dst;
}

void* memcpy(void* dst, const void* src, unsigned int count)
{
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    while (count--)
        *d++ = *s++;
    return dst;
}

static inline cheat_menu_data_t* getData(void)
{
    return sData;
}

// Returns the part of the cheat menu data at the given offset.
static inline void* getDataPart(u32 offset)
{
    return (u8*)sData + offset;
}

static inline u32 readKeys(void)
{
    u32 keys = ~REG_KEYINPUT & KEY_MASK;
    return keys;
}

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

static void showMessage(const char* message)
{
    sMessage = message;
    sMessageTimer = MESSAGE_FRAMES;
}

// ---- cheat menu file ----

// Writes the given sectors of the cheat menu file.
static bool writeFileSectors(u32 sector, u32 count, const u8* buffer)
{
    const cheat_menu_resident_t* resident = sResident;
    const dldi_header_t* driver = (const dldi_header_t*)resident->dldiDriver;
    for (u32 i = 0; i < resident->fileRunCount && count != 0; i++)
    {
        const cheat_menu_sector_run_t* run = &resident->fileRuns[i];
        if (sector >= run->count)
        {
            sector -= run->count;
            continue;
        }
        u32 n = run->count - sector < count ? run->count - sector : count;
        if (!((dldi_write_func_t)driver->writeSectorsFuncAddress)(run->sector + sector, n, buffer))
            return false;
        buffer += n * SECTOR_SIZE;
        count -= n;
        sector = 0;
    }
    return count == 0;
}

// Writes the enable bits and the position in the list back to the menu image in the file, for the next time
// the menu is opened. The cheat menu data starts at a sector boundary of the image.
static bool savePersistentData(void)
{
    u32 bytes = (getData()->persistentSize + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1);
    cheatmenu_cleanDataCache(getData(), bytes);
    return writeFileSectors(cheatmenu_header.dataOffset / SECTOR_SIZE, bytes / SECTOR_SIZE, (const u8*)getData());
}

// ---- cheat database ----

// Returns the given sector of the block of the game in usrcheat.dat, relative to the first sector of the block.
static const u8* getBlockSector(u32 blockSector)
{
    for (u32 i = 0; i < SECTOR_CACHE_SIZE; i++)
    {
        if (sSectorCacheTags[i] == blockSector + 1)
            return sSectorCache[i];
    }

    const cheat_menu_data_t* data = getData();
    u32 sector = blockSector;
    for (u32 i = 0; i < data->sectorRunCount; i++)
    {
        if (sector < data->sectorRuns[i].count)
        {
            u32 slot = sSectorCacheNext;
            sSectorCacheNext = (slot + 1) % SECTOR_CACHE_SIZE;
            sSectorCacheTags[slot] = 0;
            const dldi_header_t* driver = (const dldi_header_t*)sResident->dldiDriver;
            cheatmenu_cleanDataCache(sSectorCache[slot], SECTOR_SIZE);
            if (!((dldi_read_func_t)driver->readSectorsFuncAddress)(
                    data->sectorRuns[i].sector + sector, 1, sSectorCache[slot]))
            { sDatabaseError = true; return 0; }
            cheatmenu_cleanDataCache(sSectorCache[slot], SECTOR_SIZE);
            cheatmenu_invalidateDataCache(sSectorCache[slot], SECTOR_SIZE);
            sSectorCacheTags[slot] = blockSector + 1;
            return sSectorCache[slot];
        }
        sector -= data->sectorRuns[i].count;
    }
    sDatabaseError = true;
    return 0;
}

static bool readByte(u32 offset, u8* value)
{
    const cheat_menu_data_t* data = getData();
    if (offset >= data->blockSize)
    {
        sDatabaseError = true;
        return false;
    }
    u32 position = offset + data->blockSectorOffset;
    const u8* sector = getBlockSector(position / SECTOR_SIZE);
    if (!sector)
        return false;
    *value = sector[position % SECTOR_SIZE];
    return true;
}

static bool readWord(u32 offset, u32* value)
{
    u32 result = 0;
    for (u32 i = 0; i < 4; i++)
    {
        u8 byte;
        if (!readByte(offset + i, &byte))
            return false;
        result |= byte << (i * 8);
    }
    *value = result;
    return true;
}

// Skips a zero terminated string and returns the offset after it.
static u32 skipString(u32 offset)
{
    u8 c;
    do
    {
        if (!readByte(offset++, &c))
            return offset;
    } while (c != 0);
    return offset;
}

// Reads a zero terminated utf-8 string as ascii and returns its length.
static u32 readString(u32 offset, char* dst, u32 maxLength)
{
    u32 length = 0;
    u8 c;
    while (length < maxLength && readByte(offset++, &c) && c != 0)
    {
        if (c >= 0x80)
        {
            u32 continuationBytes = (c & 0xE0) == 0xC0 ? 1 : ((c & 0xF0) == 0xE0 ? 2 : ((c & 0xF8) == 0xF0 ? 3 : 0));
            u32 codePoint = c & (0x3F >> continuationBytes);
            for (u32 i = 0; i < continuationBytes; i++)
            {
                if (!readByte(offset++, &c))
                    break;
                codePoint = (codePoint << 6) | (c & 0x3F);
            }
            if (continuationBytes == 0)
                continue; // stray continuation byte
            c = codePoint >= 0xC0 && codePoint <= 0xFF ? sLatin1ToAscii[codePoint - 0xC0] : '?';
        }
        dst[length++] = c;
    }
    dst[length] = 0;
    return length;
}

static u32 getItemOffset(u32 item)
{
    const cheat_menu_data_t* data = getData();
    const void* offsets = getDataPart(data->itemOffsetsOffset);
    if (data->itemOffsetShift)
        return ((const u16*)offsets)[item] << data->itemOffsetShift;
    return ((const u32*)offsets)[item];
}

static u32 getItemFlags(u32 item)
{
    u32 flags = 0;
    readWord(getItemOffset(item), &flags);
    return flags;
}

static u32 readItemName(u32 item, char* dst, u32 maxLength)
{
    return readString(getItemOffset(item) + 4, dst, maxLength);
}

// Reads the opcodes of the given cheat item. Returns the number of opcode words, or 0 on failure.
static u32 readCheatOpcodes(u32 item, u32* dst, u32 maxWords)
{
    u32 offset = getItemOffset(item) + 4;
    offset = skipString(offset); // name
    offset = skipString(offset); // note
    offset = (offset + 3) & ~3;
    u32 wordCount;
    if (!readWord(offset, &wordCount) || wordCount == 0 || wordCount > maxWords || (wordCount & 1))
        return 0;
    offset += 4;
    for (u32 i = 0; i < wordCount; i++)
    {
        if (!readWord(offset + i * 4, &dst[i]))
            return 0;
    }
    return wordCount;
}

static bool isEnabled(u32 index)
{
    const u8* bits = (const u8*)getDataPart(getData()->enabledBitsOffset);
    return (bits[index >> 3] >> (index & 7)) & 1;
}

static void setEnabled(u32 index, bool enabled)
{
    u8* bits = (u8*)getDataPart(getData()->enabledBitsOffset) + (index >> 3);
    if (enabled)
        *bits |= 1 << (index & 7);
    else
        *bits &= ~(1 << (index & 7));
}

// ---- cheat preprocessing (see arm7/source/loader/CheatPreprocessor.cpp) ----

typedef struct
{
    u32 a;
    u32 b;
} opcode_t;

static int findOpcode(const opcode_t* opcodes, int start, int count, u32 a, u32 b)
{
    for (int i = start; i < count; i++)
    {
        if (opcodes[i].a == a && opcodes[i].b == b)
            return i;
    }
    return -1;
}

static void setNop(opcode_t* opcode)
{
    opcode->a = 0xD4000000;
    opcode->b = 0;
}

// Replaces the hack starting with the opcode (startA, startB) and ending with (endA, endB) by nops, and modifies
// the first opcode with a == opcodeA in between.
static void patchHack(opcode_t* opcodes, int count, u32 startA, u32 startB, u32 endA, u32 endB, u32 opcodeA,
    u32 orValue, u32 subtractValue)
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

static void preprocessCheat(u32* words, u32 wordCount)
{
    opcode_t* opcodes = (opcode_t*)words;
    int count = wordCount / 2;
    // hacks that modify the 0xD4 handler, replaced by the custom data operations of the cheat engine
    patchHack(opcodes, count, 0x023FE424, 0xE1833004, 0x023FE424, 0xE0833004, 0xD4000000, 1, 0);
    patchHack(opcodes, count, 0x023FE424, 0xE0033004, 0x023FE424, 0xE0833004, 0xD4000000, 2, 0);
    patchHack(opcodes, count, 0x023FE424, 0xE0833004, 0x023FE424, 0xE0833004, 0xD4000000, 0, 0);
    int dbFix = findOpcode(opcodes, 0, count, 0x023FE4D8, 0x0A000003);
    if (dbFix >= 0)
        setNop(&opcodes[dbFix]);
    // make the asm cheats designed for ards work: replace the 0x0E code by 0xC2 in arm mode
    patchHack(opcodes, count, 0x023FE074, 0x012FFF11, 0x023FE074, 0xE3520003, 0xE0000000, 0, 0x1E000000);
}

// ---- enabled cheats ----

// Writes the enabled cheats to the cheat list that the arm7 cheat engine of Pico Loader runs, after the marker and
// up to the resident part. The resident part has paused the engine while the menu is open, and sets the new number
// of cheats afterwards. Returns false if not all enabled cheats fit in the list.
static bool updateActiveCheats(void)
{
    const cheat_menu_data_t* data = getData();
    cheat_menu_list_header_t* list = sResident->list;
    u8* cheats = (u8*)(list + 1);
    u32 capacity = list->marker[5] - sizeof(*list);
    u32 used = 0;
    u32 count = 0;
    bool fits = true;
    for (u32 item = 0; item < data->itemCount; item++)
    {
        if (!isEnabled(item))
            continue;
        pload_cheat_t* cheat = (pload_cheat_t*)(cheats + used);
        u32 maxWords = used + 12 <= capacity ? (capacity - used - 4) / 4 : 0;
        u32 wordCount = readCheatOpcodes(item, cheat->opcodes, maxWords);
        if (wordCount == 0)
        {
            fits = false;
            continue;
        }
        cheat->length = wordCount * 4;
        preprocessCheat(cheat->opcodes, wordCount);
        used += 4 + wordCount * 4;
        count++;
    }

    const u8* extraCheat = (const u8*)getDataPart(data->extraCheatsOffset);
    for (u32 i = 0; i < data->extraCheatCount; i++)
    {
        u32 size = 4 + ((const pload_cheat_t*)extraCheat)->length;
        if (isEnabled(data->itemCount + i))
        {
            if (used + size <= capacity)
            {
                memcpy(cheats + used, extraCheat, size);
                used += size;
                count++;
            }
            else
            {
                fits = false;
            }
        }
        extraCheat += size;
    }

    cheatmenu_cleanDataCache(cheats, used);
    // the marker stays the first cheat
    sNumberOfCheats = 1 + count;
    return fits;
}

// ---- list entries ----

static u32 getEntryCount(void)
{
    const cheat_menu_data_t* data = getData();
    return sInFolder ? sFolderChildCount : data->rootCount + data->extraCheatCount;
}

// Returns the item (or for extra cheats itemCount + extra cheat index) of the given entry of the current list.
static u32 getEntryItem(u32 entry)
{
    const cheat_menu_data_t* data = getData();
    if (sInFolder)
        return sFolderItem + 1 + entry;
    if (entry < data->rootCount)
        return ((const u16*)getDataPart(data->rootItemsOffset))[entry];
    return data->itemCount + entry - data->rootCount;
}

// Formats a decimal number without using division.
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
    const cheat_menu_data_t* data = getData();
    row->item = getEntryItem(entry);
    row->enabledInFolder = 0;
    if (row->item >= data->itemCount)
    {
        row->type = ENTRY_EXTRA_CHEAT;
        getExtraCheatName(row->item - data->itemCount, row->name);
        return;
    }
    u32 flags = getItemFlags(row->item);
    row->type = (flags & ITEM_FLAG_FOLDER) ? ENTRY_FOLDER : ENTRY_CHEAT;
    if (row->type == ENTRY_FOLDER)
        row->enabledInFolder = countEnabledInFolder(row->item, flags & ITEM_SIZE_MASK) != 0;
    readItemName(row->item, row->name, NAME_VISIBLE_LENGTH);
}

static void updateRows(void)
{
    if (sRowsValid && sRowsTop == sListTop)
        return;
    u32 entryCount = getEntryCount();
    for (u32 i = 0; i < LIST_VISIBLE_ROWS && sListTop + i < entryCount; i++)
        loadRow(&sRows[i], sListTop + i);
    sRowsTop = sListTop;
    sRowsValid = true;
}

static void updateSelectedName(void)
{
    if (sSelectedNameCursor == sCursor + 1)
        return;
    sSelectedNameCursor = sCursor + 1;
    sMarqueeTimer = 0;
    sMarqueeOffset = 0;
    sSelectedNameLength = 0;
    sSelectedName[0] = 0;
    if (getEntryCount() == 0)
        return;
    u32 item = getEntryItem(sCursor);
    if (item >= getData()->itemCount)
        getExtraCheatName(item - getData()->itemCount, sSelectedName);
    else
        readItemName(item, sSelectedName, SELECTED_NAME_LENGTH);
    u32 length = 0;
    while (sSelectedName[length] != 0)
        length++;
    sSelectedNameLength = length;
}

static void updateMarquee(void)
{
    if (sSelectedNameLength <= NAME_VISIBLE_LENGTH)
        return;
    u32 maxOffset = sSelectedNameLength - NAME_VISIBLE_LENGTH;
    sMarqueeTimer++;
    if (sMarqueeOffset == 0 || sMarqueeOffset == maxOffset)
    {
        if (sMarqueeTimer < MARQUEE_PAUSE)
            return;
        if (sMarqueeOffset == maxOffset)
        {
            sMarqueeOffset = 0;
            sMarqueeTimer = 0;
            return;
        }
    }
    if (sMarqueeTimer >= MARQUEE_SPEED)
    {
        sMarqueeTimer = 0;
        sMarqueeOffset++;
    }
}

// ---- navigation ----

static void setCursor(int cursor)
{
    u32 entryCount = getEntryCount();
    if (cursor >= (int)entryCount)
        cursor = entryCount - 1;
    if (cursor < 0)
        cursor = 0;
    sCursor = cursor;
    if (sCursor < sListTop)
        sListTop = sCursor;
    else if (sCursor >= sListTop + LIST_VISIBLE_ROWS)
        sListTop = sCursor - LIST_VISIBLE_ROWS + 1;
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
}

static void closeFolder(void)
{
    sInFolder = false;
    sCursor = sRootCursor;
    sListTop = sRootTop;
    sRowsValid = false;
    sSelectedNameCursor = 0;
}

static void toggleEntry(void)
{
    if (getEntryCount() == 0)
        return;
    u32 item = getEntryItem(sCursor);
    if (item < getData()->itemCount && (getItemFlags(item) & ITEM_FLAG_FOLDER))
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

static void disableAll(void)
{
    const cheat_menu_data_t* data = getData();
    if (sInFolder)
    {
        for (u32 i = 0; i < sFolderChildCount; i++)
            setEnabled(sFolderItem + 1 + i, false);
    }
    else
    {
        for (u32 i = 0; i < data->itemCount + data->extraCheatCount; i++)
            setEnabled(i, false);
    }
    sRowsValid = false;
    sChanged = true;
    showMessage(sInFolder ? "All cheats in this folder off" : "All cheats off");
}

// Reads and wraps notes outside the scanline-timed renderer.
#include "description.inc"

// ---- drawing ----

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

static void composeHeader(char* chars, u8* palettes)
{
    if (sInFolder)
        putString(chars, palettes, 1, sFolderName, TEXT_COLUMNS - 1, PALETTE_CYAN);
    else
        putString(chars, palettes, 1, "Cheat Menu", TEXT_COLUMNS, PALETTE_YELLOW);
}

static void composeStatus(char* chars, u8* palettes)
{
    if (sMessage)
    {
        putString(chars, palettes, 1, sMessage, TEXT_COLUMNS - 1, PALETTE_YELLOW);
        return;
    }
    static const char sSignature[] = "By Esentasse";
    putString(chars, palettes, TEXT_COLUMNS - (sizeof(sSignature) - 1), sSignature, sizeof(sSignature) - 1, PALETTE_SIGNATURE);
}

// Builds the text and colors of the given list row.
static void composeListRow(u32 index, char* chars, u8* palettes)
{
    u32 entry = sListTop + index;
    if (entry >= getEntryCount())
        return;
    const row_t* row = &sRows[index];
    bool selected = entry == sCursor;
    if (selected)
        putString(chars, palettes, 0, ">", 1, PALETTE_YELLOW);
    if (row->type == ENTRY_FOLDER)
    {
        putString(chars, palettes, 1, "[+]", 3, row->enabledInFolder ? PALETTE_GREEN : PALETTE_GRAY);
        putString(chars, palettes, NAME_COLUMN, selected ? &sSelectedName[sMarqueeOffset] : row->name,
            NAME_VISIBLE_LENGTH, PALETTE_CYAN);
    }
    else
    {
        bool enabled = isEnabled(row->item);
        putString(chars, palettes, 1, enabled ? "ON" : "OFF", 3, enabled ? PALETTE_GREEN : PALETTE_RED);
        putString(chars, palettes, NAME_COLUMN, selected ? &sSelectedName[sMarqueeOffset] : row->name,
            NAME_VISIBLE_LENGTH, PALETTE_WHITE);
    }
}

static void composeRow(u32 row, char* chars, u8* palettes)
{
    for (u32 i = 0; i < TEXT_COLUMNS; i++)
    {
        chars[i] = ' ';
        palettes[i] = PALETTE_WHITE;
    }
    if (sDescriptionOpen)
    {
        if (row == 0)
            putString(chars, palettes, 1, "Description", TEXT_COLUMNS - 1, PALETTE_YELLOW);
        else if (row == 1)
            putString(chars, palettes, 0, &sSelectedName[sMarqueeOffset], TEXT_COLUMNS, PALETTE_CYAN);
        else if (row == 2)
            putString(chars, palettes, 0, "--------------------------------", TEXT_COLUMNS, PALETTE_GRAY);
        else if (row >= DESCRIPTION_FIRST_ROW && row < STATUS_ROW)
            putString(chars, palettes, 0, sDescriptionRows[row - DESCRIPTION_FIRST_ROW], TEXT_COLUMNS, PALETTE_WHITE);
        else if (row == STATUS_ROW)
        {
            char page[16];
            u32 length = formatNumber(page, sDescriptionPage);
            putString(chars, palettes, 1, "Page ", 5, PALETTE_GRAY);
            putString(chars, palettes, 6, page, length, PALETTE_GRAY);
            if (sDescriptionNextPage)
                putString(chars, palettes, 7 + length, ">", 1, PALETTE_CYAN);
        }
        return;
    }
    if (row == 0)
        composeHeader(chars, palettes);
    else if (row == 1)
        putString(chars, palettes, 0, "--------------------------------", TEXT_COLUMNS, PALETTE_GRAY);
    else if (row == STATUS_ROW)
        composeStatus(chars, palettes);
    else
        composeListRow(row - LIST_FIRST_ROW, chars, palettes);
}

// Writes the given text row to the given set of 32 sprites.
static void drawRow(u32 row, u32 spriteSet)
{
    char chars[TEXT_COLUMNS];
    u8 palettes[TEXT_COLUMNS];
    composeRow(row, chars, palettes);
    vu16* oam = &OAM_SUB[spriteSet * TEXT_COLUMNS * 4];
    for (u32 column = 0; column < TEXT_COLUMNS; column++, oam += 4)
    {
        u32 glyph = (u8)chars[column];
        if (glyph == ' ')
        {
            oam[0] = 1 << 9; // disabled
            continue;
        }
        if (glyph < FIRST_GLYPH || glyph >= FIRST_GLYPH + GLYPH_COUNT)
            glyph = '?';
        oam[1] = column * 8; // 8x8
        oam[2] = (glyph - FIRST_GLYPH) | (palettes[column] << 12);
        oam[0] = row * 8; // square, 16 colors
    }
}

// Draws one frame of the menu. Must be called in vblank.
static void drawFrame(void)
{
    // the first two rows are drawn in vblank, the next rows are drawn
    // in the sprite set of the row before the current row on the screen
    drawRow(0, 0);
    drawRow(1, 1);
    while (REG_VCOUNT >= 192);
    for (u32 row = 2; row < TEXT_ROWS; row++)
    {
        while (REG_VCOUNT < (row - 1) * 8);
        drawRow(row, row & 1);
    }
}

static void uploadGlyphs(void)
{
    vu32* dst = VRAM_SUB_OBJ;
    for (u32 i = 0; i < GLYPH_COUNT * 8; i++)
    {
        u32 bits = sFont[i];
        u32 line = 0;
        for (u32 x = 0; x < 8; x++)
            line |= ((bits & (1 << x)) ? 1 : 2) << (x * 4);
        dst[i] = line;
    }
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
    {
        sSaved.objPalette[i][0] = PALETTE_OBJ_SUB[i * 16 + 1];
        sSaved.objPalette[i][1] = PALETTE_OBJ_SUB[i * 16 + 2];
    }
    for (u32 i = 0; i < OAM_ENTRY_COUNT; i++)
    {
        for (u32 j = 0; j < 3; j++)
            sSaved.oam[i][j] = OAM_SUB[i * 4 + j];
    }
    REG_VRAMCNT_I = VRAMCNT_LCDC;
    copyWords(sSaved.glyphVram, VRAM_LCDC_I, GLYPH_VRAM_SIZE);
}

static void restoreSubEngineState(void)
{
    REG_DISPCNT_SUB = DISPCNT_FORCED_BLANK;
    REG_VRAMCNT_I = VRAMCNT_LCDC;
    copyWords(VRAM_LCDC_I, sSaved.glyphVram, GLYPH_VRAM_SIZE);
    REG_VRAMCNT_I = sSaved.vramCntI;
    REG_VRAMCNT_D = sSaved.vramCntD;
    for (u32 i = 0; i < OAM_ENTRY_COUNT; i++)
    {
        for (u32 j = 0; j < 3; j++)
            OAM_SUB[i * 4 + j] = sSaved.oam[i][j];
    }
    for (u32 i = 0; i < PALETTE_COUNT; i++)
    {
        PALETTE_OBJ_SUB[i * 16 + 1] = sSaved.objPalette[i][0];
        PALETTE_OBJ_SUB[i * 16 + 2] = sSaved.objPalette[i][1];
    }
    PALETTE_BG_SUB[0] = sSaved.backdrop;
    REG_MASTER_BRIGHT_SUB = sSaved.masterBright;
    REG_BLDCNT_SUB = sSaved.bldCnt;
    REG_POWCNT = sSaved.powCnt;
    REG_DISPCNT_SUB = sSaved.dispCnt;
}

static void setupSubEngine(void)
{
    REG_DISPCNT_SUB = DISPCNT_FORCED_BLANK;
    REG_POWCNT = sSaved.powCnt | POWCNT_LCD | POWCNT_2D_SUB;
    if ((sSaved.vramCntD & 0x87) == VRAMCNT_D_SUB_OBJ)
    {
        // bank D would overlap with bank I
        REG_VRAMCNT_D = VRAMCNT_LCDC;
    }
    REG_VRAMCNT_I = VRAMCNT_I_SUB_OBJ;
    REG_BLDCNT_SUB = 0;
    REG_MASTER_BRIGHT_SUB = 0;
    PALETTE_BG_SUB[0] = BACKDROP_COLOR;
    for (u32 i = 0; i < PALETTE_COUNT; i++)
    {
        PALETTE_OBJ_SUB[i * 16 + 1] = sTextColors[i];
        PALETTE_OBJ_SUB[i * 16 + 2] = BACKDROP_COLOR;
    }
    uploadGlyphs();
    for (u32 i = 0; i < OAM_ENTRY_COUNT; i++)
        OAM_SUB[i * 4] = 1 << 9; // disabled
}

// Shows the current state and runs one frame; to be called in loops that wait for something.
static void presentFrame(void)
{
    updateMarquee();
    if (sMessageTimer != 0 && --sMessageTimer == 0)
        sMessage = 0;
    waitVBlank();
    drawFrame();
}

void cheatmenu_run(void)
{
    cheat_menu_data_t* data = getData();
    sDescriptionOpen = false;

    waitVBlank();
    saveSubEngineState();
    setupSubEngine();
    REG_DISPCNT_SUB = DISPCNT_MENU;

    sDatabaseError = false;
    sChanged = false;
    sRowsValid = false;
    sSelectedNameCursor = 0;
    sCursor = data->rootCursor;
    sListTop = data->rootTop;
    setCursor(sCursor);
    if (data->blockSize == 0)
        showMessage("Game not found in usrcheat.dat");

    u32 previousKeys = readKeys();
    u32 repeatCounter = 0;
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

        if (sDescriptionOpen && (pressed & (KEY_B | KEY_SELECT)))
        {
            sDescriptionOpen = false;
            continue;
        }
        if (!sDescriptionOpen && (pressed & KEY_SELECT))
        {
            openDescription();
            continue;
        }
        if (pressed & KEY_B)
        {
            if (sInFolder)
            {
                closeFolder();
                continue;
            }
            break;
        }

        int move = 0;
        if (keys & (KEY_UP | KEY_DOWN))
        {
            if (pressed & (KEY_UP | KEY_DOWN))
            {
                repeatCounter = REPEAT_DELAY;
                move = (keys & KEY_UP) ? -1 : 1;
            }
            else if (--repeatCounter == 0)
            {
                repeatCounter = REPEAT_RATE;
                move = (keys & KEY_UP) ? -1 : 1;
            }
        }
        if (pressed & KEY_LEFT)
            move = -LIST_VISIBLE_ROWS;
        else if (pressed & KEY_RIGHT)
            move = LIST_VISIBLE_ROWS;

        if (sDescriptionOpen)
        {
            if (move != 0)
                moveDescriptionPage(move > 0);
            continue;
        }

        if (move != 0)
        {
            // wrap around at the ends of the list for single steps
            int cursor = (int)sCursor + move;
            int entryCount = getEntryCount();
            if (move == -1 && cursor < 0)
                cursor = entryCount - 1;
            else if (move == 1 && cursor >= entryCount)
                cursor = 0;
            setCursor(cursor);
        }

        if (pressed & KEY_A)
            toggleEntry();
        else if (pressed & KEY_START)
            disableAll();
    }

    u32 rootCursor = sInFolder ? sRootCursor : sCursor;
    u32 rootTop = sInFolder ? sRootTop : sListTop;
    bool moved = rootCursor != data->rootCursor || rootTop != data->rootTop;
    data->rootCursor = rootCursor;
    data->rootTop = rootTop;
    if (sChanged)
    {
        sMessage = "Applying cheats...";
        presentFrame();
        if (!updateActiveCheats())
        {
            showMessage("Too many cheats on, some skipped");
            while (sMessageTimer != 0)
                presentFrame();
        }
        sMessage = 0;
    }
    if ((sChanged || moved) && !savePersistentData())
    {
        showMessage("Error writing to the sd card");
        while (sMessageTimer != 0)
            presentFrame();
    }

    // wait for all keys to be released, to prevent the game from reacting to them
    while (readKeys() != 0)
        presentFrame();

    waitVBlank();
    restoreSubEngineState();
}

u32 cheatmenu_main(cheat_menu_resident_t* resident, u32 numberOfCheats)
{
    sResident = resident;
    sData = (cheat_menu_data_t*)((u8*)&cheatmenu_header + cheatmenu_header.dataOffset);
    sNumberOfCheats = numberOfCheats;
    cheatmenu_run();
    return sNumberOfCheats;
}
