# MADSPACK containers and FAB compression

Most pictures, sprite sheets and fonts of the original game (`*.PIK`, `*.SS`,
`*.FF`) are stored in the same container. Written from our own study of the
files; the code is in `src/core/src/data/madspack.cpp`.

## Container

All numbers are little-endian.

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 14 | Signature `MADSPACK 2.0` followed by `0x1A 0x00` |
| 14 | 2 | Number of entries, at most 16 |
| 16 | 10 × 16 | Entry table (unused slots hold leftover bytes) |
| 0xB0 | | Entry data, one after the other, in table order |

Each entry in the table:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 2 | Flags. Bit 0 set: the entry is FAB-compressed. The high byte varies per file and has no known meaning for reading. |
| 2 | 4 | Size after unpacking |
| 6 | 4 | Size in the file |

A stored entry has the same size in both fields. Some files have a few spare
bytes after the last entry.

## FAB compression

An LZ77-style stream with a separate supply of control bits.

- Bytes 0–2: `FAB`.
- Byte 3: the *shift*, 10 to 13 (the game's files use 12). With
  `L = 16 − shift`, long copies have an `L`-bit length field.
- Bytes 4–5: the first 16 control bits.
- From byte 6: literal bytes and copy parameters, read as they are needed.

**Control bits** are taken lowest bit first. When the 16th bit of a word is
due, the next 16-bit word is read from the stream instead, shifted up by one,
and the last unused bit of the old word becomes its lowest bit. (In practice:
each word gives 15 fresh bits plus one carried over.)

**Commands**, decided by the control bits:

- `1`: copy one literal byte from the stream to the output.
- `0 0 a b`: short copy. Length is `2·a + b + 2` (2 to 5). Then one byte `o`
  is read; the copy starts `256 − o` bytes back in the output.
- `0 1`: long copy. Two bytes are read, `lo` and `hi`.
  - The distance back is `0x10000 − offset`, where the offset's high byte is
    `hi >> L` with all bits above it set to 1, and its low byte is `lo`.
  - The length is `hi & (2^L − 1)`. If it is not 0, add 2.
  - If it is 0, one more byte `n` is read: `n = 0` ends the stream, `n = 1`
    does nothing, otherwise the length is `n + 1`.

Copies go byte by byte, so a copy may overlap the bytes it is producing
(this is how runs of one colour are made).

Viceroy rejects a stream that ends early, copies from before the start of
the output, or unpacks to anything other than the size in the table.
