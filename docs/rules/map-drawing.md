# Drawing the map

How a square of the map is put together, found by comparing our screen with
captures of the original running in DOSBox. Sprite numbers are 0-based
indices into the sprite sheet named.

## Base tile

`TERRAIN.SS` holds the ground: tiles 0 to 7 for the open land in
`@UNFORESTED` order, 8 desert with cacti, 9 arctic, 10 ocean and 11 sea
lane. A forest shows the open land under its trees, except scrub (desert
forest, terrain 9), which is tile 8 without a forest overlay. An unexplored
square is `PHYS0.SS` sprite 148.

## Ground meeting other ground

Where a square's ground differs from that of the square beside it, the
neighbour's ground shows through a sparse dither along the shared side.
The dither is `PHYS0.SS` sprite 104 + side (north, east, south, west): each
pixel 0 takes the neighbour's ground tile at the same spot. The sides are
applied in that order, so west wins in the corners (seen in the original:
west over north and south, south over east). Explored land only takes in
other land; water gets its shore instead (below). An unexplored square takes
in the ground of every explored square next to it, water too, which gives
the dotted edge of the unknown. Checked pixel by pixel against five captures
of the original.

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
