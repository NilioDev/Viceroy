# gamedata/

Put your own copy of the original 1994 DOS game files here. Everything in this
folder except this README is ignored by Git and must never be committed.

The easiest way is to copy the complete game folder (the one with
`VICEROY.EXE` and `VICEROY.PAL`) into this folder, so that those files sit
directly in `gamedata/`. File names are matched without regard to case, and
other files are ignored.

When the program starts and has not imported a game yet, it imports this
folder into its own library by itself (from the repository root, or with a
`gamedata` folder next to the executable). Tests that need the original files
skip when they are not here.
