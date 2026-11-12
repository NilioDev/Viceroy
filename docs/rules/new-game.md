# A new game on the map of America

Measured by starting two new games on the original America map (default
difficulty, the Dutch), saving at once, and comparing the saves with
`AMER2.MP` and with each other.

## The map file

`AMER2.MP`: width and height (16 bits each), a 16-bit 4, then three layers
of width × height bytes in the savegame's order (terrain, mask, path). The
mask layer is empty.

## Turning it into the game's map

The terrain comes out the same in both games:

- Terrain 16 to 23 in the file is forest of the open land 0 to 7 (as 8 to
  15); terrain 8 to 15 stays.
- A forest on hills or mountains loses its trees (123 of 124 squares).
- The top and bottom rows become arctic, without hills or rivers.
- Columns 0, 1 and the last one become sea lane, from the second row to the
  second-last, even over land.
- *Not yet explained:* two tundra squares (21, 1) and (43, 68) get mountains.

The low nibble of the path layer holds region numbers, worked out again by
the game (mostly the file's own, renumbered). Its high nibble starts at 0xF
and loses a bit where the nations have been. Mask bit 0x20 is set on the
same 1,309 water squares in both games (meaning not yet known).

## Resources

The seed of the resource pattern (see [map drawing](map-drawing.md)) is
drawn at random (78 and 130 in the two games). Mask bit 2 ("no fish") is
set on every square of the pattern at sea with no land in the 5 × 5 square
around it, its corners left out (66 of 66 squares).

## Units at the start

Each nation has a ship at a starting square that differs from game to game,
with a soldier and a pioneer aboard: a caravel, or a merchantman for the
Dutch. The colonists' professions were the same in both games:

| Nation | Soldier | Pioneer |
|---|---|---|
| English | free colonist | free colonist |
| French | free colonist | hardy pioneer |
| Spanish | veteran soldier | free colonist |
| Dutch | veteran soldier | free colonist |

## The natives at the start

Compared over eight new games. The natives have 56 sites, always in the
same order and for the same tribes: 5 Inca, 3 Aztec, 5 Arawak, 11
Iroquois, 4 Cherokee, 7 Apache, 7 Sioux and 14 Tupi. The first site of each
tribe is its capital (byte 3 is 4). Each site lands within two squares of
its own middle square, on land without hills or mountains (rivers are
fine), never next to another settlement. Viceroy keeps the middle squares
it measured and draws the offset at random, trying again when the square
does not fit; a site that finds no square is left out. *Not yet known:* in
three of the eight games the 55th site, (41, 40), was missing, which
Viceroy's rule leaves out only in about one game in fifteen.

A settlement's size (byte 4) is 3 + 2 × the tribe's level from `@TRIBES`
(9 for the Inca down to 3 for the nomads); the capital repeats it in byte
6. Byte 5 (the mission) and bytes 7 to 9 are 0xFF.

Each settlement has one brave (unit type 19) on its square, with its home
settlement's index in byte 6. In the saves the braves had already taken
their first walk; Viceroy starts them at home.

The units come in the order ship, pioneer, soldier for each nation, then
the braves. The pioneer carries 100 tools, and pioneer and soldier are on
sentry aboard the ship. The human player's ship is the active unit, and
the squares around it are explored.

## What Viceroy leaves empty

The low nibble of the "seen" layer (set on most land in a new game), mask
bit 0x20, the block of tribe data after the settlements, most of the
header and the block after the map are not understood yet and start at
zero, apart from the seed of the resource pattern.

## The nations at the start

Compared over four new games (bytes of the nation record, see
[the savegame](../formats/savegame.md)):

- Only the human player has gold: 1,000 (bytes 42-43). The tax rate is 0.
- The market prices (bytes 76 to 91) are drawn once per game and are the
  same for all four nations. Most lie within the first two numbers of the
  good's `@CARGO` row; cigars were seen at 10 and 14 (range 11-13), so the
  draw is not simply uniform. Lumber 2, silver 20, tools 2 and muskets 3
  never changed.
- The trade volumes (bytes 92 to 117, 16 bits) start at the same values in
  every game: −13, −16, −16, −25, 0, −7, −8, −2, −11, −12, −3, −11, −13 for
  the first thirteen goods, and 4, 5, 6 for trade goods, tools and muskets.
- Byte 46 (crosses) is 2; byte 48 (crosses needed) is 9 for the English and
  14 for the others.
- Bytes 50-51, the square the ships sail to from Europe, take the four
  values (51, 56), (37, 14), (56, 42) and (47, 28), dealt out to the nations
  at random.
- The three recruits waiting on the docks are random for the computer
  nations, though the first was always the same for each nation (25, 25,
  24 and 13). For the Dutch human player all three were the same in all
  four games (13, 0, 22). Viceroy draws the other two at random.
