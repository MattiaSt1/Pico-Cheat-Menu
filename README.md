# Pico Cheat Menu

An in-game cheat menu for [Pico Loader](https://github.com/LNH-team/pico-loader) on the DSpico.

Press **L + R + Down + Select** (the default combination, which can be changed on the status screen) while a game is
running to open the menu. It lists every cheat of the game from `usrcheat.dat`, including folders and notes, and turns
cheats on and off without restarting the game. Its cheat search finds new cheats while you play: search the memory of
the game for your money or your lives, then change or lock the value you found.

## Features

- All cheats of the game from `usrcheat.dat`, with their folders and notes. In folders where only one cheat can be
  on, turning a cheat on turns the others off.
- The cheats chosen in the launcher are on from the start. Launcher cheats that are not in the database are listed as
  "Launcher cheat 1", "Launcher cheat 2" and so on.
- Works with any fork of Pico Loader that accepts cheats from the launcher (API version 3). The menu is added while
  the game starts: no ROM is patched and the loader does not need to be rebuilt.
- A cheat search that finds the address of any value of the game, also in games without cheats in `usrcheat.dat`. The
  values it finds can be changed once or locked, so that they keep their value.
- The game is paused while the menu is open, and its graphics are restored when the menu closes.

## Controls

| Button       | Action                                                                   |
|--------------|--------------------------------------------------------------------------|
| Up / Down    | Move through the list (hold to scroll)                                   |
| Left / Right | Page up / page down (hold to repeat)                                     |
| A            | Turn a cheat on or off / open a folder                                   |
| B            | Go back / close the menu                                                 |
| Select       | Show the description of the selected cheat (Select or B closes it)       |
| Start        | Turn off all cheats (only those of the open folder, when inside one)     |
| R            | Open the cheat search                                                    |

The changes are applied when the menu is closed.

## Cheat search

The cheat search finds where the game keeps a value, such as the money, the lives or the time, in the memory of the
game: up to 4 MB in DS mode and about 15.5 MB in DSi mode, where a search takes longer. A typical search for the money:

1. Open the menu and press **R**. Choose the **value size** (1, 2 or 4 bytes; most values use 2 or 4) and keep
   **Search for** on "Equal to value". Enter the amount of money you see in **Value** and choose **Start search**.
2. Close the menu, earn or spend some money, open the menu again and press **R**. Enter the new amount and choose
   **Search again**. Repeat until few results are left.
3. Choose **Show results**. On a result, **A** changes its value right away and **Start** locks it: a cheat then keeps
   writing the value while the game runs.

When the game does not show the value (a hidden counter or a health bar), search for "Unknown value" first, then
use "Changed", "Unchanged", "Increased" or "Decreased" after the value changed in the game.

Up to 16 values can be locked. The search and the locked values are kept while the game runs, also when the menu is
closed, and are cleared when a game is started. The search uses `/_pico/cheatsearch.bin`, which takes about 18 MB on
the SD card and is created the first time. If the SD card has less than about 20 MB free, or its free space is too
fragmented, the menu works without the cheat search. Results are listed once at most 128 are left.

| Button       | Action in the cheat search                                               |
|--------------|--------------------------------------------------------------------------|
| Up / Down    | Move through the options or the results                                  |
| Left / Right | Change the size or the comparison / page up and down in the results      |
| A            | Select an option / edit the value of a result                            |
| Start        | Lock or unlock the selected result                                       |
| B            | Go back (R also returns from the options to the cheat list)              |

While a number is edited, Left and Right select a digit, Up and Down change it, A confirms and B cancels.

## Status screen

Hold **Down** on the startup status screen to change the combination that opens the in-game menu. Startup pauses
while the hotkey editor is open:

1. Release all buttons, then press the buttons to include. Each press adds or removes a button; you can select them
   individually or together, including **L + R + A + B**. Releasing them keeps the selection on screen.
2. Hold **Start alone for one second** to save. A short Start press adds or removes Start from the combination.
3. After saving, press **A** to continue starting the game.

To leave the editor without saving, hold **B alone for two seconds** and release it when the screen shows
"Release B": the previous combination stays active and the game starts. After a save, this works like A.

Hold **B alone for five seconds** in the editor to save the default **L + R + Down + Select** combination and return
to the main status screen. A countdown shows the remaining time; releasing B earlier (exiting after two seconds) or
pressing another button interrupts the countdown. The game stays paused while the reset is saved and until B is
released.

Supported buttons are **A, B, L, R, Start, Select and the D-pad**. X, Y and touch are not supported.
The combination is saved globally in `/_pico/cheatmenu-hotkey.bin` and takes effect for this game and future starts.
The status screen shows the current combination. Deleting the settings file restores the default; missing or invalid
settings also use the default. If saving fails, the editor reports it and keeps the previous combination active.
The editor is available when the in-game cheat menu can be installed.

While a game starts, the top screen shows the progress: the SD card, the real Pico Loader and the cheat menu with the
number of cheats of the game. If the cheat menu is not available, the screen tells why and the game starts without it:

| Message                                               | What to do                                                    |
|-------------------------------------------------------|---------------------------------------------------------------|
| Game not supported by the menu                        | The game's code cannot be hooked (for example, homebrew).     |
| Can't create /_pico/cheatsearch.bin                   | The game has no cheats and the cheat search does not fit: free about 20 MB on the SD card. |
| Too many cheats for the menu                          | The cheats of the game do not fit in memory.                  |
| Can't write /_pico/cheatmenu.bin                      | Free some space on the SD card, or delete the file to have it created again. |
| This Pico Loader version is not supported by the menu | Use a fork of Pico Loader with API version 3 in `_pico/loader/`. |
| Turned off by /_pico/parasite-transparent             | Delete that file to turn the menu back on.                    |

When the game has no cheats (it is not in `usrcheat.dat`, or the file is missing) the status screen shows
"search only": the list of the menu is empty and **R** opens the cheat search.

Messages in red mean that the game cannot start at all: the launcher passed no DLDI driver, the SD card cannot be
read, or Pico Loader is missing from `_pico/loader/`.

`_picoboot.nds`, the boot file of the DSpico (usually Pico Launcher), is always started right away with the real
Pico Loader, without the status screen and without the cheat menu.

To turn the cheat menu off for all games, create an empty file named `parasite-transparent` in `_pico/`.

## Building

Requirements: Windows, [Docker Desktop](https://www.docker.com/products/docker-desktop/) (running) and Git.

```bash
git clone --recursive https://github.com/MattiaSt1/Pico-Cheat-Menu.git
```

Then run `Build.bat`. It builds everything inside a [BlocksDS](https://blocksds.github.io/docs/) container and leaves
only the `_pico` folder next to the script.

## Installation

1. Copy the `_pico` folder to the root of the SD card.
2. Put your own Pico Loader binaries (`picoLoader7.bin` and `picoLoader9.bin`) in `_pico/loader/`.
   The fork must support cheats passed by the launcher (API version 3).
3. Put the other Pico Loader files (`aplist.bin`, `savelist.bin`, `patchlist.bin`) and your `usrcheat.dat` in
   `_pico/`.

## How it works

The binaries in `_pico/` are a "parasite" that takes the place of Pico Loader. The launcher boots it as if it were
Pico Loader; the parasite prepares the cheat menu and then boots the real Pico Loader from `_pico/loader/` with the
same parameters.

1. **Parasite, arm7 part** (`source/arm7`): mounts the SD card, loads the real Pico Loader, indexes the game's cheats
   in `usrcheat.dat`, writes the menu to `_pico/cheatmenu.bin` and builds the cheat list for Pico Loader. The list
   holds the enabled cheats and the resident part of the menu.
2. **Parasite, arm9 part** (`source/arm9`): shows the status screen, checks that the real Pico Loader can run the
   menu and boots it, with the installer in place of the game's entry point.
3. **Installer** (`source/installer`): runs right before the game starts. It finds the cheat list in the game's
   memory and hooks the game's interrupt handler.
4. **Resident part** (`source/resident`): stays in memory with the cheat list. When the button combination is
   pressed, it swaps the menu from `_pico/cheatmenu.bin` into the memory of the game's code and back.
5. **Cheat menu** (`source/menu`): reads the cheats from `usrcheat.dat` on the SD card while the game is paused and
   writes the enabled cheats to the cheat list, which the cheat engine of Pico Loader runs. The cheat search
   (`cheatSearch.c`) keeps its state, its results and a snapshot of the memory in `_pico/cheatsearch.bin`, since the
   menu has very little memory of its own; a locked value becomes an Action Replay cheat in the cheat list.

The definitions shared by these parts are in `source/common`, mainly `ParasiteDefinitions.h` and
`CheatMenuDefinitions.h`.

## Credits and license

This project is based on [Pico Loader](https://github.com/LNH-team/pico-loader) by the LNH team and is an altered
version of it. It is released under the zlib license, see [LICENSE.txt](LICENSE.txt).
Third-party components and their licenses:

- [libtwl](https://github.com/Gericom/libtwl) (submodule)
- FatFs by ChaN
- The font of the libnds console (zlib license)
- TLSF, mini-printf and newlib, see [source/licenses](source/licenses)
