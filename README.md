# Viceroy

**Play the 1994 DOS classic *Sid Meier's Colonization* on today's computers,
phones and tablets, without an emulator.**

Viceroy is a free, open-source, from-scratch engine for the original game.
Bring your own copy of the DOS version: Viceroy reads its files and plays the
game exactly as the original does, with the same rules, screens and
savegames, scaled sharply to any screen and with gamepad, mouse, keyboard and
touch controls.

> [!NOTE]
> **Status: just started.** There is nothing to play yet. The program shows
> the map of the newest savegame with the original terrain tiles; forests,
> hills, rivers and coastlines are not drawn yet. Scroll with the arrow keys,
> the numeric keypad, or a gamepad's D-pad or left stick.

Viceroy is not affiliated with or endorsed by the makers of the original
game. It contains none of the original game's files: you need your own copy.

## Planned

- 🎯 A one-to-one copy of the DOS version: same rules, same look, same
  computer players.
- 🖥️ Windows, macOS, Linux and Android from one codebase.
- 🎮 Full gamepad support: move units with the stick or D-pad, jump between
  units and colonies with the shoulder buttons, and a cursor that snaps to
  buildings, colonists and cargo in the colony and Europe screens. Every
  button can be changed.
- 💾 Your old savegames keep working.

## Try it

There are no releases yet. Every build on `main` makes test packages: open
the [latest CI run on main](https://github.com/NilioDev/Viceroy/actions/workflows/ci.yml?query=branch%3Amain)
and download one at the bottom of the page.

- 🐧 **Linux:** `Viceroy-Linux-AppImage`. Unpack the zip, make the
  `.AppImage` executable (right-click, *Properties*, *Allow executing file as
  program*, or `chmod +x`) and double-click it.
- 🪟 **Windows:** `Viceroy-Windows`, then start `Viceroy.exe`.
- 🍎 **macOS:** `Viceroy-macOS`.

## Add your game

You hand Viceroy your own copy of the DOS game **once**. On first start it
asks for it:

- drop the game folder (the one with `VICEROY.EXE`) or the `.zip` it came in
  onto the window, or
- press **Enter** or the gamepad's **A** button to choose the folder, or
  **Z** or **Y** to choose the zip file.

Viceroy checks the files, copies them into its own library and uses them from
then on, so you can delete or move the original afterwards. The savegames that
came with the game are copied too; your own saves are never overwritten. The
game may sit in a subfolder of the folder or zip, and Mac leftovers such as
`__MACOSX` are ignored. To import again, for example from another copy, just
drop it onto the window.

The library is in `~/.local/share/Viceroy/Viceroy/library` on Linux,
`%APPDATA%\Viceroy\Viceroy\library` on Windows and
`~/Library/Application Support/Viceroy/Viceroy/library` on macOS.

If there is a `gamedata` folder with the game next to the program, next to the
`.AppImage` file or in the current folder, Viceroy imports it by itself. You
can also give a folder or zip when starting it: `./Viceroy path/to/col.zip`.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
