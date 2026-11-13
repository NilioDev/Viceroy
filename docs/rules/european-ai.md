# The European nations the computer plays

How the original's computer players run their nations, worked out by
watching the original with the DOSBox debugger and a disassembler, and by
checking the numbers against savegames. Everything below is our own
description of that behaviour. Viceroy follows it in
`src/core/src/game/euro_ai.cpp`.

What is done so far: the order of the turns, the count of each nation's
forces and people, the tallies the plans start from, the cooling of old
grudges, and everything the computer buys in Europe. What each unit does
on the map, the colonies' own plans and diplomacy are still to come.

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

## Not done yet

- What the computer's units do on the map, the plans behind it, and the
  colonies' own plans (which also set the good each colony asks for and
  the colonies marked for recruits).
- Diplomacy beyond the cooling of grudges.
- Which units on the docks the original's ships take (Viceroy takes them
  in the order of the units), and what the original stores in a sailing
  ship's byte 22.
- When the original trades, the prices move for all four nations; Viceroy
  still moves only the trading nation's prices, as for the player.
