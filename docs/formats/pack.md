# The game library and packs

The player hands Viceroy their own copy of the DOS game once. Viceroy checks
it and keeps it in its own library, in the app's data folder
(`SDL_GetPrefPath("Viceroy", "Viceroy")` + `library/`). The code is in
`src/core/src/pack/library.cpp`.

```
library/
  base/
    pack.json        manifest of the base pack
    original/        the original files, upper-case 8.3 names
  saves/             savegames
```

## Import

The source is a folder or a `.zip` file (stored or deflated entries; see
`src/core/src/util/zip.cpp`).

1. Find the game: the shallowest folder that holds `VICEROY.PAL`, at most
   four levels down. Folders and files whose name starts with `.` and
   `__MACOSX` folders are skipped, so Mac leftovers never count.
2. Take every file directly in that folder whose name, in upper case, is a
   plain DOS 8.3 name (letters, digits, `_` and `-`). Anything else, such as
   `../X` or names with odd characters, is ignored, so nothing can be written
   outside the library. Subfolders are ignored.
3. Read the key files with our own readers: `VICEROY.PAL`, `TERRAIN.SS`,
   `PHYS0.SS`, `FONTSMAL.FF`, `NAMES.TXT` (the full rule set) and `GAME.TXT`.
   Any failure stops the import with a message for the player, and the
   library stays as it was.
4. Write everything to `library/base.new/`, then swap it in for `base/`.
5. Copy the `.SAV` files to `saves/` unless a save of that name is already
   there.

Limits that only hostile input reaches: 64 MB per file, 512 MB and 4096
files in total. Deflate output is capped at the size the zip directory gives,
so a "zip bomb" is refused.

## `pack.json`

```json
{
  "format": "viceroy-pack",
  "formatVersion": 1,
  "id": "base",
  "kind": "original",
  "files": [
    {"name": "AMER2.MP", "size": 12534, "crc32": "a43d15f7"}
  ]
}
```

`kind: "original"` means the files are the original game's, unchanged.
Add-on packs will use the same manifest with their own `id` and `kind`.
