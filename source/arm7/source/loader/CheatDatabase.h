#pragma once
#include "ParasiteDefinitions.h"
#include <memory>
#include <stdlib.h>
#include "../../include/picoLoader7.h"
#include "CheatMenuDefinitions.h"

/// @brief Class for preparing the data of the in-game cheat menu from the usrcheat.dat cheat database.
///        The database itself stays on the sd card; this class builds an index of the items of the game
///        and determines the sd sectors of the game's block, so that the menu can read it while the game runs.
///        The index is part of the menu image in the cheat menu file.
class CheatDatabase
{
public:
    /// @brief Looks up the game in the cheat database and indexes its cheats.
    /// @param romHeader The first 512 bytes of the rom header, used to identify the game.
    /// @param launcherCheats The cheats passed by the launcher, or \c nullptr. Must not be preprocessed yet,
    ///                       since the opcodes are compared with the database. These cheats are enabled initially.
    /// @param maxActiveCheatsSize Upper limit for the room for enabled cheats. The room is only as large as all
    ///        cheats of the game together, and never smaller than the launcher cheats.
    void Load(const void* romHeader, const pload_cheats_t* launcherCheats, u32 maxActiveCheatsSize);

    bool HasCheats() const { return _itemCount != 0 || _extraCheatCount != 0; }

    bool IsGameInDatabase() const { return _blockSize != 0; }

    u32 GetItemCount() const { return _itemCount; }

    /// @brief Returns the number of cheats in the menu: the cheats of the game, without its folders, and the extra cheats.
    u32 GetCheatCount() const { return _cheatCount + _extraCheatCount; }

    u32 GetMatchedLauncherCheatCount() const { return _matchedLauncherCheatCount; }

    /// @brief Returns the room for enabled cheats in the cheat list (pload_cheat_t entries, without the list header).
    u32 GetActiveCheatsSize() const { return _activeCheatsSize; }

    u32 GetImageDataSize() const;

    /// @brief Writes the cheat menu data of the menu image.
    /// @param data The destination, \see GetImageDataSize bytes.
    /// @param launcherCheats The preprocessed launcher cheats, or \c nullptr. The ones that are not in the database
    ///                       are shown as extra cheats.
    void WriteImageData(cheat_menu_data_t* data, const pload_cheats_t* launcherCheats) const;

private:
    struct FreeArray { void operator()(void* pointer) const { free(pointer); } };
    static constexpr u32 BUFFER_SIZE = 512;

    struct DataLayout
    {
        u32 enabledBitsOffset;
        u32 persistentSize;
        u32 itemOffsetsOffset;
        u32 rootItemsOffset;
        u32 extraCheatsOffset;
        u32 totalSize;
    };

    FIL* _file = nullptr;
    u32 _bufferOffset = 0;
    u32 _bufferLength = 0;
    u32 _position = 0;
    u8 _buffer[BUFFER_SIZE];

    u32 _blockOffset = 0;
    u32 _blockSize = 0;
    u32 _sectorRunCount = 0;
    cheat_menu_sector_run_t _sectorRuns[CHEAT_MENU_MAX_SECTOR_RUNS];

    u32 _itemCount = 0;
    u32 _cheatCount = 0;
    std::unique_ptr<u32[], FreeArray> _itemOffsets;
    u32 _rootCount = 0;
    std::unique_ptr<u16[], FreeArray> _rootItems;
    std::unique_ptr<u8[], FreeArray> _enabledBits;
    std::unique_ptr<bool[], FreeArray> _launcherCheatMatched;
    u32 _launcherCheatCount = 0;
    u32 _matchedLauncherCheatCount = 0;
    u32 _extraCheatCount = 0;
    u32 _extraCheatsSize = 0;
    u32 _activeCheatsSize = 0;
    /// @brief Size of all cheats of the game as a pload_cheat_t list (length word and opcodes of each cheat).
    u32 _allCheatsSize = 0;

    void Seek(u32 position) { _position = position; }
    u32 GetPosition() const { return _position; }
    bool ReadByte(u8& value);
    bool ReadU32(u32& value);
    bool SkipString();
    u32 AlignInBlock(u32 position) const;
    bool CompareOpcodes(const pload_cheat_t* cheat);

    bool FindGame(u32 gameCode, u32 crc, u32& gameOffset, u32& nextGameOffset);
    bool IndexGame(const pload_cheats_t* launcherCheats);
    void MatchLauncherCheats(u32 itemIndex, u32 opcodeWordCount, const pload_cheats_t* launcherCheats);
    bool CreateSectorRuns();
    void SetEnabled(u32 index);
    DataLayout GetDataLayout() const;
};
