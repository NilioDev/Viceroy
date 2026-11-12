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
other experts make twice as much, the road included for furs and ore but
not for lumber: an expert lumberjack on a rain forest (yield 2) with a road
makes 2 × 2 × 2 + 2 = 10 (San Salvador, measured from its hammers over two
turns). Indian converts make 1 more as farmers, fishermen, planters and fur
trappers (San Salvador's convert farmer and fisherman), not as ore miners. The Sons of Liberty add to all of this or
take from it (see [liberty.md](liberty.md)).

Measured on test colonies: the ocean (yield 3) gives a colonist 2 food with
land on one or two sides, 4 with three or more (also when the land is only
at the corners); an expert fisherman makes 2 more (6 next to land).

The colony's own square (the town commons) makes food and one other good;
see "The colony's own square" below.

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
| Deer (broadleaf, boreal forest) | fur trapper, farmer (seen on the colony square) | yield + 2 |
| Beaver (mixed forest) | fur trapper | yield + 3 |
| Ore (hills) | ore miner | yield + 2 |
| Minerals (swamp, marsh) | ore miner | yield + 3 |
| Cotton, tobacco, sugar | planter of that crop | twice the crop, after the bonus |

The additions go in where the Sons of Liberty bonus goes, so an expert who
doubles doubles them too, and expert farmers and fishermen get them twice
(grain: a colonist 7, an expert 11).

Silver on mountains: the resource there is a silver deposit, not ore (an
ore miner gets nothing from it). A silver miner makes 3 on a deposit (an
expert 6), 1 on a mountain whose mask has bit 2 (with or without the
pattern: a worked-out deposit), and nothing on other mountains.

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

The other good is the first of the largest yields in the order sugar,
tobacco, cotton, furs, ore, silver (rain forest, all 1: sugar), at the
land's own yield, 1 more on a minor river and 2 on a major one, and 1 more at
Discoverer: plains make 2 cotton at Conquistador, 3 at Discoverer, 3 on a
minor river. A resource under the colony counts for both, as for a
worker (grain: 2 more food; deer: 2 more food and furs; prime sugar:
twice the sugar; minerals in a rain forest: ore 1 + 3 = 4, which then beats
the sugar). The Sons of Liberty bonus adds to both; the tories' penalty
does not apply here.

A colony we placed on mountains by editing a save stored 37 more food than
this explains; that case is not understood yet.

## In buildings

A free colonist makes 3, an expert 6, an indentured servant 2 and a petty
criminal or a convert 1. A shop (and the lumber mill) doubles that and a
factory adds half again: a colonist makes 3 in a house, 6 in a shop and 9
in a factory. Every colony also makes 1 bell and 1 cross of its own, and the church 1
more cross for each of its levels (see europe.md for immigration).
Workshops use up as much of their raw good as they make, except the
factory's extra half, which is free (a colonist in a fur factory: 6 furs
make 9 coats).

In the craft workshops (rum, cigars, cloth, coats, tools, muskets) the Sons
of Liberty bonus and the tories' penalty go in after the shop and before an
expert doubles: an expert blacksmith in a house makes 10 tools with +2 and 4
with a penalty of 1; a colonist in a blacksmith's shop with +1 makes 7. For
carpenters they are added once at the end (see liberty.md); statesmen and
preachers are treated the same way until measured.

Measured on test colonies (one worker, one turn, the raw good used and the
goods made), and in our saves: Santo Domingo's fur trading post (6 coats
from a colonist), Vlissingen's blacksmith's shop, Fort Nassau's expert
blacksmith with a penalty.

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
  out the store stays at 0 and nobody starves: a colony of eight with 10
  food short a turn kept all eight for ten turns. (`@STARVE1/2` exist, so
  starving happens somewhere; when is not known yet.)
- Hammers add up towards the building under way. It is finished when there
  are enough hammers and tools (`@BUILDING` lists both, the tools in tens).
  The tools are used up and the hammers go back to 0: what is left over is
  lost (measured: 600 hammers on a 40-hammer wagon train leave 0).
- Byte 148 can also name a unit: 42 + n is `@UNIT` row 11 + n, so 42
  artillery, 43 wagon train, 44 caravel, 45 merchantman, 46 galleon, 47
  privateer, 48 frigate. The frigate is offered in the list of a colony
  with a shipyard but never finishes; nothing higher exists. A unit costs its `@UNIT`
  cost column times 32 hammers (artillery 192, caravel 128), except the wagon
  train, 40, and its tools column times 10 tools. The colony keeps building
  the same unit afterwards. The new unit stands on the colony's square,
  added at the end of the unit list. Measured on Curacao in `COLONY00.SAV`
  (one turn per unit, and 1 hammer under and at the cost).
- Building the warehouse expansion only raises byte 149 (1 warehouse, 2
  expansion).
- Messages, from the colonial advisor with "Continue turn." and "Zoom to
  colony." (as the liberty reports): `@BUILT` ("Curacao colony produces
  Wagon Train."), `@NEWCOLONIST` when the colony grows, and `@NEEDTOOLS` /
  `@NEEDTOOLS0` while the hammers are there but the tools are not (tools
  needed and tools there as the numbers).
- The warehouse holds 100 of each good, 200 with a warehouse and 300 with
  the expansion; the rest is thrown away. Food is not limited this way.

- Reports about the warehouse and the food, from the woman in the white
  bonnet (`MSS5.SS`, the box centred on row 130), with "Continue turn." and
  "Zoom to colony.". Measured on test colonies, one or more turns each:
  - a workshop that finds none of its raw good, neither made this turn nor
    stored, reports `@FURS`, `@ORE`, `@LUMBER`, `@COTTON`, `@TOBACCO`,
    `@CANESUGAR` or `@TOOLS` every turn; one that finds some but not
    enough says nothing;
  - a good (not food) whose store passes a hundred (99 to 102, 199 to 200)
    gives `@CARGOREADY0`, or at the warehouse's limit `@CARGOREADY1` (a
    larger warehouse can still be built) or `@CARGOREADY2`, with the limit;
    what was thrown away then is not reported again;
  - other goods thrown away: `@SPOIL1` with the amount and the good when it
    is one good, `@SPOIL2` for more; `@SPOIL3/4` once the expansion stands;
  - food short and fewer than four turns of it left (10 short: 39 left
    warns, 49 does not): `@FOODLOW` with what is left;
  - food gone: `@FOOD1`, or `@FOOD2` ("Winter is coming") after a spring
    turn from 1600 on.
- `@NEWCOLONIST` comes from the priest (`MSS4.SS`), placed as the
  immigration reports.

In our test turns the Sons of Liberty report came first. The order of the
other reports among themselves is not measured; Viceroy gives run-outs,
then cargo and waste, then food.

A line break in `GAME.TXT` counts as two spaces when the original wraps a
report (it is drawn as one): "Your Excellency. Our fur traders" leaves
"cannot" for the next line although one space would just fit.

**Not yet done:** the tutorial's hints (`@TUTORIAL6` "there are 100 Cotton
in ..." followed the cargo report in our test game).

## What a colony can build

Measured on the lists of Curacao and Roanoke (`COLONY00.SAV` with buildings
added and taken away):

- of each kind of building, the next level only; the warehouse and its
  expansion follow byte 149;
- never the upper town hall levels (rows 10, 11) or the capitol (30, 31),
  and not the factory levels or the arsenal, which need Adam Smith;
- docks only next to water, and nothing whose `@BUILDING` colony size is
  bigger than the colony;
- what the colony is still set to build although it is already there,
  marked "(COMPLETE)";
- then artillery with an armory, a wagon train always, and the caravel,
  merchantman, galleon, privateer and frigate with a shipyard (docks
  level 3).

## Buying the construction

BUY on the Make panel asks `@BUYME1` ("Cost to complete …", "Never mind." /
"Complete it."), or `@BUYME0` without the choices when the treasury is too
small. Measured on Curacao with a wagon train, the fort, the fortress and
artillery at many amounts of hammers, tools in the warehouse, tools prices
and tax rates:

- each missing hammer costs 13;
- each missing tool (needed minus those in the warehouse) costs 4 more
  than Europe asks for tools (5 at 1, 6 at 2, 24 at 20);
- the whole is doubled while the colony has no hammers at all;
- the tax rate plays no part.

Completing it takes the gold, raises the hammers to what is needed and the
tools in the warehouse to what is needed; the construction then finishes
at the end of the turn as usual.

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
