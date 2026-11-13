# Continental Congress

How the founding fathers come into a nation's Continental Congress. Read
from the original with a debugger and its saves, and checked against what
it shows; see the notes at the end for what is still open.

## Bells

At the start of each colony's turn the liberty bells it makes go to its
nation: they are added to the nation's count toward the next father (a
16-bit word at byte 12 of the nation record) and to a second count at
byte 14 (what clears that one is still to be found). Every nation does
this, the player's and the computer's alike.

Then, before the declaration of independence:

1. If nobody is up for election (byte 18 of the nation record, a 16-bit
   word, is -1), the nation holds an election (below).
2. If the count has reached what the next father needs, the father up for
   election joins, and the count starts again from 0. When nobody is up,
   the count goes back to 0 all the same.

## What the next father needs

Start from 2 × (level + 3) for the player and 14 − level for a computer
nation (the level from 0, Discoverer, to 4, Viceroy), and multiply by 8.
From 1600, 1650, 1700 and 1750 on, add half of it (rounded down) each
time. The next father then needs (fathers so far + 1) × that + 1, halved
(rounded down) for the very first.

For the player at the easiest level in 1494 that is 24 bells for the
first father and 97 for the second.

## The election

The era is set by the year: before 1600, before 1700, and after. Each
father in `@FATHERS` gives a weight for each era. For each of the five
categories (trade, exploration, military, political, religious) the
fathers not yet in congress are drawn from by their weights: a number from
1 to the sum of the weights, from which the fathers' weights are taken in
the order of `@FATHERS` until it reaches 0 or less. A category whose
fathers all have weight 0 in this era, or are all in congress, has no
candidate.

The player chooses among the candidates (`@WHICHFREEDOM`, one line per
category such as "Peter Minuit (Trade Adviser)", with "(F1 for Help)");
the question comes back until one is chosen. A computer nation takes the
candidate of the category with the most fathers left with a weight in this
era (of equal ones, the last).

## Joining

The father's bit is set (bytes 7 to 10 of the nation record), the count of
fathers (byte 20) goes up, and nobody is up for election any more. The
first nation to get each father is kept in the header (byte 57 + the
father's row, -1 for none). The player is told `@FREEDOM` in a plain box
in the middle of the screen and then sees the new father in the hall of
congress with all the others (`CCBKGD.PIK`; each father `CC-nn.SS` by his
row, standing with the middle of his feet on the point his sheet gives,
drawn from the back of the hall to the front) until a key is pressed.

Some fathers act at once:

| Father | When he joins |
| --- | --- |
| Jakob Fugger | the nation's boycotts end (the 16-bit word at byte 32) |
| Francisco Coronado | the nation sees the land within 5 squares of every colony (not the map's edge), and the colonies there |
| Sieur De La Salle | every colony of the nation with 3 or more people gets a stockade |
| John Paul Jones | a frigate sails from Europe for the nation's sea lane |
| Pocahontas | every tribe's tension toward the nation drops to 0, and every settlement's alarm |
| Simon Bolivar | for the player only, the share of rebels in all colonies (header byte 96) goes up by 20, at most 100 |
| William Brewster | criminals and servants waiting on the docks become free colonists |
| Jean de Brebeuf | the nation's missions are run by experts |
| Bartolome de las Casas | native converts, in the field and in the colonies, become free colonists |

The others work from then on wherever they matter (see the rules for
combat, natives, Europe and production, and below).

## Fathers in the colonies' work

Read from the original's production of a colony:

- Henry Hudson: the furs a colonist takes from the land count twice
  (after the land's own yield, its improvements and the river, before a
  resource or an expert doubles them).
- William Penn: each preacher's crosses get half again (after the
  cathedral's doubling).
- The bells of a colony are its statesmen's plus its own 1. To these, in
  this order: Thomas Jefferson adds half (rounded down); Thomas Paine adds
  the nation's tax rate in percent of them (rounded down); Simon Bolivar,
  in a computer nation's colony only, adds (people + 3) / 5; a printing
  press adds half, a newspaper doubles them instead.

## Fathers elsewhere

Read from the original:

- Ferdinand Magellan: the nation's ships (caravel to man-o'-war) have one
  move more.
- Francis Drake: a privateer's strength gets half again, in attack and in
  defence (after the veteran's bonus); the Combat Analysis shows it on its
  own line, "Drake +50%".
- Simon Bolivar: for the player, every colony counts 20 points more Sons of
  Liberty than its bells make (at most 100%), for the rebels, the tories'
  penalty and the bonus alike.
- Adam Smith: without him the factory level of the craft shops (textile
  mill, cigar, rum and fur factory, iron works) and the arsenal are not
  offered in a colony's build list.
- Peter Stuyvesant: without him the custom house is not offered.
- Paul Revere: only with him does a colonist defending a colony without
  soldiers take up 50 of its muskets (+1).

The build list also offers the drydock only to a colony on the coast (byte
28 of the colony, bit 0x40) and a wagon train only while the nation has
fewer wagon trains than colonies.

## The report (F3)

Reports, Continental Congress (F3) shows, on `REPORT3.PIK`, as the original
does:

- The title, `CONTINENTAL CONGRESS ACTIVITIES`, centred at the top.
- "Next Continental Congress Session: (Adam Smith)", the father up for
  election in brackets, and under it a bar of bells: the bells the next
  father needs, spread to fill 300 pixels (packed a pixel apart and the
  count halved until they fit, the spare pixels handed out along the row),
  of which only those the nation has are drawn, with their number on a
  black box when they are packed tight.
- "Rebel Sentiment: 10%   Tory Sentiment: 90%" (header word 96) and a row
  of flags and crowns for the rebels and tories among the nation's people.
  The people are those in its colonies and every unit that carries a
  colonist (colonist, soldier, pioneer, missionary, dragoon, scout,
  continental army and cavalry), wherever it is, at most 255; the rebels
  are that times the share, rounded down.
- When the King has one, "Dutch Expeditionary Force:" with his regulars,
  dragoons, artillery and men-of-war (header words 106, 108, 112 and
  110), each group with its number.
- "Founding Fathers:" and the fathers in congress, four to a line, 78
  pixels apart.
- An OK box in the lower right corner. Any key or click closes the report.

The groups of icons share the 300 pixels the same way in every report:
first the 4-pixel gaps between groups shrink, then the icons overlap, until
every group has a pixel for each icon after its first; each group then
spaces its icons evenly, at most one pixel more than an icon apart.

## Not done yet

- After the original's `@FREEDOM` and the hall it opens the
  Colonizopedia page of the new father; the Colonizopedia is not built
  yet.
- What the bells do after the declaration of independence.
- The report's lines after the declaration of independence (the
  intervention force) and its line for the cheat mode.
- Peter Minuit (the natives' land for nothing), Benjamin Franklin (the
  wars in Europe) and Jan de Witt (foreign colonies and the foreign
  affairs report) wait for the parts of the game they change: buying land
  from the natives, the diplomacy between the European nations and the
  reports.
- The ship's penalty for its cargo in combat.
