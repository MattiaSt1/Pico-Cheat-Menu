// Icons of the in-game cheat menu, in the same 1bpp format as the font: one byte per pixel row with the leftmost pixel
// in bit 0. Holes in an icon, such as the letter of a button, show what is behind the sprite.
//
// An icon is a character of a text row: names never contain these characters, since they are limited to the ASCII
// characters of the font. Icons that are wider than one cell are strings of several characters.
#pragma once

#define ICON_FIRST_CHAR         0x80
#define ICON_COUNT              28

#define ICON_LINE               "\x80"
#define ICON_CURSOR             "\x81"
#define ICON_ON                 "\x82\x83"
#define ICON_OFF                "\x84\x85"
#define ICON_FOLDER             "\x86\x87"
#define ICON_KEY_A              "\x88"
#define ICON_KEY_B              "\x89"
#define ICON_KEY_R              "\x8A"
#define ICON_KEY_START          "\x8B\x8C\x8D"
#define ICON_KEY_SELECT         "\x8E\x8F"
#define ICON_DPAD_UP_DOWN       "\x90"
#define ICON_DPAD_LEFT_RIGHT    "\x91"
#define ICON_LOCK               "\x92"

/// @brief The character of a cell of the progress bar with the given number of filled pixel columns (0-8).
#define ICON_PROGRESS_CHAR(pixels) ((char)(0x93 + (pixels)))

static const unsigned char sIcons[ICON_COUNT * 8] =
{
    // LINE
    0x00, // ........
    0x00, // ........
    0x00, // ........
    0xFF, // ########
    0x00, // ........
    0x00, // ........
    0x00, // ........
    0x00, // ........
    // CURSOR
    0x06, // .##.....
    0x0C, // ..##....
    0x18, // ...##...
    0x30, // ....##..
    0x18, // ...##...
    0x0C, // ..##....
    0x06, // .##.....
    0x00, // ........
    // ON_LEFT
    0x00, // ........
    0xFC, // ..######
    0xFE, // .#######
    0xFE, // .#######
    0xFE, // .#######
    0xFE, // .#######
    0xFE, // .#######
    0xFC, // ..######
    // ON_RIGHT
    0x00, // ........
    0x3F, // ######..
    0x7F, // #######.
    0x47, // ###...#.
    0x47, // ###...#.
    0x47, // ###...#.
    0x7F, // #######.
    0x3F, // ######..
    // OFF_LEFT
    0x00, // ........
    0xFC, // ..######
    0x02, // .#......
    0x3A, // .#.###..
    0x3A, // .#.###..
    0x3A, // .#.###..
    0x02, // .#......
    0xFC, // ..######
    // OFF_RIGHT
    0x00, // ........
    0x3F, // ######..
    0x40, // ......#.
    0x40, // ......#.
    0x40, // ......#.
    0x40, // ......#.
    0x40, // ......#.
    0x3F, // ######..
    // FOLDER_LEFT
    0x00, // ........
    0x7C, // ..#####.
    0xFE, // .#######
    0xFE, // .#######
    0xFE, // .#######
    0xFE, // .#######
    0xFE, // .#######
    0x00, // ........
    // FOLDER_RIGHT
    0x00, // ........
    0x00, // ........
    0x7F, // #######.
    0x7F, // #######.
    0x7F, // #######.
    0x7F, // #######.
    0x7F, // #######.
    0x00, // ........
    // KEY_A
    0x3C, // ..####..
    0x7E, // .######.
    0xEF, // ####.###
    0xD7, // ###.#.##
    0xC7, // ###...##
    0xD7, // ###.#.##
    0x56, // .##.#.#.
    0x3C, // ..####..
    // KEY_B
    0x3C, // ..####..
    0x7E, // .######.
    0xE7, // ###..###
    0xD7, // ###.#.##
    0xE7, // ###..###
    0xD7, // ###.#.##
    0x66, // .##..##.
    0x3C, // ..####..
    // KEY_R
    0x3C, // ..####..
    0x7E, // .######.
    0xE7, // ###..###
    0xD7, // ###.#.##
    0xE7, // ###..###
    0xD7, // ###.#.##
    0x56, // .##.#.#.
    0x3C, // ..####..
    // KEY_START_LEFT
    0xFC, // ..######
    0xFE, // .#######
    0x47, // ###...#.
    0xF7, // ###.####
    0xC7, // ###...##
    0xDF, // #####.##
    0xC6, // .##...##
    0xFC, // ..######
    // KEY_START_MIDDLE
    0xFF, // ########
    0xFF, // ########
    0x6C, // ..##.##.
    0x56, // .##.#.#.
    0x46, // .##...#.
    0x56, // .##.#.#.
    0x56, // .##.#.#.
    0xFF, // ########
    // KEY_START_RIGHT
    0x3F, // ######..
    0x7F, // #######.
    0xC6, // .##...##
    0xED, // #.##.###
    0xEE, // .###.###
    0xED, // #.##.###
    0x6D, // #.##.##.
    0x3F, // ######..
    // KEY_SELECT_LEFT
    0xFC, // ..######
    0xFE, // .#######
    0x47, // ###...#.
    0x77, // ###.###.
    0x47, // ###...#.
    0x5F, // #####.#.
    0x46, // .##...#.
    0xFC, // ..######
    // KEY_SELECT_RIGHT
    0x3F, // ######..
    0x7F, // #######.
    0xF4, // ..#.####
    0xF7, // ###.####
    0xF6, // .##.####
    0xF7, // ###.####
    0x44, // ..#...#.
    0x3F, // ######..
    // DPAD_UP_DOWN
    0x18, // ...##...
    0x3C, // ..####..
    0x7E, // .######.
    0x00, // ........
    0x7E, // .######.
    0x3C, // ..####..
    0x18, // ...##...
    0x00, // ........
    // DPAD_LEFT_RIGHT
    0x00, // ........
    0x24, // ..#..#..
    0x66, // .##..##.
    0xE7, // ###..###
    0x66, // .##..##.
    0x24, // ..#..#..
    0x00, // ........
    0x00, // ........
    // LOCK
    0x3C, // ..####..
    0x42, // .#....#.
    0x42, // .#....#.
    0xFF, // ########
    0xE7, // ###..###
    0xE7, // ###..###
    0xFF, // ########
    0x00, // ........
    // PROGRESS_0
    0x00, // ........
    0xFF, // ########
    0x00, // ........
    0x00, // ........
    0x00, // ........
    0x00, // ........
    0xFF, // ########
    0x00, // ........
    // PROGRESS_1
    0x00, // ........
    0xFF, // ########
    0x01, // #.......
    0x01, // #.......
    0x01, // #.......
    0x01, // #.......
    0xFF, // ########
    0x00, // ........
    // PROGRESS_2
    0x00, // ........
    0xFF, // ########
    0x03, // ##......
    0x03, // ##......
    0x03, // ##......
    0x03, // ##......
    0xFF, // ########
    0x00, // ........
    // PROGRESS_3
    0x00, // ........
    0xFF, // ########
    0x07, // ###.....
    0x07, // ###.....
    0x07, // ###.....
    0x07, // ###.....
    0xFF, // ########
    0x00, // ........
    // PROGRESS_4
    0x00, // ........
    0xFF, // ########
    0x0F, // ####....
    0x0F, // ####....
    0x0F, // ####....
    0x0F, // ####....
    0xFF, // ########
    0x00, // ........
    // PROGRESS_5
    0x00, // ........
    0xFF, // ########
    0x1F, // #####...
    0x1F, // #####...
    0x1F, // #####...
    0x1F, // #####...
    0xFF, // ########
    0x00, // ........
    // PROGRESS_6
    0x00, // ........
    0xFF, // ########
    0x3F, // ######..
    0x3F, // ######..
    0x3F, // ######..
    0x3F, // ######..
    0xFF, // ########
    0x00, // ........
    // PROGRESS_7
    0x00, // ........
    0xFF, // ########
    0x7F, // #######.
    0x7F, // #######.
    0x7F, // #######.
    0x7F, // #######.
    0xFF, // ########
    0x00, // ........
    // PROGRESS_8
    0x00, // ........
    0xFF, // ########
    0xFF, // ########
    0xFF, // ########
    0xFF, // ########
    0xFF, // ########
    0xFF, // ########
    0x00, // ........
};
