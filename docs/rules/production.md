# Production

What a colony makes each turn. The base values are the yield columns of the
terrain tables in `NAMES.TXT` (see names.md). The rules below were measured
on the colonies of the original's savegames; they are a first model and will
be checked against more colonies.

## On the land

| Job | Amount |
| --- | --- |
| Farmer | yield + 1, + 1 more when plowed |
| Sugar, tobacco, cotton planter | yield, + 1 when plowed |
| Lumberjack | yield × 2, + 2 with a road |
| Ore or silver miner | yield, + 1 with a road |
| Fisherman | yield + 1 |

A river adds 1 to all of them. An expert farmer makes 2 more; other experts
make twice as much.

The colony's own square (the town commons) counts as plowed. It makes food
and one other good: furs in a forest, otherwise its best crop or ore, at its
yield + 1.

Checked: Isabella (savannah commons: 5 food, 4 sugar), Jamestown (tropical
forest commons: 4 food, 3 furs; farmers on plowed savannah: 5, expert 7;
lumberjacks: 4, with a road 6).

**Not yet known:** fish and other resources (where they are and what they
add). Jamestown's fisherman makes 7 on an ocean square where this model
gives 4.

## In buildings

A free colonist makes 3, an expert 6, an indentured servant 2 and a petty
criminal or a convert 1. The lumber mill doubles a carpenter's work and a
factory adds half. Every colony also makes 1 bell and 1 cross of its own.
Workshops use up as much of their raw good as they make.

Checked: Isabella (a criminal and a colonist distil 4 rum), Jamestown (an
expert carpenter in a lumber mill: 12 hammers; town hall with a colonist: 4
bells).

## Food

Every colonist eats 2 food.

## At the end of the turn

Every colony, the computer's too, stores what it made:

- Workshops take their raw good from this turn's harvest first, then from
  the warehouse. What they can't get is not made.
- Food left after eating goes into the warehouse. At 200 food, 200 are used
  up and a free colonist appears on the colony's square. When the food runs
  out, the last colonist starves (a colony never starves to nothing).
- Hammers add up towards the building under way. It is finished when there
  are enough hammers and tools (`@BUILDING` lists both); those are used up.
- The warehouse holds 100 of each good, 200 with a warehouse and 300 with
  the expansion; the rest is thrown away. Food is not limited this way.

**Not yet done:** bells (Sons of Liberty), crosses (immigration), horses
breeding, building ships and units, and the warnings before goods are lost.

## The jobs menu

Pick a colonist on the colony screen and press Enter (or A) for the jobs
menu. It lists each job with what the colonist would make at the best free
place for it. Picking a job moves them there. Work on the land needs a free
square (water squares need docks, and a square a neighbouring colony works is
taken). Work in a building needs the building and room for up to three.
