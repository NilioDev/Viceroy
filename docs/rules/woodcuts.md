# Woodcuts

Read from the original's code, and the screen checked pixel for pixel
against the original.

The game has thirteen pictures in the style of old woodcuts
(`WDCUT01.SS` to `WDCUT13.SS`). Each is shown the first time its event
happens to the human player, once a game: the savegame keeps one bit for
each, bit n-1 of header bytes 154 and 155 for woodcut n. Nothing turns
them off.

## The screen

The screen is black. The wooden frame (`WOODFRAM.SS`) and the picture
stand on the points their sheets give, those being the middle of their
bottom rows. Both are drawn in the picture's colours: the frame's own are
not used.

Below the picture a brass plate carries the title, line n of `@WOODCUT`
in `WOODCUT.TXT`. The plate is built from `NAMEPLAT.SS`: a left end 18
wide, as many middle pieces of 16 as the title needs (its width in
`FONT-NP.FF`, divided by 16 and rounded up), and a right end, centred on
column 160 at row 162. The title is centred on column 160 at row 165,
its pixel values 1, 2 and 3 in colours 5Ch, 5Eh and 5Dh.

The screen stays until a key is pressed or the mouse is clicked.

## When each is shown

| n | Title | When | Music |
| --- | --- | --- | --- |
| 1 | Discovery of the New World | before the player names the new land (`LANDHO`) | the mood 2 at once, only with Event Music on and Background Music off |
| 2 | Building a Colony | the player founds a colony (after sound 54h, played for every colony), before its screen opens | event mood 2 |
| 3 | Meeting the Natives | the player meets a tribe other than the Inca and the Aztecs, before `INDIANWELCOME` | tune 33h |
| 4 | The Aztec Empire | the player meets the Aztecs | tune 35h |
| 5 | The Inca Nation | the player meets the Inca | tune 36h |
| 6 | Discovery of the Pacific Ocean | a unit of the player's comes onto or next to a square of the Pacific | tune 39h |
| 7 | Entering Indian Village | a unit of the player's enters a village, before the greeting (after a ship is sent away) | |
| 8 | The Fountain of Youth | a rumour is the Fountain of Youth, after its tune and before the news | |
| 9 | Cargo from the New World | at the end of the turn the first ship of the player's to reach the port of Europe not on a trade route has cargo | the mood 2 at once |
| 10 | Meeting Fellow Europeans | the player meets another nation of Europe for the first time, before its leader speaks | |
| 11 | Colony Burning | braves burn a colony of the player's, after the fire and the natives' tune, before `INDIANBURNCOLONY` | |
| 13 | Indian Raid | braves attack the player's men, after their war cry | |

The tribe's or the event's own music, started just before, plays on
under the picture. With the woodcut of the raid already shown, a fight in
which braves attack the player's men opens with the war cry alone, unless
Combat Analysis is on (its usual sound then follows, see
[sound](sound.md)).

The original also shows woodcut 11 when another nation burns a colony of
the player's (`BURNED`), which Viceroy does not have yet. Woodcut 12
(Colony Destroyed) is never called for.

## The Pacific

A new game marks the Pacific with bit 0x20 of the mask layer: in every
row but the arctic ones, the water from the second column eastwards up
to the first land, short of the last 16 columns. On the America map this
gives exactly the squares the original's own games mark.
