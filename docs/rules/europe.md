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

We take it that ships move one stage a turn towards the lower numbers (a
guess: the real voyage may take longer, and byte 22 of a ship on the high
seas counts something on the way). A ship that reaches the port at 2 stops
there; one that passes 0 arrives in the New World. Land units on board have the stage of their ship, and the
ship is found by following the chain of units (bytes 24-27 of the unit
record): the passengers come before the ship. The original shows them
after the ship, walking the chain backwards from it.

In our tests, a ship in port at stage 2 with land units of the same chain
showed the land units on the docks, not in the ship.

## Sailing

A ship that moves onto a sea lane square (terrain 26) is asked the high
seas question (`@SAILHOME`, see [message boxes](dialogs.md)). If it sails,
the square is kept as bytes 50 and 51 of the nation record (the original's
saves hold the square there too: the Dutch galleon of COLONY00 sailing to
the New World carries 50, 33 as its destination, the same as the nation),
and it enters stage 4 with everyone aboard. From the port a ship sails
back after `@SAILAWAY`, enters stage 1, and comes out on that square.

Land units on the docks may board a ship in port. We keep them as sentried
units in the ship's chain at the port's stage; the original shows such
units on the docks, so how it stores passengers in port is still to be
found.

## The cargo holds

Byte 12 of a ship is the number of holds in use; the good in hold k is a
nibble at byte 13 + k / 2 (the low nibble for even k) and its amount is
byte 16 + k. In port the original shows a full hold (100) in colour and a
part-filled one with the grey icon (`ICONS.SS` 38 + good), each centred on
x 152 + 12k at y 165.

## The market

Byte 76 + good of the nation record holds a price. The market buys at
that price less one and sells at that price plus the fifth number of the
good's `@CARGO` row, so food 1 shows as 0/8, sugar 7 as 6/8, silver 14 as
13/14. Prices are per unit; cargo moves 100 at a time. Buying tops up a
hold of the same good or fills a free one, as far as the gold goes; the
tax is taken from what a sale brings in.

### Prices moving

Measured by selling sugar a hundred at a time and saving after each sale.
Each good has a volume (nation record, 16 bits at 92 + 2 × good). A sale
adds to it and a purchase takes from it. When it reaches the good's
`fall` column of `@CARGO` in hundreds (sugar: 600), the price drops by one
and the volume loses that much again. At minus `rise` hundreds the price
goes up by one. The price stays within the good's `low` and `high` columns.
"The price of … has fallen to …" (`@PRICEDOWN`, or `@PRICEUP`) follows.

Seen in the original: COLONY00's sugar at 7 with volume 143 kept its price
through four sales of 100 and fell to 6 with the fifth, leaving the volume
at 78. Sales are counted hold by hold, so the price can change between two
holds of the same good.

The original also keeps the gold and tons traded per good (at 124 and at
188 and 252, 32 bits each), which we update the same way.

**Not yet known:** a sale of 100 sugar added 107 to the volume, but 100
furs added exactly 100; we add the amount itself. Also not yet known are
how the volume recovers each turn (the `attrition` column), what
`volatility` does, how other nations' trade moves prices, and boycotts.

### The trade note

After each sale or purchase the original writes a note over the sky,
in the labels font in colour 0x30 with 0x36 for the font's second colour
and a black shadow; it stays until the next action. A sale shows
"Sold 100 Sugar at 6$/ton" centred at y 12 (centred on 160 less half of
the width minus one), then three rows 11 apart: "Price:", "2% Tax:" and,
below a black line from x 100 to 200 at y 45, "Net:" at y 48. Labels end
at x 147 and the amounts ("600$", "-12$", "588$") at x 200. A purchase
shows "Bought 100 Sugar at 8$/ton" and "Price: 800$" centred below it.
Both were compared pixel for pixel with the original.

## Crosses and immigration

Measured by ending turns in the original with changed saves.

- The nation gathers 2 crosses a turn (byte 46) even without a colony, plus
  what its colonies make: every colony makes 1 cross, and 1 more for each
  level of its church.
- When the crosses reach the number needed (byte 48) one of the three people
  on the docks (bytes 2 to 4) comes over for free, the crosses go back to 0
  and someone new takes the empty place. Which of the three comes is a guess
  (chance); the original took the second in the one case measured.
- The number needed does not change when someone comes. It is a running
  value the original moves in other ways (it was 2 lower after an artillery
  was lost, and the English need 2/3 of what the others do); the rule for
  it is not known, so Viceroy keeps the save's number.
- The religious adviser tells you in `@UNREST`: %COUNTRY is the nation's
  name ("the Netherlands"), STRING0 the home port and STRING1 the expert
  name of whoever came. The box is centred across the screen and its top
  is at (252 − height) / 2; the adviser's portrait (`MSS4.SS`) is drawn over
  it at (114, top − 52). The date in the panel stays on the old turn until
  the box is closed.

## Recruit, Purchase and Train

Measured by changing the nation record of a save and reading what the
original then asks (43 readings).

- **Recruit** offers the three people waiting at bytes 2 to 4 of the nation
  record, named by the plural column of `@JOB` (profession 0x1C, a free
  colonist, uses the Colonist row), after "(None)". The passage costs
  140 + 20 × the recruits so far (byte 6) with no crosses. As crosses
  (byte 46) gather towards those needed (byte 48) the price falls in a
  straight line: price = full − ⌊K × crosses / needed⌋ with
  K = min(0.99 × (full − 100), 0.79253 × full). This fits 36 of the 39
  readings exactly and the rest within two gold; the exact rule is not
  known. After a recruit the count goes up by one and the crosses go back
  to 0 (the next price was the full one). The recruit's place is taken by
  someone new; who comes is a guess weighted towards the unskilled, as the
  original's odds are not known.
- **Purchase** lists Artillery for 500 + 100 × the artillery bought so far
  (byte 30), Caravel 1000, Merchantman 2000, Galleon 3000, Privateer 2000
  and Frigate 5000, then asks "Purchase <unit> for <price>$?" (`@REALLYBUY`).
- **Train** (`@KINGRECRUIT`, in the small font) lists every `@JOB` row with
  a price in its fourth column, cheapest first. Where two cost the same the
  original's order is that of a selection sort that takes the last of equal
  prices: Master Carpenters before Expert Fishermen, Hardy Pioneers before
  Master Tobacconists.
- In all three, choices the gold doesn't cover are grey (0x08) and the bar
  skips them. Recruit and Train take the gold at once; nothing asks again.
- The new unit stands on the docks, first in line. Veteran soldiers arrive
  as soldiers, hardy pioneers as pioneers, Jesuit missionaries as
  missionaries and seasoned scouts as scouts (seen for soldiers; the others
  are assumed). Ships bought wait in port.
- The original shows units on the docks three to a row; a fourth stands on
  the quay below, about 25 lower.

The boxes themselves are in [dialogs](dialogs.md).

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
