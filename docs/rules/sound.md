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
