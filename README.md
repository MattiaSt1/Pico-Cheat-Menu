# Pico Cheat Menu

An in-game cheat menu for [Pico Loader](https://github.com/LNH-team/pico-loader) on the DSpico.

Press **L + R + Down + Select** while a game is running to open the menu. It lists every cheat of the game from
`usrcheat.dat`, including folders and notes, and turns cheats on and off without restarting the game.

## Features

- All cheats of the game from `usrcheat.dat`, with their folders and notes. In folders where only one cheat can be
  on, turning a cheat on turns the others off.
- The cheats chosen in the launcher are on from the start. Launcher cheats that are not in the database are listed as
  "Launcher cheat 1", "Launcher cheat 2" and so on.
- Works with any fork of Pico Loader that accepts cheats from the launcher (API version 3). The menu is added while
  the game starts: no ROM is patched and the loader does not need to be rebuilt.
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

The changes are applied when the menu is closed.

## Status screen

While a game starts, the top screen shows the progress: the SD card, the real Pico Loader and the cheat menu with the
number of cheats of the game. If the cheat menu is not available, the screen tells why and the game starts without it:

| Message                                               | What to do                                                    |
|-------------------------------------------------------|---------------------------------------------------------------|
| Game not found in usrcheat.dat                        | The database has no cheats for this game.                     |
| /_pico/usrcheat.dat not found                         | Put `usrcheat.dat` in `_pico/`.                               |
| Game not supported by the menu                        | The game's code cannot be hooked (for example, homebrew).     |
| Too many cheats for the menu                          | The cheats of the game do not fit in memory.                  |
| Can't write /_pico/cheatmenu.bin                      | Free some space on the SD card, or delete the file to have it created again. |
| This Pico Loader version is not supported by the menu | Use a fork of Pico Loader with API version 3 in `_pico/loader/`. |
| Turned off by /_pico/parasite-transparent             | Delete that file to turn the menu back on.                    |

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
   writes the enabled cheats to the cheat list, which the cheat engine of Pico Loader runs.

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
