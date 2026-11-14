# Moving, turns and colonies

What `src/core/src/game/game.cpp` does, and where it still differs from the
original. The manual is the specification; where it is silent we mark the
rule as a guess until it is checked against the original.

## Movement

- Each unit has the movement allowance of its `@UNIT` row. Moves are counted
  in thirds: entering a square costs its terrain's movement value (from
  `NAMES.TXT`; mountains and hills use their own rows) times three.
- Following a road (both squares have a road or a colony) or a river (both
  squares have one, and the step is not corner to corner) costs one third;
  entering a colony or settlement costs at most a move (read from the
  original).
- When a unit does not have enough moves left for a square, it gets there
  with a chance of its moves left in the cost; otherwise its turn is over
  (read from the original). A unit that has not moved yet this turn always
  makes its move.
- Land units stay on land, ships on water or in their own colonies. A land
  unit boards a ship by moving onto it if a hold is free, and is then on
  sentry.
- Read from the original: a ship steering onto land with a unit aboard that
  still has moves asks `@LANDFALL` ("Shall we make landfall ... and leave the
  ships behind?"), or `@LANDFALL2` when the square has a river ("Our ships
  cannot navigate up this river"). "Stay With Ships" is the default and what
  Escape picks. "Make Landfall" wakes everyone aboard, and the last unit
  aboard with moves left steps ashore. With nobody able to go, nothing
  happens.
- Ships do not enter inland lakes (water that is not part of the main body
  of water, the low half of the path layer being 1): `@SHIPLAKE`.
- A ship without guns (not a privateer, frigate or man-o-war) moving onto
  foreign ships is told `@SHIPCOMBAT`; a land unit at sea moving onto a
  square with foreign units, a foreign colony or a settlement is told
  `@LANDFIRST`.
- Read from the original: a ship that ends a move on water next to the
  warships of a nation it is not at peace with (any nation, for a privateer)
  meets each of them in turn while it has moves left. A privateer would
  cost it 4 thirds of a move, a frigate 6 and a man-o-war 8. A roll from 1
  to its evasion plus the warship's evasion plus 2 (the evasion as for
  combat) below its own evasion lets it slip past (`@SHIPRUN`, "Dutch
  Merchantman slips past French Privateer!", when either side is the
  player); equal to it, the ship loses half; otherwise all of it
  (`@SHIPSLOW` for the player's ship).
- Foreign units, colonies and settlements block the square (no combat or
  meetings yet).
- Moving reveals the squares around the unit (read from the original and
  measured): one square; two for a galleon, privateer or frigate; a land
  unit sees two with Hernando de Soto in congress, and a scout one more
  than that (a dragoon 1, with de Soto 2; a scout 2, with de Soto 3).

## Turns

Ending the turn gives every unit its moves back and moves the calendar on:
one turn a year until 1599, then spring and autumn. The natives move (see
[natives](natives.md)); the other European nations do not move yet.

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
