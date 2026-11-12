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
the dotted edge of the unknown. The ground a forest square lends is the open
land under its trees, so scrub lends desert. Water squares are blended before
the coast is drawn over them. Checked pixel by pixel against five captures
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
Rivers run over the forest (checked against the view-mode captures). A
river's mask counts only neighbours that have a river themselves; a water
square counts when the map marks a river on it (the mouth), not for being
water.

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

## Beaches and river mouths

After the coast, a water square gets a beach (`PHYS0.SS` 150 to 153 for the
north-west, north-east, south-west and south-east corners) when the two sides
of that corner and the square between them are land and the two opposite
sides and their corner are water. The beach's open pixels show land ground:
from the west or east neighbour for the first three, from the square below
for the south-east one (measured on 68 beaches; the leftover differences are
a few pixels each).

A water square on which the map marks a river draws a mouth for each side
whose land neighbour has a river: sprite 140 + side for a major river, 144 +
side for a minor one (sides north, east, south, west).

## Still unknown

- Where fish and other resources appear. See "Resources" below.
- Where the orders box of a scout, treasure, wagon train or cavalry goes
  (they follow the dragoon or the artillery until measured).

## Colonies and units

Measured on the original (COLONY00, around Isabella; every unit in about
250 captures of the map):

- Only units the player may see are drawn: the player's own, and others whose byte 3
  has the bit 4 + nation of the player set. Units in a colony or a native
  settlement are not drawn on the map.
- Units on one square are chained (bytes 24 and 25 point to the next one);
  the unit drawn is the last of the chain, the one with no next (three
  stacks checked). The active unit always shows on its square.
- A native settlement with a mission has a black plate with a cross in the
  missionary's nation colour (raw byte 5 of the settlement, low nibble).
- First all colonies, in the order of the save, each with its name: a later
  colony covers the end of an earlier one's name. Then the units, which cover
  colonies and names.
- A colony picture (21 wide) is centred on its square, so it starts 2 pixels
  to the left. The owner's pennant (`ICONS.SS` 118 + nation) is drawn over
  the one in the picture, at the square's left + 3, top.
- A unit's sprite starts 2 pixels into its square; a person 6 or 7 wide
  (a pioneer) starts 3 pixels in. A black copy of its
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
ground. The view is a white frame of 15 × 12 squares. The frame's top row is
clamped so the small map never goes past map height − 41. The map window never
shows the outer row and column of the map: its corner stays between 1 and
width − 1 − 15 (and height − 1 − 12). All but 4 of the
small map's pixels matched; those are settlements the original leaves out
(perhaps not yet visited) and one corner of the view frame.

Moving the map (tried on the original with the mouse): there is no
scrolling at the edges. A click on a map square that is not next to the
active unit brings that square to the middle (column 7, row 6 of the view).
A click on the small map does the same for the square one up and to the
left of the one drawn under the pointer's tip; the small map then follows
the view.

## The mouse pointer

The original draws its own pointer into the picture: `CURSOR.SS` sprite 0,
the arrow, with its tip at the sprite's top-left corner. The sheet marks the
sprite's box with colour 9 in two corners, which is not drawn. Sprite 1 is a
second pointer whose use is not yet known.

## Resources

Measured on the original with two savegames revealed in full (every square
explored) and a view-mode tour over each map, plus one view of seven other
saves. All 152 resources of COLONY00 and 257 of COLONY09 lie on the rule
below; the squares where it predicts one and none shows are colonies and
settlements (their pictures cover it) and arctic squares.

**The pattern.** Cut the map into blocks of 4 × 4 squares. Each block holds
two pattern squares, (a, b) and (a + 2, b + 2) inside the block, the column
taken modulo 4. With bx, by the block's column and row:

- b = (bx + by + 1) mod 2;
- e = ((bx − b − by + 9) / 2) mod 8, and
  a = (2 × bit 0 of e + bit 1 of e + 2 × bit 2 of e + by) mod 4.

The whole pattern is moved by a seed kept with each game: byte 612 of the
block after the map. A square (x, y) is on the pattern when (x + 4 × (seed +
7) mod 64, y) is on the one above. The seed stays the same through a game
(four saves of one game have the same one); five games gave five seeds that
all fit.

**Which squares use it.**

- Open ground, hills, mountains and ocean: the square itself.
- Forest (without hills or mountains): the square 4 to the west, so clearing
  a forest moves the resource away.
- Sea lanes and arctic: never.
- Ocean: no fish when bit 2 of the square's mask is set. The original sets
  it on every pattern square three or more squares from land and on some
  two from land.
- Mountains: a silver deposit (sprite 101), or sprite 89 when mask bit 2 is
  set, which works like a worked-out deposit (see production.md).

**Which sprite** (`PHYS0.SS`, drawn last on the square):

| Ground | Sprite |
|--------|--------|
| Ocean | 96 (fish) |
| Desert, scrub | 90 (oasis) |
| Plains | 91 |
| Prairie | 92 |
| Grassland | 93 |
| Savannah | 94 |
| Swamp, wetland, rain forest | 95 |
| Mixed forest | 97 (beaver) |
| Broadleaf and boreal forest | 98 (deer) |
| Conifer and tropical forest | 99 (timber) |
| Hills | 102 |
| Mountains | 101 (silver deposit), or 89 with mask bit 2 |

*Guess:* tundra never came up on a pattern square in the tours; we draw
grain (91). Marsh has minerals (95): an ore miner there makes 3 more, as on
swamp (see production.md).

## Lost city rumours

Sprite 103. Seen on 36 squares in two games: every one is on the resource
pattern moved by (8, 8), so (x + 8, y + 8) is on the pattern, on land, and
the high nibble of the third map layer is 0xF there. About 40 % of the
squares that meet all of that have one; what decides the rest is not yet
known (no layer, bit array or list in the savegame matches), so Viceroy does
not draw rumours yet.

## Water colour cycling

With the game option for it on, the original cycles the palette colours of
the water, so captures of open sea differ from frame to frame. Not done yet.
