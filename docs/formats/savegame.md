# Savegames (`COLONY00.SAV` to `COLONY09.SAV`)

Worked out by comparing the ten savegames that came with our copy and
checking the results against the map they show. Community notes on the
format were read as documentation only (see the clean-room rules).
Everything is little-endian. This page grows as more of the file is
understood; the parts marked *opaque* are kept byte for byte.

## Overall layout

| Block | Size in bytes |
| --- | --- |
| Header | 158 |
| Players and other game-wide data | 4 × 52 + 24 |
| Colonies | colony count × 202 |
| Units | unit count × 28 |
| Nations | 4 × 316 (*opaque*) |
| Native settlements | settlement count × 18 (*opaque*) |
| Tribes and other data | 8 × 78 + 727 (*opaque*) |
| Map: terrain | width × height |
| Map: mask | width × height |
| Map: path | width × height |
| Map: seen | width × height |
| Rest | 1502 (*opaque*) |

The three counts come from the header, so the start of the map moves with
the number of colonies, units and settlements. In all ten saves the blocks
add up exactly to the file size. The split between the nation block, the
settlements and the block after them is still to be confirmed; only their
total matters for finding the map.

## Header

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 9 | `COLONIZE` and a 0 byte |
| 12 | 2 | Map width (58 in a normal game) |
| 14 | 2 | Map height (72) |
| 26 | 2 | Year |
| 28 | 2 | Season: 0 spring, 1 autumn |
| 30 | 2 | Turn number |
| 34 | 2 | Active unit (index, -1 for none) |
| 40 | 2 | The human player's nation (0 English, 1 French, 2 Spanish, 3 Dutch) |
| 42 | 2 | Number of native settlements |
| 44 | 2 | Number of units |
| 46 | 2 | Number of colonies |
| 54 | 1 | Difficulty, 0 (Discoverer) to 4 (Viceroy) |

## Map layers

Each layer has one byte per square, row by row from the top-left.

**Terrain.** The low five bits are the terrain type:

| Value | Terrain |
| --- | --- |
| 0–7 | Open land, in the order of `@UNFORESTED` in `NAMES.TXT` |
| 8–15 | The forest of the open land 8 lower |
| 16–23 | Also forest; seen in some saves, treated like 8–15 |
| 24 | Arctic |
| 25 | Ocean |
| 26 | Sea lane |

The top three bits are relief and rivers: 1 hills, 2 minor river, 3 minor
river on hills, 5 mountains, 6 major river. (Checked: value 5 forms the
Andes and the Rockies in the saves.)

**Mask.** Flags per square: bit 0 a unit is here, bit 1 a colony is here,
bit 3 road, bit 6 plowed. The other bits are kept as they are.

**Path.** Region numbers used for route finding (kept as they are).

**Seen.** Bits 4 to 7: explored by the English, French, Spanish and Dutch.
Checked: in `COLONY00.SAV` (a Dutch game) exactly the squares without bit
7 are drawn as unexplored by the original. The low four bits are kept.

The turn number follows the calendar: one turn a year from 1492 to 1599,
then two (spring and autumn). All ten saves agree.

## Records

Every record is kept byte for byte; the fields below are read and written
back over it. Writing an unchanged save gives the same file (tested with all
ten saves).

**Player** (52 bytes): leader name (24, text), name of the new land (24),
flags (1), control (1: 0 human, 1 computer, 2 withdrawn), colonies founded
(1), diplomacy (1). The 24 bytes after the four players are kept as they are.

**Colony** (202 bytes): x, y (1 each), name (24), nation (1), …,
population at 31, the colonists' jobs at 32 (one byte each), their skills
at 64, …, the squares worked at 112 (8 bytes, 0xFF free), …, buildings from
132: the low three bits are the fortification (0 none, 1 stockade, 3 fort,
7 fortress).

**Unit** (28 bytes): x, y, type (row of `@UNIT`), nation in the low four
bits of byte 3, moves used at 5 (in thirds: a move along a road costs one),
orders at 8 (row of `@ORDERS`), cargo holds used at 12, skill at 23, and
the next and previous unit of a ship's cargo chain at 24 and 26. Units in
Europe or on the high seas have equal x and y above 200.

**Nation** (316 bytes): tax rate at 1, gold at 42 (32 bits).

**Native settlement** (18 bytes): x, y, tribe (4 to 11), …, population at 4.
