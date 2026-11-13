# Natives

What happens when a colonist walks into a native settlement, worked out on
the original running in DOSBox: by changing fields in a save, entering a
village and reading what the original then says, and by stepping through
the original with the DOSBox debugger to see which fields it reads. The
numbers below are our own description of that behaviour.

## Entering a settlement

A unit that is not a ship and cannot attack (a colonist, pioneer, scout,
missionary and so on) that moves onto a settlement is greeted with a box in
the middle of the screen. The text depends on how alarmed the tribe is by
the unit's nation (the tribe's *tension*, 0 to 100 and more):

| Tension | Greeting (`GAME.TXT`) |
| --- | --- |
| 75 or more | `@VILLAGEWAR` |
| 50 or more | `@VILLAGEBAD` |
| 25 or more, or the settlement's own alarm at 128 or more | `@VILLAGEMEDIUM` |
| otherwise, for the Arawaks | `@VILLAGESAVAGE` |
| otherwise | `@VILLAGEHAPPY` |

The first blank is the settlement's word from the second column of
`@LEVELS` in `NAMES.TXT` (Camp, Village or City by the tribe's level), the
second the tribe's name ("Sioux").

The options come from `@ACTIONS`. A colonist or pioneer of a nation that
has met the tribe is offered **Live Among The Natives**; everyone is
offered **Cancel Action**, which leaves the unit where it was. A scout of
a nation that has met the tribe is offered **Ask to Speak With Chief**.
Any land unit with an attack above 0, except a missionary, is offered
**Demand Tribute** when its nation has met the tribe. Any land unit with
an attack above 0 is offered **Attack Village**, met or not: first (after
Speak) to units with an attack above 1, and after Demand Tribute to the
others (seen: artillery gets Attack Village, Demand Tribute, Cancel
Action; a scout Speak, Demand Tribute, Attack Village, Cancel Action).
Without contact with the tribe the others are not there. A missionary is
offered **Establish Mission** at a settlement without a mission,
**Denounce Heresy of French Mission** at another nation's mission, and
**Incite Indians** (see below).

Every native answer is told by a figure of the tribe (`IND<tribe>A<mood>.SS`)
whose mood is the step of the tribe's tension toward the player: 0 below
25, 1 below 50, 2 below 75, 3 above. The Iroquois chief at tension 80
was seen in his furious pose. The figure stands centred on the rows (its
top at (198 − height) / 2). The Iroquois and the Apache stand left of the
box, 6 pixels apart, and the two together are centred on the screen
(measured on two Iroquois moods and the Apache); the others stand right.

## First contact

A nation meets a tribe when one of its units stands on land and one of the
eight squares around it, also land, holds a unit, colony or settlement of
the tribe (the same check runs for a native unit beside a European one).
Ships at sea meet no one. The original makes the check after each move:

- Both sides get the "met" bit 0x20 in their relation bytes, the tribe's
  tension toward the nation drops to at most 20, and the tribe's 16-bit
  word at 46 + 2 × nation becomes 2 (its use is not read yet).
- A computer nation always accepts the treaty: bit 0x40 on both sides.
- The player sees the map centre on the unit and the tribe's chief ask
  `INDIANWELCOME`: "The Iroquois tribe welcomes you. We are a glorious
  nation of 11 Villages", with the tribe's adjective, its number of
  settlements and the plural of its level from the third column of
  `@LEVELS`.
  - **Yes**: the treaty bit 0x40 on both sides, then `INDIANPEACE` ("The
    Iroquois welcome peace with our brothers the Dutch") and, when the
    tribe's tension is below 25 (always, just after the meeting),
    `INDIANCOME`, the invitation to visit and trade.
  - **No**: no treaty, the tribe's tension becomes 100 (measured from 2),
    and the chief, now angry, answers `INDIANSHUN`: "Prepare for WAR!".
    No war is declared yet.

The answers were measured in DOSBox: Yes and No on the Iroquois, after
clearing the Dutch contact bytes in a save. Two things are not done yet:
the original also has `INDIANTREATY` and `INDIANHELLO1`/`2` in GAME.TXT,
which its code never asks for, and the welcome's map shows the unit still
on its old square, where we draw it on the new one. Without the treaty
bit a settlement offers only Cancel Action.

## How tension moves

The original changes a tribe's tension toward a nation in one place, which
first contact, an attack, inciting and trade all use:

- A rise is halved for the French, and halved again for a nation with
  Pocahontas. The result stays within 0 to 100.
- A fall ends a war (bit 0x04 off on both sides) and, below 75, clears bit
  0x02 too. When it crosses a step of 5, each of the tribe's settlements
  calms toward the nation: its alarm is cut to at most 0x20 (tension below
  50) or 0x60.
- Not done yet: a rise to 100 with a treaty may make the tribe declare war,
  with a chance of the difficulty level plus 2 in 11 (3 in 11 for a
  computer nation).

## Inciting a tribe

A missionary at a tribe its nation has a treaty with is always offered
**Incite Indians** (after Establish Mission, or Denounce Heresy at another
nation's mission). The chief asks `INDIANWARPATH`, "Whom would you like us
to attack?", listing the other European nations by name.

- If the tribe has not met that nation: `NOCONTACT`, "We have no contact
  with the English."
- Otherwise `INDIANWARPATH2` names the price, with **Pay** or **Never mind**.
  The price is (the tribe's tension toward the missionary's nation + 75) ×
  (6 × its settlements + its defence total / 4 rounded down to even + 2 ×
  its bytes 7 and 8), the defence total being the one the force count of
  [tribute](#counting-forces) keeps. The French pay two thirds. Each of the
  tribe's settlements with a mission of the nation takes 250 off (1000 for
  an expert mission; twice either at a capital), a Jesuit 1500, and a
  capital 500. It is at least 500.
- Paying with too little gold: `UNFORTUNATE`. A tribe already at 75 or
  more toward the target: `ALREADYSMITE`.
- Otherwise the colonial advisor tells `INDIANWARFARE`: "Apache nation
  holds War Council! Dutch missionaries incite Apache to warfare against
  the French!". The tribe's tension toward the target rises by 100 (as
  above, so 50 for the French), and the price leaves the treasury.

Either way the missionary's turn is over. Measured in DOSBox with a Dutch
missionary at the Apache: 6552 at tension 51 with defence 56 (6
settlements, bytes 0 and 1), 4650 at tension 0 with defence 100; paying
took 4650 and put the Apache at 50 toward the French.

## Pressure on a settlement

The original weighs how hard the Europeans press on a native settlement
(read with a debugger; denouncing heresy uses it):

- Each European nation's armed units on land within two squares count:
  the attack values above 1 of the units on a square (ships left out),
  halved when the square is a colony and halved again two squares out.
- Each colony within 6 (the longer side plus half the shorter) scores
  ((people beyond 6) × 2 + min(people / 2, the tribe's level) + min(people,
  6) + B) × 2 − distance − 1, divided by distance + 4. B is (buildings −
  8) / 4, where buildings counts the colony's building bits; for the
  player's colonies each bit weighs 1, 3, 1, 3, 2 and the sum is divided
  by 2, 4, 1, 2, 1 on difficulty levels 0 to 4, and the difficulty level is
  added to B. The score is halved for a colony on other land, then the
  nation's armed units are added, and it is halved for the French and
  again with Pocahontas.
- The colony with the highest score names the nation pressing hardest. A
  mission of another nation adds half (an expert mission doubles it); the
  nation's own mission takes a quarter off (an expert mission half).

## Denouncing heresy

A missionary at another nation's mission weighs the two sides:

- For each of the tribe's settlements, the pressure goes to the
  denouncer when the mission's nation presses hardest, and to the
  mission's nation when the denouncer does. Every settlement with a
  mission also adds its people (doubled for an expert mission and again
  for a capital) to the denouncer; the original's test there compares the
  two nations rather than the mission's, so it always takes this branch.
- The denouncer adds the tribe's tension toward the mission's nation (×16
  at a capital); the other side the tension toward the denouncer, halved
  (nothing at a capital, where the original shifts by 29).
- Each side's anger is its weight's tension step + 1 (0 to 3, from the
  weights so far). At a capital both weights get 1 to 20 more and both
  angers double. A missionary whose profession byte is 3 doubles the
  denouncer's weight and the anger toward the mission's nation (the
  original compares 3, not the Jesuit's 24); an expert mission doubles the
  other side's.
- A roll from 1 to the sum at most the denouncer's weight wins:
  `HERESY0`, "Iroquois converts burn French mission and erect a new, Dutch
  one!"; the mission becomes the denouncer's, and the tribe calms toward
  the denouncer by its anger and grows tenser toward the other nation by
  its anger (through [the tension rule](#how-tension-moves)). Otherwise
  `HERESY1`: the missionary is burned at the stake, and the tribe calms
  toward the other nation and grows tenser toward the denouncer.
- The religious advisor tells either; the missionary is gone either way.

Seen twice in DOSBox at the Iroquois capital with a French mission: the
Dutch won, the mission turned Dutch, and the tension went from 2 to 0
toward the Dutch and from 0 to 1 toward the French.

## Learning a skill

Living among the natives uses up the unit's moves for the turn, whatever
the answer. The tribe's chief (the tribe's own portrait) answers, checking
in this order:

1. Tension 50 or more: `@LEARNMAD`, nothing is taught.
2. A petty criminal: `@LEARNCRIMINAL`.
3. An Indian convert: `@TEACHCONVERT`.
4. Anyone but a free colonist or an indentured servant (an expert
   already): `@LEARNMASTER`, naming the unit's own skill.
5. A settlement that has taught before, unless it is the tribe's capital:
   `@LEARNALREADY`. A capital teaches again and again.
6. Tension 25 or more: a roll of 1 to 1000 below 200 × difficulty + 100
   gives `@LEARNSLOW`, nothing is taught this time.
7. Otherwise `@LEARNSTAY` offers the settlement's skill. Accepting makes
   the unit an expert in it (`@LEARNDONE`) and marks the settlement as
   having taught; declining answers `@LEARNLATER`.

## A scout speaks with the chief

Speaking with the chief uses up the scout's moves. The chief (the tribe's
portrait) first checks for broken taboos:

1. Tension 75 or more breaks a taboo.
2. Otherwise a roll of 0 to 100 is made (0 to 140 for a seasoned scout).
   With tension 25 or more, a roll at or below a quarter of the tension
   breaks a taboo.
3. The Arawaks also take offence on a roll of 0 out of 0 to
   8 − difficulty (doubled for a seasoned scout).

A broken taboo kills the scout (`@CHIEFKILL`). A nation with Francisco
Coronado in its congress is only told it is welcome (`@CHIEFBORED`)
instead.

Otherwise the chief greets the scout (`@CHIEFHOWDY`). The greeting names
the village's experts (the second column of `@JOB` for the skill it
teaches) and the three goods it wants most. Those come from the
settlement's economy, after taking out the goods it makes most of, food
when it makes more food than trade goods, and the goods in the
settlement's bytes 8 and 9. They are sorted with the original's own
sort, whose order for equal values decides ties.

Then, if the roll was above the tension and no scout has spoken with this
chief before, the settlement remembers the visit and a roll of 1 to 3
decides:

1. **Guides** (`@CHIEFGUIDES`): the scout becomes a seasoned scout. A
   seasoned scout gets the tales instead.
2. **Tales of nearby lands** (`@CHIEFAREA`): the squares next to the
   scout and the land of the same region within 6 squares are explored.
3. **A gift** (`@CHIEFGIFT`): three rolls of 1 to 10 − difficulty, added
   up, times a roll of 1 to 6, times 4, times the tribe's level + 1, in
   gold.

In every other case the chief only says the scout is welcome
(`@CHIEFBORED`). The tribe is named by its adjective ("Arawak") and the
nation by its own ("Dutch").

Checked on six settlements of four tribes: the experts and the goods
asked for match the original's greetings.

## A missionary founds a mission

The missionary stays in the settlement: the unit is gone and the
settlement's byte 5 holds the nation (with bit 0x10 for a Jesuit
missionary, or for any missionary of a nation with Jean de Brebeuf).

How the tribe takes it depends on the missions the nation already has
among the tribe. Their number is doubled with Juan de Sepulveda, halved
with Bartolome de las Casas, halved with Pocahontas and halved for the
French (each halving rounds down). The change in tension is that number
× 8, less 25, 15, 10 or 5 for a tribe whose tension is below 25, 50, 75 or
higher. At a capital the change moves 8 further from zero. The change is
added to the tribe's tension toward the nation (not below 0).

The message (`@MISSION0` to `@MISSION3`: curious, cautious, offended,
hostile) starts from the tension's step (0 to 3); a change of −5 or more
makes it at least cautious, a change above 0 at least offended, and 10 or
more hostile. It names the mission by `@MISSION` in `NAMES.TXT` and the
nation's nearest colony ("Church of Curaçao mission founded in Autumn,
1653. Apache are offended."), measured as the longer side plus half the
shorter, a later colony winning a tie. The religious advisor shows it.
Measured once: the Apache, at tension 51, went to 41 and were offended.

## Demanding tribute

Demanding tribute uses up the unit's moves. The answer weighs two counts
of forces that the original keeps by land region (see *Counting forces*
below), for the region of the square the unit stands on:

- the nation's *might*: its attack outside colonies in the region, plus
  half its attack in all; half again for Spain, and half again with Hernan
  Cortes;
- the tribe's *defence*: twice (its defence in the region plus half its
  defence in all), plus half the tension.

A roll of 0 to the might against a roll of 0 to the defence: the village
is *cowed* when the defence roll is below the might roll, and only when
the nation has a colony on land of the same region (the nearest, measured
as for missions). Then, in this order:

1. Not cowed with a might no greater than the defence, or a tension of 75
   or more: `@EXTORTLAUGH` ("... {Iroquois} warriors are known for their
   ferocity ..."). The tension rises by difficulty + 1.
2. Not cowed with a tension of 50 or more: `@EXTORTNO`, naming the
   player by the difficulty's title and the leader's name. The tension
   rises by difficulty + 1.
3. Not cowed, or the village has paid tribute before (byte 3, bit 0x10):
   `@EXTORTPOOR`. The tension does not move.
4. Otherwise the village pays: it marks itself as having paid, and
   delivers the good it makes most of (its economy as in the skill pick,
   sorted the original's way) to the nearest colony: as much as the
   warehouse has room for, at most three times what the village makes of
   it plus 10 and at most 100, but at least 10 (`@EXTORTSTUFF`, "Great
   Discoverer, ... the humble and peaceloving Iroquois shall gladly
   deliver 30 Food to Roanoke ..."). The tension rises by twice
   (difficulty + 1).

For a computer nation the tension moves by 1 (2 when paid) instead.
Measured: with the Dutch strong in the region and no Iroquois braves
counted, a scout at an Iroquois village got 30 Food delivered to
Roanoke; with the tension at 80 the chief laughed.

### Counting forces

When the player's turn ends the original counts every nation's units,
leaving out ships. A unit's attack (or defence) is its column of `@UNIT`,
2 less for damaged artillery, times 8, and half again for a veteran
soldier or dragoon. Its region is the low four bits of the path layer
under it, for land inside the map's border (none for units in Europe or
at sea).

- A European nation adds each unit's attack to its total (16 bits), and,
  when the unit has a region, to that region (a byte that stops at 255).
  Units standing in a colony are left out of the region for the human
  player, and for a computer player when the unit's byte 7 holds `A` or
  `G`.
- A tribe adds each brave's defence to its total and to its region (bytes
  that stop at 255). The counts in `COLONY00.SAV` match its braves: 8 for
  a brave, 16 for an armed one.

The counts are kept in the save; see [the savegame format](../formats/savegame.md).

## Attacking a village

Read from the original with a debugger, then measured on the Apache camp
at (4, 18) in `COLONY00.SAV` (easiest level):

- Choosing **Attack Village** makes war at once, whatever the fight's
  outcome: the war bit 0x04 is set in the nation's relation to the tribe
  and in the tribe's relation to the nation; the tribe's tension toward
  the nation goes up by 10 and twice the difficulty level (measured 10,
  14 and 18 on levels 0, 2 and 4, from 0 and from 40 alike); the high byte
  of the village's alarm toward the nation goes up by one.
- A native unit standing on the settlement defends it. When there is
  none, one of the village's people stands up: a **brave**, armed when
  the tribe has muskets (its byte 7 is not 0), mounted when it has 25
  horses or more.
- The fight is the land fight of [combat](combat.md) with the settlement
  in place of the colony: +50% for a camp or village, +100% for a city of
  an advanced or civilized tribe, twice that for a capital; fortified
  adds +50% only below +125%; no artillery penalty in a settlement; Spain
  attacks natives in their settlements with half again. A brave cannot
  stand up to firearms: against an attack above 1 its defence is halved,
  as for anyone with a combat value below 2. The Combat Analysis box shows
  the brave ("Braves 1") and the settlement by its word in `@LEVELS`
  ("Camp +50%") with its picture.
- A beaten brave is gone, without a message; half the time the tribe gets
  back its muskets (byte 7 one up) and its horses (25 more).
- A won fight against the village's own people costs it a person. When
  the last one falls it burns: the settlement and the braves who live
  there (unit byte 6 is their home) are gone, the other natives' home
  numbers above it move down, the tribe's settlement count (byte 581 +
  tribe of the block after the tribe records) drops by one, and the
  nation's count of burned villages (its byte 24) goes up. A human
  player's burning sets bit 0x40 of the tribe's byte 3. The attacker's
  own mission sends its missionary out onto the settlement's square (a
  Jesuit for an expert mission).
- Each such win where the attacker runs the mission may bring converts:
  the chance is 4 in 13 (8 for an expert mission), 4 more for Spain and
  with Juan de Sepulveda, 4 less with Bartolome de las Casas. A convert
  appears on the attacker's square: "Frightened Apache flock to Dutch
  mission as converts." (`@INDIANSLAVES`).
- The ruins may hold treasure, in hundreds: a camp 2 to 4 (only one time
  in 7, one in 4 for Spain), a village 3 to 8 (one time in 3); doubled
  for a capital, half again with Hernan Cortes, and a capital or Cortes
  always finds some. A city of an advanced tribe has 20 to 60 (40 to 100
  for a capital), 60 more with Cortes and 30 more for Spain; a civilized
  tribe's city 2 to 6 times 16 (25 for a capital, 10 more with Cortes,
  5 more for Spain). The treasure stands on the settlement's square (unit
  byte 23 holds the hundreds): "Dutch loot and burn Apache Camp! Natives
  flee in panic! 300 worth of treasure recovered from ruins!" (`@LOOT`,
  measured with Cortes), or `@LOOT2` without treasure.
- A burned capital calms the tribe down to 15 and clears every one of its
  villages' alarm toward the nation; the chief tells a human player that
  the tribe bows (`@INDIANBOW`).
- A lost fight is lost as anywhere: a soldier becomes a colonist, a
  dragoon a soldier; a scout dies, without a message (measured).
- The attacker's turn is over, win or lose.

## Trade

A wagon train, or a ship sailing into a coastal settlement, of a nation
that has met the tribe is offered **Trade With Village**. Trading uses up
the unit's moves.

**Selling.** With more than one hold the player picks the cargo
(`@TRADEWHICH`). The village's wants are its economy's, less the three
goods it makes most of and less food when it makes more food than trade
goods. A good it does not want, or the good last sold to it (byte 8) or
last bought from it (byte 9), is refused (`@BADCARGO`, naming the three
goods it wants most). A good it refused after haggling (byte 7) gets
`@BADHAGGLE1`. Otherwise it offers (`@TRADE0`):

- a roll of 1 to 5 (*luck*); a *worth* of 6, or 7 for goods after horses,
  less a roll of 0 to 7 for trade goods, plus 12 − the tribe's byte 7 for
  muskets, 10 − its byte 8 for horses, and 1 for tools;
- a *temper* of twice the tension step (0 for muskets and horses), halved
  when the village wants the good 20 or more;
- the price: (5 × luck + 2 × (worth − difficulty − temper + luck + 4) ×
  want) × amount / 100, halved, at least 1;
- the quality word (`@VALUES`): (want − temper + 4) / 10, at most 3;
- the "fairer price": (want + 1) × 4 + the price;
- a *calm* of a roll of 0 to 1 + (want − temper + 4) / 4.

Measured: a hundred cigars at an Iroquois village with nothing between
them brought 434, 511 (want 36, rolls 1 and 3), sixty brought 283.

Accepting pays the price, adds the goods to the tribe's stock, lowers the
tension by 2 × calm and the settlement's alarm by the amount (all of it
for a hundred), and remembers the good in byte 8. (The original compares
the unit's number, not the good, with muskets and horses there; that is
kept.) Muskets and horses also arm and mount the tribe (its bytes 7 and 8
and its horses).

Asking a fairer price works while there is calm left and a roll of 1 to 8
× calm beats the difficulty: calm drops by one and the offer rises by a
roll of want / 2 + 1 to 2 × want + 1, times the amount / 100
(`@TRADE1`). Otherwise the village will not take the good again
(`@BADHAGGLE0`), the tension rises by temper / 2 + 1 and trading ends.

Giving the goods away (only before haggling) lowers the tension by 4 ×
(calm + 1) and the alarm by twice the amount.

**Buying.** After a sale or a gift, with a hold free: unless the good
offered was one of the two most wanted, the village names what it needs
(`@BRING`). After a refusal to sell (byte 7 = 0xFE) it says so
(`@BADHAGGLE3`); a unit that brought nothing hears `@DEFICIT`. Otherwise
it offers the three goods it makes most of, leaving out muskets, food,
tools and trade goods (`@BUYWHICH`). A wagon train is filled with as much
as it sold, a ship with a quarter. The price (`@BUY0`): 200, or (8 −
level) × 50 for horses and after; for silver and after plus the
European bid × (2 × difficulty + 15); plus a roll of 0 to itself; less 4
× what the village makes of it; plus 4 × tension; times the amount / 100;
plus (difficulty + a roll of 0 to 2) × 10; at least 50. Buying without
the gold (`@NOTENOUGH`) raises the tension by 1; buying lowers it by a
roll of 0 to price / 25 + 1. Haggling (a roll of 0 to made / 25 + 8 above
difficulty + 1, with the price above 10) takes a quarter off (`@BUY1`),
sometimes raising the tension by 1; failing raises it by 2 and the
village sells nothing more (`@BADHAGGLE2`).

## The skill a settlement teaches

A settlement always teaches the same skill in a game. The original works
it out each time from the land around it, with a random pick whose seed is
fixed by the settlement's square:

1. **The land.** The 25 squares of the 5 × 5 block around the settlement
   are counted by kind, leaving out the edge of the map and squares a
   colony works. Open land, forests, hills, mountains, arctic and water
   each feed their own tallies (for example sugar from savannah, swamp
   and tropical forest, tobacco from grassland, marsh and conifer forest,
   cotton from prairie and broadleaf forest, furs from forest, ore from
   hills and mountains).
2. **The economy.** From the tallies, the settlement's size and the
   tribe's level (semi-nomadic, agrarian, advanced, civilized) come two
   lists of 16 goods: what the settlement offers and what it is after. A
   capital counts double. What the tribe already holds shifts both lists.
   This is the same economy the original uses when trading.
3. **The weights.** The second list is the weight of each good. Horses
   never count. Lower tribes lose some goods: below agrarian no coats and
   no ore and half the food, below advanced no cloth, cigars or silver and
   a quarter less food, below civilized no rum; a civilized tribe adds half
   again to silver.
4. **The pick.** The original's C library random generator is seeded with
   (row × 256 + column + the game's seed) and draws a number from 1 to the
   total weight; walking the goods in order picks one. The good's row is
   the row of the skill in `@JOB` (food is farmer, furs fur trapper, and so
   on).
5. **Two exceptions.** Furs on a square whose column + row is a multiple
   of 3 teach scouting instead. Food teaches fishing instead when a second
   roll of 1 to 20 is below the number of water squares in the ring of 20
   around the settlement.

Checked against eleven settlements in one game, among them four Sioux
camps (fur trapping), Iroquois villages teaching farming, scouting and
fishing, Cherokee villages teaching ore mining and cotton planting and
an Apache one teaching cotton planting; all eleven match.

## Save fields

See [the savegame format](../formats/savegame.md) for the settlement
record, the tribe records and the nation's contact bytes.
