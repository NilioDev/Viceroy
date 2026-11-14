# Contributing

## Clean-room rules

Viceroy is an independent, from-scratch implementation.

1. **Write your own code.** Do not copy or adapt code from other
   reimplementations, remakes or tools for the original game, whatever their
   license. They may be read only as documentation of a file format; describe
   what you learned in your own words in `docs/formats/` and implement from
   that description.
2. **No decompiled code.** A disassembler or debugger may be used to observe
   how the original program behaves. Record the behaviour as a specification,
   never paste decompiler output into the source tree.
3. **No game data.** Never commit original data files, executables, or
   anything extracted from them (sprites, pictures, fonts, palettes, sounds,
   text, rule tables). Tests that need game data must skip when the files are
   not present.
4. **No trademarks.** Do not use the original game's title, its publisher's
   names or logos in the program, the packages or the store pages. Saying
   which game Viceroy plays, in plain text in the README, is fine.

## Layout

| Path | What |
| --- | --- |
| `src/core` | Engine core. Platform independent, no SDL, fully unit-testable. |
| `src/app` | Platform layer and game executable (SDL3). |
| `tests` | Unit tests for the core. |
| `third_party` | Code by others, copied unchanged with its license: Nuked OPL3 (LGPL 2.1+), the FM chip emulator for the music. Not game code. |
| `docs` | Our own documentation: the file formats in `docs/formats`, and how the engine is built in `docs/architecture.md`. |

## Code style

C++20. Warnings are errors in CI (`-DVCR_WARNINGS_AS_ERRORS=ON`). Every source
file starts with `// SPDX-License-Identifier: GPL-3.0-or-later` (except the
unchanged files in `third_party`).

## Building

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

SDL3 is used from the system when found, otherwise the pinned release is
downloaded and built (`-DVCR_USE_SYSTEM_SDL3=OFF` forces that).

## Linux AppImage

`packaging/linux/make-appimage.sh build out` turns a Release build with SDL
linked in (`-DVCR_USE_SYSTEM_SDL3=OFF`) into
`out/Viceroy-<version>-linux-<arch>.AppImage`. CI makes one on every run
(artifact `Viceroy-Linux-AppImage`). The icon in `packaging/linux` is our own
art.
