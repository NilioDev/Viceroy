# Tutorial hints and the new land's name

Read from the original's code and checked on new games of the original.

## The option

Tutorial Hints is bit 0x80 of header byte 18 (Game Options). A new game
turns it on at Discoverer only. A new game also starts with header byte 22
at 0x0E, at every level.

Each hint is given once a game. The savegame keeps which hints were given,
one bit each:

| Hint | Byte | Bit | Adviser |
| --- | --- | --- | --- |
| `@TUTORIAL1` sailing | 22 | 0x10 | naval |
| `@TUTORIAL3` a site for the first colony | 22 | 0x40 | frontiersman |
| `@TUTORIAL4` jobs | 22 | 0x80 | woman of the buy box |
| `@TUTORIAL5` colonists on the docks | 23 | 0x01 | naval |
| `@TUTORIAL6` a cargo to ship home | 23 | 0x02 | naval |
| `@TUTORIAL7` a stockade | 23 | 0x04 | colonial |
| `@TUTORIAL8` natives teach | 23 | 0x08 | woman |
| `@TUTORIAL9` roads | 23 | 0x10 | frontiersman |
| `@TUTORIAL10` plowing | 23 | 0x20 | frontiersman |
| `@TUTORIAL11` ships | 23 | 0x40 | naval |
| `@TUTORIAL12` loading a ship in port | 23 | 0x80 | woman |
| `@TUTORIAL13` pioneers | 16 | 0x01 | frontiersman |
| `@TUTORIAL14` soldiers | 16 | 0x02 | colonial |
| `@TUTORIAL15` newcomers in a colony | 16 | 0x08 | woman |
| `@TUTORIAL16` food | 16 | 0x10 | none |
| `@TUTORIAL17` the Europe screen | 16 | 0x20 | none |
| `@TUTORIAL19` converts | 16 | 0x80 | religious |

`@TUTORIAL2` and `@TUTORIAL18` have no bit: see below.

## When a unit is up for orders

Each time one of the player's units waits for orders (with no orders, on
the map), the original looks for a hint about it, in this order, and gives
the first that is due:

1. `@TUTORIAL1` on turn 0 of a Discoverer game ("Our Caravel carrying a
   pioneer and a soldier ...").
2. `@TUTORIAL11` before turn 20, for a ship with no cargo and nobody aboard.
3. `@TUTORIAL13` before turn 20, for a pioneer on land while the nation
   has no colony.
4. `@TUTORIAL14` before turn 20, for a soldier on land.
5. `@TUTORIAL15` before turn 40, for a colonist standing in a colony.
6. `@TUTORIAL3` while the nation has no colony, for a pioneer on a good
   site: not on water, hills or mountains, not next to a colony, next to
   water that belongs to the open sea; of its nine squares at least five
   count (a square counts when its terrain gives food to a farmer, and the
   site once more when it lies on a land region of fewer than 7 squares);
   and a resource names the good: cotton, tobacco, sugar, ore (minerals or
   hills), furs (beaver or deer) or silver, the last such square in the
   order N, NE, E, SE, S, SW, W, NW, centre; grain (food) or timber
   (lumber) only when no other was found.
7. `@TUTORIAL8` for a free colonist or indentured servant once the nation
   has met a tribe.
8. `@TUTORIAL9` for a pioneer next to one of the nation's colonies (the
   nearest colony, one square away) on a square of forest, hills or
   mountains without a road.
9. `@TUTORIAL10` for such a pioneer on a square not plowed, when it is
   plains, prairie, grassland or savannah; or on a forest of one of those
   from turn 16 on, when the colony has none of those four around it and
   at least two forests.
10. `@TUTORIAL19` for a native convert.

## Elsewhere

- `@TUTORIAL4` and `@TUTORIAL12`, when a colony screen opens: what the
  first colonist makes and another good of its square (the good is named
  by the job's number, so a carpenter makes hammers); then, with a ship
  in the colony, how to load it.
- `@TUTORIAL16` when the colony screen shows the colony eating more food
  than it makes.
- `@TUTORIAL7` when a colonist is put to work in a colony of three or more
  without a stockade.
- `@TUTORIAL17` when the Europe screen opens.
- `@TUTORIAL5` after the news of a colonist brought to the docks by
  religious unrest (`@UNREST`).
- `@TUTORIAL6` after the first report of a cargo ready (see
  [production](production.md)).
- `@TUTORIAL18` when a purchase in Europe would cost more than the
  treasury holds. It is shown whatever the option says, and nothing is
  bought.

## Where the boxes stand

A hint with an adviser stands where that adviser's other news stands: the
naval adviser and the woman 4 columns left of the box and 71 rows above
it, the two centred on the screen; the colonial adviser at the right (box
at column 39). A box too tall for the adviser to stand over it within the
screen (the pioneers' hint) comes alone, in the middle. A hint without an
adviser is a plain box at the row its text's `@y` gives (`@TUTORIAL17` at
row 10), centred across; we take it that `@x` places it across the same
way (only `@TUTORIAL16` has one).

## Naming the new land

The first time one of the player's units comes onto a square with land
among the nine squares around it, player flag 0x80 is set and the naval
adviser asks for the name of the new land (`@LANDHO`, the box filled in
with the name it has, "New England"). The box is widened for the line to
type in and its text wrapped to the new width. With Tutorial Hints on,
`@TUTORIAL2` (how to go ashore) follows. The woodcut of the discovery
comes first (see [woodcuts](woodcuts.md)).
