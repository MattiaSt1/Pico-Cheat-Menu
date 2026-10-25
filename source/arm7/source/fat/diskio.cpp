#include "common.h"
#include "diskio.h"
#include "loader/DldiDriver.h"
extern DldiDriver* gDiskDriver;
extern "C" DSTATUS disk_status(BYTE drive) { return drive == 0 && gDiskDriver ? 0 : STA_NOINIT; }
extern "C" DSTATUS disk_initialize(BYTE drive) { return disk_status(drive); }
extern "C" DRESULT disk_read(BYTE drive, BYTE* buffer, DWORD sector, UINT count)
{
    if (disk_status(drive) || !count) return RES_PARERR;
    return gDiskDriver->ReadSectors(sector, count, buffer) ? RES_OK : RES_ERROR;
}
extern "C" DRESULT disk_write(BYTE drive, const BYTE* buffer, DWORD sector, UINT count)
{
    if (disk_status(drive) || !count) return RES_PARERR;
    return gDiskDriver->WriteSectors(sector, count, buffer) ? RES_OK : RES_ERROR;
}
extern "C" DRESULT disk_ioctl(BYTE drive, BYTE command, void*)
{
    return drive == 0 && command == CTRL_SYNC ? RES_OK : RES_PARERR;
}
