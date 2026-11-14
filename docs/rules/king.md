# The King

How the Crown taxes the colonies, how the player can refuse a rise, how
boycotts end, how the King's army grows and how the nation's rebel
sentiment is reported. Measured in the original with a debugger watching
the turn, then checked by playing.

## When the King speaks

At the start of each nation's turn, before its colonies, the King may
change the tax. He only does so for a nation with colonies, from turn 30
on, while the tax is 85% or less, and only on turns that are a multiple of
his period:

- 18 turns, or 15 once the year is past 1600; 3 less past 1700 and 3 less
  again past 1750;
- for the player, 2 less for each difficulty level above the third
  (Conquistador), 2 more for each one below.

The player's turn counts with the new turn number: ending turn 227 on
Discoverer brought the King on turn 228 (period 19).

## What he says

A roll decides the news: a number from 1 to 1000, plus 5 × (twice the
rebel share minus the tax), plus the treasury / 100, plus the nation's
people, plus the turn / 30.

| Roll | News | Tax |
| --- | --- | --- |
| below 100 | a victory in Europe (`KINGVICTORY`) | down 2 to 5 (never below 0) |
| below 650 | another wedding (`KINGWIFE`, with his wife's ordinal), while he has had fewer than 30 wives | up 1 |
| below 950 | a war with a European country (`KINGWAR`), never the one of the last war | up 2 |
| below 1100 | a new navigation act (`KINGNAVACT`) | up 3 or 4 |
| otherwise | a stamp act on the colony's new land (`KINGSTAMPACT`) | up 5 to 8 |

The difficulty's title ("Discoverer") and the player's name head the text.
The tax always stays between 0% and 75%.

## A rise: kiss the ring or hold a party

When the tax goes up the player may refuse it (`TAXOPTIONS`) by throwing a
good into the harbour. The good is chosen from the coastal colony with the
most of it: the good traded most in Europe (tons traded / 100), with food
and tools counting half and horses and muskets a quarter, and never a good
that is already boycotted. With no such good there is no choice, the King
just announces the rise (sound 3Eh).

Holding the party (`TEAPARTY`, sound 56h):

- undoes the rise;
- destroys up to 100 of the good in that colony; the colony's Sons of
  Liberty get as many bells for it;
- boycotts the good in the home port: it can't be bought or sold there and
  the market shows a red cross over it.

A computer nation never holds a party.

## Ending a boycott

Trying to buy or sell a boycotted good in Europe asks for the back taxes
(`KISSUP`): 500 times the good's buying price. Paying ends the boycott and
the gold goes to the Crown. Without enough gold the King says how much
the player has (`KISSSORRY`). Jakob Fugger ends every boycott (see
[congress.md](congress.md)).

## The King's army

During the player's turn the Crown puts money aside for the army:
difficulty × 8 + 10, doubled once each from 1600, 1700 and 1750. When the
fund reaches 1800 the King buys a unit and says so (`KINGBUY`):

- artillery when there are more than four regulars per gun;
- dragoons when there are more than three regulars per dragoon (rounded up);
- a man-of-war when there is less than one per ten land units (rounded up);
- otherwise regulars.

The Continental Congress report shows the army (see [congress.md](congress.md)).

## Rebel sentiment

After the colonies' turn the nation's rebel share is worked out: each
colony's Sons of Liberty share weighed by its people. The King announces
it whenever it crosses a tenth (with at least four people in all):

- up past a tenth: `REBELUP`, or `REBELUP50` at 50% or more;
- down by more than a tenth (rounding up by 4): `REBELDOWN`.

Each colony also says when its own share passes a tenth (`SONSUP`,
`SONSDOWN`), unless Sons of Liberty messages are off in the colony report
options.

## The King's box

The King (`KING.SS`) stands at (20, 18), the box at x 105, centred
vertically. His arm (`KING2.SS`, eight frames over him at the same corner)
stays still for about four seconds, then rises through frames 1 to 7 at
about a sixth of a second each and stays up.

**Not yet done:** mercenaries (`MERCS`), the audience at the start (the King
lowering or raising the tax on request), treasure transport
(`KINGGALLEON*`, `CASHTREASURE`), the War of the Spanish Succession and the
King's wars between nations (see the European notes), and everything about
independence.
