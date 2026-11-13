# The European nations the computer plays

How the original's computer players run their nations, worked out by
watching the original with the DOSBox debugger and a disassembler, and by
checking the numbers against savegames. Everything below is our own
description of that behaviour. Viceroy follows it in
`src/core/src/game/euro_ai.cpp`.

What is done so far: the order of the turns, the count of each nation's
forces and people, the tallies the plans start from, the cooling of old
grudges, everything the computer buys in Europe, the plan each nation
makes for its units, and what each unit then does on the map, but for
attacks. Attacks, the colonies' own plans and diplomacy are still to come.

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

Each colony of the nation is first looked after by its own plans (still to
come). Then the turn tallies what the colonies ask for, by good: the good
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

## Not done yet

- What the original's units do on reaching a village they were sent to,
  and how a wagon train trades there, is not read yet: Viceroy uses the
  player's rules.
- Attacks after independence (the original then lets the computer's
  units attack only the player's).
- Missionaries pass by villages with a mission of their own (the
  original weighs those against the player, by a ranking of the nations
  not read yet).

- The colonies' own plans (which also set the good each colony asks for,
  the guards it wants and the colonies marked for recruits), and which
  job a colonist takes on joining a colony.
- Diplomacy beyond the cooling of grudges.
- Which units on the docks the original's ships take (Viceroy takes them
  in the order of the units), and what the original stores in a sailing
  ship's byte 22.
- When the original trades, the prices move for all four nations; Viceroy
  still moves only the trading nation's prices, as for the player.
