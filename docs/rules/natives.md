# Natives

What happens when a colonist walks into a native settlement, worked out on
the original running in DOSBox: by changing fields in a save, entering a
village and reading what the original then says, and by stepping through
the original with the DOSBox debugger to see which fields it reads. The
numbers below are our own description of that behaviour.

## Entering a settlement

A unit that is not a ship and cannot attack (a colonist, pioneer, scout,
missionary and so on) that moves onto a settlement is greeted with a box in
the middle of the screen. The text depends on how alarmed the tribe is by
the unit's nation (the tribe's *tension*, 0 to 100 and more):

| Tension | Greeting (`GAME.TXT`) |
| --- | --- |
| 75 or more | `@VILLAGEWAR` |
| 50 or more | `@VILLAGEBAD` |
| 25 or more, or the settlement's own alarm at 128 or more | `@VILLAGEMEDIUM` |
| otherwise, for the Arawaks | `@VILLAGESAVAGE` |
| otherwise | `@VILLAGEHAPPY` |

The first blank is the settlement's word from the second column of
`@LEVELS` in `NAMES.TXT` (Camp, Village or City by the tribe's level), the
second the tribe's name ("Sioux").

The options come from `@ACTIONS`. A colonist or pioneer of a nation that
has met the tribe is offered **Live Among The Natives**; everyone is
offered **Cancel Action**, which leaves the unit where it was. A scout of
a nation that has met the tribe is offered **Ask to Speak With Chief**;
the original also offers it Demand Tribute and Attack Village, which
Viceroy does not have yet. Without contact with the tribe only Cancel
Action is there. Missionaries and soldiers get other options, which
Viceroy does not have yet either.

## Learning a skill

Living among the natives uses up the unit's moves for the turn, whatever
the answer. The tribe's chief (the tribe's own portrait) answers, checking
in this order:

1. Tension 50 or more: `@LEARNMAD`, nothing is taught.
2. A petty criminal: `@LEARNCRIMINAL`.
3. An Indian convert: `@TEACHCONVERT`.
4. Anyone but a free colonist or an indentured servant (an expert
   already): `@LEARNMASTER`, naming the unit's own skill.
5. A settlement that has taught before, unless it is the tribe's capital:
   `@LEARNALREADY`. A capital teaches again and again.
6. Tension 25 or more: a roll of 1 to 1000 below 200 × difficulty + 100
   gives `@LEARNSLOW`, nothing is taught this time.
7. Otherwise `@LEARNSTAY` offers the settlement's skill. Accepting makes
   the unit an expert in it (`@LEARNDONE`) and marks the settlement as
   having taught; declining answers `@LEARNLATER`.

## A scout speaks with the chief

Speaking with the chief uses up the scout's moves. The chief (the tribe's
portrait) first checks for broken taboos:

1. Tension 75 or more breaks a taboo.
2. Otherwise a roll of 0 to 100 is made (0 to 140 for a seasoned scout).
   With tension 25 or more, a roll at or below a quarter of the tension
   breaks a taboo.
3. The Arawaks also take offence on a roll of 0 out of 0 to
   8 − difficulty (doubled for a seasoned scout).

A broken taboo kills the scout (`@CHIEFKILL`). A nation with Francisco
Coronado in its congress is only told it is welcome (`@CHIEFBORED`)
instead.

Otherwise the chief greets the scout (`@CHIEFHOWDY`). The greeting names
the village's experts (the second column of `@JOB` for the skill it
teaches) and the three goods it wants most. Those come from the
settlement's economy, after taking out the goods it makes most of, food
when it makes more food than trade goods, and the goods in the
settlement's bytes 8 and 9. They are sorted with the original's own
sort, whose order for equal values decides ties.

Then, if the roll was above the tension and no scout has spoken with this
chief before, the settlement remembers the visit and a roll of 1 to 3
decides:

1. **Guides** (`@CHIEFGUIDES`): the scout becomes a seasoned scout. A
   seasoned scout gets the tales instead.
2. **Tales of nearby lands** (`@CHIEFAREA`): the squares next to the
   scout and the land of the same region within 6 squares are explored.
3. **A gift** (`@CHIEFGIFT`): three rolls of 1 to 10 − difficulty, added
   up, times a roll of 1 to 6, times 4, times the tribe's level + 1, in
   gold.

In every other case the chief only says the scout is welcome
(`@CHIEFBORED`). The tribe is named by its adjective ("Arawak") and the
nation by its own ("Dutch").

Checked on six settlements of four tribes: the experts and the goods
asked for match the original's greetings.

## The skill a settlement teaches

A settlement always teaches the same skill in a game. The original works
it out each time from the land around it, with a random pick whose seed is
fixed by the settlement's square:

1. **The land.** The 25 squares of the 5 × 5 block around the settlement
   are counted by kind, leaving out the edge of the map and squares a
   colony works. Open land, forests, hills, mountains, arctic and water
   each feed their own tallies (for example sugar from savannah, swamp
   and tropical forest, tobacco from grassland, marsh and conifer forest,
   cotton from prairie and broadleaf forest, furs from forest, ore from
   hills and mountains).
2. **The economy.** From the tallies, the settlement's size and the
   tribe's level (semi-nomadic, agrarian, advanced, civilized) come two
   lists of 16 goods: what the settlement offers and what it is after. A
   capital counts double. What the tribe already holds shifts both lists.
   This is the same economy the original uses when trading.
3. **The weights.** The second list is the weight of each good. Horses
   never count. Lower tribes lose some goods: below agrarian no coats and
   no ore and half the food, below advanced no cloth, cigars or silver and
   a quarter less food, below civilized no rum; a civilized tribe adds half
   again to silver.
4. **The pick.** The original's C library random generator is seeded with
   (row × 256 + column + the game's seed) and draws a number from 1 to the
   total weight; walking the goods in order picks one. The good's row is
   the row of the skill in `@JOB` (food is farmer, furs fur trapper, and so
   on).
5. **Two exceptions.** Furs on a square whose column + row is a multiple
   of 3 teach scouting instead. Food teaches fishing instead when a second
   roll of 1 to 20 is below the number of water squares in the ring of 20
   around the settlement.

Checked against eleven settlements in one game, among them four Sioux
camps (fur trapping), Iroquois villages teaching farming, scouting and
fishing, Cherokee villages teaching ore mining and cotton planting and
an Apache one teaching cotton planting; all eleven match.

## Save fields

See [the savegame format](../formats/savegame.md) for the settlement
record, the tribe records and the nation's contact bytes.
