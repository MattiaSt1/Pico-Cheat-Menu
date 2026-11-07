# Tests

## Cheat search

`tests/cheatSearchTest.c` tests the cheat search of the menu (`source/menu/cheatSearch.c`) with the cheat search file
and the memory of the game in host memory: searches for a value, for an unknown value and the comparisons with the
previous values, the listed results, the memory of DS mode and of DSi mode, the memory of the menu image that is never
scanned, an error on every file transfer, states that are not valid, and the cheats of the locked values. It also
covers the size of the scanned memory, which stops before the cheat list, and the number of cheats of locked values
that the parasite reads from a cheat list. Run from the repository root with a native C compiler and the BlocksDS
headers available:

```sh
cc -std=gnu17 -Wall -Wextra -fsanitize=address,undefined -Isource/common -Isource/menu \
  -I"${BLOCKSDS:-/opt/blocksds/core}/libs/libnds/include" tests/cheatSearchTest.c -o /tmp/pico-search-tests
/tmp/pico-search-tests
```

No image used by `Build.bat` contains a native C compiler. One way to run the test is a throwaway container:

```sh
docker run --rm -v "$PWD:/proj:ro" pico-cheat-menu-build sh -c 'apt-get update -qq && apt-get install -y -qq gcc &&
  cd /proj && cc -std=gnu17 -Wall -Wextra -fsanitize=address,undefined -Isource/common -Isource/menu \
  -I"$BLOCKSDS/libs/libnds/include" tests/cheatSearchTest.c -o /tmp/t && /tmp/t'
```

## Cheat list

`tests/cheatListTest.c` includes the menu and tests how it writes the cheat list (`updateActiveCheats`): the cheats of
the locked values come first, their number is stored in the resident part, and they stay in the list if the state of
the cheat search cannot be read. Run from the repository root, in the same way as the cheat search test:

```sh
cc -std=gnu17 -Wall -Wextra -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -fno-builtin \
  -fsanitize=address,undefined -Isource/common -Isource/menu \
  -I"${BLOCKSDS:-/opt/blocksds/core}/libs/libnds/include" \
  tests/cheatListTest.c source/menu/cheatSearch.c -o /tmp/pico-list-tests
/tmp/pico-list-tests
```

## Hotkey editor regression test

Run from the repository root with a native C++ compiler and the BlocksDS headers available:

```sh
c++ -std=c++17 -fshort-enums -Wall -Wextra -Werror \
  -I"${BLOCKSDS:-/opt/blocksds/core}/libs/libnds/include" -Isource/common \
  tests/hotkeyEditor.cpp -o /tmp/pico-hotkey-tests
/tmp/pico-hotkey-tests
```

The test exercises holding Down during loading, the unavailable-menu path, selecting every valid combination
(including L+R+A+B), toggling buttons, saving with a long Start press, leaving without saving by releasing B after two
seconds, the five-second B reset, interrupted reset gestures, retry after a failed reset, reset after saving, and
validation of saved button masks. It runs the same input state machine as the startup screen; SD I/O and actual
in-game hooks still need a console or emulator check.

No image used by `Build.bat` contains a native C++ compiler. One way to run the test is a throwaway container:

```sh
docker run --rm -v "$PWD:/proj:ro" pico-cheat-menu-build sh -c 'apt-get update -qq && apt-get install -y -qq g++ &&
  cd /proj && c++ -std=c++17 -fshort-enums -Wall -Wextra -Werror -I"$BLOCKSDS/libs/libnds/include" \
  -Isource/common tests/hotkeyEditor.cpp -o /tmp/t && /tmp/t'
```

`-fshort-enums` reproduces the ARM toolchain's enum sizing. The editor state must stay 32-bit, since byte stores to
VRAM are ignored on the console. The test also waits ten seconds while selecting buttons and during an outstanding
save to check that startup stays paused.
