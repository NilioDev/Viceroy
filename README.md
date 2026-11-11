# Viceroy

**Play the 1994 DOS classic *Sid Meier's Colonization* on today's computers,
phones and tablets, without an emulator.**

Viceroy is a free, open-source, from-scratch engine for the original game.
Bring your own copy of the DOS version: Viceroy reads its files and plays the
game exactly as the original does, with the same rules, screens and
savegames, scaled sharply to any screen and with gamepad, mouse, keyboard and
touch controls.

> [!NOTE]
> **Status: first playable steps.** Load one of your savegames, move your
> units over the original map, board ships and make landfall, found
> colonies, look inside your colonies, end turns and save. The colony screen
> shows its buildings, people, what they produce and the warehouse; give
> colonists other jobs with the jobs menu. At the end of the turn colonies
> store what they make, grow and finish buildings. The other nations don't
> move yet, and the Europe and trade screens, combat and meeting the natives
> are still to come. Fish and other resources are not drawn yet.

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

## Controls

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Move the active unit | Numeric keypad 1-9, arrow keys, Home / PgUp / End / PgDn for diagonals, or click a square next to it | Left stick or D-pad |
| No orders this turn | Space | A |
| End the turn (when asked) | Enter | A |
| End the turn now | Ctrl+E | Y |
| Wait (come back to this unit later) | W | |
| Next / previous unit | Tab / Shift+Tab | RB / LB |
| Sentry | S | B |
| Fortify | F | Right stick click |
| Build colony | B | X |
| Centre the map on the unit | C | Left stick click |
| Look around the map (view mode), back to moving | V, M | Back, B |
| Open your colony | Click it, or Enter on it in view mode | A on it in view mode |
| Pick a colonist on the colony screen | Arrow keys, Tab | D-pad, LB / RB |
| Give the colonist another job (jobs menu) | Enter, then the arrow keys and Enter | A, then the D-pad and A |
| Close the colony screen | Esc, or Enter with nobody picked, or click Exit | B |
| Save the game | Ctrl+S | Start |
| Back to the load menu | Esc | Guide |

**Debug menu.** F12, or Back and Start together on a gamepad, opens a menu
for testing that is not part of the original game: reveal the map, add gold,
give units their moves back, end the turn, or jump to your next colony. On a
colony screen it adds goods, a colonist, a level to every building or Sons of
Liberty. Nothing changes unless you pick an entry.

Saving writes over the savegame you loaded, in Viceroy's library (see
below). Your original files are never changed.

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
