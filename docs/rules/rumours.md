# Lost city rumours

Read from the original's code. A rumour lies on a square as described in
[map drawing](map-drawing.md); it is gone once a unit has come onto the
square (the unit writes its nation into the path layer; a tribe's unit
does not). A European unit (not a ship) that comes onto a rumour explores
it after its step and before it looks around for people to meet. Only the
player is told; a computer player's units explore the same way.

The original reseeds its random numbers from the clock first, so the same
step can end differently each time.

## The roll

The unit counts as **1** for a scout, **2** for a seasoned scout, and **0**
otherwise. A scout of a nation with Hernando de Soto counts one more, and
every bad result is rolled again.

The original also counts the rumours explored since the game was started
(not saved: loading a game starts it again at 0) and the cities of gold
found. Each exploration adds one to the first count. Then:

1. The outcome is a roll of 1 to 9, but at least 1, 2 and 3 on the first,
   second and later rolls. A quality is rolled at the same time: 1 to 100
   plus 10 for each point of the unit's count.
2. **5 (vanish)** and **8 (shrines)**: a roll of 0 to the count that is not
   0 makes it 6 (nothing), or another roll with de Soto.
3. **1 (Fountain of Youth)** only on grassland, savannah, marsh or swamp
   (or their forests), and from the fourth rumour of the game on. Otherwise
   a quality of 10 or less makes it 5 and more makes it 6 (not with de Soto,
   who keeps the Fountain). After independence (bit 0 of header byte 18)
   it is always 2.
4. **2 (Seven Cities of Cibola)** only on hills, mountains or desert (with
   de Soto also on a roll of 0 in 0 to 2), and only seven times in a game.
   The treasure is 1 to 20 plus 10 × (the count + 2), in hundreds; a
   treasure train holding it appears on the square. Otherwise the quality
   decides: 10 or less is 5, below 25 is 8, else 6.
5. **8 (holy shrines)**: rolled again with de Soto. The original takes
   the nearest settlement's *number* where it means its distance, so
   only next to the first three settlements of the save (by number) are
   there shrines, and only of a tribe the nation has met; else 6. The tribe
   grows tenser by 1 to 6 plus 5 × (the difficulty less the count + 1).
6. **3 (ruins)**: three rolls of 1 to 8, times 10, in gold; for a scout,
   times (the count + 2) / 2.
7. **7 (a small tribe's gift)**: four rolls of 1 to 10, times 2, in gold.
8. **5 (vanish)**: a nation of 4 people or fewer (see the natives' count of
   a nation's people) with 2 colonies or fewer finds nothing instead, and
   so does a pioneer of a nation of 8 or fewer on a roll of 1 in 2. The first
   time a nation would vanish (bit `0x40` of the player's byte 48) it finds
   burial mounds (4) instead. With de Soto it is 6.
9. **6 (nothing)** with de Soto is rolled again.

## What comes of it

The message is `LOSTCITY` and the outcome's number (NUMBER0 the gold,
NUMBER1 the treasure).

- **1**: eight free recruits from the docks, one at a time
  (`LOSTCITY0`, "Which of the following individuals shall we
  recruit?"): the player picks one of the three; a computer player always
  takes the second. Each place is filled again as after a recruit.
- **2**: the treasure train (above).
- **3, 7**: the gold goes to the nation's treasury.
- **4 (burial mounds)**: "Let us search for treasure!" or "Stay clear of
  those!"; a computer player searches. The mounds belong to the nearest
  settlement's tribe on a roll of 1 to (its distance + 5) × 2 to the power
  of the count of 3 or less, if the nation has met it. Then by the quality:
  below 25 they are empty (`BURIAL1`); below 50, or below 65 when no tribe
  is angered, trinkets worth three rolls of 1 to 8 times 10 (`BURIAL2`);
  else a treasure of (1 to 8 + 2 × (the count + 5)) × 2 hundreds on a
  treasure train (`BURIAL3`). An angered tribe says so (`SCREWED`) and
  grows 100 tenser.
- **5**: the unit is gone.
- **8**: the tribe's tension rises (above).
- **9 (survivors)**: a free colonist of the nation joins on the square.
