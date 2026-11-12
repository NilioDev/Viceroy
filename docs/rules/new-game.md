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

The natives start with 56 settlements, in the same order and tribes in both
games but at places that differ by a square or so, and one brave (type 19)
for each settlement.
