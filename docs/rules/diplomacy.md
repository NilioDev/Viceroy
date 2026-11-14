# The European nations

How the European nations meet, talk, make peace and go to war. Measured in
the original with a debugger watching the contact after a move and the
meeting that follows, then checked by playing.

## Relations

Each nation keeps a byte for every other nation and tribe:

| Bit | Meaning |
| --- | --- |
| 20h | met |
| 40h | at peace (a treaty) |
| 10h | insulted: the leader talks only after 16 turns |
| 08h | grudge: a meeting that ended badly |
| 02h | war intent: this side has been paid to attack the other |
| 80h | a complaint about piracy is due |

"Met" and "peace" are always set and cleared on both sides together. A
second byte per nation is a timer: at peace, the computer leaves the
player alone for 2 × (6 − difficulty) turns after a talk (half that with
Benjamin Franklin in the player's Congress).

## Contact

After a unit moves on land, the original looks at the eight squares
around it (north first, then clockwise). A square belongs to the nation of
the first unit on it, or of its colony or settlement. Each other nation is
dealt with once per move.

- Two computer nations settle peace or war at once (see below).
- When the player is one of them, the computer's leader comes to talk. The
  player's side is always the one that listens, whoever moved.
- During the war of independence nobody talks.

The leader talks when the two have not met, when 16 turns have passed
since this leader last talked to the player (which also forgets an
insult), or when the talk is forced (a scout's visit). A leader who talks
remembers the turn. Each leader has a tune of its own.

## A scout at a foreign colony

A scout of the player's that moves into another European nation's colony
is asked what to do (`SCOUTCOLONY`, told by the frontiersman):

- **Meet With Mayor**: the leader comes to talk at once, however recently
  they talked. Not during the war of independence (`NOMAYORSDURINGREV`).
- **Infiltrate Colony**: the scout is caught when a roll from 1 to 36 is
  at most (6 + the colony's walls: 1 stockade, 2 fort, 3 fortress) × 2,
  halved for a seasoned scout, plus the difficulty minus 2 (so less on the
  two easiest levels). Caught, the scout is gone and the colony gets 100
  horses (`LOSTOURSCOUTS`). Otherwise the player looks round the colony
  screen, without a say in it.
- **Attack Colony**: as any attack.
- **Nothing**.

Except for an attack, the scout's turn is over. A computer scout always
attacks, so the text for the player catching a foreign scout
(`LOSTTHEIRSCOUTS`) never shows.

## What the leader weighs

Before talking, the leader looks at the map through the turn's census:

- **Threat.** The player's soldiers in a region where the leader has
  colonies and fewer men make the leader meek. Each such region adds the
  player's force divided by the leader's men (doubled, or four times on
  the easiest level).
- **Hostility and the amount.** The leader's own soldiers out in the field
  near the player's people make it bold, and the difference in force sets
  how much it will ask for. Soldiers standing next to the player's
  colonies count double, and make it bolder still when the colony has no
  defender.
- The strongest computer nation is aggressive from turn 80 on, once the
  player has more than three colonies. A nation that has been paid to
  attack the player is bold too, as long as it is strong enough.
- In the first 10 × (10 − difficulty) turns, or with Franklin, nobody is
  bold.
- The amount grows with the difficulty and is halved early in the game, a
  quarter on the first meeting, at most 20,000, rounded to 50, and halved
  again with Franklin. A player who cannot pay it all but has half of it
  (and at least 300) is asked for what they have.
- Troubles with tribes or other nations make the leader keener on peace;
  a tribe or nation the player is friends with may be named.

## The talk

1. **Greeting.** `HELLOFIRST` on a first meeting (`HELLOAHOY` when it
   starts at sea), later `HELLOMEEK` or `HELLOMANLY`; the United States
   use `HELLOUSA`. The leader is named by the level's title and name,
   with the great kings and deeds of their country.
2. **Friends of the player.** A nation the player has a treaty with, or a
   tribe the leader is at odds with, may be named (`APOSTATES`,
   `HEATHEN`). Agreeing breaks the treaty (or angers the tribe by 100)
   and calms the leader.
3. **Piracy.** After the player's privateers have struck, the leader may
   ask for them to go home (`PIRACY`); agreeing sends them to Europe.
4. **Sieges.** The leader asks the player to withdraw soldiers from its
   colonies (`SIEGES`). The original counts those soldiers in a way that
   never finds any, so in practice only a nation with fewer than four
   people in its colonies asks, and agreeing sends nobody home.
5. **Tribute.** A bold leader asks for the amount (`TRIBUTE`). Paying calms
   it; either way it asks for nothing more.
6. **Goods.** A bold leader whose unit stands next to a player's colony may
   ask for its most valuable load (`WANTSTUFF`), the most it has room for
   at home. The original then moves that amount of furs, whatever was
   asked for; we do the same.
7. **War or peace.** Still bold, the leader breaks a treaty (`PROVOKE`),
   declares war after a refused tribute (`WARMANLY`), or just warns
   (`RID`). Otherwise, not at peace, it offers a treaty (`WORTHY`): taking
   it makes peace and keeps the leader away for 16 turns. A meek leader
   may first offer gold for peace (`GIVECASH`, 200 for each point of
   threat over 2, as far as its treasury goes). Refused, it is war
   (`WARMEEK` / `WARMANLY`), and the music changes to the war mood.
8. **The menu.** At peace and calm the leader asks what the player wishes
   (`PEACEMEEK`, `OLDPEACEMEEK` and so on):
   - go in peace;
   - ask it to withdraw its men from the player's colonies: nothing to
     withdraw, done at once when it is meek, refused when it holds a
     grudge or the player cannot pay, or offered for (difficulty + 2) × its
     force × 25 (at least 100). Refusing to pay and pressing on may make it
     withdraw or go to war;
   - threaten it: a meek leader pays 100 per point of threat (`GIFTS`), a
     bold one goes to war (`PROVOKE`), otherwise it laughs (`THREATS`);
   - propose an alliance against a nation or tribe the player has met
     (`MILITARY`): it has not met them (`NOCONTACT`), is already at war
     with them (`ALREADYSMITE`), or asks a price that grows with the
     player's treasury and the enemy's strength (`SMITEINDIANS`,
     `SMITEEUROPE`, from 500 to 10,000, half with Franklin). Paying
     declares the war (`MERCENARY`); without the gold, `UNFORTUNATE`.

After a gift for peace the original keeps the wrong text name and shows no
menu; we do the same. A meeting that ends while the leader was still
bold leaves a grudge.

## Two computer nations

Two computer nations that meet look at each other only one turn in three
once they have met (and not while one of them has been paid to attack the
other):

- When neither wants war and they are not at peace, they sign a treaty
  ("The French and Spanish have signed a peace treaty.").
- When one wants war and they have not met, or were at peace, the treaty
  ends; the player is told only of a declared war ("... are now at
  war."), since the original's text for a broken treaty is missing.

A nation wants war from turn 40 on, never when the player is the strongest,
when both have fewer than eight people in their colonies, or with the
United States. It keeps out when it already fights a stronger player, or
when its colonies would not be safe, and it weighs its force against the
other's in the regions where both have men, against the number of wars it
already fights and its character.

## The King declares war

On a turn of the player's when the King leaves the tax alone, he may
declare war on a nation the player has a treaty with (`KINGNEWWAR`):

- never with Benjamin Franklin, never while the player is at war with
  another nation, and not before (difficulty + 2) × turn reaches 800;
- the chance is 1 + difficulty in (4 − treaties) × 20 + 1, so higher with
  more treaties and at the harder levels;
- the enemy is one of the nations at peace with the player, at random.

The Crown sends (difficulty + 1) × 100 gold and one veteran soldier to the
docks, more against an enemy with more force in the field: one more
soldier for every 8 points of difference, and 25 gold a point. At most
6 − difficulty soldiers and (5 − difficulty) × 500 gold. The treaty is
broken and the enemy feels insulted (it waits 16 turns before talking
again).

## The War of the Spanish Succession

The first time the player's rebels reach 50%, the war ends in Europe
(`SUCCESSION`, told by the man of the home port, with the music's mood
changing). The computer nations are ranked by 3 × ships + 2 × colonies +
people: the weakest gives up its New World to the next weakest.

- Its colonies change hands, and their liberty bells are lost.
- Its men on the map change sides; those in Europe or on the high seas
  are gone.
- What it had explored and its missions go to the winner.
- It leaves the game for good, and is left out of later talks and wars.

This does not happen in a game with several human players.
