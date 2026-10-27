#pragma once

// Sector access to the cheat menu file, shared by the resident part and the menu. While the game runs there is no
// file system: the file is accessed through the sector runs that the parasite determined, with the DLDI driver of the
// resident part.

#include "CheatMenuDefinitions.h"
#include "dldiHeader.h"

/// @brief Reads or writes sectors of the cheat menu file.
/// @param resident The resident part, which holds the sector runs of the file and the DLDI driver.
/// @param sector The first sector to transfer, relative to the start of the file.
/// @param count The number of sectors to transfer.
/// @param buffer The data to write, or the destination of the data that is read.
/// @param write True to write the sectors, false to read them.
/// @return True on success, false if the driver failed or the sectors are not all inside the file.
static inline bool cheatMenuFile_transfer(const cheat_menu_resident_t* resident, u32 sector, u32 count, void* buffer,
    bool write)
{
    const dldi_header_t* driver = (const dldi_header_t*)resident->dldiDriver;
    u8* data = (u8*)buffer;
    for (u32 i = 0; i < resident->fileRunCount && count != 0; i++)
    {
        const cheat_menu_sector_run_t* run = &resident->fileRuns[i];
        if (sector >= run->count)
        {
            sector -= run->count;
            continue;
        }
        u32 runCount = run->count - sector < count ? run->count - sector : count;
        bool ok = write ?
            ((dldi_write_func_t)driver->writeSectorsFuncAddress)(run->sector + sector, runCount, data) :
            ((dldi_read_func_t)driver->readSectorsFuncAddress)(run->sector + sector, runCount, data);
        if (!ok)
            return false;
        data += runCount * CHEAT_MENU_SECTOR_SIZE;
        count -= runCount;
        sector = 0;
    }
    return count == 0;
}
