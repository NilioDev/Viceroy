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
| Fur trapper | yield, + 2 with a road (a guess: it could also be yield × 2) |
| Lumberjack | yield × 2, + 2 with a road |
| Ore or silver miner | yield, + 1 with a road |
| Fisherman | yield + 1 where the water has land on 3 or more of its 8 sides, yield − 1 elsewhere |

A river adds 1 to all of them. An expert farmer or fisherman makes 2 more;
other experts make twice as much. The Sons of Liberty add to all of this or
take from it (see [liberty.md](liberty.md)).

Measured on test colonies: the ocean (yield 3) gives a colonist 2 food with
land on one or two sides, 4 with three or more (also when the land is only
at the corners); an expert fisherman makes 2 more (6 next to land).

The colony's own square (the town commons) counts as plowed. It makes food
and one other good: furs in a forest, otherwise its best crop or ore, at its
yield + 1.

Checked: Isabella (savannah commons: 5 food, 4 sugar), Jamestown (tropical
forest commons: 4 food, 3 furs; farmers on plowed savannah: 5, expert 7;
lumberjacks: 4, with a road 6).

The original's jobs menu for a petty criminal in Isabella agrees: farmer 3
(tropical forest), sugar 1, furs 4 and lumber 6 (forest with a road), fish 5.

**Not yet known:** what fish and the other resources add (where they are
is in map-drawing.md). Jamestown's fisherman makes 7 on an ocean square
where this model gives 5. At higher difficulty the colony's own square
makes less food (5, 4, 3, 3, 3 on plains from Discoverer to Viceroy); the
rule is not known yet and Viceroy does not do this.

## In buildings

A free colonist makes 3, an expert 6, an indentured servant 2 and a petty
criminal or a convert 1. The lumber mill doubles a carpenter's work and a
factory adds half. Every colony also makes 1 bell and 1 cross of its own, and the church 1
more cross for each of its levels (see europe.md for immigration).
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

**Not yet done:** horses
breeding, building ships and units, and the warnings before goods are lost.

## The jobs menu

Pick a colonist on the colony screen and press Enter (or A) for the jobs
menu. It lists each job with what the colonist would make at the best free
place for it. Picking a job moves them there. Work on the land needs a free
square (water squares need docks, and a square a neighbouring colony works is
taken). Work in a building needs the building and room for up to three.
The menu lists only the buildings the colony has (Isabella, without an armory
or a church, shows 16 jobs), then "(More)".
