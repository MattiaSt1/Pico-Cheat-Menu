#pragma once

#define DEFINE_SECTION_SYMBOLS(sectionName) \
    extern u8 __start_##sectionName[]; \
    extern u8 __stop_##sectionName[]

#define SECTION_START(sectionName)      ((void*)__start_##sectionName)
#define SECTION_END(sectionName)        ((void*)__stop_##sectionName)
#define SECTION_SIZE(sectionName)       ((u32)__stop_##sectionName - (u32)__start_##sectionName)
