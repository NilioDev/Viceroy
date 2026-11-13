# Moving, turns and colonies

What `src/core/src/game/game.cpp` does, and where it still differs from the
original. The manual is the specification; where it is silent we mark the
rule as a guess until it is checked against the original.

## Movement

- Each unit has the movement allowance of its `@UNIT` row. Moves are counted
  in thirds: entering a square costs its terrain's movement value (from
  `NAMES.TXT`; mountains and hills use their own rows) times three.
- Following a road (both squares have a road or a colony) or a river (both
  squares have one) costs one third. *Guess:* the original may treat major
  and minor rivers differently.
- When a unit does not have enough moves left for a square, its turn is over
  (manual, "Moving Units"). *Guess:* a unit that has not moved yet this turn
  may always make one move.
- Land units stay on land, ships on water or in their own colonies. A land
  unit boards a ship by moving onto it if a hold is free, and is then on
  sentry. A ship steering onto land makes landfall: the units aboard wake up
  to go ashore.
- Foreign units, colonies and settlements block the square (no combat or
  meetings yet).
- Moving reveals the squares around the unit (read from the original and
  measured): one square; two for a galleon, privateer or frigate; a land
  unit sees two with Hernando de Soto in congress, and a scout one more
  than that (a dragoon 1, with de Soto 2; a scout 2, with de Soto 3).

## Turns

Ending the turn gives every unit its moves back and moves the calendar on:
one turn a year until 1599, then spring and autumn. The other nations do not
move yet.

## Colonies

Any colonist, soldier, pioneer, missionary, scout or dragoon can build a
colony on land that is not mountains and has no colony or settlement next to
it (*guess*: the original's exact distance rule is still to be checked). The
name is the nation's next unused name from `COLONY.TXT`. The founder becomes
the colony's first colonist, working as a farmer.

## Moving the view

Seen in the original: a click on a map square that is not next to the
active unit brings that square to the middle of the map window, and a click
on the small map in the panel does the same for the square under the click.
The active unit stays where it is.

A moving unit glides into its new square. When it has used up its moves the
game waits a moment before the next unit starts blinking. The timings
(160 ms a square, 250 ms pause) are chosen by eye, not yet measured.

Not yet matched: on loading `COLONY00.SAV` the original starts with the
frigate (unit 51) although the header's active unit is 88.
