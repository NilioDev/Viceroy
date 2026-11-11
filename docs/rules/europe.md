# Europe

What we know of the home port, measured on the original with savegames
that we changed one byte at a time.

## Where units are

A unit in Europe or on the high seas between has x = y = 228 + nation +
4 × stage (nations 0-3: English, French, Spanish, Dutch), so the Dutch use
231, 235, 239, 243 and 247.

| Stage | Shown in |
| --- | --- |
| 4, 3 | "Expected Soon": sailing to Europe |
| 2 | the port: ships in port, land units on the docks |
| 1, 0 | "Bound For New …": sailing to the New World |

We take it that ships move one stage a step towards the lower numbers. A
ship that reaches the port at 2 stops there; one that passes 0 arrives in
the New World. Land units on board have the stage of their ship, and the
ship is found by following the chain of units (bytes 24-27 of the unit
record): the passengers come before the ship. The original shows them
after the ship, walking the chain backwards from it.

In our tests, a ship in port at stage 2 with land units of the same chain
showed the land units on the docks, not in the ship.

## The market

Byte 76 + good of the nation record holds a price. The market buys at
that price less one and sells at that price plus the fifth number of the
good's `@CARGO` row, so food 1 shows as 0/8, sugar 7 as 6/8, silver 14 as
13/14. Prices are per unit; cargo moves 100 at a time.

## The screen

Everything below was compared pixel by pixel with the original.

- `EUROPE.PIK` is the whole picture, with the three boxes over the water,
  the open holds of the port and the blue market along the bottom.
- The top line is drawn as on the colony screen: "<home port>, <country>.
  <season>, <year>.  Tax: N% Gold: N$", with the home port from
  `@HOMEPORT` and the country from `@COUNTRY` in `NAMES.TXT`.
- Three buttons at x 281, y 89, 100 and 111, 37 × 9: colour 0x39 above and
  left, 0x30 below and right, the label from `@EUROLABEL` in `LABELS.TXT`
  in white with its first letter in yellow (0x0E).
- Box titles in colour 0x45 at y 120 ("Bound For" has the colony name
  below at y 127), centred as left + (width + 2 − text width) / 2 on boxes
  70 wide at x 1 and 72 and 80 wide at 143. The port box says "No Ships In
  Port", or "Loading:  <ship>" with a ship chosen.
- Units stand at y 146. In a box the first ship is centred on left + 10,
  the next unit 14 further, then every 17 pixels. Each unit has a black
  shadow two pixels to its left and its orders box: beside the mast of a
  ship (at x + width − 7), at the feet of a person (x + width − 2, y + 7).
  The letter is black for no orders, otherwise the dark half of the
  nation's colour (0x05 for the Dutch 0x0D).
- The chosen ship in port has a frame of colour 0x0A, 18 × 18, from
  (x − 3, 145).
- The holds sit at (147 + 12k, 165): the ship's own holds show the open
  holds of the picture, the rest are crates (`ICONS.SS` 122).
- The docks: land units centred on x 239 + 17k at y 138, last first.
- The market: each good centred on x 10 + 19k at y 181, "bid/ask" in colour
  0x2F centred below at y 194. The chosen good has a yellow frame (0x0E)
  at (19k, 179), 20 × 21.

**Not yet known:** the small cargo icons the original draws above a ship
that carries goods, and why the "%" on the top line stands two pixels
closer to the number than our font puts it.
