# The colony screen

Measured on the original running in DOSBox with savegames of our own copy.
All positions are on the 320×200 screen.

## Layout

| Part | Where | What |
| --- | --- | --- |
| Top line | y 0-6, black line at 7 | `WOODTILE` tiled; "Name.  Season, Year.  Gold: N$" centred on x 160 in `FONTTINY`, colour 0x44 |
| Town | x 0-198, y 8-127 | `PARCH.SS` tiled from the screen origin; black line at x 199 |
| Side | x 200-319, y 8-127 | `WOODTILE` tiled; the area view in a black frame at (223, 31), 74×74 |
| Lower part | from y 128 | `COLONY.PIK` (320×72), with a black line at y 128 |

## Town (`BUILDING.SS`)

The town has fifteen spots (read from the original): seven small (23×27),
four wide (44×22), two large (53×37), the stockade's (73×18) and the docks'
(75×48).

| Spots | Where |
| --- | --- |
| small | (56, 13), (145, 15), (173, 18), (8, 41), (37, 45), (67, 54), (96, 53) |
| wide | (6, 14), (128, 53), (10, 76), (15, 102) |
| large | (87, 11), (66, 87) |
| stockade, docks | (123, 106), (123, 55) |

Each kind of building has a place among the spots of its size, counted in
the order the kinds first appear in `@BUILDING`: small custom house,
printing press, weaver, tobacconist, distiller, fur trader, blacksmith; wide
armory, schoolhouse, warehouse (with the stable), carpenter; large town
hall, church. Every colony shuffles the spots of each size among those
places, so that no two colonies look alike, but one colony always looks the
same: the C library's random numbers are seeded with the colony's square
(y × 256 + x) plus the game's seed, kept to 15 bits, and spot by spot, in
the order above, a free place of the spot's size is drawn at random (a
taken place is drawn again). Isabella in our test game comes out with the
identity, which is how it was first measured.

An empty spot shows trees or a shore instead (sprites 42-45, by its size).
The fortification's spot shows a rail fence (16) when there is none. The
warehouse and the stable share a spot: barn (15), corral (46) or both (47).

### People at work and what they make

Read from the original, by the size of the spot:

| Size | People at (x, y) | Their space | Row at (x, y) | Row width |
| --- | --- | --- | --- | --- |
| small | +3, +13 | 17 | −5, −2 | 20 |
| wide | +20, +9 | 21 | +0, +2 | 22 |
| large | +25, +23 | 25 | +0, +2 | 30 |

The people share their space centred in it. Each counts as wide as the
figure of an expert at the job (`ICONS.SS` 81 + job) plus 2 for the shadow;
they stand (space − width) / (n − 1) apart but at most one pixel more than
that width. A figure is drawn 2 pixels right of its place with its shadow
(colour 0x5F) on the place itself, one figure after the other. The selected
one has a bright green frame (colour 10) from 1 pixel left of its place to
1 pixel right of the figure, and from its second row to its last.

The row of what they make is drawn first, centred in its width, its number
3 pixels in from where the row starts. Nobody is drawn in a building that
is not there. The town hall's row shows the colony's bells but the printing
press's share, which the printing press shows; the church shows all the
crosses, its own included.

## Rows of goods

A row shows one icon per unit, `width + 1` apart, squeezed when the row would
not fit. The number is written in white on a black box over the first icon
(text at icon x + (width − text + 1) / 2 + 1, y + 2). A shortage is drawn as
the good with a red cross (`ICONS.SS` 55) and a red number.

## Area view

The colony's 3×3 squares at 24 pixels each, inside the frame at (224, 32).
What a square makes is centred in its top row (y + 1), the town commons'
second good at y + 14, and its worker stands at (x + 14, y + 6).

**Known difference:** the original draws the larger squares with more detail
than a plain enlargement of the 16-pixel map squares; we enlarge them for now.

## People view

The rebel flag (`ICONS.SS` 123) at (2, 132) with "N% (rebels)" in white at
(15, 133); the crown (124) at (104, 132) with the Tories' share right-aligned
before it. Colonists stand from (2, 142), 10 pixels apart. Below, at y 163:
the food eaten, the food left over, the crosses and the bells, each a row of
icons with 4 pixels between rows.

## Harbour, production, warehouse

"No Ships In Port" in colour 0x39 centred on x 163 at y 132, above six empty
holds (122) at (127 + 12k, 165).

The production view (x 213) has three rows: sugar, tobacco, cotton, furs;
rum, cigars, cloth, coats; lumber, hammers, ore, tools, muskets, silver. The
buttons are `ICONS.SS` 67-69.

The warehouse shows the 16 goods (`ICONS.SS` 22-37) centred on
x 10 + 19k at y 181, with the amount in colour 0x3D centred below at y 194.

## Jobs menu

Measured on Isabella with a petty criminal distiller selected; our drawing
matches it pixel for pixel. A box at (62, 20), 196 × 160, filled with
`WOODTILE.SS` tiled from its corner. Its edge, from the outside in: black, a
ring of colour 0x86, then a sunken edge (0x80 above and on the right, 0x8A
on the left and below). All text is `FONTTINY.FF`:

- the title in colour 0x44 at (67, 26): "Select a Profession for
  <expert plural>(<present job>):" (no space before the bracket);
- one line per job at x 75, y 36 + 8k, with "N <good>" right-aligned to end
  at x 245;
- the highlighted line has a bar of 0x8A at (64, y − 1), 190 × 7;
- "(More)" as the last line, and "(F1 for Help)" in gold (0x95)
  right-aligned to x 256 at y 171.

A title too wide for 186 pixels goes on to a second line 6 pixels lower,
and the box grows to hold every line: 24 + 8 pixels a line high, plus 6 for
each extra title line, centred on the screen. The job lines then start 16
(plus the extra) below the box's top and the help text sits 9 above its
bottom.

For an expert the first line is "(CLEAR SPECIALTY)" (`@MISC` row 44).
Choosing it asks `@LOBOTOMIZE`, naming the expert plural, from the buying
advisor placed as for BUY; "Yes" makes the colonist a free colonist (skill
0x1C). Measured on Curacao with a master gunsmith working as a blacksmith.

The menu opens on the colonist's present job. Teaching is offered only
where a school stands and only to an expert it can take (see below).

## Jobs outside the colony

"(More)" opens a second menu of the same make, 24 + 8 pixels a line high
and centred on the screen (with six lines: (62, 64), 196 × 72). The title is
the same; the lines start 16 pixels below the box's top. The first line is
"(More)" again (back to the jobs menu); then Colonist, Pioneer "(N Tools)",
Soldier "(50 Muskets)", Scout "(50 Horses)", Dragoon "(50 Muskets) (50
Horses)", right-aligned as in the jobs menu, and Missionary where a church
stands (or for a Jesuit). A job the warehouse cannot equip is greyed (colour
0x08) and cannot be picked. The pioneer takes the tools in twenties, at
least 20 and at most 100. What the person carries already counts as in the
warehouse. Converts go out only as colonists. Clicking the stockade with a
colonist picked opens the same menu, its first line then the present job
(which leaves things as they are). Measured on Curacao; our drawing matches
it pixel for pixel.

## Units outside the colony

The people standing on the colony's square (not artillery, wagon trains or
treasure) stand in the town over the stockade's spot, in the order of the
Units Present panel, 11 pixels apart and centred on x 157: the first at
157 − 11 × (n − 1) / 2, at row 111, each with a shadow of colour 0x5F two
pixels to the left. A picked one has a frame of 0x45. Measured with two and
three units on Curacao.

## Moving people

A click on a colonist (in a building, on a square or outside) picks them; a
click on a building, a free square or the stockade then moves them there. On
a square they keep their work on the land if they had some, fish on water,
and otherwise take the work that yields most there. Read from the original,
in its order, a move is refused with a message (most from the woman of the
BUY box, `MSS5`):

- out of the colony: a stockade keeps three colonists (`KEEPSTOCKADE`);
  under siege only soldiers and dragoons leave (`SIEGE`, the colonial
  adviser): the armed units (attack 2 or more) of nations without peace on
  the eight squares around outnumber the colony's own on its square and
  around; the last colonist asks first (`ABANDON`, or `ABANDON2` when the
  nation has fewer than two colonies after 1575), and the colony is gone;
- into the colony: 32 at most (`FULL`);
- fishing needs docks (`NODOCKS`, the admiral);
- teaching: the schoolhouse takes one teacher, the college two, the
  university three (`SCHOOL1`, `COLLEGE2`, `UNIV3`); only an expert teaches
  (`NOTEACHER`), one of level 2 needs a college and one of level 3 a
  university (`NEEDCOLLEGE`, `NEEDUNIVERSITY`);
- three to a building (`MORETHANTHREE`), counted by job, but only for the
  jobs after the distiller's: the distiller's house takes a fourth (seen:
  four free colonists there made 12 rum).

A colonist who goes out becomes a unit on the square and takes 50 muskets,
50 horses or the tools from the warehouse; a unit that comes in leaves
them there.

## Custom house

Read from the original: a click on the custom house (when there is one)
opens `@CUSTOM`, a box with a tick for each of the 16 goods, set for those
the custom house sells. Return keeps the ticks.

## Renaming

Read from the original: a click on the top line (y 0 to 6, the whole
width) asks `@RENAMECOLONY` with a line to type in (23 letters at most) that
offers the colony's name. Return takes the new name, unless nothing was
typed; Escape keeps the old one.

## Panel buttons

Right of the panel stand three buttons, `ICONS.SS` 67 (production), 68
(units present) and 69 (Make), at x 303 and y 132, 147 and 162. The one
whose panel is showing is pressed in: drawn a pixel down and to the right,
with a line of colour 0x3F above and left of it. The others stand out, with
the 0x3F line below and right of them.

## Units present

The middle button shows the land units standing in the colony (not the
colonists at work). The title, `LABELS.TXT` `@CMISC` row 1, is centred
like the Make panel's title. The units stand in a row at y 158, 18 pixels
apart from x 213, in the reverse order of the units on the square, each
with its shadow and orders box as on the map. A unit without orders shows
an "S" (sentry). The letters are in the darker half of the nation's colour,
on the Dutch orange too (0x05, unlike the map). Damaged artillery is the
small picture `ICONS.SS` 65, two pixels right of the place, with its box at
+6, +2, as on the map (both artillery on Curacao were damaged). The first unit has a frame of colour 0x0A at x − 1, y 157, 18 × 18.

Measured on Curacao in a save with two colonists and two artillery, and
with up to twelve units moved there.

The order is that of the original's list of units on the square (each
unit's bytes 24-27 link it to the next and previous unit there): the last
unit on the list stands first. We keep the savegame's order of units
instead, which is the same in the saves we measured.

Past five, the rest stand small on the horizon behind the row, 5 pixels
apart, each centred on x 214 + 5k (k from 0) with its top at y 151: the
unit's own picture shrunk to three eighths, taking rows and columns 1, 4,
7, 9, 12 and 15 of the 16. Where the original stops when there are very
many is not known; we stop at the buttons.

Clicking one of the five in front frames it in white (colour 0x0F) instead
of the green on the first unit, and asks `@COLONYUNIT` ("Options for
Colonists (Indentured Servants):", the profession in brackets except for a
free colonist) with the choices of `@UNITOPTIONS`:

- "Move to front." is left out for the first unit. It puts the unit at the
  end of the square's list, so it stands first; the white frame follows it.
- "Clear orders." is always offered.
- "Sentry / Board ship." is left out when the unit is sentried or has no
  orders (shown as sentried here).
- "Fortify." is left out when it is fortifying or fortified; it gives
  orders 5 (fortifying). Here its F is black, while the F of a fortified
  unit is dark orange like the other letters.
- "No changes."

The box is 22 pixels wider than the section's width for the unit, which
stands at (x + 5, y + 6) as in the panel's row; text and choices move 22
to the right, and so does the left end of the bar. The box is centred on
the screen (x = (320 − w + 1) / 2).

## Make panel

Measured on Curacao for a wagon train, the fort and the fortress at several
amounts of hammers, and "Nothing"; our drawing matches pixel for pixel.

- The name of what is being built (`@BUILDING` or `@UNIT`, or "Nothing")
  in colour 0x39, centred on x 256 at y 132 (x = 256 − (width − 1) / 2).
- BUY (not shown for Nothing) at (216, 138), 17 × 9, and CHANGE at
  (270, 138), 29 × 9: colour 0x39 along the top and left, 0x30 along the
  bottom and right, the word in white 3 pixels in and 2 down.
- The hammers so far in four rows at y 147 + 6r: each row holds a quarter
  of what is needed (n, rounded up) and the rows fill in order. Hammer k of
  a row stands at x 212 + 9k while the row fits (9n ≤ 92). Otherwise the
  hammers spread so the last one lands about 80 pixels on: with
  A = ⌈80n / (n − 1)⌉, hammer k stands at 212 + ⌊kA / n⌋. Once they stand
  closer than 2 pixels (A < 2n, so from 42 a row), each row also shows its
  count in white on black over its start, as the production rows do,
  2 pixels further right. Measured with 10, 13, 14, 20, 30, 40, 44, 48, 50,
  60, 64 and 80 a row (the capitol's 100 a row differs, but it is never
  offered).
- With nothing to build the hammers kept show as one row of at most 10,
  9 pixels apart.
- "(Requires N Tools)" in white centred the same way at y 170 when tools are
  needed.

BUY (or P, or Y on a gamepad) opens its question in a box centred across the
screen with its bottom edge at row 163, and the woman of `MSS5.SS` at
(130, top − 62) in front of it, her hands over its top edge. The price is in
[production.md](production.md#buying-the-construction).

## Select An Item To Build

CHANGE (or B) opens a wooden box like the jobs menu, 184 wide at x 68,
8n + 32 high for n things and centred on the screen (top (200 − h) / 2).
Measured on Curacao and Roanoke with 16 to 20 lines; our drawing matches.

- "Select An Item To Build" in 0x44 at (73, top + 6), "(No Production)" at
  x 77, top + 16, then one line per thing at top + 24 + 8k, its name in
  capitals.
- On the right, ending at x 243: "(N Hammers)" still wanted (never below 0)
  followed straight away by "(N Tools)" when it needs tools, or
  "(COMPLETE)" for something already built that the colony was still set
  to build.
- The highlighted line is gold (0x95) on a bar of 0x8A at (72, y − 1),
  176 × 7, and "(F1 for Help)" in gold ends at x 250, 9 pixels above the
  bottom.
- The list opens on what is being built now.

What the list offers is in [production.md](production.md#what-a-colony-can-build).
