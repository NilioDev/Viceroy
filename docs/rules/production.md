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

## Resources

Measured on test colonies with a resource placed by the game's seed (where
resources lie is in map-drawing.md), with colonists and experts, with and
without the Sons of Liberty bonus. A resource helps only the job it suits:

| Resource | Job | Effect |
| --- | --- | --- |
| Grain (plains), oasis (desert) | farmer | yield + 2 |
| Fish (ocean) | fisherman | yield + 3 |
| Timber (conifer, tropical forest) | lumberjack | yield + 2 (so 4 more lumber) |
| Deer (broadleaf, boreal forest) | fur trapper | yield + 2 |
| Beaver (mixed forest) | fur trapper | yield + 3 |
| Ore (hills) | ore miner | yield + 2 |
| Minerals (swamp, marsh) | ore miner | yield + 3 |
| Cotton, tobacco, sugar | planter of that crop | twice the crop, after the bonus |

The additions go in where the Sons of Liberty bonus goes, so an expert who
doubles doubles them too, and expert farmers and fishermen get them twice
(grain: a colonist 7, an expert 11). Ore on mountains made no difference to
an ore miner; silver is not measured yet.

**Not yet known:** whether the colony's own square uses a resource under
it.

## The colony's own square

The square the colony stands on makes food and one other good by itself.
Measured on test colonies (eight farmers on plains around it, one turn
each, the food the colony stored afterwards):

| At Conquistador | Food |
| --- | --- |
| Tundra, grassland, prairie, marsh, swamp | 3 |
| Plains, savannah | 3 |
| Desert | 1 |
| Hills | 2 |
| Boreal, conifer, wetland, rain forest | 2 |
| Mixed, tropical forest | 2 |
| Scrub | 1 |
| Arctic | 0 |

That is the land's farming yield plus 1, 1 less on desert, plains and
savannah and their forests, and never more than 3. Rivers add nothing to
the food. A plowed square makes 1 more (plains 4). Discoverer gives 2 more
food and Explorer 1; Governor and Viceroy are the same as Conquistador. Our
ten saves agree (Discoverer: savannah 5, tropical 4, boreal 4, prairie 5).

The other good is the land's own yield of its best crop or ore (furs in a
forest), 1 more on a minor river and 2 on a major one, and 1 more at
Discoverer: plains make 2 cotton at Conquistador, 3 at Discoverer, 3 on a
minor river. The Sons of Liberty bonus adds to both; the tories' penalty
does not apply here.

A colony we placed on mountains by editing a save stored 37 more food than
this explains; that case is not understood yet.

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

**Not yet done:** building ships and units, and the warnings before goods
are lost.

## Horses

Horses breed from the food left over after the colonists have eaten, and
each new horse uses one of those food. Measured on test colonies, one turn
each:

- A colony with fewer than 2 horses, or no surplus, breeds none.
- At most 2 horses for every started herd of 50 (2 to 50 horses: 2, 51 to
  100: 4, 151: 8). With stables the herd is 25 (26 horses: 4, 76: 8).
- At most half the surplus, rounded up (a surplus of 5 gives 3 horses and
  keeps 2 food).
- No more than the warehouse holds (99 horses in a warehouse of 100 breed 1).

The original's own turns agree: Guadeloupe (87 horses, stables, 5 food over)
gets 3, Plymouth (54, stables, 5 over) 3, New Holland (15, 4 over) 2.

## The jobs menu

Pick a colonist on the colony screen and press Enter (or A) for the jobs
menu. It lists each job with what the colonist would make at the best free
place for it. Picking a job moves them there. Work on the land needs a free
square (water squares need docks, and a square a neighbouring colony works is
taken). Work in a building needs the building and room for up to three.
The menu lists only the buildings the colony has (Isabella, without an armory
or a church, shows 16 jobs), then "(More)".
