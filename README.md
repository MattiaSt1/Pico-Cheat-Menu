# Pico Cheat Menu

An in-game cheat menu for [Pico Loader](https://github.com/LNH-team/pico-loader) on the DSpico.

Press **L + R + Down + Select** while a game is running to open the menu. It lists every cheat of the
game from `usrcheat.dat`, including folders and notes, and lets you turn cheats on and off without
restarting the game.

## Controls

| Button       | Action                          |
|--------------|---------------------------------|
| Up / Down    | Move through the list           |
| Left / Right | Page up / page down             |
| A            | Toggle a cheat / open a folder  |
| B            | Go back / close the menu        |
| Select       | Show the cheat's description    |
| Start        | Turn off all cheats (in the open folder only, if inside one) |

## Building

Requirements: Windows, [Docker Desktop](https://www.docker.com/products/docker-desktop/) (running) and Git.

```bash
git clone --recursive https://github.com/MattiaSt1/Pico-Cheat-Menu.git
```

Then run `Build.bat`. It builds everything inside a [BlocksDS](https://blocksds.github.io/docs/) container
and leaves only the `_pico` folder next to the script.

## Installation

1. Copy the `_pico` folder to the root of the SD card.
2. Put your own Pico Loader binaries (`picoLoader7.bin` and `picoLoader9.bin`) in `_pico/loader/`.
   The fork must support cheats passed by the launcher (API v3).
3. Put the other Pico Loader files (`aplist.bin`, `savelist.bin`, `patchlist.bin`) and your
   `usrcheat.dat` in `_pico/`.

## Credits and license

This project is based on [Pico Loader](https://github.com/LNH-team/pico-loader) by the LNH team and is
an altered version of it. It is released under the zlib license, see [LICENSE.txt](LICENSE.txt).
Third-party components and their licenses:

- [libtwl](https://github.com/Gericom/libtwl) (submodule)
- FatFs by ChaN
- TLSF, mini-printf, newlib and dsi_sdmmc, see [source/licenses](source/licenses)
