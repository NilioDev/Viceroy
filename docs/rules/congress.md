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
combat, natives, Europe and production).

## Not done yet

- After the original's `@FREEDOM` and the hall it opens the
  Colonizopedia page of the new father; the Colonizopedia is not built
  yet.
- What the bells do after the declaration of independence.
- The Continental Congress report.
- Fathers whose effect lasts (Jefferson, Paine, Penn, Hudson, Magellan and
  others) where it is not yet in our rules.
