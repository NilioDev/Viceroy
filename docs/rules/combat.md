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

Who wins is not measured yet: we let the attacker win in proportion to the
two strengths.

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
