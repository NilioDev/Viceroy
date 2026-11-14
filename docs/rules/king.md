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

## Mercenaries

At the start of the player's turn, before independence, one turn in 21
a European king offers mercenaries (`MERCENARIES`). The king is picked from
the four nations at random; another nation's king only offers once the
player has met that nation. The force is 1 to 3 dragoons, then a coin: one
more dragoon, or 1 or 2 artillery. Each dragoon costs (0 to 6 + 2 ×
(difficulty + 4)) × 100, the same for all of them, and a gun twice that.
The offer only comes when the player has the gold.

Paying takes the gold at once. The mercenaries, veteran soldiers by
profession, come by ship to one of the player's first ten coastal colonies,
picked by their people, and walk in (`MERCS`). Without a coastal colony the
gold is gone and nobody comes.

## Treasure

When the player's treasure walks into a colony by the sea, the King offers
to carry it home (sound 3Eh) if the player has no galleon
(`KINGGALLEON2`), or always once Hernan Cortes is in congress
(`KINGGALLEON3`, "for no extra charge"). He asks again each time the
treasure enters such a colony; refusing leaves it there.

The Crown's share is the tax rate with Cortes. Without him it is twice the
tax rate, but at least 50% (5% more for each difficulty level above
Discoverer), and never more than 90%. The share goes into the Crown's army
fund, the rest into the treasury, and the treasure is gone (`LOOTCASH`,
told by the trade adviser). The original also adds the player's part to a
32-bit total at byte 38 of the nation record.

During the war of independence the treasure is sold to foreign agents at
once for its full value (`CASHTREASURE`).

The treasure's value is kept in hundreds in byte 23 of the unit, where a
colonist keeps its profession.

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

**Not yet done:** the mercenaries offered during the war of independence, the audience at the start (the King
lowering or raising the tax on request), the War of the Spanish Succession and the
King's wars between nations (see the European notes), and everything about
independence.
