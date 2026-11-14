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
