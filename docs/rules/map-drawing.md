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
other land and water only other water (ocean and sea lane take each other
in); between land and water the shore is drawn instead (below). An unexplored square takes
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

Order on a square: ground, plowed, forest, river, hills, mountains, road.
Rivers run over the forest (checked against the view-mode captures).

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

- Where fish and other resources appear. See "Resources" below.
- Where the orders box of a scout, treasure, wagon train or cavalry goes
  (they follow the dragoon or the artillery until measured).

## Colonies and units

Measured on the original (COLONY00, around Isabella; every unit in about
250 captures of the map):

- First all colonies, in the order of the save, each with its name: a later
  colony covers the end of an earlier one's name. Then the units, which cover
  colonies and names.
- A colony picture (21 wide) is centred on its square, so it starts 2 pixels
  to the left. The owner's pennant (`ICONS.SS` 118 + nation) is drawn over
  the one in the picture, at the square's left + 3, top.
- A unit's sprite starts 2 pixels into its square; a person 6 wide is
  centred on the 8 pixels from there (so 3 pixels in). A black copy of its
  shape lies 2 pixels to its left, as in the port.
- Then the orders box (7 × 9), then the unit over it. Where the box goes, from
  the sprite's left `x`: a ship at (x + 7, top), a person at (x + width − 2,
  top + 7), a dragoon at (x − 2, top), artillery at (x + 4, top).
- The panel shows the active unit the same way, its square at (242, 68).
- A soldier, dragoon, pioneer, scout or missionary who is not the matching
  expert shows the plainer sprite 28 before the one `@UNIT` names (a soldier
  without a blue coat).

## The small map

Measured on the original: a frame (colour 0x06) from x 251 to 308 and y 8
to 48; inside, one pixel per square for map columns 1 to 56 and 39 rows, the
frame's top row standing for the row `top` = view's middle row − 20
(clamped to the map). Unexplored squares are black. A square with a colony
or a unit shows its nation's colour (French 0x09, Spanish 0x0E, Dutch 0x0D;
English 0x0C is a guess), a native settlement its tribe's colour (last
column of `@TRIBES`), any other square the middle pixel (8, 8) of its ground
tile, or of hills sprite 48 or mountains sprite 32. Forests show their
ground. The view is a white frame of 15 × 12 squares. All but 4 of the
small map's pixels matched; those are settlements the original leaves out
(perhaps not yet visited) and one corner of the view frame.

Moving the map (tried on the original with the mouse): there is no
scrolling at the edges. A click on a map square that is not next to the
active unit brings that square to the middle (column 7, row 6 of the view).
A click on the small map does the same for the square one up and to the
left of the one drawn under the pointer's tip; the small map then follows
the view.

## Resources

What 28 captures of a view-mode tour of one map showed (153 squares with a
resource out of about 1,800 explored ones). The resource sprite is drawn over
the finished square; which sprite depends on the ground:

| Ground | PHYS0.SS sprite |
|--------|-----------------|
| Ocean | 96 (fish) |
| Desert, scrub | 90 (oasis) |
| Plains | 91 |
| Prairie | 92 |
| Grassland | 93 |
| Savannah | 94 |
| Swamp, wetland and rain forest | 95 |
| Mixed forest | 97 (beaver) |
| Broadleaf forest | 98 (deer), seen twice |
| Conifer and tropical forest | 99 (timber) |
| Hills | 102 |
| Mountains | 101, and 89 twice |

Tundra, marsh and boreal forest showed none in this map; sea lanes never
have one.

Where they appear is not stored in the savegame: no bit of the four map
layers, nor any bit array elsewhere in the file, matches. It is computed, and
not with a simple period: no 4×4 or 8×8 block holds a fixed number. The one
strong clue is `(x + 19 × y) mod 64`: open ground, hills and ocean only take
a resource on 21 of the 64 values, forests on another 18, but even then only
a quarter to a half of those squares have one. The rest of the rule is
still unknown, so Viceroy does not draw resources yet.
