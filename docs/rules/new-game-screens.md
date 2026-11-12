# The screens of a new game

What the original shows after **Start a Game in AMERICA**, measured on
captures of it running in DOSBox. Viceroy draws every one of these screens
the same pixel for pixel (checked against the captures, the mouse pointer
aside), with the exceptions listed at the end.

## Which map

The `AMERICA` message of `GAME.TXT` in a box centred on a black screen:
"Original America" or "Map Editor". Viceroy offers only the original map
for now; Map Editor goes back to the main menu.

## Difficulty

`DIFFICUL.PIK` with its own palette. On it, centred on column 57 in
`FONTINTR.FF` (letter colour 254, edge 253, a black shadow one pixel to the
right): "Choose" at row 16 and "Difficulty Level" at row 29, from the `MISC`
section of `LABELS.TXT`; "(Click Here When Finished)" in `FONTTINY.FF` at
row 81, colour 254, no shadow.

The five men stand in two rows, Discoverer to Viceroy:

| Level | Frame (x, y, w, h) | Colour |
|---|---|---|
| Discoverer | 128, 7, 68, 90 | 10 |
| Explorer | 233, 7, 68, 90 | 9 |
| Conquistador | 23, 103, 68, 90 | 14 |
| Governor | 128, 103, 68, 90 | 13 |
| Viceroy | 233, 103, 68, 90 | 12 |

The chosen one gets a one-pixel frame in its colour, its name in capitals
with a colon (`@DIFFICULTY` of `NAMES.TXT`) 38 rows below the frame's top,
and "Easiest" to "Toughest" (`LABELS.TXT`) 8 rows lower, both in
`FONTTINY.FF` in the frame's colour with a black shadow one pixel to the
right. Text of odd width sits half a pixel to the left: x = middle −
(width + 1) / 2 + 1. Every arrow key steps through the five in turn
(right and down forward, left and up back, round from the last to the first).

## Nation

`NATIONS.PIK`; "Select" at row 36, "European Power" at row 49, both centred
on column 56, and the click line at row 182. The flags, England to the
Netherlands, are framed at (112, 13), (211, 13), (112, 104) and (211, 104),
88 × 82, in the nation's colour from `@COUNTRY` (12, 9, 14, 13). The name in
capitals with a colon goes 2 rows below the frame's top, the nation's
strength ("Immigration", "Cooperation", "Conquest", "Trade") 74 rows below.
The flags lie in a square: up and down change the row, left and right step
through all four.

## The player's name

`WOODPANL.PIK` with the game palette. "Please Enter Your Name." (the
`LEADERNAME` message) centred at row 88 in the message box colours. The
field is a one-pixel frame at (79, 98), 168 × 14, in colour 0x44; the name
(at first the nation's leader from `@LEADERNAME`) is written at (82, 101)
over a mark in colour 0x8A from (81, 100), as wide as the name plus one and
10 rows high.

## The nation's two pages

`NATIONnA` and `NATIONnB` of `GAME.TXT` on `WOODPANL.PIK`: lines starting
`^^` are centred on their own (`^^_` is an empty line), the rest is joined
and wrapped to 300 pixels starting at column 10, in the message box
colours, 10 rows a line, and the whole page is centred on the screen's
height.

## The audience

`KINGLSS1.PIK` with its palette, the nation's banners (`ENGLND1.SS`,
`FRANCE1.SS`, `SPAIN1.SS`, `DUTCH1.SS`), then the king and his dog
(`KING1.SS`), each where its sheet places it: the position stored with the
sprite is the middle of its bottom row (checked for the English and Dutch
banners and the king). On the scroll, the `VICEROY` message (`VICEROY2`
for the Dutch, who have a stadtholder) in `FONTKING.FF` from (232, 21), 78
wide and 8 rows a line: pixel value 3 in colour 0, value 1 in 242.
`%COUNTRY` is the nation's name, "the Netherlands" for the Dutch.

## The departure

`LEVN0001.PIK` to `LEVN0010.PIK`, each with its palette, a new one every 9.6
seconds; each comes in over the last scattered pixel by pixel in about 1.3
seconds. Over each, `BUILD1` to `BUILD10` centred in 310 pixels at row 30,
in `FONTINTR.FF` with colours 14, 54 and 47. The blanks are filled with the
difficulty's name and the player's name, the home port, the country and
"Stadtholder" (the Dutch) or "King" (seen for the English), and the
country again. A key or click goes straight to the game.

## Not yet the same

- The original wraps the `AMERICA` question after "Americas map, or a map"
  and the second Dutch page one word later than Viceroy. Its line breaks
  do not follow from the widths of `FONTINTR.FF`'s letters alone; the rule
  is still to be found.
- The dissolve's pixel order is Viceroy's own.
- The original asks which of its ten slots to use when you save. Viceroy
  saves a new game in the first free slot, or over the one saved longest
  ago.
