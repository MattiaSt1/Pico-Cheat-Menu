#pragma once
#include <nds/ndstypes.h>

#define BOOT_TYPE_INVALID   0
#define BOOT_TYPE_CARD      1
#define BOOT_TYPE_MULTIBOOT 2
#define BOOT_TYPE_NAND      3
#define BOOT_TYPE_MEMORY    4

struct shared_memory_multi_boot_info_t
{
    u16 bootType;
    u8 multibootInfo[0x40 - 2];
};

//0x02FFF800: 2KB
struct shared_memory_ntr_t
{
    u8 gap0[0x200];
    u8 gap200[0x80];
    u8 cardRomHeader[0x160]; // without debug footer
    u8 downloadParams[0x20];
    u8 bootCheckInfo[0x20];
    u32 resetParam;
    u8 gap424[8];
    u32 romOffset;
    u8 slot2Info[0xC];
    u32 vblankCounter;
    shared_memory_multi_boot_info_t multibootInfo;
    u8 firmwareUserData[0x100];
    u8 arm9ExceptionStack[0x1C];
    u32 arm9ExceptionVector;
    u8 arenaInfo[0x48];
    u8 rtcData[8];
    u8 systemConfig[6];
    u8 arm9Print;
    u8 arm7Print;
    u8 arm9ErrorPrint;
    u8 arm7ErrorPrint;
    u8 gap1FA[6];
    u8 romHeader[0x160];
    u8 isDebuggerData[0x20];
    u32 arm9IpcSignalParam;
    u32 arm7IpcSignalParam;
    u32 arm9IpcHandleChecker;
    u32 arm7IpcHandleChecker;
    u32 lastMicrophoneAddress;
    u16 microphoneSampleData;
    u16 wifiCallbackControl;
    u16 wifiRssiPool;
    u8 setSlot2InfoOnce;
    u8 slot2Inserted;
    u32 arm7Param;
    u32 arm9ThreadInfo;
    u32 arm7ThreadInfo;
    u16 padXYBuffer;
    u8 touchData[4];
    u16 autoloadSync;
    u8 arm9LockId[8];
    u8 arm7LockId[8];
    u8 vramCLock[8];
    u8 vramDLock[8];
    u8 wram0Lock[8];
    u8 wram1Lock[8];
    u8 slot1Lock[8];
    u8 slot2Lock[8];
    u8 initLock[8];
    u16 arm9MainMemoryCheck;
    u16 arm7MainMemoryCheck;
    u16 mainMemoryCmd;
};

static_assert(sizeof(shared_memory_ntr_t) == 0x800);

#define NTR_SHARED_MEMORY      ((shared_memory_ntr_t*)0x027FF800)
#define NTR_SHARED_MEMORY_SDK5 ((shared_memory_ntr_t*)0x02FFF800)

//0x02FFC000: 16KB
struct shared_memory_twl_t
{
    u8 twlCardRomHeader[0x1000];
    u8 gap1000[0x7B0];
    char sysMenuVersionInfoContentId[9];
    char sysMenuVersionInfoContentLastInitialCode;
    u16 sysMenuDisableSetHotboot;
    u8 sdNandContext[0x44];
    u8 titleIdList[0x400];
    u8 mountInfo[0x3C0];
    u8 bootSrlPath[0x40];
    u8 twlRomHeader[0x1000];
    u8 sharedArena[0x680];
    u8 gap3680[0x180];
    shared_memory_ntr_t ntrSharedMem;
};

static_assert(sizeof(shared_memory_twl_t) == 0x4000);

#define TWL_SHARED_MEMORY ((shared_memory_twl_t*)0x02FFC000)
