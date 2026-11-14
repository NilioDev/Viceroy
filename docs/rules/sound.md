# Music and sound options

What the original plays when. The tunes themselves come from the sound
driver (see [the driver format](../formats/sound-driver.md)).

## Sound Options

Three switches, kept in byte 22 of the save game's header:

| Bit | Option | Effect |
| --- | --- | --- |
| 0x02 | Background Music | A new tune starts whenever the last one ends. |
| 0x04 | Event Music | Tunes may play at all (every tune command needs it). |
| 0x08 | Sound Effects | Effects may play. |

Control commands (below 0x10) are always sent. A command with bit 0x20
(the tunes) needs Event Music, one with bit 0x40 (the effects) needs Sound
Effects. With Event Music on and Background Music off, a tune still plays
when an event or Pick Music asks for one, but nothing follows it.

## The next tune

Every frame the game asks the driver whether anything is still playing.
When nothing is, and Background Music is on (or an event asked for a tune),
it picks the next one:

- A tune picked in Pick Music, or asked for by an event, comes first.
- Otherwise it draws from the tunes of the current mood (below). With no
  mood set, before independence it draws one of the twelve Pick Music tunes,
  and one time in nine one of tunes 13 to 23 instead. After independence it
  draws one of tunes 13 to 18, and one time in five one of the twelve.
- It draws again, up to 64 times, while the draw is the tune just played.

The draw uses the game's own random numbers: a number from lo to hi is
lo + ((hi − lo + 1) × r ÷ 32768), with r the next 15-bit random value.

## Tunes

| Tune | Driver command |
| --- | --- |
| 1–12 | The Pick Music list, in its order: 20, 21, 22, 23, 3A, 3B, 38, 24, 25, 26, 27, 39 (hex) |
| 13–22 | 28 to 31 (hex), the independence and military tunes |
| 23, 24, 25, 26 | 33, 32, 35, 36 (hex), the native tunes |

## Moods

Game events set a mood, which picks the group the next tune comes from:

| Mood | Tunes |
| --- | --- |
| 1 | 1–7 |
| 2 | 8–12 |
| 3 | 13–18 |
| 4 | 19–22 |
| 5 | 23, unless it is playing |
| 6 | 25, unless it is playing |
| 7 | 26, unless it is playing |

After a pick, the mood becomes the one set for the tune after next, then
none. With no mood set, the tune drawn sets it. An event can set the mood
of the next tune, of the tune after it, or change the mood at once: then
the tune playing fades out if its mood differs. Pick Music fades the tune
playing out as well.

## Music at events

The game asks for music when it tells the player of an event, always the
player's own (computer players' events never change the music). There
are four kinds of call:

- **next mood**: the mood of the next tune;
- **mood now**: a new mood, and the tune playing fades if its mood differs;
- **event mood**: mood now with Event Music on and Background Music off,
  next mood otherwise;
- **tune**: a tune of its own (a driver command), which fades the one playing.

| Event | Music |
| --- | --- |
| A colony's food runs out (`FOOD1`, `FOOD2`) | event mood 1 |
| A colonist starves (`STARVE1`, `STARVE2`), the colony vanishes (`VANISH`) | mood now 1 |
| Food is running low (`FOODLOW`) | next mood 1 |
| A cargo is ready (`CARGOREADY0` to `2`) | next mood 2 |
| A colony finishes something (`BUILT`) | event mood 3 for an armory, magazine, printing press or newspaper, event mood 2 otherwise |
| A tribe is met (`INDIANWELCOME`) | the tribe's mood now; in the first 20 turns with Background Music on, a tribe other than the Inca and the Aztecs as next mood |
| A tribe visits a colony | one visit in nine: the tribe's mood next |
| A tribe demands goods (`INDIANCITY`, `INDIANWAGONS`) | the tribe's mood now |
| A ship sinks (`SHIPSUNK`) | event mood 1 for its owner, event mood 4 for the winner |
| Braves are beaten off (`INDIANLOSE`) | mood now 4 |
| Braves win an ambush or at a colony (`INDIANWIN0` to `2`, `INDIANWINCOLONY`, `INDIANBURNCOLONY`) | tune 32h ("Natives") |
| A raid on a colony | next mood 2 when it comes to nothing, tune 32h otherwise |
| Converts flock to the mission after an attack (`INDIANSLAVES`) | mood now 4 |
| A burned village leaves treasure (`LOOT`) | mood now 2 |
| Fountain of Youth, city of gold, burial mounds (`LOSTCITY1`, `2`, `4`) | tunes 37h, 3Ch and 33h |
| An expedition vanishes (`LOSTCITY5`) | next mood 1 (and mood now without Background Music) |
| A rumour with gold | next mood 2 (and mood now without Background Music) |
| Treasure in burial mounds (`BURIAL3`), with no tribe angered | tune 24h |
| The mounds were a tribe's burial ground (`SCREWED`) | tune 32h |
| The War of the Spanish Succession ends (`SUCCESSION`) | mood now 3 |
| The leader declares war (`WARMEEK`, `WARMANLY`, `PROVOKE` after a threat), or takes the player's money for a war (`MERCENARY`) | mood now 4 |

The tribe's mood is 7 for the Inca, 6 for the Aztecs and 5 for the others.

Not yet in Viceroy, so not yet with music: the King's wars,
independence and the War of Independence, the
natives' talks and threats, colonies taken or burned by other nations, and
the end of the game.

## Sound effects at events

The game sends an effect (driver commands 40h–5Dh, see [the driver
format](../formats/sound-driver.md)) or one of the short pieces from 8020h
at once, when the player's unit, colony or mission is in the event. An
effect needs Sound Effects; a piece from 8020h needs Event Music, like the
tunes.

A fight starts with:

- when braves attack the player's men, first a war cry: 3Bh plus the
  brave's unit type (4Eh to 51h);
- 42h when the attacker is a ship or artillery, or the defender artillery;
- else 4Ch when the attacker is mounted (dragoon, scout, cavalry);
- else 41h when either side has an attack above 1;
- else 40h.

When the attacker wins:

| What it beat | Effect |
| --- | --- |
| A unit, beaten by a ship or artillery | 43h |
| A unit in a colony or a village | 49h |
| A unit in the open | 40h |
| A colony's colonist, beaten by Europeans (the colony is taken) | 4Bh |
| A colony's colonist, beaten by braves (one dies, or the colony burns with its last) | 4Ah |
| A village's brave (the village loses a person) | 48h |
| A village's last brave (the village burns) | 4Ah |

After that, braves who win at a colony and stay there play 45h, and a ship
beaten by a ship plays 4Dh, whoever attacked.

| Event | Sound |
| --- | --- |
| A ship sinks (`SHIPSUNK`) | 57h |
| Braves burn a colony (`INDIANBURNCOLONY`) | 53h |
| A raid takes stores (`RAIDSTORES`) | 4Fh |
| A raid burns a building (`RAIDBURN`) | 53h |
| A raid hits a ship in the harbour (`RAIDSHIP`) | 4Bh, then 4Dh |
| A raid takes gold (`RAIDGOLD`) | 4Eh |
| A raid comes to nothing (`RAIDNOTHING`) | 5Bh |
| A mission is founded (`MISSION0` to `3`) | 8024h |
| A mission is won from another nation (`HERESY0`) | 8024h |
| A mission is lost to another nation (`HERESY1`) | 53h |
| The chief kills the scout (`CHIEFKILL`) | 55h |
| A unit in a colony is told to fortify (`UNITOPTIONS`) | 58h |
| The King raises the tax and no party is possible | 3Eh |
| A tea party (`TEAPARTY`) | 56h |
| A European leader comes to talk (`HELLO...`) | 8020h + the leader's nation |

Read from the original but not yet in Viceroy: 5Ch when a colonist or
soldier takes horses and 8024h when one is blessed as a missionary (in the
home port), 3Fh when a foreign power lands troops to help in the war of independence
(`INTERVENE`), 8020h near a
declaration of war, 21h or 25h with the final score, 39h before the
opening, 52h for a wagon train of the player's, 8024h when a colony
finishes some buildings, and a few calls that pass on a sound chosen
elsewhere. Still to be placed: 54h in three places (one of them as a
player's turn begins) and 58h in one more.
