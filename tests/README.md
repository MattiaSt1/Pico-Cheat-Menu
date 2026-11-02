# Hotkey editor regression test

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
