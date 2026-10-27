#include "common.h"
#include <string.h>
#include "CheatDatabase.h"

// Format of usrcheat.dat (see for example the cheat parser of Wood R4):
// - The file starts with "R4 CheatCode", followed at 0x100 by an index of 16-byte entries (game code, crc32 of the
//   rom header, u64 offset of the game's block), terminated by an entry with offset 0.
// - The block of a game consists of the game title, the number of items (low 28 bits), 8 master code words and the
//   items. Strings are padded to 4 bytes from the start of the block.
// - An item is either a folder (bit 28 set, the low 24 bits are the number of cheats in the folder), followed by its
//   name and note, or a cheat (the low 24 bits are the size in words of the rest of the item), followed by its name,
//   note, padding to 4 bytes, the number of opcode words and the opcode words. Folders are not nested; the cheats of
//   a folder directly follow it.

#define USRCHEAT_INDEX_OFFSET   0x100
#define SECTOR_SIZE             CHEAT_MENU_SECTOR_SIZE

#define ITEM_FLAG_FOLDER        (1u << 28)
#define ITEM_SIZE_MASK          0x00FFFFFF

#define MAX_ITEMS               0xFFFF
#define MAX_CLUSTER_MAP_LENGTH  (1 + 2 * 128 + 1)

static u32 crc32(const u8* data, u32 length)
{
    u32 crc = 0xFFFFFFFF;
    for (u32 i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc;
}

static inline u32 align4(u32 value)
{
    return (value + 3) & ~3;
}

bool CheatDatabase::ReadByte(u8& value)
{
    if (_blockSize && (_position < _blockOffset || _position - _blockOffset >= _blockSize)) return false;
    if (_position < _bufferOffset || _position >= _bufferOffset + _bufferLength)
    {
        UINT bytesRead = 0;
        if (f_lseek(_file, _position) != FR_OK ||
            f_read(_file, _buffer, BUFFER_SIZE, &bytesRead) != FR_OK ||
            bytesRead == 0)
        {
            return false;
        }
        _bufferOffset = _position;
        _bufferLength = bytesRead;
    }
    value = _buffer[_position++ - _bufferOffset];
    return true;
}

bool CheatDatabase::ReadU32(u32& value)
{
    u32 result = 0;
    for (int i = 0; i < 4; i++)
    {
        u8 byte;
        if (!ReadByte(byte))
        {
            return false;
        }
        result |= (u32)byte << (i * 8);
    }
    value = result;
    return true;
}

bool CheatDatabase::SkipString()
{
    u8 c;
    do
    {
        if (!ReadByte(c))
        {
            return false;
        }
    } while (c != 0);
    return true;
}

bool CheatDatabase::CompareOpcodes(const pload_cheat_t* cheat)
{
    const u32* opcodes = (const u32*)cheat->opcodes;
    for (u32 i = 0; i < cheat->length / 4; i++)
    {
        u32 value;
        if (!ReadU32(value) || value != opcodes[i])
        {
            return false;
        }
    }
    return true;
}

bool CheatDatabase::FindGame(u32 gameCode, u32 crc, u32& gameOffset, u32& nextGameOffset)
{
    char signature[12];
    Seek(0);
    for (u32 i = 0; i < sizeof(signature); i++)
    {
        if (!ReadByte((u8&)signature[i]))
        {
            return false;
        }
    }
    if (memcmp(signature, "R4 CheatCode", sizeof(signature)) != 0)
    {
        return false;
    }

    u32 entryOffset = USRCHEAT_INDEX_OFFSET;
    while (true)
    {
        u32 entryGameCode, entryCrc, entryOffsetLow, entryOffsetHigh;
        Seek(entryOffset);
        if (!ReadU32(entryGameCode) || !ReadU32(entryCrc) || !ReadU32(entryOffsetLow) || !ReadU32(entryOffsetHigh))
        {
            return false;
        }
        if (entryOffsetLow == 0)
        {
            return false;
        }
        if (entryGameCode == gameCode && entryCrc == crc)
        {
            gameOffset = entryOffsetLow;
            Seek(entryOffset + 16 + 8);
            u32 nextOffset;
            if (!ReadU32(nextOffset) || nextOffset == 0)
            {
                nextOffset = f_size(_file);
            }
            nextGameOffset = nextOffset;
            return nextGameOffset > gameOffset;
        }
        entryOffset += 16;
    }
}

// Strings are padded to 4 bytes relative to the start of the game's block.
u32 CheatDatabase::AlignInBlock(u32 position) const
{
    return _blockOffset + align4(position - _blockOffset);
}

void CheatDatabase::SetEnabled(u32 index)
{
    _enabledBits[index >> 3] |= 1 << (index & 7);
}

void CheatDatabase::MatchLauncherCheats(u32 itemIndex, u32 opcodeWordCount, const pload_cheats_t* launcherCheats)
{
    if (!launcherCheats)
    {
        return;
    }
    u32 opcodesStart = GetPosition();
    auto cheat = &launcherCheats->firstCheat;
    for (u32 i = 0; i < launcherCheats->numberOfCheats; i++)
    {
        if (!_launcherCheatMatched[i] && cheat->length == opcodeWordCount * 4)
        {
            Seek(opcodesStart);
            if (CompareOpcodes(cheat))
            {
                _launcherCheatMatched[i] = true;
                _matchedLauncherCheatCount++;
                SetEnabled(itemIndex);
                return;
            }
        }
        cheat = (const pload_cheat_t*)((const u8*)cheat + sizeof(u32) + cheat->length);
    }
}

bool CheatDatabase::IndexGame(const pload_cheats_t* launcherCheats)
{
    Seek(_blockOffset);
    if (!SkipString())
    {
        return false;
    }
    Seek(AlignInBlock(GetPosition()));
    u32 itemCount;
    if (!ReadU32(itemCount))
    {
        return false;
    }
    itemCount &= 0x0FFFFFFF;
    if (itemCount == 0 || itemCount > MAX_ITEMS)
    {
        return false;
    }
    Seek(GetPosition() + 8 * 4); // skip master codes

    _itemOffsets.reset((u32*)malloc(itemCount * sizeof(u32)));
    _rootItems.reset((u16*)malloc(itemCount * sizeof(u16)));
    // The enable bits have room for the maximum number of extra cheats; WriteImageData copies the used part.
    u32 bitBytes = (itemCount + _launcherCheatCount + 7) / 8;
    _enabledBits.reset((u8*)malloc(bitBytes));
    if (!_itemOffsets || !_rootItems || !_enabledBits) return false;
    memset(_enabledBits.get(), 0, bitBytes);
    u32 blockEnd = _blockOffset + _blockSize;
    u32 item = 0;
    while (item < itemCount)
    {
        u32 itemStart = GetPosition();
        u32 flags;
        if (itemStart >= blockEnd || !ReadU32(flags))
        {
            return false;
        }
        _rootItems[_rootCount++] = item;
        u32 cheatsInItem = 1;
        if (flags & ITEM_FLAG_FOLDER)
        {
            _itemOffsets[item++] = itemStart - _blockOffset;
            cheatsInItem = flags & ITEM_SIZE_MASK;
            if (cheatsInItem > itemCount - item) return false;
            if (!SkipString() || !SkipString())
            {
                return false;
            }
            Seek(AlignInBlock(GetPosition()));
        }
        else
        {
            // A cheat outside of a folder, parsed below.
            Seek(itemStart);
        }

        for (u32 i = 0; i < cheatsInItem && item < itemCount; i++)
        {
            u32 cheatStart = GetPosition();
            if (cheatStart >= blockEnd || !ReadU32(flags))
            {
                return false;
            }
            _itemOffsets[item] = cheatStart - _blockOffset;
            u32 cheatEnd = cheatStart + ((flags & ITEM_SIZE_MASK) + 1) * 4;
            if ((flags & ITEM_FLAG_FOLDER) || cheatEnd <= cheatStart || cheatEnd > blockEnd) return false;
            if (!SkipString() || !SkipString())
            {
                return false;
            }
            Seek(AlignInBlock(GetPosition()));
            u32 opcodeWordCount;
            if (!ReadU32(opcodeWordCount))
            {
                return false;
            }
            if ((opcodeWordCount & 1) || GetPosition() > cheatEnd || opcodeWordCount > (cheatEnd - GetPosition()) / 4)
                return false;
            MatchLauncherCheats(item, opcodeWordCount, launcherCheats);
            _allCheatsSize += sizeof(u32) + opcodeWordCount * 4;
            Seek(cheatEnd);
            item++;
            _cheatCount++;
        }
    }
    _itemCount = itemCount;
    return true;
}

bool CheatDatabase::CreateSectorRuns()
{
    std::unique_ptr<DWORD[], FreeArray> clusterMap((DWORD*)malloc(MAX_CLUSTER_MAP_LENGTH * sizeof(DWORD)));
    if (!clusterMap) return false;
    clusterMap[0] = MAX_CLUSTER_MAP_LENGTH;
    _file->cltbl = clusterMap.get();
    FRESULT result = f_lseek(_file, CREATE_LINKMAP);
    _file->cltbl = nullptr;
    if (result != FR_OK)
    {
        return false;
    }

    const FATFS* fs = _file->obj.fs;
    u32 sectorsPerCluster = fs->csize;
    u32 firstSector = _blockOffset / SECTOR_SIZE;
    u32 endSector = (_blockOffset + _blockSize + SECTOR_SIZE - 1) / SECTOR_SIZE;
    u32 fileSector = 0;
    for (const DWORD* fragment = &clusterMap[1]; fragment[0] != 0; fragment += 2)
    {
        u32 fragmentSectors = fragment[0] * sectorsPerCluster;
        u32 fragmentFirstSector = fs->database + (fragment[1] - 2) * sectorsPerCluster;
        u32 start = firstSector > fileSector ? firstSector : fileSector;
        u32 end = endSector < fileSector + fragmentSectors ? endSector : fileSector + fragmentSectors;
        if (start < end)
        {
            u32 sector = fragmentFirstSector + (start - fileSector);
            u32 count = end - start;
            if (_sectorRunCount > 0 &&
                _sectorRuns[_sectorRunCount - 1].sector + _sectorRuns[_sectorRunCount - 1].count == sector)
            {
                _sectorRuns[_sectorRunCount - 1].count += count;
            }
            else if (_sectorRunCount < CHEAT_MENU_MAX_SECTOR_RUNS)
            {
                _sectorRuns[_sectorRunCount].sector = sector;
                _sectorRuns[_sectorRunCount].count = count;
                _sectorRunCount++;
            }
            else
            {
                return false;
            }
        }
        fileSector += fragmentSectors;
        if (fileSector >= endSector)
        {
            break;
        }
    }
    return true;
}

void CheatDatabase::Load(const void* romHeader, const pload_cheats_t* launcherCheats, u32 maxActiveCheatsSize)
{
    _launcherCheatCount = launcherCheats ? launcherCheats->numberOfCheats : 0;
    _launcherCheatMatched.reset((bool*)malloc((_launcherCheatCount + 1) * sizeof(bool)));
    if (!_launcherCheatMatched) return;
    memset(_launcherCheatMatched.get(), 0, (_launcherCheatCount + 1) * sizeof(bool));

    auto file = std::make_unique<FIL>();
    if (f_open(file.get(), PARASITE_USRCHEAT_PATH, FA_OPEN_EXISTING | FA_READ) == FR_OK)
    {
        _file = file.get();
        u32 gameCode = *(const u32*)((const u8*)romHeader + 0xC);
        // usrcheat.dat stores the crc32 of the first 512 bytes of the rom header, without the final inversion.
        u32 crc = crc32((const u8*)romHeader, 512);
        u32 gameOffset, nextGameOffset;
        if (FindGame(gameCode, crc, gameOffset, nextGameOffset))
        {
            _blockOffset = gameOffset;
            _blockSize = nextGameOffset - gameOffset;
            if (!IndexGame(launcherCheats) || !CreateSectorRuns())
            {
                _blockSize = 0;
                _sectorRunCount = 0;
                _itemCount = 0;
                _cheatCount = 0;
                _rootCount = 0;
                _allCheatsSize = 0;
                _matchedLauncherCheatCount = 0;
                for (u32 i = 0; i < _launcherCheatCount; i++)
                {
                    _launcherCheatMatched[i] = false;
                }
            }
        }
        f_close(_file);
        _file = nullptr;
    }

    if (_itemCount == 0)
    {
        u32 bitBytes = (_launcherCheatCount + 7) / 8 + 1;
        _enabledBits.reset((u8*)malloc(bitBytes));
        if (!_enabledBits) return;
        memset(_enabledBits.get(), 0, bitBytes);
    }

    // The launcher cheats that are not in the database are shown as extra cheats.
    _extraCheatCount = 0;
    _extraCheatsSize = 0;
    if (launcherCheats)
    {
        auto cheat = &launcherCheats->firstCheat;
        for (u32 i = 0; i < launcherCheats->numberOfCheats; i++)
        {
            if (!_launcherCheatMatched[i])
            {
                SetEnabled(_itemCount + _extraCheatCount);
                _extraCheatCount++;
                _extraCheatsSize += sizeof(u32) + cheat->length;
            }
            cheat = (const pload_cheat_t*)((const u8*)cheat + sizeof(u32) + cheat->length);
        }
    }

    // Room for all cheats of the game turned on together, up to the limit. Most games need far less than the
    // limit; only games with hundreds of cheats can run out of room, which the menu reports.
    u32 allCheats = _allCheatsSize + _extraCheatsSize;
    _activeCheatsSize = allCheats < maxActiveCheatsSize ? allCheats : maxActiveCheatsSize;
    if (launcherCheats && launcherCheats->length - 8 > _activeCheatsSize)
        _activeCheatsSize = launcherCheats->length - 8;
}

CheatDatabase::DataLayout CheatDatabase::GetDataLayout() const
{
    DataLayout layout;
    // The enable bits directly follow the struct, so that the menu can write both back together.
    u32 offset = align4(sizeof(cheat_menu_data_t));
    layout.enabledBitsOffset = offset;
    offset += align4((_itemCount + _extraCheatCount + 7) / 8);
    layout.persistentSize = offset;
    layout.itemOffsetsOffset = offset;
    offset += align4(_itemCount * (_blockSize < 0x40000 ? sizeof(u16) : sizeof(u32)));
    layout.rootItemsOffset = offset;
    offset += align4(_rootCount * sizeof(u16));
    layout.extraCheatsOffset = offset;
    offset += align4(_extraCheatsSize);
    layout.totalSize = offset;
    return layout;
}

u32 CheatDatabase::GetImageDataSize() const
{
    return GetDataLayout().totalSize;
}

void CheatDatabase::WriteImageData(cheat_menu_data_t* data, const pload_cheats_t* launcherCheats) const
{
    DataLayout layout = GetDataLayout();
    u8* base = (u8*)data;
    memset(data, 0, layout.totalSize);

    data->persistentSize = layout.persistentSize;
    data->enabledBitsOffset = layout.enabledBitsOffset;
    memcpy(base + layout.enabledBitsOffset, _enabledBits.get(), (_itemCount + _extraCheatCount + 7) / 8);

    data->blockSize = _blockSize;
    data->blockSectorOffset = _blockOffset % SECTOR_SIZE;
    data->sectorRunCount = _sectorRunCount;
    memcpy(data->sectorRuns, _sectorRuns, sizeof(_sectorRuns));

    data->itemCount = _itemCount;
    data->itemOffsetsOffset = layout.itemOffsetsOffset;
    if (_blockSize < 0x40000)
    {
        data->itemOffsetShift = 2;
        u16* itemOffsets = (u16*)(base + layout.itemOffsetsOffset);
        for (u32 i = 0; i < _itemCount; i++)
        {
            itemOffsets[i] = _itemOffsets[i] >> 2;
        }
    }
    else
    {
        data->itemOffsetShift = 0;
        memcpy(base + layout.itemOffsetsOffset, _itemOffsets.get(), _itemCount * sizeof(u32));
    }

    data->rootCount = _rootCount;
    data->rootItemsOffset = layout.rootItemsOffset;
    memcpy(base + layout.rootItemsOffset, _rootItems.get(), _rootCount * sizeof(u16));

    // The launcher cheats that are not in the database.
    data->extraCheatCount = _extraCheatCount;
    data->extraCheatsOffset = layout.extraCheatsOffset;
    if (!launcherCheats)
    {
        return;
    }
    u8* extraCheats = base + layout.extraCheatsOffset;
    auto cheat = &launcherCheats->firstCheat;
    for (u32 i = 0; i < _launcherCheatCount; i++)
    {
        u32 cheatSize = sizeof(u32) + cheat->length;
        if (!_launcherCheatMatched[i])
        {
            memcpy(extraCheats, cheat, cheatSize);
            extraCheats += cheatSize;
        }
        cheat = (const pload_cheat_t*)((const u8*)cheat + cheatSize);
    }
}
