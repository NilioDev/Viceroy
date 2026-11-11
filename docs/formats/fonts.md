# Fonts (`*.FF`)

A MADSPACK container (see `madspack.md`) with one entry. In that entry:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 1 | Height of every glyph in pixels |
| 1 | 1 | Width of the widest glyph |
| 1 + c | 1 | Width of character `c` (for `c` = 1 to 126) |
| 128 + 2c | 2 | Offset of character `c`'s pixels in this entry (u16) |

Width 0 means the character has no glyph. The two tables overlap at byte
128, so character 127 has no usable entry.

Pixels use two bits each, leftmost pixel in the highest two bits of a byte.
Each row starts on a new byte, so a row takes `ceil(width · 2 / 8)` bytes, and
there are `height` rows. Value 0 is transparent; 1, 2 and 3 select one of the
three colours the game draws the text with (for example text, shadow and
outline).

Some fonts only have capital letters: their lower-case characters point to
the same pixels as the capitals.
