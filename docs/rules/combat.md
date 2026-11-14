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
- A foreign colony can be attacked too. Units on its square defend it as
  anywhere else. With nobody there, one of its colonists, picked at random
  (seen: a different one in each try on Quebec), stands up to defend it.

## The strengths

Each side has a base value from `@UNIT` in `NAMES.TXT` (attack for the
attacker, combat for the defender), and bonuses that add up as percentages.
The Combat Analysis box lists them in this order:

| Side | Bonus | Value | When |
|---|---|---|---|
| Attacker | Veteran | +50% | a veteran soldier or dragoon |
| Attacker | Ambush | the square's defence value × 25% | a tribe attacking a European in the open who is not fortified (the defender then gets no terrain) |
| Attacker | Attack Bonus | +50% | always |
| Attacker | Artillery In Open | −75% | artillery attacking outside a colony a unit that is not fortified |
| Defender | Veteran | +50% | a veteran soldier or dragoon |
| Defender | Terrain | the square's defence value × 25% | outside a colony; shown with the square's picture |
| Defender | Muskets | +1 to the base | a colony's colonist, when the colony has 50 muskets (49 give nothing) and its nation Paul Revere |
| Defender | Colony | +50%, +100%, +150%, +200% | in a colony; named after its stockade, fort or fortress when it has one, shown with the colony's picture |
| Defender | Fortified | +50% | orders "fortified", where the colony gives less than +125% |
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
3. A tribe attacking a European in the open who is not fortified: attack ×
   (4 + the square's defence value) / 4, the ambush. Then attack × 3/2, the
   attack bonus.
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
9. In a colony: a tribe's attack on a nation's last colony is 0, and a
   colony of the player holding at least half the player's colonists in
   colonies adds 4 × (4 − difficulty) to its defence.
10. A roll from 1 to attack + defence: the attacker wins at the attack or
   below.

All divisions round down. See [natives](natives.md) for the natives' side.

## During the war of independence

Read from the original with a disassembler. After Spain's bonus (step 7)
and before the steps for the easy levels:

- On a colony, the Crown's attack gets half again, and then the colony's
  Tories in % (100 − its Sons of Liberty). The rebels' attack on a colony
  gets its Sons of Liberty in %, with half again first once the foreign
  power has joined the war.
- In the open, the Crown's attack on land gets 5% for each level of
  difficulty (attack × difficulty / 20).
- On Discoverer and Explorer the help to a human colony under attack
  (step 8, its first two parts) is gone during the war, unless a ship
  attacks.

At sea, the last ship of a kind is no longer only damaged during the war;
only the Crown's last man-of-war is. A rebel ship that is damaged with no
drydock of its own to go to is lost, as Europe is closed. The Crown's
damaged ships go home to the player's home port.

## Hardening after a win

Watched in the original with a debugger. After every fight the winner, if it
is a soldier or dragoon, may move up:

- A free colonist becomes a veteran, an indentured servant a free colonist,
  and a petty criminal an indentured servant. Indian converts stay converts.
  Experts don't move. Veterans move only during the war of independence,
  once the player has raised the Continental Army (the player's flag,
  whoever the unit is): a veteran soldier becomes Continental Army and a
  veteran dragoon Continental Cavalry, only for a human player
  (`@CONTINENTAL`, which names the new kind).
- With George Washington in the nation's congress it always happens.
- Otherwise the original rolls 1 to (own + opponent's number), using the two
  numbers the fight was rolled on. The human player's units add the
  difficulty level to that range, other nations subtract it, and a criminal
  takes off 10 and a servant 5. The unit moves up when the roll is at or
  below the opponent's number. So an unlikely win hardens more often.
- The human player is told: `@VETERAN` for a new veteran, `@VALOR` for a
  servant or criminal moving up, both by the colonial advisor.

## Taking a colony

Measured on Martinique (1 colonist) and Quebec (11) of a French nation with
16 colonists in its colonies and 1000 gold:

- The colonist fights with the colonist's combat value of 1, named by their
  skill ("Ore Miner"). They count as someone who cannot fight, so the
  defence is halved as for any colonist.
- When the attacker wins it marches in: the colony changes hands with its
  colonists, buildings and goods, and its liberty bells start again.
- The owner loses gold × the colony's colonists / all their colonists in
  colonies, rounded down, to the attacker: 62 for Martinique, 687 for
  Quebec. `@CAPTURED` says so: "Dutch march into Martinique! 62 plundered!"
- Then the colony's screen opens.
- When the colonist wins, the attacker loses as in any fight.

Not covered yet: ships in the harbour (an attack is refused while one is
there), what the defending colonist's win does, and the foreign affairs
counts in the save.

## What the loser loses

All told by the colonial advisor, box at x 54 centred on row 139:

- A dragoon is demoted to soldier, a soldier to colonist (`@DEMOTE`). A
  fortified unit stays fortified. Read from the original's list: cavalry
  become regulars, continental cavalry continental army, continental army
  colonists, and a Jesuit soldier a missionary again. Scouts and regulars
  are not on the list: they die, without a message (measured for a
  scout).
- Artillery is damaged the first time (`@ARTILLERY`) and destroyed the
  second (`@ARTILLERY2`). The damaged unit stays on its square.
- A beaten native unit dies; see [natives](natives.md).
- A colonist, wagon train or treasure is captured (natives take no one;
  anyone else who cannot fight dies): it changes nation,
  moves onto the winner's square and waits for orders (`@COLONISTCAPTURE`,
  `@COLONISTCAPTURE2` for a veteran, `@WAGONCAPTURE`, `@LOOTCAPTURE`).
- The attacker's turn is over, win or lose.

The map stays on the fight while the message is up; then the next unit
gets its turn.

## Naval combat

Measured the same way, with a Dutch frigate (and a Dutch merchantman as
the attacked side) next to English and French ships, and partly read from
the original with a debugger.

- A ship with an attack value attacks a foreign ship by sailing onto it.
  Ships only fight ships; a ship cannot attack a colony.
- The strengths and the roll are those of a land fight.
- After the roll, whatever it gave, a defender with less attack than its
  attacker may get away: a second roll of 1 to `f(attacker) + f(defender)`
  at or below `f(defender)` lets it evade (`@EVASIVE`). `f` is the ship's
  moves in thirds plus 3, doubled for a privateer, plus 3 for a galleon,
  minus 4 for each hold in use, and at least 1.
- The losing ship is sunk or damaged: a roll of 1 to the winner's guns
  plus the loser's hull at or below the hull means damaged. A nation's last
  ship of a kind is always damaged, and so is a ship beaten by one without
  guns. A sunk ship (`@SHIPSUNK`) takes the units aboard down with it.
- The attacker can lose too: a frigate attacking a merchantman (392 to
  48 with its bonuses) lost about one fight in nine.
- A damaged ship (bit `0x80` of byte 4) loses its cargo and the units
  aboard and goes at once to the nearest of its nation's colonies with a
  drydock, or else to the docks in Europe (`@SHIPDAMAGE`). It cannot move
  or sail until it is repaired.
- Its repair counter (byte 22) starts at the loser's combat minus the
  winner's (doubled if the winner is not a ship), at least 4 for a frigate
  and 8 for a man-o-war. Each turn it goes up 1, or 2 at a colony on the
  map, and the ship is repaired once it reaches its combat value
  (`@REFIT`). Measured: a frigate beaten by a merchantman started at 10 and
  was repaired after three turns at Jamestown.
- These messages come from the naval advisor.

Not measured yet: the exact rules for which ships may be sunk in some rarer
cases, how the nearest drydock is counted, and repairs in Europe (taken as
1 a turn).

## The Combat Analysis box

214 pixels wide and 26 + 20 per row high, centred on the screen, in the
tiny font: the title in green one pixel right of centre at row 9, the
attacker from x 5 and the defender from x 107. Each column starts with the
unit (one pixel higher than on a map square) and its name, then a row per
bonus; names and labels are green, values gold and right-aligned 80 pixels
in. Any key closes it and the fight follows.
