# FM sound drivers (`ASOUND.COL`, `PSOUND.COL`)

The original picks one sound driver at start-up from `CONFIG.COL`.
`PSOUND.COL` is for OPL3 cards (Sound Blaster Pro and 16, Pro Audio
Spectrum); `ASOUND.COL` is for AdLib (OPL2). Each driver carries its own
version of every tune and sound effect. Viceroy reads that data and plays it
with its own player (`src/core/src/sound`). The driver's code is never run.
So far only `PSOUND.COL` is read.

## File

The file is a DOS executable (`MZ`). After its header comes the driver
image. At image offset 0x10 is a name ("Coloniza…"). At 0x2A is the
paragraph where the data segment starts. At 0x30 is the number of entry
points, followed by the entry points themselves. The second entry takes the
game's commands:

| Command | Meaning |
| --- | --- |
| 0–8 | Control: reset, stop (fade out) all or some voices, "is music playing?" |
| 0x20–0x3F | Tunes |
| 0x40–0x5D | Sound effects |
| 0x8020–0x8026 | Short pieces built like the tunes: Viceroy plays 8020h to 8025h |

Each tune command fades out what is playing. Then it starts some of the six
music voices, each on its own stream in the data segment. It can also name
one voice as the drum voice. Viceroy finds each command's streams by reading
that small piece of start-up code.

One tune (3Ch, the city of gold's) fades nothing out. Its first three
voices each begin with a rest whose length the command sets before starting
the voice: 14, 28 or 42 ticks plus a random 0 to 7 (the driver's own random
numbers). The fourth starts at once on the stream the three run into, so
the piece comes in as a round that differs every time.

Tune 34h is not played by Viceroy yet: it sets the beat clock (48 ticks)
itself and starts eight voices, and command 8026h joins it on the beat.

## Voices and the chip

There are nine voices:

- **Voices 0–2** play on the chip's first register set and **voices 3–5** on
  the second. Each uses channels 0–2 as four-operator channels.
- **Voices 6–8** are for effects. They are two-operator voices on channels
  6–8 of both register sets, so they can be panned by loudness.
- **The pan setting** of a music voice picks the left, the right or both
  outputs.

## Ticks

The game calls the driver's service routine about 60.88 times a second.
Every call:

1. advances the random number generator;
2. updates each voice in turn;
3. moves fading voices on;
4. steps noise instruments.

All lengths in a stream are counted in these ticks.

## Instruments

Each instrument is 64 bytes at data offset 0x31E + 64·n. It starts with four
operator records of 14 bytes each:

| Byte | Meaning |
| --- | --- |
| 0, 1, 2, 3 | Attack, decay, sustain, release |
| 4 | Sustained envelope |
| 5 | Key scaling of the envelope |
| 6 | Level (higher is louder) |
| 7 | Key scaling of the level |
| 8 | Wave form |
| 9 | Frequency multiplier |
| 10 | Feedback (first record only) |
| 11, 12 | Tremolo, vibrato |
| 13 | Connection (first record only) |

The connection picks which operators are carriers. Only carriers follow the
voice's volume. Byte 0x38 marks a noise instrument: it is keyed on later,
with a frequency that changes every tick, using the words at 0x3A–0x3F. Note
frequencies come from twelve frequency numbers at data offset 0x176 (C to B).
The octave is the note divided by 12.

## Streams

A stream is a list of notes and commands. A byte below 0xBC is a note: the
note byte, then its length in ticks. A note of 0 is a rest. A length of 0
ends the voice. The key is released a set number of ticks before the end of
the note, or after a fixed hold time.

Commands, with their argument bytes:

| Byte | Arguments | Meaning |
| --- | --- | --- |
| F8 | n | Instrument n |
| F4 | v | Volume v/2 |
| F1 | a | Attenuation a/2 − 50 |
| F0 | p | Pan p (0x40 is the middle) |
| EE | n | Transpose by n notes |
| F2 | d | Detune: add d to the frequency number |
| F5 | b | Bend: add b to the frequency number every tick |
| F3 | t, s | Add s to the attenuation every t ticks |
| EF | t, s | Add s to the pan every t ticks |
| F6 | h | Release the key h ticks into each note |
| F7 | g | Release the key g ticks before each note ends |
| FA | address | Call (one level deep) |
| F9 | — | Return |
| FB | address | Jump |
| FC | address | New start for the voice |
| FD | — | Back to the start, with effects reset |
| FF | n | Repeat from the inner mark n times; 0 sets the mark |
| FE | n | Repeat from the outer mark n times; 0 sets the mark |
| E9 | v, n | Variable v = n (256 variables shared by all voices) |
| E8 | v, w | Variable v = variable w |
| E6 / E5 | v | Increase / decrease variable v |
| E2 | v, n | Variable v −= n |
| C9–CC | v, n, address | Call if n <, >, ≠, = variable v |
| D1–D4 | v, n, address | Jump if n <, >, ≠, = variable v |
| CD–D0 | v, w, address | Jump if variable w <, >, ≠, = variable v |
| E7 | v, o | Write variable v into the stream, o bytes on |
| EA | v, n, list…, o | Write the v-th byte of the list into the stream |
| EB | lo, hi, o | Write a random number from lo to hi into the stream |
| EC | n, list…, o | Write a random byte of the list into the stream |
| ED | n | Skip n further bytes |
| C3 | s | Play sample s (cards with digital sound) |
| C4 | address | Run a routine of the driver |
| others | | Skipped |

Writing into the stream is how the tunes vary each time they are played. It
changes the notes, the repeat counts and the jump targets ahead of the
player. The one routine a tune runs (C4) swaps two bytes in four places at
random, so two parts of the piece play in either order.

The random number generator adds 0x9248 to the last value and rotates it
right by three bits. Its first value is the word at data offset 0x56.

## Fading out

Stopping a voice marks it as fading. From then on its attenuation goes down
by two every four ticks. When attenuation plus volume falls to 8 or below, or
rises above 0x78, the voice jumps to a short stream at data offset 0x1CC. That
stream rests and then ends the voice.

## Digitised sound

With a card that plays samples, the drum voice plays recorded drums from
`COLDIG.BIN` instead of FM notes. Louder notes pick one of five stronger hits,
samples 0–4. Sample commands play other recordings from that file.
`COLDIG.BIN` is raw unsigned 8-bit sound (128 is silence) with no header.

The driver lists the recordings in a table of offset and length pairs, which
the routine after the text "FINDWV" walks; there are 35, one after another,
filling the file. The five drum hits play at 19050 samples a second, the
rest at 11025 (the rates the driver gives the card).

Recordings wait in a queue of 32 and play one after another. Queuing fails
when the queue is full.

## Sound effects

Each effect command (40h–5Dh) first tries a recording: when the card plays
samples and no piece is using digitised drums, it queues that recording and
is done. Otherwise, or when the queue is full, it starts one or more streams
on a free effect voice: voice 7, 8 or 6 if one is silent, else 6, 7 or 8 if
one is fading. With none free the stream is not played. Some effects have
only the recording, some only streams.

In DOSBox (Sound Blaster settings as the game sets them) the chip plays as
loud as Viceroy's emulated chip, and a step of an 8-bit recording comes out
as 42 on the 16-bit scale.

## Pieces that follow one another

One tune command (25h, "Northfire") does not fade out what plays. It keeps
a clock of its own that counts down 192 ticks and starts over. When the
command comes and the clock is at zero or no music voice plays, the chip is
reset and the first block of streams starts at once. When one of two named
streams of another piece is playing, a second block is queued instead;
otherwise the first block is queued, unless it is already playing. A queued
block starts when the clock next runs out, so it lands on the beat of what
is playing. The clock steps after the voices and before the fading voices.
