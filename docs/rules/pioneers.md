# Pioneers

What a pioneer does, measured on the original running in DOSBox: a pioneer
alone on a small island of one kind of land, given the order from the
Orders menu, then turn after turn ended until the work was done.

## The jobs

- **Plow Fields (P)** on open land marks the square plowed.
- **Clear Forest (P)**, the same entry on a forest, takes the forest away:
  mixed forest becomes plains, and so on (terrain − 8). The square is not
  plowed by it.
- **Build Road (R)** puts a road on the square.
- Hills and mountains can't be plowed: the menu doesn't show the entry.
  Arctic can.
- Each job uses 20 of the pioneer's tools. A pioneer whose tools run out is
  a colonist again, and the player is told (`@USEDUPTOOLS` of `GAME.TXT`).
- Clearing a forest brings lumber (read from the original) to the
  nation's nearest colony, when that is within 3 squares (the longer side
  plus half the shorter; of colonies as near, the last listed): 20 for
  each point of the forest's lumber yield plus one, but just 20 without a
  lumber mill, twice as much for a hardy pioneer, and no more than the
  warehouse still holds. The player hears `@CLEARCUT`.
- On a plowed square or one with a road, the entries are still there;
  picking them says "That land has already been plowed." (`@NOPLOW`) or
  "There is already a road here, Your Excellency." (`@NOROAD`).

## How long

The effort is the square's Improvement value in `NAMES.TXT` (the third
number: plains 3, mixed forest 4, marsh 5, swamp 7; hills and mountains use
their own rows), and 2 less for a road.

| Pioneer | Done after |
| --- | --- |
| Any other pioneer | effort + 1 turns |
| Hardy pioneer | effort ÷ 2 turns (rounded down); at once when that is 0 |

Measured: plowing plains 4 turns, clearing mixed forest 5, plowing marsh 6;
roads on plains 2, tundra 3, mixed forest 3, hills 3, swamp 6, mountains 6.
A hardy pioneer: plowing plains 1, clearing mixed or tropical forest 2 and
3, plowing swamp 3, a road on swamp 2, a road on plains at once.

Giving the order ends the pioneer's turn. The turn in which the work is
done is used up too: the pioneer waits for orders only a turn later.

The unit's byte 22 counts the turns: 1 when the order is given, one more at
each end of turn, back to 0 when the work is done.

## What the screen shows

- The panel, for a pioneer: the skill ("Colonist", "Pioneer"), then
  "(100 Tools)", the orders, the terrain, "(Road)" and "(Plowed)", one line
  every 7 rows from row 93. The skill and tools are gold, the square green.
- The orders box shows P or R. On orange (the Dutch) the letter is black.
- The messages stand in a box centred on row 144 (`y` = (288 − height) / 2),
  the frontiersman (`MSS3.SS`) at (86, `y` − 87) over it.
- When no unit is left to move, the white square stays on the last unit's
  square and the panel tells about it: "Locat: (x, y) n" from row 68, the
  country's name, the terrain, then each unit there (its square at (242,
  92), tools and orders beside it at x 260, rows 96 and 102) and "End of
  Turn" in white 25 rows below the last unit's top. *Guess:* `n` is the low
  four bits of the square's path layer (1 in the one capture).

## Not yet

- Natives may object to a road or a cleared forest near them
  (`@INDIANROAD`, `@INDIANFOREST`); that comes with the natives.
