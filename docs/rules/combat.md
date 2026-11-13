# Combat

How a fight on the map goes, measured on the original running in DOSBox: a
Dutch attacker next to a French unit in conifer forest, the savegame edited
between runs (veteran or not, fortified or not, damaged artillery), with
Combat Analysis switched on in the game options.

## Starting a fight

- A soldier, dragoon, artillery or other unit with an attack value that
  moves onto a square with a foreign European unit attacks it. The strongest
  unit there defends.
- With a peace treaty (bit `0x40` of the nation's attitude byte towards the
  other nation) the colonial advisor asks first (`@HAVETREATY`). Breaking it
  clears the bit on both sides and says so (`@CANCELPEACE`, the attacked
  nation first).
- A unit with less than a full move left is asked about (`@HALF`): it
  fights at the thirds of a move it has left.

## The strengths

Each side has a base value from `@UNIT` in `NAMES.TXT` (attack for the
attacker, combat for the defender), and bonuses that add up as percentages.
The Combat Analysis box lists them in this order:

| Side | Bonus | Value | When |
|---|---|---|---|
| Attacker | Veteran | +50% | a veteran soldier or dragoon |
| Attacker | Attack Bonus | +50% | always |
| Attacker | Artillery In Open | −75% | artillery attacking outside a colony a unit that is not fortified |
| Defender | Veteran | +50% | a veteran soldier or dragoon |
| Defender | Terrain | the square's defence value × 25% | outside a colony; shown with the square's picture |
| Defender | Fortified | +50% | orders "fortified" |
| Defender | Artillery In Open | −75% | artillery outside a colony |

Artillery against a fortified unit gets no penalty, which matches the
manual saying artillery is good at attacking fortified units.

Damaged artillery (bit `0x80` of the unit's fifth byte) attacks at 5
instead of 7 and is drawn with its own, smaller picture. Its defence of 3
is the manual's value and not measured.

## Who wins

The box rounds things off. The fight itself is rolled on two whole numbers,
watched in the original with a debugger and checked on a test fight (a
veteran soldier attacking a dragoon in forest on Discoverer: 80 against 36).

1. Each side starts from its base (damaged artillery: 2 less) × 8. A
   veteran soldier or dragoon adds half of that.
2. The defender's bonus `m`, in quarters: in a colony 2 × (stockade level
   + 1), so 2, 4, 6 or 8; outside one, the square's defence value when both
   sides are European. A fortified unit (not a ship) adds 2 more when `m` is
   below 5. Defence = strength × (4 + m) / 4.
3. Attack × 3/2, the attack bonus.
4. A human attacker adds 4 − difficulty to its attack, a human defender the
   same to its defence.
5. With less than a full move left, the attack is cut to the thirds left.
6. Against someone who cannot fight (combat below 2), an attacker with an
   attack above 1 halves the defence.
7. Outside a colony, artillery fights at a quarter, unless the defender is
   a European fortifying or fortified.
8. On Discoverer and Explorer: before turn 80 an attack on a human colony is
   cut by a quarter (Discoverer) or a half (Explorer); a human defender is
   attacked at half strength before turn 80 or by natives; and on Discoverer
   a human attacker doubles its attack.
9. A roll from 1 to attack + defence: the attacker wins at the attack or
   below.

All divisions round down. Not covered yet: natives' settlements and
ambushes, ships, a colony's own garrison bonus, and the rules after
independence.

## What the loser loses

All told by the colonial advisor, box at x 54 centred on row 139:

- A dragoon is demoted to soldier, a soldier to colonist (`@DEMOTE`). A
  fortified unit stays fortified.
- Artillery is damaged the first time (`@ARTILLERY`) and destroyed the
  second (`@ARTILLERY2`). The damaged unit stays on its square.
- A colonist, wagon train or treasure is captured: it changes nation,
  moves onto the winner's square and waits for orders (`@COLONISTCAPTURE`,
  `@COLONISTCAPTURE2` for a veteran, `@WAGONCAPTURE`, `@LOOTCAPTURE`).
- The attacker's turn is over, win or lose.

The map stays on the fight while the message is up; then the next unit
gets its turn.

## The Combat Analysis box

214 pixels wide and 26 + 20 per row high, centred on the screen, in the
tiny font: the title in green one pixel right of centre at row 9, the
attacker from x 5 and the defender from x 107. Each column starts with the
unit (one pixel higher than on a map square) and its name, then a row per
bonus; names and labels are green, values gold and right-aligned 80 pixels
in. Any key closes it and the fight follows.
