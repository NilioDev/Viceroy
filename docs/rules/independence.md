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
- The music: mood now 3. Then the leader signs the declaration (see
  below).
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

## The signing

`DECOIND.PIK` is shown and the leader's name (the player's name) is
written on it with a pen, each word starting with a capital. The pen
starts at x 126, y 148 (the top left of a letter's pictures). For each
character in turn:

- a capital is written with the 10 pictures 1 to 10 of `DEC-UPPx.SS` (x
  the letter), and the line climbs 3 pixels;
- a small letter with the 7 pictures 1 to 7 of `DEC-LOWx.SS`, the line
  climbing 2;
- a space or a punctuation mark writes nothing: the pen moves 3 on and
  the line climbs 1;
- anything else (a digit) gets the flourish, the 10 pictures of
  `DEC-SQIG.SS`, and the name ends there.

After a letter the pen moves on by the width of its pictures. Once it has
reached x 220 the next character is not written: the flourish is, and
the name ends. The pictures of a letter are drawn over each other, each
staying at least 5 ticks of the game's clock (the timer runs at
1,193,182 / 1,960 Hz, so about 8 ms); a key finishes the letter at once.
When the name is written the map comes back without waiting.

## During the war

The rebels are no longer counted: each turn, after the colonies, the
player's side and the Crown's take their turns of the war instead.

**Names.** Where the original names a nation in its news it calls the
player's side "Rebel" and the Crown's "Tory" (`LABELS.TXT`), or "Rebels"
and "Tories" where it names the people ("Tories march into ..."). Six
places keep the nations' own names (not looked at which yet), and the
people's form is used so far only for the capture of a colony.

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
`KINGMOBILIZE` news never comes. Then, while the Crown has land units at
home (regulars, or any dragoons or artillery), it lands some; otherwise
the Tories may rise.

## The Crown's landings

The royal force at home is in header words 106 to 112 (regulars,
dragoons, men-of-war, artillery).

- With no man-of-war at home, it gets one when it has none out on the map
  either (its census count of men-of-war), and nothing lands this turn.
- When the whole force is four or fewer, or only men-of-war, this is its
  last chance: if nothing lands this turn, the whole force at home is
  struck off. With only men-of-war left that happens at once.
- The candidates are the rebels' first ten coastal colonies, each with a
  key of people × (Tories in % + 25) − 75 × its armed men (non-ships with
  a combat above 1), at least the Tories in %, sorted from the smallest
  as the original sorts.
- What a colony needs: (muskets + 50) / 100 + 1, plus for every non-ship
  standing there its defence in eighths (combat × 8, half again for a
  veteran soldier or dragoon, damaged artillery 2 less) divided by 16;
  twice that behind a fortress, half again behind a fort; at least 1.
  (The original also means to take one off for every armed royal unit
  next to the colony, but it checks the wrong unit's nation, so it never
  does.)
- From the highest key down it takes the first colony the regulars, plus
  up to need / 8 (at least 1) dragoons and as many guns, can match; when
  there are no more dragoons and guns than regulars, one of each. If none
  can, the same again with the need cut to a shipload (the man-of-war's
  holds, 6), then simply the first.
- The ship's square: the open sea (the ocean's region) next to the colony
  with the most free land of the colony's own next to it; a square with a
  rebel man-of-war on it counts as one. Without one nothing lands.
- Everyone on that square who is not the Crown's is lost (`SEIZURESEA`
  for the rebels' ships). A man-of-war leaves home and appears there, with
  `INVASION` (mood now 3).
- The men: at least 3, at most a shipload, onto the land squares next to
  both the ship and the colony, in the colony's own land region and
  without a colony. First onto the square where the fewest men stand
  (everyone there is lost, `SEIZURELAND` for the rebels' men); then, round
  after round, onto the squares the Crown already holds: regulars, with
  up to need / 8 (at most 2 while there are two regulars or more, 1 when
  dragoons and guns are no more than the regulars) cavalry and as many
  guns among them. A kind that is used up is skipped.
- Every unit landed has used its moves, and the rebels see it.

## The Tory uprising

When the Crown has no land units at home, one turn in d + 2 (d the
difficulty) passes quietly; otherwise the Tories of one colony rise,
never twice in the same colony (bit 01h of colony byte 28 marks it). The
colony is the one with most Tories: (100 − Sons of Liberty) × 2 × people
/ 100 + d + 1, less the combat of every unit standing in it. A colony with
royal units next to it, or with no free land next to it, does not count.
That many Tory soldiers of the Crown are put round the colony, on the free
land and the squares the Crown already holds, round after round. Each has
a chance of d + 1 in d + 2 to be a veteran (every odd one) and to be
mounted as dragoons (every third). Then `TORYUPRISING`. If none fits, the
colony may rise later.

## The tribes

At the start of each tribe's turn during the war, a tribe that has not
yet done so may take the Crown's side (bit 20h of its flags, byte 3). It
is minded to when its alarm towards the player is 25 or more and a roll
of 1 to 400 comes at or below it, or when the player has burned one of
its villages (bit 40h). Then one turn in (5 − difficulty) × 2 + 1 it
does: `INDIANGRUDGE` ("... nation holds War Council!"), its alarm towards
the player rises by 100 and towards the Crown falls by 100, it throws out
the player's missions (`INDIANBURN`), and the Tories arm it: its muskets
(byte 7) become 4 × the larger of its village count and its muskets, its
breeding (byte 8) the larger of the village count and itself, and its
horses (word 10) 25 × that.

## Not yet done

- How the Crown's units move once landed (for now they move as any
  computer nation's). How they fight is in [combat.md](combat.md), the end
  of the war in [the-end.md](the-end.md).
- The Foreign Affairs report's `FOREIGNNOTAVAIL`, with the reports.
- Where the boxes of the war stand on the screen is not measured; they
  stand where the battle news does.
- The units on a square are taken in the order of the save; the original
  follows its own chain of the square's units.
