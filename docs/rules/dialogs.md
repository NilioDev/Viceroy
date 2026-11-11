# Message boxes

The original asks its questions in a wooden box. Compared pixel by pixel
with the high seas question (`@SAILHOME` in `GAME.TXT`).

## Text

A `GAME.TXT` section starts with lines such as `@width=190` (the width of
the text) and `@default=1` (the choice that starts highlighted, counted
from 1). Then come the text lines, a blank line, and one line per choice.
The text lines are joined and wrapped again to fit the width. `%STRING0`,
`%STRING1`, ... are filled in; words in `{braces}` are drawn in gold.

The font is `FONTINTR.FF`. Its pixel values 1, 2 and 3 take the colours
from `@COLORS` in `NAMES.TXT`: the letter (basic 0x44, or hilite 0x95 in
braces), enhance 0x80 and shadow 0x2F.

## The box

- 6 pixels wider than `@width`, filled with `WOODTILE.SS` tiled from its
  corner, edged like the jobs menu: black, a ring of 0x86, then a sunken
  edge (0x80 above and right, 0x8A left and below).
- Text lines start at (x + 5, y + 6), 10 pixels apart.
- The choices start 4 pixels below the last text line, 12 pixels apart, at
  x + 9. The chosen one has a bar of 0x8A from (x + 4, y − 1), as wide as
  the box less 8 and 11 high.
- The box ends 2 pixels below the last choice's 12-pixel line.

## Placement

The high seas question has the naval adviser (`MSS0.SS`) at (60, 37) and
the box at (64, 108); the portrait is drawn over the box. Other questions
are centred on the screen until we have measured them.
