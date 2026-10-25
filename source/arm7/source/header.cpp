#include "common.h"
extern "C" void _start();
[[gnu::section(".crt0"), gnu::used]]
pload_header7_t gLoaderHeader { .entryPoint = (void*)&_start, .apiVersion = PICO_LOADER_API_VERSION };
