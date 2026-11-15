# A new world

How the original makes up a map for **Start a Game in NEW WORLD** and
**Customize New World**, sets the natives on it and gets the computer
players' tables ready. Everything here was observed by running the original
with chosen seeds and comparing every layer of the map, square by square,
with what Viceroy makes from the same seed. Viceroy's code is
`src/core/src/game/new_world.cpp`.

## The dice

The original draws from the C library's generator: the state is multiplied
by 214013 and 2531011 added; a number is bits 16 to 30 of the new state
(0 to 32767). A roll from *lo* to *hi*, both included, is
*lo* + ((*hi* − *lo* + 1) × number) / 32768. The map, the tribes and the
settlements all come from one stream of these rolls in the order given
below, so the same seed always makes the same world.

## The settings

There are five, each 0 to 3 in a NEW WORLD game (one roll each) and 0 to
2 on the Customize screen, where they start at 1:

| | 0 | 1 | 2 |
|---|---|---|---|
| land mass | small | moderate | large |
| land form | archipelago | normal | continents |
| temperature | cool | temperate | warm |
| climate | arid | normal | wet |
| hill country (not on the screen) | | | |

## The Customize screen

The picture CUSTOMIZ.PIK with its own palette. "CUSTOMIZE NEW WORLD" is
centred at the top in green with its edge colour, "(Click Here When
Finished)" centred at y 190 in the small font. The twelve pictures are four
columns (the four settings) of three (the values), each 72 × 48, the column
*c* at x = 10 + 76*c*, the row *r* at y = 16 + 60*r* (one higher for the
last row). The chosen picture of each column has a frame and, in the small
font with a black shadow one to the right, the setting's name with a colon
and its value below it; colour 14 (yellow) in the column being changed,
10 (green) in the others.

- Left and right, and Backspace and Tab, change the column (round the
  ends); up and down change its value (round); the space bar steps the value
  on. Enter finishes, Escape goes back to the menu.
- A click on a picture not chosen yet chooses it and makes its column the
  one being changed. A click below y 185 finishes.
- After that the game is started as after NEW WORLD: difficulty, nation,
  name and the rest, as in [new-game-screens.md](new-game-screens.md).

## The map

58 × 72 squares. The work happens on four layers: the terrain, a second
layer the generator uses for the height of the land (later the mask: the
Pacific, sea without fish), the regions, and a scratch layer. The layers lie
one after another and are read without looking at the edges: a square one
past the end of a row is the first square of the next row, and the
generator's results depend on that.

1. **Seed of the resource pattern**: a roll from 1 to 32767; its low byte is
   the map's resource seed (see [production.md](production.md)).
2. **Where land may go**: columns 3 to 52. One roll of 0 or 1 keeps the
   five northern rows free of land, or else the six southern ones; that
   side later becomes open sea.
3. **Land**: blobs are added until the land counted (a square counts once
   for every blob over it) reaches 320 × (land form + land mass + 1). A
   blob starts at a roll of 1 to 42 plus 7 across and 1 to 64 plus 3 down
   and is a walk of a few dozen steps, one square at a time to a diagonal
   neighbour, each step raising the square and the ones to its east and
   south. With land form 2 or more the walk is shorter and also raises each
   diagonal neighbour with odds of 1 in 4.
4. **Regions** are numbered (see below). If there are fewer than 15 bodies
   of land and water together, islands make up the rest (with land form 1
   or more, a random number of them fewer): each a short straight walk on
   open sea, two or three of them with odds of 3 in 10.
5. Two pieces of land touching only at a corner become a block of four.
6. **Latitude**: each square's distance from the middle row, made a little
   ragged with a roll per square, and moved by the temperature, gives its
   band: from the equator, savannah-like ground to tundra. One blob over a
   square makes it land, two make it hills, three mountains.
7. **Moisture**: each row is crossed from the west and then from the east
   with a store of wetness that the sea fills (more with a wetter climate,
   less near the middle latitudes) and the land uses up; mountains take
   three, hills stop it. Dry land becomes the drier kind of its band and
   wet land the wetter (marsh and swamp among them).
8. **Hills and mountains**: (hill country + 1) × 800 rounds, every other one
   at a new random square and the rest a step from the last. Hills become
   mountains; a mountain with no sea at any of its four corners is worn
   flat; other land changes its kind a little and may rise into hills (and
   from hills to mountains) with odds that depend on the kind. The odds are
   kept from one round to the next when a round changes nothing.
9. **Forests**: land the generator raised exactly once is forest with odds
   of 8 in 9; land by the sea is forest or the other kind of forest with
   odds of about one half each.
10. **Rivers**: up to 512 tries, until there are 8 × (climate + land mass +
   2). A river starts on a random flat land square and runs on, mostly
   straight, turning a quarter with odds of 4 in 10 (alternately left and
   right, swapping at random), until it meets the sea or another river. A
   river of fewer than three squares is undone. A river that reaches the
   sea may become a major river from the mouth upward; the twenty squares
   around the spring get forest at random.
11. **The edges**: four rows of ocean on the side kept free of land, ocean
   down columns 2 and 55 and along the top and bottom rows, and 40 arctic
   squares at random along the second and second-last rows.
12. **Sea lanes**: each row from the east as far as the first land (not past
   the middle), then ocean again within three squares of the easternmost
   land of each row, and no sea lane west of the first ocean. The rows near
   the poles get arctic and tundra.
13. **Last touches**: sea lane down the outer two columns of each side,
   arctic along the top and bottom rows; hills and mountains lose their
   forest. The regions are numbered again.
14. **The Pacific**: the water of each row from the west up to the first
   land, short of the middle.
15. **No fish**: ocean squares on the resource pattern with no land among
   the twenty squares around them (the 5 × 5 square without its corners).
16. **Where the ships come in**: four rows, a fifth of the map apart (14,
   28, 42 and 56), each at the westernmost square of the sea lane that
   reaches the east edge. The human player gets one of the middle two
   rows (a roll of 1 or 2); the others in turn a roll of 0 to 3 until a
   free row comes up.

### Regions

Water first, then land, the map is scanned row by row from the top, each
row from east to west; a square joins the region of a square above it
(three of them) or of the last square of its run, and two regions that
meet are merged into the lower number. The run carries over from the end
of one row to the start of the next. Land first seen on the second or
second-last row is numbered after the rest. At the end the regions are
renumbered 1 to 15 in the order of their squares, any past 15 sharing 15.
The square counts of the land regions are kept for the computer players.

## The natives

1. **The tribes**: each tribe's record gets its level from @TRIBES, its
   capital at (1, 1) until it has one, and for each nation a tension of a
   roll from 0 to 14, two more per difficulty level toward the human
   player.
2. **Capitals**, one per tribe in order: up to 12000 tries at a square 8
   to 50 across and 12 to 60 down that is flat land. It must be at least
   90 − tries/4 from the nearest settlement (distance is the longer side
   plus half the shorter); nearer than 8 only after (8 − distance) × 1000
   tries. The first two tribes keep to the west: x × 8 may not be more than
   the tries. And the 5 × 5 part of the map may not hold a settlement yet,
   for the first 10000 tries.
3. **More settlements**: the map is cut into 15 × 18 parts of 5 × 5 squares.
   Up to 2160 times: from a random tribe's capital, a random walk over the
   parts to one not used yet (a walk that leaves the map is wasted). In
   that part, the inner 3 × 3 squares that are open land or forest (no
   desert or swamp) with no settlement or unit on them or next to them are
   candidates; one is picked at random, and the settlement belongs to the
   tribe of the nearest settlement (the later of two as near). The part is
   then used, even when nothing was built.
4. **A brave for each settlement**, at a random square within two of it in
   the same land region, with nothing on it; up to 100 tries.
5. **Silver**: every mountain within two squares of a settlement adds the
   tribe's level to the tribe's silver.

A settlement starts with 2 × level + 3 people; a capital is marked as one.
The square gets the settlement mark and the tribe in the region layer's
high half.

## Ready for the computer players

Both a NEW WORLD game and a game on the map of America end the same way:

- **Site values**: for every land square, how good a place for a colony it
  is, 0 to 15, in the low half of the seen layer. The square itself counts
  4 and its twenty neighbours 9, 6, 4 or 3 by how near they are; each square
  is worth its resource's value, or for ocean a value from the land around
  it, or else its terrain's, one more with a river. The sum is halved
  unless the square lies next to the open sea; on mountains the value is 0
  and on hills half.
- **Route tables** and per-region counts, as in [routes.md](routes.md): the
  links between the map's blocks by land and by sea, how many farm squares
  and how many squares each land region has.
- The header says whether the map is America (1) or a new world (0); only
  the natives' code reads it.
