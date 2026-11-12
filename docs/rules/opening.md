# The opening and the main menu

Measured on the original running in DOSBox, recorded at 20 frames a second.
Viceroy plays the opening from the player's own files; the publisher's logo
that comes before it is left out.

## Pictures

- `OPENBORD.PIK`: the wood above and below the map, with a window from row
  24 to 155 where the map shows.
- `OPENING.PIK`: the old map, 960 × 132, Europe at its right end and the
  coast of the New World at its left.
- `OPENMENU.PIK`: the still title picture behind the menu.
- The opening uses the palette of `OPENING.PIK` throughout.

## Timing

`OPENING.TXT` counts in frames. One frame lasted 131.6 ms on the recording
(Viceroy uses 132). The view starts at the map's right end and moves one
pixel west each frame, so it reaches the coast at frame 640.

## Credits (`@CREDITS`)

Each line is `start, end, series, sprite`: the sprite (counted from 1) of
`OPENCRD1.SS`, `OPENCRD2.SS` or `OPENCRD3.SS` (series 0 to 2) shows from the
start frame to the end frame, both included. It is centred in the wood below
the map: left `(320 − width + 1) / 2`, top `184 − (height + 1) / 2`. All 19
lines matched the recording.

## Animations (`@OPENING`)

Each line is `series, frame, repeats, baseX`. The series are, in order,
`OPENWND1.SS`, `OPENSUN.SS`, `OPENMON1.SS`, `OPENWND2.SS`, `OPENMON2.SS`,
`OPENMON3.SS`, `OPENFISH.SS`, `OPENGUY.SS`, `OPENLOGO.SS`, `OPENBONK.SS`.
From its frame on, a series shows one sprite a frame. A sprite's x on its
sheet is its middle and its y the row below it, on the screen; the x is
counted from `baseX` on the map, so it moves with the map (a `baseX` of 0
is the screen itself; the view stands still by then). Series `-1` ends the
opening: at frame 891 the menu comes up.

- With repeats above 0 the series plays that many times, then is gone.
- With repeats 0 it plays once and its last sprite stays: the ship aground,
  the title.
- The man wading ashore (series 7) ends by waving: his last six sprites over
  and over.
- The sun (series 1) changes its face only now and then, back and forth.
  The recording showed changes about 20 frames apart, irregularly; Viceroy
  changes it every 20 frames.

The pictures were checked pixel by pixel against the recording at ten
frames; only the mouse pointer differed.

## The ship

`OPENSHIP.SS` is the ship the view follows. Its course wanders up and down
and its eight sprites (four headings, each in two swells) change with no
pattern we could find, so Viceroy replays the course as measured, about
every four frames. From frame 640, when the view stops, it sails one pixel
west a frame until it runs aground at frame 701, where `OPENBONK.SS` takes
over.

## The main menu

The `@BEGINMENU` box of `GAME.TXT`: `@y=91` puts its top at row 91 and
`@options` starts the choices straight after the heading. The heading names
the program and its version; Viceroy puts its own name there. The box is
drawn as any message box, then its colours are swapped for the opening's:

| Game colour | 0x44 | 0x80 | 0x81 | 0x82 | 0x83 | 0x84 | 0x85 | 0x86 | 0x87 | 0x88 | 0x89 | 0x8A | 0x95 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| In the opening | 0xFE | 0xFD | 0xF5 | 0xF8 | 0xF6 | 0x2C | 0x3C | 0x2D | 0x30 | 0x2E | 0x35 | 0x37 | 0xFC |

except that in the wood itself 0x8A becomes 0x36 and 0x80 becomes 0xF5, and
the box's inner ring is 0x2E. After the opening has played to the end the
menu stands over its last picture, and only colours 0xFC to 0xFE take the
values of `OPENMENU.PIK`'s palette; after a skipped opening it stands over
`OPENMENU.PIK` with that picture's whole palette. Both were checked pixel
by pixel; only the heading and the mouse pointer differed.
