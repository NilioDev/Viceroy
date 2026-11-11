# The colony screen

Measured on the original running in DOSBox with savegames of our own copy.
All positions are on the 320×200 screen.

## Layout

| Part | Where | What |
| --- | --- | --- |
| Top line | y 0-6, black line at 7 | `WOODTILE` tiled; "Name.  Season, Year.  Gold: N$" centred on x 160 in `FONTTINY`, colour 0x44 |
| Town | x 0-199, y 8-127 | `PARCH.SS` tiled from the screen origin; black line at x 200 |
| Side | x 201-319, y 8-127 | `WOODTILE` tiled; the area view in a black frame at (223, 31), 74×74 |
| Lower part | from y 128 | `COLONY.PIK` (320×72), with a black line at y 128 |

## Town (`BUILDING.SS`)

Every kind of building has a fixed spot. An empty spot shows trees or a
shore instead (sprites 42-45, by the spot's size). The fortification's spot
shows a rail fence (16) when there is none. The warehouse and the stable
share a spot: barn (15), corral (46) or both (47).

| Spot | Size | Buildings |
| --- | --- | --- |
| (123, 55) | 75×48 | docks 6-8, else shore 45 |
| (123, 106) | 73×18 | stockade, fort, fortress 0-2 |
| (87, 11), (66, 87) | 53×37 | town hall 9, church 37-38 |
| (6, 14), (10, 76), (15, 102), (128, 53) | 44×22 | warehouse/stable, carpenter 35-36, school 12-14, armory 3-5 |
| seven spots of 23×27 | | the craft houses, printing press, custom house |

**Still unknown:** the spots of buildings of the same size are shuffled from
colony to colony (in Isabella the town hall is at (87, 11), in Jamestown at
(66, 87)). We use the arrangement of Isabella for every colony until we know
the rule.

People working in a building stand at (x + 5 + 9k, y + 13); what they make is
shown as a row of icons at (x − 5, y − 2).

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
