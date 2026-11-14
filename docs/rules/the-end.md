# The end of the game

Read from the original with a debugger and a disassembler: we looked at
what it does and wrote it down here in our own words. Header words are
counted in bytes from the start of the savegame.

The original looks at the end of the game once a turn. Viceroy does so
when the year turns, before the player's units under Go To set off (where
the original calls it is not found yet).

## All colonies lost

From 1600, a player outside the war with no colony left hears the King
(`LOSENOCOLONIES`: "Our efforts in the New World have proven fruitless")
and the game is over.

## The war of independence

During the war, until it is won (header byte 18 bit 08h):

- **Won** when the Crown holds none of the rebels' colonies, its land
  units in the New World (regulars, cavalry and artillery) are fewer than
  one (fewer than eight once it has taken a colony in the war: header byte
  18 bit 40h, set when it does), and its force at home, regulars plus one
  for any dragoons and one for any artillery, is below four. Then the music
  turns (mood now 3), `WINNING` ("Royal Expeditionary Force annihilated!")
  and the King's `KINGLOSE`. Header byte 18 gets bit 08h and header word 50
  becomes 1.
- Otherwise the rebels' ports (coastal colonies), colonies and the
  Crown's share of the colonists in colonies (each side counted one more)
  are looked at:

  | | warning | defeat |
  |---|---|---|
  | ports | fewer than 3: `WARN1` | none: `LOSING1` |
  | Crown's share | 80% or more: `WARN3` | 90% or more: `LOSING3` |
  | colonies | fewer than 3: `WARN2` | none: `LOSING2` |

  The later rows win over the earlier ones. A defeat is followed by the
  King's `KINGWIN` and the game is over; otherwise the warning, every
  turn, with the number of ports, of colonies and the share.

The first time during the war that the rebels take a colony (back), they
hear `HOWTOWIN` (header byte 22 bit 01h), before the capture's own news.

## The other nations' independence

Outside the war, each computer nation that is not yet independent (bit
04h of its flags) is looked at. Its rebels are its rebels in % times its
people / 100, at most 100; it needs 10 × (8 − difficulty) of them.

- With enough, its King lets its colonies go (`OTHERGRANTED`): its new
  land takes the name of its line of `@INDEPENDENT` in NAMES.TXT, it gets
  bit 04h, and its relations with every other European nation keep only
  peace (and bit 04h).
- Within 20 of that and more than the spies last told (byte 1Ah of the
  nation record): `OTHERMIGHT`, and the new number is kept.
- More than 5 fewer than last told: `OTHERLESS`, and the new number is
  kept.

## Retirement

Until the end has come (header byte 18 bit 10h):

- In the spring of 1790 outside the war, `SOONRETIRING0`; in the spring of
  1840, `SOONRETIRING1`.
- In 1800 outside the war, or in 1850, the Viceroy retires (`RETIRING`, or
  `RETIRING2` during the war) to the biggest colony (of the biggest, the
  last). Right after that message the leader is scored (below), then the
  rest of the news follows (`SCORED`).
- **Retire** in the Game menu asks `RETIRE` ("Do you really want to
  quit?", No first). Yes scores the leader the same way and then ends the
  program; once scoring is complete it just ends.

### Scoring a retirement

Read from the original:

1. The Colonization Score (F10, see [reports](reports.md)), until a key.
2. The score weighed by the difficulty: 4, 5, 6, 8 or 10 in a hundred
   (Discoverer to Viceroy). The step reached is the last n of 1 to 24
   with n² ÷ 3 below that weight, counted from 0 (at most 23); the
   rating is half the weight. With no step reached the next screen is
   left out.
3. What the citizens name after the leader, on `WOODPAN2.PIK`: the three
   lines of `EXPLOITS` centred from y 5 in colour 0xFC (the rating, the
   new land's name), the step's picture (`SCORE01.SS` to `SCORE24.SS`,
   their colours from 16 on) at (33, 40), under it at y 142 the step's
   name with the leader's last name (`@SCORE`, after the comma, one space
   kept before it) centred in x 34 to 174, and the list of the steps
   reached so far centred in the right half, from the bottom up one line
   each, the last in colour 0xFC and the others in 0xFE. A tune plays:
   driver command 24h for the last step, 25h above the seventh, 21h
   otherwise.
4. The Colonization Hall of Fame on `WOODPANL.PIK`: the title centred at
   y 3 in green (0x44), then for each of five places from y 20, 4 pixels
   apart: "n.  difficulty name of the [Free] nation" at x 10; at x 25
   "Leader, nation Colonies", "General, Continental Army" after the
   declaration or "President, " and the new nation's name (`@INDEPENDENT`)
   once won, then " to A.D. year.  Score: n"; and "--- Colonization Rating:
   n% ---" centred. The newcomer is in gold (0x95), the others in green;
   all with a black shadow a pixel to the right, in the map's label font.
   The newcomer goes before the first place with a lower rating; when it
   comes sixth it is shown in the fifth place but not kept.
5. `HALLFAME.DAT` keeps the five places, 42 bytes each: the leader's name
   (24 bytes), then 16-bit words for the nation (-1 for an empty place),
   declared, won, year, season, difficulty, score, rating and step.
   Viceroy keeps it beside the saved games, in the original's format.

Known difference: in the Hall of Fame the original leaves out a few of the
shadow's pixels inside the letters.

## After the end

A win or a retirement asks `SCORED`: "That's all." ends the game, "Keep
playing anyway." sets header word 82 and the game goes on with no more
retirement and no more scoring. A defeat ends the game even then. In
either case header byte 18 gets bit 10h. For now Viceroy closes when the
game is over; what the original does after its score is not looked at
yet.

## Not yet done

- The score the original may reckon before the end after a defeat.
- Where the boxes stand on the screen is not measured; they stand where
  the battle news does.
