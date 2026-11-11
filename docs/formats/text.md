# Text files (`*.TXT`)

The game keeps its tables (`NAMES.TXT`, `TRIBE.TXT`, ...), messages
(`GAME.TXT`) and encyclopedia (`PEDIA.TXT`) in plain DOS text: code page 437,
lines ending in CR LF.

- A line starting with `;` is a comment.
- A line `@NAME`, where the name has only capital letters, digits and `_`,
  starts a section. It may be followed by spaces and a `; comment`.
- Every other line belongs to the current section. Lines starting with a
  lower-case directive such as `@width=160`, `@default=2`, `@smallfont` or
  `@options`, and lines starting with `@@` or `@^`, are part of a message,
  not new sections.

## Tables

Table sections such as `@UNIT` or `@BUILDING` have one row per line. Fields
are separated by commas and trimmed; everything after `;` on a line is a
comment. Numbers are written in decimal; some columns hold flags written as
a row of 0s and 1s (for example `00011100`).

What each column of each table means is documented in `docs/rules/` as it is
worked out. The engine reads the numbers from the player's own files, never
from a copy in this repository.
