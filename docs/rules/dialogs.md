# Message boxes

The original asks its questions in a wooden box. Compared pixel by pixel
with the high seas question (`@SAILHOME` in `GAME.TXT`).

## Text

A `GAME.TXT` section starts with lines such as `@width=190` (the width of
the text) and `@default=1` (the choice that starts highlighted, counted
from 1). Then come the text lines, a blank line, and one line per choice.
The text lines are joined and wrapped again to fit the width. `%STRING0`,
`%STRING1`, ... and `%NUMBER0`, ... are filled in and `%%` is a percent sign; words in `{braces}` are
drawn in gold. A line is wrapped when the next word would make it wider
than `@width` − 5. `@y=91` puts the box's top at that row instead of
centring it, and `@options` starts the choices with no blank line before
them (the main menu, see [opening.md](opening.md)). A text line starting
with `^` starts a new line in the box (`@BUYME1`: "Treasury" on a line of
its own).

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
- `@smallfont` (the Train list) uses `FONTTINY.FF` without shadow: text
  lines 6 apart, choices 8 apart with a bar 7 high. The chosen line is
  drawn in gold (0x95) instead of green (seen in Pick Music).
- `@checkbox` (Sound Options, Colony Report Options) puts a ring before each
  choice: the font's `]` (a ring with a dot) when on, `[` (a filled ring)
  when off, in gold, then a space and the text. Space or the choice's key
  letter turns it over; Return keeps the changes, Escape drops them. A `~`
  in a choice makes the next letter gold, as in the menus.
- Lists of things to buy put the choices at x + 13 and a value such as
  "(Cost: 1000)" right-aligned to end at x + width − 13. Choices that can't
  be had are grey: letters 0x08, with the same enhance and shadow.
- Those lists also show "(F1 for Help)" in gold in `FONTTINY.FF`,
  right-aligned at x + width − 2 and y + height − 7 (− 9 with
  `@smallfont`); the box is 3 pixels taller for it (6 with `@smallfont`).

## Placement

The high seas question has the naval adviser (`MSS0.SS`) at (60, 37) and
the box at (64, 108); the portrait is drawn over the box. Other questions
are centred on the screen until we have measured them.

In the home port (Recruit, Purchase, Train and the purchase question) the
box is centred on the harbour, rows 80 to 199: y = (279 − height) / 2,
x centred on the screen. The agent (`MSS2.SS`) stands at (99, y − 78),
drawn over the box so his hands rest on its top edge. A box too tall for
the harbour (Train) is centred on the whole screen and the agent is not
shown.

Reports about a colony (Sons of Liberty) are described in
[liberty.md](liberty.md).

Portraits bring their own colours: palette entries 152 to 251 come from
the portrait's sheet while it is shown.
