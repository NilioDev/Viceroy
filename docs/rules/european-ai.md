# The European nations the computer plays

How the original's computer players run their nations, worked out by
watching the original with the DOSBox debugger and a disassembler, and by
checking the numbers against savegames. Everything below is our own
description of that behaviour. Viceroy follows it in
`src/core/src/game/euro_ai.cpp`.

What is done so far: the order of the turns, the count of each nation's
forces and people, the tallies the plans start from, the cooling of old
grudges, everything the computer buys in Europe, the plan each nation
makes for its units, what each unit then does on the map, its attacks,
and most of each colony's own plans (its guards, what it is short of, the
units it takes in or sends out, the goods it asks for, its own work on the
land and what it builds). The colony's jobs and teaching and diplomacy are
still to come.

## The round

A round starts with the natives' turn. Then the four European nations take
their turns in order, English, French, Spanish, Dutch. The player's nation
plays itself; a nation the computer plays (control byte 1 in the player
record) takes the computer's turn; a nation that withdrew (2) is skipped.
After the Dutch the year moves on.

So the computer's nations after the player's move before the year turns,
and those before it move at the start of the next round.

## The count

At the start of its turn each nation counts what it has, and the original
keeps the count in the save, in the 727-byte block after the tribe
records. Some of it is per nation, some per nation and region (the land
regions of the map, 0 to 15), and a little per region for the nation
counted last. Bytes stop at 255 where marked "capped".

For each unit of the nation:

| What | Where (offset in the block) |
| --- | --- |
| units of every kind | 12 + nation |
| units of each type (the first 19 types, capped) | 64 + 19 × nation + type |
| free colonists | 20 + nation |
| people: colonists and the units made from them (types 0 to 5, 7 and 9), capped | 28 + nation; by region 364 |
| the units by region | 172 + 16 × nation + region |
| for ships: their holds (capped), the ships (capped), and the ships that can attack | 36, 40, 52 + nation |
| for land units: their defence (as in battle, capped) | 32 + nation; by region 428 |
| and their attack | a 16-bit word at 44 + 2 × nation; by region 492 (capped) |
| the attack of land units outside towns (capped) | 60 + nation; by region 300 |

Units in a town do not count towards the last line when the nation is
the player's, nor when the unit's goal letter is `A` or `G` (it guards the
town). A unit that has no home colony takes the colony it stands in as
its home.

The count also notes, in memory only, the land units on the docks in
Europe and the ships on the way there.

For each colony of the nation it counts the colonies (16 + nation), the
people in them (24 + nation, and again in the people at 28, both capped),
the colonies and people by region, and the average people per colony (a
word at 556 + 2 × nation). It then looks at the squares up to five away
in each direction: an armed ship of another nation (or tribe) that the
route finder can bring to the colony for 5 thirds of a move or less (that
is, from the next square) marks the colony as threatened, by a frigate or
by other armed ships. The number of threatened colonies and their people
feed the purchases below.

The region flags (16 bytes at 156) say, for the nation counted last, where
another European nation's units are (2), where another's colonies are (4),
where there are native settlements (1), and where its own armed units stand
fortified outside a colony (8).

The natives' count, each tribe's turn, keeps the settlements of the tribe
counted last by region in the 16 bytes at 140 (the plans below weigh a
region's room by them, oddly enough only for that one tribe).

The count reproduces the block in the original's saves byte for byte for
the player's nation (whose count is the last one in a save made during the
player's turn).

## Before the purchases

Each colony of the nation is first looked after by its own plans (see
"Colony plans" below). Then the turn tallies what the colonies ask for, by good: the good
a colony asks for (byte 141 of the colony), muskets twice over when that is
muskets, and one more for each colony out of muskets, horses or tools.
Goods already in the nation's ships come off the tally, and each pioneer
takes one off the tools.

An old grudge with another nation may cool off: when the relation byte has
bit 8 set and its timer has run out, there is a 1 in 4 chance that the
relation loses bits 64 and 8 and gains bit 1. Each timer counts down by one
a turn.

## In Europe

The Crown helps the computer: every turn the nation gets
(year − 1500) / 50 plus its colonies, nothing before turn 20, twice that
from 1700, times the difficulty level, half as much again at level 3 and
twice at level 4, and times 4, in gold.

### Ships

A nation threatened at sea wants a frigate when half its colonies or half
their people are threatened by a frigate (or after turn 200 with 2000 gold
in the bank), it has no frigate, the player has one, and the other nations
have privateers or frigates. It wants a privateer in the same way with the
other threats, after turn 100 with 1000 gold, with fewer than two
privateers of its own while the player has one.

The Crown makes up its gold to the price of a caravel when it has no ship,
and to the price of the privateer or frigate it wants.

When it has more people than holds (half its people plus its colonies at
least its holds), it buys one ship, trying in order: the frigate it wants
(or gives up for this turn), the privateer it wants (likewise), a frigate
half of the time when it has fewer than eight armed ships and is not ahead
of everyone at sea, a galleon three times in four, a merchantman half of
the time with fewer than 12 holds, a caravel with 2 holds or fewer, and a
privateer one time in four with fewer than four armed ships, when it is not
ahead and not short of ships. With no ship at all and none bought, the
rest of the turn in Europe is skipped.

"Short of ships" here means that a quarter of its people plus its colonies
is at least its holds. A nation short of ships buys no soldiers or
recruits and its ships sail without cargo.

### Artillery, recruits and the docks

With no artillery on the docks, colonies asking for muskets, a 1 in 4
chance, more than 4 holds and not short of ships, it buys artillery.

It holds back every third turn. Otherwise, with nobody on the docks, not
short of ships, and enough colonies marked for recruits, it recruits one of
the three people on the docks at its own price: 20 × (recruits so far −
level + 7), less the same in proportion to the crosses gathered, when it
can keep 2 × (30 × people − turn) gold besides.

Then each person on the docks may take up arms or tools, those without a
skill first, the experts after. A free colonist becomes a soldier when
colonies ask for muskets (a 1 in 2 chance, 1 in 3 for an expert) or, from
turn 100, when the nation wants colonies (1 in 3, 1 in 4 for an expert),
paying for 50 muskets unless it has a lot of muskets kept in Europe. An
expert made a soldier leaves his skill on the docks: he swaps with the
first waiting recruit who has none. With a college somewhere the soldier
may come out a veteran. If it can pay for 50 horses (or has 50 kept), the
soldier is mounted. A colonist not armed may become a pioneer with 100
tools when the nation wants more colonies than it has pioneers; and every
seventh turn after turn 50 a colonist may become a missionary, free, when
the nation has none.

### Soldiers for the colonies

When there are soldiers or dragoons on the docks and a ship to carry
them, it is not short of ships and not holding back, it buys more: first
artillery when the largest ship has 6 holds or more ((10 − level) × 100
gold from turn 40, free while the Crown owes it some), then recruits from
the docks as dragoons, one after the other while there is room on the
ship and gold to pay: the recruit's price as above but from 2 × (recruits
+ 7) less the level, the muskets and horses, a tenth off for each level
from turn 100. Muskets and horses it keeps in Europe are used first, and
it keeps the two stocks level with each other after turn 80.

### Ships in port

Each ship in port unloads: muskets and horses go into the stocks kept in
Europe, everything else is sold. The largest ship sails with the new
soldiers. The others load 100 of each good the colonies ask for, from
muskets down to food, when enough colonies ask for it, there is room and
the gold to pay; then every ship sails for the New World, taking the
people on the docks along.

## The plan

After Europe the nation makes its plan, in memory only. Its heart is a
list of up to 64 places worth going to, each with a kind (what may go
there: each unit type has a role bit for each kind) and a weight. The
list is kept heaviest first; a place already listed at least as heavily
is not added again, and when the list is full the lightest falls off.

The kinds: 0 a place at sea for warships, 1 a landing for colonists and
pioneers, 2 where a wagon train, treasure or artillery has stopped, 3 a
colony short of guards, 4 a town to deal with, 7 a landing beside a rival.

The plan goes through:

- **Its own units.** Goal `A` (guard) becomes `G`. A ship (or a square)
  with armed units or pioneers that is full may land them (flags 8 and
  4), and one ship a turn is chosen to fetch recruits from Europe: a
  merchantman when it has one and two of merchantmen and galleons
  together, else a caravel when it has two. Units off the map are put to
  sleep; on the map, their 4 × 4 block of the map is marked as held. Units
  asleep, or on their way to anything but a target, lose their orders; a
  unit with a threat next to it (below) gets orders 10; a land unit on a
  ship sleeps.
- **The other nations' ships** it can see: a privateer, or any ship but a
  frigate of a nation it is at war with, is a place for warships (weight 3).
- **The other nations' colonies.** Early in the game, on the easier
  levels, a player's colony it has not seen is passed by. A colony in a
  region where the nation has units or colonies is a town to deal with,
  three turns in four, when it holds more than 6 less a fiftieth of the
  turn in people and units (weight 3 at peace, 5 otherwise). At war, the
  open sea two squares from a colony by the water that looks on most of
  the colony's waters is a place for warships (weight 2 to 4 by its
  people). And when the rival has more colonies in the region (with 8
  people or more there), or the nation has none there and the rival fewer
  than 8 people, the best water square up to three away with the region's
  land next to it is a landing: weight 2 (3 for a stronger rival), more
  against the player and in a large region the player has to itself,
  less in a crowded region, more at war, twice as much before turn 150.
- **Its own colonies by the water**: threatened from the sea, a place for
  warships (weight 8 for a frigate, 5 for others). The pioneers and spare
  soldiers there, and the goods above what the warehouse keeps (horses
  less), add up to what a ship could fetch; the colonies worth a visit go
  on a list of up to 16, best first. A colony short of guards (byte 0x8E)
  is a place for soldiers, and its units stay to guard it, artillery
  first, then soldiers, veterans, dragoons.
- **The settlements**: a furious tribe's village in a region where the
  nation is present is a town to deal with (weight 4, 2 with a mission),
  and in a region without a colony of the nation the water next to a
  village with the most land of the region beside it is a landing.

Then what each region is to the nation: 0 when it is crowded (twenty
times the colonies there, its own counted twice, beyond the region's land
squares), 6 when open, 4 where it is the stronger of the rivals at war
there (or has no colony there itself, or nothing at all), 3 where it is
the weaker. The rivals are the other nations at war or without a treaty,
and the furious tribes, weighed by their forces in the region. The
largest colony of others in a region, or up to 4 of their units, is
noted too.

Last, every unit of the nation with no orders or fortified takes the
target that suits it best: the number already sent there (starting at an
eighth of its units, at least 3) times the distance, divided by the
weight plus one, as long as that is at most one and a half times the
weight in eighths of that start. Soldiers and dragoons stay when they are
two or fewer in a region. A fortified unit only leaves for a heavy
target, never from a colony. The unit goes there with goal `1` (`t` for a
landing, `i` beside a rival).

## The units

Then the units take their turns, from the last unit down: in each round
the first treasure, artillery or wagon train with moves left, then the
first unit of any kind, each until its moves are spent (or 20 steps). A
treasure, artillery or wagon train that stops is a place of kind 2.

A unit on its way goes on without thinking again, unless it is a warship
with someone next to it. Otherwise it decides:

- **Guards** stay in their colony; the last one keeps it.
- In a crowded region, units go back to their colony.
- A **scout** next to a village whose chief it has not met goes in; a
  free colonist or servant next to a village that has not taught goes in
  to learn.
- A colonist or pioneer (and a soldier or dragoon with no orders, in an
  open region, or where the nation has no colony and little force) looks
  for a **site** for a colony within three squares (fewer as it searches
  again and again, and near other nations): a square by the open sea, not
  next to another colony, not too near another or a village, worth most
  by the seen layer's number for it. On the site it founds the colony
  (with the next name of its nation's list); otherwise it goes there.
  Three units at most search in a region a turn (two colonists).
- A **colonist** that is not looking joins the nearest colony of its
  region marked for recruits, the smaller first; with none, a colonist in
  a colony becomes a pioneer with 20 tools.
- **Ships and wagon trains** in a colony unload everything; a ship takes
  aboard the pioneers and soldiers waiting there, then each loads the
  goods worth most (a wagon train what the natives pay well for, and goes
  trading with them).
- **Ships** put ashore those aboard whose work lies on the land next to
  them, carry the rest to the colony that needs them most, deliver
  muskets, horses, tools and trade goods to the colony that lacks them
  most (or sell them where they are), fetch goods from the list of
  colonies, and sail to Europe when there are more people on the docks
  than ships on the way, when chosen to fetch recruits, or now and then.
- **Treasure** goes to the nearest colony; there the gold goes to the
  nation, and the player hears of it.
- **Missionaries** go to the village where the tribe is most tense,
  nearest first; with none left they become colonists.
- A **pioneer** with nothing better builds a road, unless a village or a
  rival's colony is near.
- A unit called by its colony goes back.

The rest choose a direction: each neighbour by chance, by the terrain, by
roads and rivers, by unexplored land ahead, less for turning back. A unit
that chooses to stay fortifies; next to a colony of a nation at peace it
does not.

### Attacks

A neighbour with someone else's unit, colony or village is weighed as an
attack, or passed by:

- A European nation's square is passed by when the two are at peace (the
  treaty bit), unless a privateer attacks or defends.
- A tribe's square is passed by unless the tribe is at least 75 tense
  towards the nation or the war bit is set (which doubles the square's
  chance), and unless the nation has a colony in the region.
- A unit without an attack value passes by. So do soldiers and dragoons
  facing a colony with artillery, unless more of their own artillery
  stands around it.

The odds come from the attack routine itself, asked only to weigh: eight
times the attack over the defence plus one, with every step of a real
fight up to Spain's bonus (the steps for the easy levels and for colonies
are left out). An empty colony is weighed against one of its colonists
picked at random. The odds are then multiplied by the defenders' strength
plus one over their number, divided by the attacker's strength, tripled
against a colony and doubled against a village; artillery weighs nothing
outside a colony or village, and units of the role for attacks triple
their odds in a region where their nation is the stronger. Odds of 1000 or
more count as 1000. Under 12 a land unit turns away (999 less); otherwise
the square gains four times the odds. An armed land unit also gains 16
for a step onto someone else's colony.

When the attack is chosen with less than a full move left, the unit
waits. Otherwise it steps and fights as the player's units do: against
the unit that defends the square, an empty colony's colonist, or the
village (which is then at war with the nation). An armed unit that
weighed an attack but chose none, and stands next to someone else's
colony, waits there (goal `F`). The original checks there that the unit
heads its square's stack; Viceroy only checks that it stands on land.

## Colony plans

Before the tallies, each of the nation's colonies makes its own plans, in
this order. Viceroy follows it in `src/core/src/game/euro_ai_colony.cpp`.

1. **Housekeeping.** Bytes 138 and 139 are cleared, and the counters at 140
   (turns of tools for its own land work) and 143 (the pickup bonus) go up
   by one, to at most 127.
2. **The threat.** Every armed land unit of another nation within five
   squares counts with its attack force, scaled by nearness: force ×
   (8 − distance) / 8, where distance is the longer side plus half the
   shorter one. A unit on a colony or village square counts half. A unit of
   another European nation counts only with a force above 1, and half as
   much again when it is the player's; one standing right next to the
   colony is noted as adjacent. A brave counts only when its tribe's
   tension with the nation is at least 25 and its home village's alarm is
   at least 128. The threat divided by one more than the fortification
   level, but at least the threat itself up to 16, gives the threat level;
   byte 30 keeps it in eighths.
3. **Guards wanted.** An eighth of the threat level, but at most half of
   its colonists and people on the square, is the number of guards it
   wants; one more after independence, and at least one when an enemy is
   adjacent and it has more than one person. Byte 142 keeps it. Each armed
   unit already on the square takes one off; units there without a home
   colony take this one as their home.
4. **The land.** Of the squares it can work (8, or 12 and 20 with a larger
   town hall), it counts water and off-map squares, forests (and those with
   good food under the trees), good farmland (food 3 or more) and poor
   land (food under 2), and of the squares worked, those without a road
   and the open land not yet plowed. Docks make the water count as land.
5. **What it is short of** (byte 27). The low three bits stay. It wants
   people when it was marked for recruits, or when it is below its size
   for its town hall (8, 12 or 32 colonists, with room for twice the
   nation's colony need) and has free land. It wants guards (bit 64)
   whenever it wants any. A guard ratio comes from its size, the nation's
   first trait and the turn, with two more when the region is quiet and
   one more when it is contested; when the region is fortified against the
   nation's attack (not Spain), at most one. Bit 8 is set when the ratio is
   above the armed units that call it home, bit 4 when they are more than
   the ratio needs. Mostly water or forest around, or too little good
   farmland, sets bits 32 and 128 (forest to clear); land without roads or
   plowing sets bit 128 (a pioneer wanted).
6. **War on a tribe.** When the nation is set on attacking in the region
   (its attack count there at least 2), the nearest tribe in the same land
   is marked for war when its strength in the natives' count is
   at most twice the nation's attack, its defence in the region is under
   four times the nation's attack out there (both doubled for Spain), and
   it is tense with the nation (above 25; Spain does not wait for that). Both relation bytes get bit 2.
7. **Units join.** While it wants people, people on its square join it,
   one at a time: a soldier or dragoon when it has too many guards (and
   it then wants one more), a pioneer when it needs one but has no tools
   or the region is quiet, a scout when the region is quiet or it has
   under 52 horses, and every free colonist. Their muskets, horses and
   tools go into the store.
8. **One goes out.** With more than one colonist, it may send one out: a
   scout when it has 102 horses or more and is full; a pioneer when the
   region is quiet, it has more than 10 colonists, a 1 in 4 chance comes up,
   the nation has no pioneer and wants colonies, and there are 20 tools;
   and a soldier (a dragoon with 52 horses) when it wants guards, or in that
   crowded case, and has 50 muskets. The colonist chosen is one who already
   has the role's skill, then one without a skill, never an expert for a
   soldier unless guards are wanted; indentured servants and criminals are
   picked first, and converts never. It takes its gear from the store: 50
   muskets, 50 horses, or the tools in twenties up to 100.
9. **The good it asks for** (byte 141, -1 for none). In order, each later
   one winning: muskets when it has fewer than 50 × (first trait + 2);
   trade goods when it has under 100, they are cheap and it is marked for
   them; horses under 50; tools when it needs a pioneer and has none; and
   muskets again when it is short of 50 and wants guards. A good it
   already makes, or has a full store of, is never asked for. With no
   guards wanted and 200 muskets or more, 50 go back to the nation's store
   in Europe (up to 20 lots).
10. **Tools.** When it needs a pioneer, or every tenth turn, and has under 20
    tools, it buys 20 in Europe if the nation has the gold.
11. **Its own work on the land.** Every seventh turn, with 20 tools, it may
    lay a road towards another of its nation's colonies (see below). On the
    other turns, when it wants a pioneer and has 20 tools, it works one of
    its squares itself (see below). Either way it uses 20 tools and its
    count of saved-up turns (byte 140) starts again from 0.
12. **Who works where** (see below).
13. **What it builds** (see below).
14. **Teaching and training** (see below).
15. **Horses.** From turn 40, a colony with under 2 horses and a ship or
    wagon train on its square buys 2 for 10 gold.

Before the units join (step 7) it counts its colonists by skill, those who
are no experts as free colonists; the jobs and the construction list use
that count.

### A road to a neighbour

When the nation has at least two colonies in the land, each other colony of
the nation in the same land within six squares each way gets a 1 in
(colonies − 1) chance, in the order of the colonies. The route finder walks
from this colony towards it, with every step costing the same; the first
square on the way that is neither a town nor has a road is the one. It is
skipped when someone else's units or town are there, or when it is the
worked land of a nation the mover's relation marks. The road is laid at
once when the colony has saved up the square's improvement value + 2
turns; when it has not, nothing is done this turn.

### Working a square

Of its eight squares that are land, with no town, nobody else's units and
no other colony working them, it picks the one worth most:

- the terrain's value from `NAMES.TXT`, or the resource's own value when
  there is one (silver 12, sugar 7, fish 5, minerals and grain 4, an oasis
  3, the others and spent silver 6; no resources at all when the map's
  resource seed is 0);
- twice that for a forest when it wants forest cleared, or for a square its
  worker would do better on: unplowed for a farmer, planter or the like
  (jobs 0 to 3), without a road for the rest;
- squares that have both a road and a plow are left alone (but for
  forest clearing);
- land a tribe claims costs the colony goodwill: 4 − the tribe's tension,
  twice that on a resource, and twice again when the region has
  settlements and the colony is not marked for that (where it only clears
  forest); the cost is taken twice from the value while the nation has
  under 2000 gold, a half after. A tribe claims a square within reach of
  its nearest settlement in the land (1 square, 2 for the Aztecs' level, 3
  for the Incas'), once met, but never a square the colony works, water,
  land marked 16 or with Peter Minuit.

It does nothing when the player's units stand next to the square, unless
its own nation's units are on it, or when it has not saved up the square's
improvement value + 2 turns (+ 2 more to clear a forest). Then it clears
the forest, or plows the square for a worker in jobs 0 to 3, or builds a
road for a worker in another job; with nobody there, it plows open land
(rows 2 to 7) and builds a road on the rest.

### Who works where

Each turn every colonist leaves their work (their turns at the same work,
kept in a nibble from byte 96, start again) and the colony gives the jobs
out anew. Only the computer's colonies do this, and none of them loses
work to tories (see liberty.md).

**What it counts.** As the jobs are given, the colony keeps a balance of
each good: what it makes, what it needs, how much it is short of after
this turn's making (the deficit), and short even with its store (the
shortfall). Food is needed for two a colonist plus what the horses eat:
two horses for each started herd of 50 (25 with stables), fed with half
the food left over and as far as the warehouse has room; the horses that
cannot be fed are their shortfall. Lumber is needed for the hammers made.
Each craft needs its raw good for what it makes, two thirds of it in a
factory (when short in a factory, it is short of all of it, or of half as
much again). Tools are needed for the muskets made, from the tools made
less what is short of ore.

**Placing a colonist on the land.** For each square that the colony may
work and nobody works (not off the map, water only with docks, no other
colony working it, no town on it, and no tribe's armed units standing
fortified there) and each of the nine jobs on the land, the colonist's
amount there is weighed. Asked for one job, the best amount wins (8 for
each, plus 7 less the steps across and down). Asked for the best job:

- the amount counts only up to the room left in the warehouse for the
  job's good (as read, the job's own number picks the good: a fisherman's
  room is that of the horses), at least 1;
- when food is short (more is needed than made, the store is no fuller
  than the warehouse and holds no more than 16 times the deficit, and
  this is still the food step), food work counts 32 times over (a
  fisherman 8 more first), less what the square is worth for other work
  (its best yield times that good's highest starting price; 24 more on a
  forest while the colony has at most 10 lumber and none is cut); and a
  square a tribe claims counts half;
- otherwise the work counts its price in Europe (food: 4 once the food
  step is past and the colony has fewer than twice its squares in people,
  else 0), plus 1, plus 1 when its good is short (twice over when even the
  store does not cover it), less 1 for lumber when there is 2 at hand, plus
  2 for a crop a craft uses (sugar, tobacco, cotton, furs, ore). From turn
  80, ore mining in a colony of 8 counts 2 more, and when the nation ranks
  at least as high as the player, the levels of its blacksmith's shop and
  twice those of its armory. Land a tribe claims costs 4 − the tribe's
  tension, twice that when the tribe is stronger than the nation in the
  region, half again when the tribe is stronger overall, less one for each
  armed unit on the colony's square. On a forest, work other than lumber
  costs 10 when the colony has at most 10 lumber and none is cut.

The best score takes the job and the square. (The nations' ranking: each
scores its gold in hundreds, twice its colonies, its people and its attack
force; the original sorts them from the weakest.)

**The order.**

1. *Food by skill.* While the colony has fewer people than twice its
   squares, or between 2 horses and a full store of them, or has under 75
   food, its expert farmers (and expert fishermen, with docks) go to their
   own work, until it makes more food than it needs; one who would make
   under 3 is sent back, and the rest of the food steps are passed by.
2. *Food from the others.* In two rounds, while the store is down to 16
   times the deficit or the colony is small or has horses to feed: with
   more food than that only converts are placed in the first round, and
   non-experts in the second; otherwise non-experts first and experts
   second. Whoever would make under 3, or under 5 when there is food
   enough, is sent back and the food steps end.
3. *More people on the land.* While its land is not all taken and it is
   below its size for its town hall, the colony weighs every colonist left
   and puts the best on the land. (As read, it weighs them without placing
   them, which leaves each with nothing, so this never puts anyone to work.)
4. *Lumber*, unless the colony builds little (byte 29 bit 128): with under
   10 lumber and none cut, an expert lumberjack, then a colonist who is no
   expert, then anyone. Without lumber or a lumberjack, every eighth turn
   it buys 100 lumber for 200 gold (the lumber comes even when the gold is
   short).
5. *Carpenters*, when there are 2 lumber at hand: master carpenters, then
   free colonists, then indentured servants, then anyone, until hammers
   are made. Servants and criminals become free colonists on the way, and
   a colonist who is no expert in a colony of 6 without a master carpenter
   becomes one at a chance of 1 in (17 − difficulty).
6. *Experts on the land* at their own work, while their good's store is
   not over full; an expert lumberjack only as the first, or when the
   colony wants forest cleared or is short of lumber.
7. *Experts in the buildings* at their own trade, three to a building: a
   craftsman when the raw good is not short and there is some (in store or
   made); a master carpenter when lumber is not short and there is some
   (in a colony that builds little, a second master carpenter becomes a
   free colonist instead); preachers and statesmen before independence (a
   preacher after it becomes a free colonist).
8. *The rest*, one by one: the best place on the land is weighed against
   each building job (three to a building). Inside, the amount counts up
   to the raw good at hand (as read, the jobs that use no good keep the
   last count), weighed by the good's price less its raw good's price
   (tools and muskets: price + 4, doubled from turn 50 when the player
   ranks no higher), and for the others by 3; a carpenter by 5 less a third
   of the hammers made (halved when the colony builds little), a preacher
   by 3 less half the crosses made, the turn in hundreds and less 6; a
   statesman by 4 for each printing press level, the tories, 7 and 4 for
   byte 150, doubled with 10 tories, with Thomas Jefferson and after 1600 and
   1700, nothing before 1540 or after independence, halved in a colony of
   3 and again under 6, halved when the nation ranks above the player and
   doubled below, halved with nation byte 0 bit 4, less the bells made, from
   1 to 100. The score is (8 × amount + 5) × weight. When the land is worth
   at least as much, the colonist goes on the land, and when no place is
   found, to the church as a preacher (with a church, lumber to spare and
   fewer than 3 preachers) or else as a carpenter.

### Teaching and training

**Teaching.** A colony with a schoolhouse teaches someone once its count
of saved-up turns (byte 140, shared with its own land work) reaches 4 for
each level (16 at the university). The lesson: while under 10 colonists
and with no more food experts than food workers, fishing when it has fewer
expert fishermen than ocean squares, else farming; then for each craft
(armory, blacksmith, fur trader, distiller, tobacconist, weaver) whose
building has its second level (the armory its first), whose raw good is
made and that has no master yet, as read the craft's place in that list
becomes the skill (farmer to lumberjack). Without a lesson, the colonist
learns the work they do. One of the colonists and people on the square who
are not already experts at their work (converts never) is picked at random,
and the count of turns starts again.

**Training.** A hungry colony (more than half its people on food, more
than one, or short of food even with the store), in a nation with taxes of
at most 25% and the price of an expert farmer in gold, trains its last
colonist who is no expert (not a convert): as a fisherman when it has docks
and no expert fisherman, else as a farmer when it has no expert farmer. As
read, the price paid is the one listed for the expert at the colonist's own
number in the list, and an expert that cannot be bought (-1) gives one gold
piece back.

### What it builds

The choice is made anew each turn, from nothing. It goes down this list;
for each building it takes the building, or else the level below that is
still missing (when that can be built). A building can be built when the
level below stands, the colony is big enough, and for the docks when it
has water among its squares, the drydock when it lies by the sea, the
warehouse expansion below a store of 300, the custom house with Peter
Stuyvesant and the factories with Adam Smith; never the upper town halls or
the capitol.

1. The docks, when its land squares are no more than its colonists, or it
   has water and is hungry (more than half its people on food, or not
   enough food even with the store).
2. The stockade; the stables with 2 horses.
3. Under 4 colonists it stops here.
4. A warehouse, when it has no warehouse and at least 6 colonists for each
   level it would have.
5. With 6 colonists or more: the custom house, when ships threaten it, the
   player has more than 2 armed ships more than the nation, or it has 12
   colonists.
6. Before 1600, in a region with settlements, a colony not marked for them
   and near a tribe with tension under 50 and no enemy next to it builds a
   wagon train.
7. The schoolhouse, when one of its experts could teach and its colonists
   and experts together number 4.
8. Under 6 colonists it notes that it builds little (byte 29 bit 128).
9. The armory, when muskets cost 4 or more (counting half the difficulty) or
   after turn 80, with 6 colonists and 40 tools or tools being made, or
   when it has a master gunsmith; the church when it has a firebrand
   preacher.
10. The lumber mill, then the fort.
11. The blacksmith's shop, when muskets cost 4 or more, with 4 colonists and
    40 ore or ore being mined.
12. The warehouse expansion, for 6 colonists per level.
13. The newspaper (after the printing press) once it makes 4 bells.
14. The college with level-2 teachers and 10 colonists and experts; under 8
    colonists it stops here; the university with level-3 teachers and 16.
15. The church, then the fortress with 10 colonists.
16. With one of its crafts at its top level and by the sea: the shipyard,
    then ships: a galleon while the nation's holds fall short of half its
    people and its colonies, else a privateer, a frigate when it has none.
17. With a craft at its top level and no artillery on its square, or no
    guards wanted: the armory when it has tools, then artillery.
18. The next level of each craft's building (weaver, tobacconist,
    distiller, fur trader, blacksmith, armory, in that order) when its raw
    good is made 3 a turn (level 2) or 8 a turn, or 100 are stored (level 3).
19. The cathedral. Then, with a shipyard, galleons up to the nation's armed
    ships (at most 8), or a frigate while it has fewer than 8.
20. Under 10 colonists it is marked for recruits. With fewer than 3
    artillery: the armory, then artillery, through the arsenal once muskets
    are made. With 3 it builds nothing.

## Not done yet

- What the original's units do on reaching a village they were sent to,
  and how a wagon train trades there, is not read yet: Viceroy uses the
  player's rules.
- Attacks after independence (the original then lets the computer's
  units attack only the player's).
- Missionaries pass by villages with a mission of their own (the
  original weighs those against the player by the nations' ranking).
- In the original, once a colony's own land work has picked a square, the
  rest of its plans read the nation from the units next to that square
  (often nobody); Viceroy keeps the colony's own nation.
- One mark of a square's state the original checks before giving it work
  is not read; Viceroy uses the squares the player's colonies may work.
- The original's message after training a colonist.
- Diplomacy beyond the cooling of grudges.
- Which units on the docks the original's ships take (Viceroy takes them
  in the order of the units), and what the original stores in a sailing
  ship's byte 22.
- When the original trades, the prices move for all four nations; Viceroy
  still moves only the trading nation's prices, as for the player.
