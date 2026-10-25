#pragma once

#define PICO_AGB_PRINT_ARM9_RING                ((vu16*)0x08000000)
#define PICO_AGB_PRINT_ARM9_READ_PTR            (*(vu16*)0x08010000)
#define PICO_AGB_PRINT_ARM9_WRITE_PTR           (*(vu16*)0x08010002)
#define PICO_AGB_PRINT_RING_LENGTH              0x8000

#define PICO_AGB_IDENTIFIER                     (*(vu32*)0x08010008)
#define PICO_AGB_IDENTIFIER_VALUE               0x4F434950