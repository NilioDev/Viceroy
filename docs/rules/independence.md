# The war of independence

Read from the original with a debugger and a disassembler: we looked at
what it does and wrote it down here in our own words. Header words are
counted in bytes from the start of the savegame.

## Declaring

"DECLARE INDEPENDENCE" in the Game menu first checks, in this order:

1. The war has already begun: `ALREADYREVOLUTION`.
2. Fewer than half of the player's colonists are rebels (header word 96,
   counted every turn): `TOOTORY` with the percentage.
3. A game of several human players (header byte 17, bit 80h): `MULTIREV`.
   "Declare independence" makes it a game for one player: the bit goes,
   and the human nation (header word 40) becomes the one in header word 36.
4. `DECLARE`, naming the player's mother country. "Yes! Give me liberty or
   give me death!" declares; the player's turn ends.

## The declaration

- The year of the declaration goes into header bytes 55 (year / 100) and
  56 (year % 100), where the King's weddings and wars were kept. The
  player's bells towards the next father are cleared.
- If the War of the Spanish Succession has not happened, it happens now
  (see [diplomacy.md](diplomacy.md)). The Crown's side of the war takes
  the slot of the nation that left then (header word 98).
- The music: mood now 3. Then the signing of the declaration (the screen,
  `DECOIND.PIK` with letters from `DEC-UPP*.SS` and `DEC-LOW*.SS`, is not
  built yet).
- The power that will help (header word 100) and the next one (header
  word 102): the nations are sorted from the weakest by 3 × ships + 2 ×
  colonies + people, the same sort as for the succession, and the first
  two that are neither the player nor the Crown's side are taken. Header
  word 100 is only set when it is still −1 (as in every new game).
- The power's force, in header words 114 to 120 (regulars, dragoons,
  men-of-war, artillery), from its census and the difficulty d. With e =
  (4 − d) / 2 rounded down:
  - regulars: (people / 10 − d + 9) / 2;
  - dragoons: ((attack abroad + 1) / 16 + e + 2) / 2;
  - artillery: (e + (attack + 1) / 32 + 4) / 2;
  - men-of-war: (e + privateers + frigates + 4) / 2;
  - then at most two guns and two dragoons per man-of-war, and regulars at
    most 6 × men-of-war less the guns and dragoons.
  The halves are rounded towards zero.
- Every other nation leaves the New World: its relations with the player
  and with the Crown lose bits 01h, 02h and 08h and gain met and peace,
  all its men are removed and it is withdrawn (control 2). Its colonies
  stay; they cannot be attacked during the war (`NOWARSDURINGREV`).
- The player's men in Europe and at sea between are lost. Each ship among
  them is named (`SEIZURE`); the others go without a word.
- Header byte 18 gets bit 0: the war has begun.
- The Crown's side: on every square its seen bit is taken away (the
  original masks with the nation's number, not its bit, so bit 0 or 1 of
  the layer goes instead) and the squares the player has explored become
  explored for it too. It is played by the computer (control 1), and its
  relation with the player gains met and bit 02h and loses peace. Its
  flag is white (colour 0Fh; the four nations are 0Ch, 09h, 0Eh, 0Dh).
- `INDEPENDENCE`, with the player's leader as the general. All the
  player's units have used their moves. A word of both nation records
  (offset 16h) is cleared, and bit 04h of the Crown side's flags.

## During the war

The rebels are no longer counted: each turn, after the colonies, the
player's side and the Crown's take their turns of the war instead.

**The Continental Army.** On the player's first turn of the war (bit 08h
of the nation's flags marks it done), in every colony where the Sons of
Liberty are at least half, max(1, min((Sons − 50) × people / 50, people /
2)) of the veteran soldiers and veteran dragoons standing there become
Continental Army and Continental Cavalry. The music: mood now 3. The
colony tells it: `MOBILIZE` for one ("Our Veteran Soldiers have been
promoted"), `MOBILIZE2` with the number for more.

**The power joins.** After the declaration the bells no longer elect
fathers. The first time the bells run past one turn's worth, the player
hears `AMBUSHHINT` and `CONSIDER`: the power waits for 1500 × difficulty
+ 2000 bells (header byte 18 gets bit 2). Once they are rung the power
declares war on the Crown (`INTERVENTION`, mood now 3; its envoy goes to
the player's biggest colony by the sea) and header byte 18 gets bit 1;
the bells start again from 0.

**The power's force.** From then on, each turn while it has a man-of-war
left, one comes (and the mercenaries are not offered): to one of the
player's first ten coastal colonies, picked at random by their people. It
takes the open sea next to the colony (the ocean's region, no royal
man-of-war on it, nobody else's ships) with the most free land of the
colony's own next to it. It brings at most two dragoons and two guns, the
rest regulars up to six, as Continental Army, Continental Cavalry and
artillery, veterans, who go into the colony (`INTERVENE`: event mood 3
and tune 3Fh). The man-of-war stays, the player's. Without such sea the
turn's ship does not come.

**Mercenaries.** Otherwise, one turn in three, the king of the nation in
header word 102 offers mercenaries (`MERCENARIES`): 2 to 2 + e regulars,
then a coin: one cavalry or one artillery. Each costs ((d + 3) × 2 + 0 to
6) × 100, cavalry and guns twice that, and the offer only comes when the
player has the gold. They come the way the power's force does, all of
them, as Continental Army, Continental Cavalry or artillery (`MERCS`),
and their ship sails home. (Before the war the same landing brings the
mercenaries of [king.md](king.md).)

**The Crown.** The original calls for the Crown's army to grow at the
start of its turn, but the routine returns at once during the war, so its
`KINGMOBILIZE` news never comes.

## Not yet done

- The signing screen.
- The Crown's campaign: the landing (`INVASION`), the Tory uprising
  (`TORYUPRISING`), the captures (`SEIZURESEA`, `SEIZURELAND`), how its
  units fight and move, and the end of the war (`KINGWIN`, `KINGLOSE`).
- Veterans that harden to Continental Army in battle (`CONTINENTAL`).
- The Foreign Affairs report's `FOREIGNNOTAVAIL`, with the reports.
- Where the boxes of the war stand on the screen is not measured; they
  stand where the battle news does.
- The units on a square are taken in the order of the save; the original
  follows its own chain of the square's units.
