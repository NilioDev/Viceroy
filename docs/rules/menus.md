# The menu bar

The menus come from `MENU.TXT`: one section per menu (`@GAME`, `@VIEW`,
`@ORDERS`, `@REPORTS`, `@TRADE`, `@PEDIA`; `@CUP` is the original's own
cheat menu, not on the bar). The first line is the title, the rest are the
entries. In both, `~` makes the next letter gold (the key for it) and `#`
stands for a blank as wide as a figure, so that "Zoom Level #60 x 48" lines
up under "Zoom Level 120 x 96".

Measured on captures of the original, all in pixels of the 320 × 200 screen:

- **Titles** sit on the bar at y 1 in the tiny font, green with the gold
  letter. The first starts at x 13, each next one 14 after the end of the
  one before; COLONIZOPEDIA ends at x 307.
- **Open title:** a dark (0x8A) block from one left of the title, four wider
  than the text, over the bar's rows 0 to 6.
- **Box:** from one left of the title (pushed left so it ends before x 318),
  top at y 9, ten wider than its longest entry, hidden entries included.
  Black frame; the wood inside is tiled from the box's own corner (not the
  screen's).
- **Entries:** text 5 in from the frame, the first 4 below its top, one
  every 8 rows. A line between groups takes one such row: green (0x44),
  across the inside, 2 rows below where text would start. The box ends 8
  rows below the last entry's top.
- **Colours:** green 0x44 with gold 0x95 letters; an entry that cannot be
  used now is grey (0x08), letter included.
- **Highlight:** keyboard arrows move a dark (0x8A) bar, 7 rows high from one
  above the text, from 2 inside the frame on the left to 2 inside on the
  right. The first arrow press highlights the first entry; a menu opened by
  a click has none.

Where the group lines go is not in `MENU.TXT`; seen in the original:

| Menu | Lines after |
| --- | --- |
| GAME | Colony Report Options, Pick Music, Load Game, DECLARE INDEPENDENCE |
| VIEW | European Status, Find Colony, Zoom Out, Zoom Level 15 × 12 |
| ORDERS | Sentry, Pillage, Return to Europe, No Orders |
| REPORTS | F1, F5, F9 |
| PEDIA | Terrain Types |

A line only shows between entries that are shown.

## What the orders menu shows

Seen in the original for a frigate at sea: Activate unit, Wait, Fortify,
Sentry, Go to Port, Begin Trade Route, No Orders and Disband Unit can be
used; Plow Fields, Build Road, Load and Unload Cargo, Return to Europe and
Dump Cargo Overboard are grey; Build Colony, Join Colony, Clear Forest,
Pillage, Go to Place and the second Fortify are not shown.

Seen for a pioneer on land: Activate unit, Wait, Fortify, Sentry, Build
Colony, Plow Fields (Clear Forest on a forest, neither on hills or
mountains), Build Road, Go to Place, No Orders and Disband Unit, all usable;
Build Colony even on mountains and arctic, where picking it says why not.
Load, Unload, Pillage, Go to Port, Trade Route, Return to Europe and Dump
are not shown. For other units
Viceroy follows the manual (pioneers clear, plow and build roads; land
units build or join colonies; soldiers pillage; ships and wagon trains trade
and carry cargo); these are still to be checked against the original. The
encyclopedia shows six of its eight entries.

Load Cargo (L) and Unload Cargo (U) work as the manual describes, for a
ship or wagon train in one of the player's colonies, on the map and on the
colony screen (for the chosen ship). Load puts the good of the warehouse
that sells for the most in Europe (up to 100, at the nation's price) in an
empty hold, leaving out horses, tools and muskets. Unload empties the first
hold into the warehouse; on the colony screen the warehouse warning comes
first, as when it is dragged. Pressed again, they go on hold by hold.

Seen in the original for a veteran soldier at war with France, standing on
road and plowed land next to Martinique, and on a plowed square that
Quebec works: Pillage is not shown either. No text in GAME.TXT belongs to
it, so Viceroy does not show it at all.

Entries Viceroy cannot do yet say so in the panel.

## Game Options

Read from the original and compared pixel by pixel. The box (`GAMEOPTIONS`)
stands at (62, 41). Its eight options are kept in header bytes 18 and 19 of
the savegame:

| Option | Bit | Viceroy |
| --- | --- | --- |
| Show Indian Moves | byte 19, 0x80 | kept; the other players' moves are not shown yet |
| Show Foreign Moves | byte 19, 0x40 | kept, as above |
| Fast Piece Slide | byte 19, 0x10 | units glide twice as fast (by eye) |
| End of Turn | byte 19, 0x08 | off: the turn ends by itself once no unit waits for orders |
| Autosave | byte 19, 0x04 | each new turn is saved in `COLONY09.SAV`; from the third turn on, the spring of a year ending in 0 goes to `COLONY08.SAV` instead |
| Combat Analysis | byte 19, 0x02 | the analysis before a fight |
| Water Color Cycling | byte 19, 0x01, set when off | kept; the water does not cycle yet |
| Tutorial Hints | byte 18, 0x80 | the hints given so far |

OK sets the eight bits anew and keeps the others of the two bytes (the
original masks the word with 0x207F). Every savegame of the original we have
seen has byte 19 at 0xC6, so a new game starts with it. The gold letter of
an option turns it over.

## The VIEW menu

Seen in the original:

- **European Status** opens the home port's screen, as E does.
- **Find Colony** asks "Where the heck is . . ." (`FINDCITY`) with an empty
  field. The whole name of a colony, in any case, brings the map round to
  it with the view cursor on it (View Pieces); anything else, part of a
  name too, says `"Bah" not found.` (`NOCITY`).
- **Show Hidden Terrain** draws the map without forests, roads, the land's
  resources, lost city rumours, colonies, settlements and units (the fish
  stay) until a key or a click, which does nothing else.
- **Zoom In** (Z), **Zoom Out** (X) and the four **Zoom Level** entries
  show 15 x 12, 30 x 24, 60 x 48 or 120 x 96 squares in the same window,
  16, 8, 4 or 2 pixels each. After a zoom the window is centred on the
  active unit (or stays where it was with none). A zoomed square is a
  shrunk picture of the full-size one without the coast and the blending
  of its edges; from 60 x 48 on, colonies, settlements and the units you
  can see are squares in their owner's colour. When the map is narrower
  or lower than the window it is centred and the rest is wood; at 120 x 96
  the whole map shows with the land's name (`@COLONYNAME` of NAMES.TXT,
  "New Netherlands") above it in white. The small map's frame is cut to
  the small map, and a window taller than the small map keeps the small
  map at its top.

## The Colonizopedia

Seen in the original (PEDIA.TXT holds the pages, `@PEDIA` the parts):

- Each of the six entries opens an index on WOODPANL.PIK: "ENCYCLOPEDIA OF
  COLONIZATION" (LABELS.TXT `@MISC` row 108) in white centred at y 5,
  "(Exit)" (row 110) in green ending 6 pixels from the right, and the names
  in green, alphabetical, from (7, 26), 7 rows apart, 24 to a column and
  columns 100 apart. The name picked is gold on a dark bar from one pixel
  above and left of it, as wide as the name plus 2 and 7 high.
- Left out of the index: hammers, crosses, bells and flags; the Teacher;
  the second and third Town Hall rows; the Capitol and its expansion.
  Terrain names are those of `@UNFORESTED` and `@OTHER`, and the
  `@FORESTED` ones followed by "Forest"; their pages are numbered by the
  terrain (0-15, then 24 Arctic to 28 Hills).
- Arrows move the bar, Enter (or a click on a name) opens the page, Escape
  or "(Exit)" closes the index. On a page any key or click goes back to
  the index, with the same name picked.
- A page: the title in gold, "(Name: Part)" in gold centred at y 13, then
  the text of `@CARGOn`, `@UNITn`, `@TERRAINn`, `@JOBn`, `@BUILDINGn` or
  `@FATHERn` from x 10, 7 rows a line: its first line (the name) alone,
  the rest wrapped, a line with only `^` leaving an empty line, braces in
  gold. The text starts at y 43 plus 20 for each row of goods, 69 for units (89 for the free
  colonist), 85 for terrain (or lower, below), 119 for skills, 67 for buildings and 33 for
  founding fathers, below the pictures of the page.
- **Wrapping**, worked out from the breaks of 170 lines on 44 pages: a
  word goes on the line while the line stays within 286 pixels, measuring
  each space as 1 pixel, a word that ends a sentence 1 pixel narrower and
  the space after it as 2, 3 more where a line of PEDIA.TXT ends without a
  trailing space, and the paragraph's last word 4 pixels wider. Seven of
  the 310 breaks seen do not follow this (Continental Cavalry, Furs,
  Tools, Free Colonist, Fisherman, Missionary, Pioneer). A line holding
  only `^` and a tab also starts a paragraph.
- **A unit's page:** the unit in the player's colour at (8, 33), with its
  orders box showing "-", and its name in green 6 rows lower, 18 pixels
  right of the last picture. Units with an expert (soldier, dragoon,
  pioneer, missionary, scout) show the plain one and the expert one, 18
  apart, titled "Soldiers (and Veteran Soldiers)"; artillery shows the
  damaged artillery second. The free colonist's page shows all 25 kinds of
  colonist, 17 to a row, rows 20 apart. 24 rows below the pictures, in
  green, each item 6 after the one before: "Combat: n", "(Veteran: n)" for
  soldiers and dragoons (half as much again), "(Attack: +n Damaged: -2)"
  for artillery, "Moves: n" and "(Cargo Holds: n)" for units that carry.
  The caravel's box is before its bow.
- **A good's page:** one row for each good of its chain, 20 apart from y
  33: food and fish; sugar and rum; tobacco and cigars; cotton and cloth;
  furs and coats; lumber and hammers; ore, tools and muskets; silver alone.
  Each row: the expert at (10, y - 2), one load at x 24, six loads from x
  40 four apart, and "Cloth (With Master Weavers)" in green at (76, y + 4).
  Horses and trade goods have no expert: their row starts 14 further left
  and has only the name. The text starts at y 43 + 20 per row.
- **A skill's page:** the expert (the convert's picture is the one icon
  66) at x 10 and the experts' name ("Master Blacksmiths") in gold 14 to
  its right. The buildings they work in (BUILDING.SS, sprite = row of
  `@BUILDING`) stand 24 right of that name, from y 22, each 4 rows below
  the one before, with their names in gold 3 right of the picture; the
  statesman's shows only the first Town Hall, farmers, planters, trappers,
  miners and fishermen none. What they make (fish for fishermen, crosses
  for preachers, liberty bells for statesmen) stands 24 right of the
  first building's name, or of the experts' name without buildings, with
  its name in gold 16 right of its left edge. The expert's top is 33, or
  centred on the first building (22 + (height − 16) / 2 + 1); the good is
  2 lower and the names 6 below each top (7 + half the difference for the
  buildings). The text starts 4 rows below the last building, or at 53.
- **A building's page:** the building (its BUILDING.SS sprite) at
  (10, 33) with its name in gold 3 to its right, then 24 further on the
  experts who work in it and, 24 after their name, what they make, as on
  the skill's page (the good's name 14 right of its picture here), facing
  the middle of the building. The text starts 12 rows below the picture.
  A building that replaces another (Magazine, Arsenal, Fort, College,
  Warehouse Expansion, ...) first says "Prerequisite: Armory" (`@MISC`
  row 188) in green there, and the text starts 20 rows lower. The
  stable shows the town's stables (sprite 46); the warehouse expansion
  has no picture, its name at x 10, laid out as if 24 rows tall.
- **A terrain's page:** a picture of three squares by three of the terrain
  at (9, 23) in a two-pixel frame, joined up among themselves (forest,
  hills, mountains), the terrain's resource in the middle, a minor river
  coming in from the west down the left column (not on arctic, hills,
  mountains or water) and a road going down the right column (not on
  water); on open ground the corners are its forest (scrub for desert)
  and the bottom middle is plowed. Right of it, from y 21 and 16 apart, a
  row for each job the terrain yields anything for: the expert at x 63,
  then from x 75, 8 pixels apart, "Farmer: 2" in gold (the yield, one
  more for farmers and fishermen, twice for lumber), "Plow/River: +1",
  "Road/River: +2" or "Coast/River: +1" in green, the resource's picture
  6 to the left of where the next text would go and its name 12 further
  on ("Game: +2/4" for farmers and fishermen, "Prime Cotton: x2" for
  crops it doubles; the tobacco's is only "Prime") in gold, and "Expert:
  +3" (farmers, fishermen) or "Expert: x2" in green. Below the rows,
  "Move Cost: 2" and "Defense or Ambush Bonus: +50%" (25% for each point
  of the table) in green. The text starts at y 85, or 23 below that last
  row when it is lower.
- F1 on the map opens the page of the active unit's square (its mountains
  or hills, else its forest or ground); a key goes back to the map.
