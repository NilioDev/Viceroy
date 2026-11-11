# How Viceroy is built

## A one-to-one copy first

Without add-ons, Viceroy plays exactly like the 1994 DOS version: the same
rules, screens, computer players, numbers and savegames. Anything new, such
as gamepad controls or scaling to modern screens, is added on top without
changing how the game plays or looks.

When the original's behaviour is unclear, we observe the original program and
write down what it does (see the clean-room rules in `CONTRIBUTING.md`).
Behaviour that has been pinned down gets a test.

## Game content is data

The engine knows the mechanisms (movement, production, trade, combat,
diplomacy). The content they work on comes from data: units, buildings,
goods, terrain, Founding Fathers, nations, prices, texts and pictures. The
original keeps much of this in readable tables already (for example
`NAMES.TXT`).

## Packs and add-ons

- On first start, Viceroy imports the player's own original files into the
  **base pack** in its library (see [formats/pack.md](formats/pack.md)). For
  now the base pack holds the original files unchanged with a manifest; the
  game's tables will be converted into an open format there step by step.
  The base pack is the classic rule set.
- **Add-ons** are packs that are loaded on top of the base pack. They can add
  things (a unit, a nation, a map) or change values, but never change files of
  the base pack.
- A savegame records which add-ons were active.
- Add-ons hold data only, no program code, so that a downloaded add-on is
  safe to use. Every file in an add-on is checked like any other outside file
  (see `SECURITY.md`).
