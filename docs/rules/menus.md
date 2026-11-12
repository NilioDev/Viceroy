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
