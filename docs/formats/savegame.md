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

**Mask.** Flags per square, among them: a unit is here, a colony is here,
road, plowed. Exact bits still to be confirmed.

**Path** and **seen.** Region numbers used for route finding, and which
nations have explored the square. Still to be documented in detail.
