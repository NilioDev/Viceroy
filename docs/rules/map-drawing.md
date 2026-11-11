# Drawing the map

How a square of the map is put together, found by comparing our screen with
captures of the original running in DOSBox. Sprite numbers are 0-based
indices into the sprite sheet named.

## Base tile

`TERRAIN.SS` holds one tile per terrain row of the ruleset. An unexplored
square is `PHYS0.SS` sprite 148.

## Overlays (PHYS0.SS)

Overlays that join up with their neighbours use a 4-bit neighbour mask:
1 = east, 2 = west, 4 = south, 8 = north, set when that neighbour has the same
feature.

| Feature   | Sprite        | Certain? |
|-----------|---------------|----------|
| Mountains | 32 + mask     | yes      |
| Hills     | 48 + mask     | yes      |
| Forest    | 64 + mask     | yes      |
| Major river | 0 + mask    | guess    |
| Minor river | 16 + mask   | guess    |
| Road      | 81 + direction | guess   |
| Plowed    | 149           | yes      |

## Coast

A water square next to land is drawn in four 8×8 quarters, each from its own
sprite:

    sprite = 108 + 4 × (first + 2 × corner + 4 × second) + quarter

where `first`, `corner` and `second` are 1 when that neighbour is land:

| Quarter | Index | first | corner | second |
|---------|-------|-------|--------|--------|
| Top left     | 0 | W | NW | N |
| Top right    | 1 | N | NE | E |
| Bottom right | 2 | E | SE | S |
| Bottom left  | 3 | S | SW | W |

In those sprites pixel value 0 keeps the water underneath. A transparent pixel
shows the land: the pixel at the same position of the land neighbour's base
tile (the first side if it is land, else the second, else the corner).

## Still unknown

- Where fish and other resources appear (not a simple lattice).
- The recolouring of a colony's flag to the nation's colour.
- The dithered edge between explored and unexplored squares.
