# Palettes, pictures and sprite sheets

## VGA palette (`VICEROY.PAL`)

256 colours × 3 bytes (red, green, blue), each 0–63 as the VGA hardware
uses them. `VICEROY.PAL` is 1024 bytes; only the first 768 are colours.
Viceroy scales to 0–255 with `(v · 255 + 31) / 63`.

## Pictures (`*.PIK`)

A MADSPACK container with two or three entries:

| Entry | Contents |
| --- | --- |
| 0 | 8 bytes: height (u16), width (u16), then 4 more bytes |
| 1 | `width × height` palette indices, row by row |
| 2 | Optional: a 768-byte VGA palette |

Most pictures are 320 × 200. The title picture is three screens wide.
Pictures without their own palette use `VICEROY.PAL`.

## Sprite sheets (`*.SS`)

A MADSPACK container with four entries:

| Entry | Contents |
| --- | --- |
| 0 | 152-byte header (not needed for drawing; still to be documented) |
| 1 | One 16-byte record per sprite |
| 2 | 768-byte VGA palette |
| 3 | The encoded pixels of all sprites |

Sprite record:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | Offset of the sprite in entry 3 |
| 4 | 4 | Length of the sprite in entry 3 |
| 8 | 2 | X position on the artist's sheet |
| 10 | 2 | Y position on the artist's sheet |
| 12 | 2 | Width |
| 14 | 2 | Height |

### Sprite encoding

Line by line, each line starting with a command byte:

- `0xFF`: an empty line.
- `0xFC`: the sprite ends; all remaining lines are empty.
- `0xFE`: a *raw* line. Each byte is one pixel, except `0xFE n c`, which is
  `n` pixels of colour `c`.
- `0xFD`: a *runs* line. Pairs `n c` give `n` pixels of colour `c`.

In both kinds of line, `0xFF` ends the line. Colour `0xFD` means
transparent. A line can be shorter than the sprite; the rest is transparent.
