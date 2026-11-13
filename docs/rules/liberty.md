# Sons of Liberty

How membership in the Sons of Liberty grows in a colony, and what it does to
the colony's work. Measured by changing the two numbers in the colony
record (bytes 194 and 198, see [the savegame](../formats/savegame.md)) and
ending turns in the original, about 60 colony turns in all, and by reading
the colony screen of test colonies.

## Membership

A colony keeps two numbers: the rebels R and the total T. Membership is
⌊100 × R / T⌋ percent. The colony screen shows it with the number of rebels,
(population × percent + 49) / 100 (halves round down: 5% of 10 is no rebel,
36% of 2 is one), and the rest as tories.

Every turn, after the colony has made its goods:

- T becomes T + 2 × population − ⌊T / 64⌋. T therefore settles at
  128 per colonist.
- R becomes R + bells − ⌊R / 64⌋ − ⌊5 × R / T⌋, with R and T as they were
  before the turn, and never more than the new T or less than 0.
- The bells are those the colony made this turn (its own 1, statesmen and so
  on).

A new colony starts at R = 0, T = 200 and at once gathers its first bells
(a new colony shows 1 of 199). A colonist joining a colony adds 100 to T and
the colony gathers again (1 of 199 became 2 of 299, 150 of 199 became 147
of 299).

A colonist who dies or leaves takes 100 from T (read from the original's
code).

## The bonus

At the end of the turn, after the bells, the colony's bonus moves one step
towards what membership allows: +1 from 50%, +2 at 100%. It never jumps two
steps: a colony set to 100% from nothing has +1 after the first turn and +2
after the second. The bonus is kept in byte 28 of the colony (0x04 for +1,
0x02 and 0x04 for +2), and the colony works with it from the next turn.
Membership changed in the save does not change the bonus until the turn
ends.

When the bonus changes the original tells the player, in the order it goes
through the colonies (the last founded first): `@REBELMAJORITY` (+1),
`@REBELUNANIMOUS` (+2), `@TORYMINORITY` (from +2 to +1), `@TORYMAJORITY`
(to nothing), with the colony's name, the country ("the Netherlands") and
the membership.

The box is at x 39 with its middle at row 139 (top = (278 − height) / 2);
the man in the tricorne with a musket (`MSS1.SS`) stands at (210, top − 77)
in front of it. Below the text come "Continue turn." and "Zoom to colony."
(`@MISC` lines 34 and 35 of `LABELS.TXT`); the map is centred on the colony
while the box is shown. The second choice opens the colony.

## What it does to work

The bonus is added to a colonist's work, and tories take some away: 1 for
every 10 − difficulty of them (10 tories at Discoverer, 6 at Viceroy). A
colony of 12 with no rebels at Viceroy loses 2.

- On the land the bonus goes into the land's own yield, before anything
  doubles it: a lumberjack expert on forest that gives 12 gives 16 with +1
  and 20 with +2. Expert farmers and fishermen, who make 2 more instead of
  twice as much, get the bonus twice. The penalty comes off the end: 12
  becomes 11.
- In a building each worker, expert or not, makes the bonus more and the
  penalty less (a carpenter's house with three experts and a colonist: 21,
  17 with a penalty of 1, 13 with 2).
- The colony's own square gets the bonus but not the penalty.

**Not yet done:** the nation-wide messages about rebel sentiment
(`@REBELUP`, `@REBELUP50`, `@REBELDOWN`), `@SONSUP`/`@SONSDOWN` and the
inefficient government message (`@INEFFICIENT`, `@EFFICIENT`). Founding
fathers that add bells (Jefferson, Paine) come with the Continental
Congress.
