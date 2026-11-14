# Founding a colony

Read from the original's Build Colony command (B) and seen with its colonist
at (17, 24) of `COLONY00.SAV`.

## What keeps a colony from being founded

In this order:

1. During the war of independence: `@NOCOLONIESEITHER`.
2. When the game has 48 colonies or the nation 38 of its own (and the
   square holds none): `@TOOMANYCOLONIES`.
3. On a square that holds a colony the command does something else (the
   unit joins its own colony; not built yet in Viceroy).
4. At sea: "It may surprise you to learn that colonies cannot be built at
   sea." (`@SEACOLONY`, the naval advisor).
5. Next to any colony: "This land is too near to Jamestown for a new
   colony" (`@TOONEAR`, from the woman of the buy box). A native settlement
   next door does not stop it.
6. On mountains: `@TOOMOUNTAIN` (the frontiersman).

## Warnings

Then the frontiersman warns; the first answer gives up, the second goes on:

- When no water square next to the site belongs to the open sea:
  "This square does not have access to the ocean" (`@NOPORT`).
- Below the third difficulty level, counting the site and its eight
  neighbours: a square counts as productive unless it is water, arctic,
  desert or a native settlement, and once more for a resource. Under four:
  `@TUTNOSPACES`. Without any forest among them: `@TUTNOLUMBER`.

## The name

The map is centred on the site and the woman of the buy box asks "What
shall we name this colony?" (`@COLONY`), offering a name from the nation's
list in `COLONY.TXT`: the line given by the nation's count of names (word
50 of its player record), which goes up by one whether the player keeps
the name, types another, or gives up with Escape. Past the end of the list
the first name is offered and the count stays. The computer players take
their names the same way. A name holds 23 letters; an empty one is
accepted. See [dialogs.md](dialogs.md) for the box.
