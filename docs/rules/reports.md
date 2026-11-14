# The advisers' reports

The REPORTS menu and the function keys F2 to F9 open the advisers'
reports. Each is drawn on its own picture (`REPORT1.PIK` to `REPORT9.PIK`)
with its title centred at the top, and closes on a key or a click. Read
from the original with a debugger and checked pixel by pixel against it
(`tests/test_reports.cpp`). F3, the Continental Congress, is in
[congress.md](congress.md).

## Common to all

- The picture's colours replace all but the first 16 of the palette, so
  the nations' colours (orders boxes) stay as on the map.
- The title is in colour 0x90 at y 5 (the Foreign Affairs report at y 2).
- The OK box is at (286, 184), 30 × 14, in colour 0x77.

## F2: the Religious Adviser

The crosses toward the next immigrant: one cross icon for each, in a bar
300 pixels wide at (10, 25), drawn as the Congress report's bell bar (only
the crosses gathered are drawn, out of those needed).

## F4: the Labor Adviser

- A grid of the professions in three columns of 8, 9 and 9, 105 pixels
  apart from x 2, rows 18 pixels high from y 25. Each shows its icon with a
  black copy two pixels to its left, the plural name in colour 0x92 and the
  count of the nation's people in that profession (colonists in colonies
  by their skill, people on the map by their profession) in colour 0x61.
  Teachers and veteran dragoons are left out; Free Colonists come last.
- A click on a profession zooms on it: its name under the title, and the
  colonies where such people work, three to a line. A key goes back to
  the grid; a click elsewhere closes the report.

## F5: the Economic Adviser

- First European trade: for each good the tons bought and sold, the gold
  made or lost (gains in colour 0x0A, losses in 0x0C), and the current
  bid and ask prices. From 10000 on figures are shown in thousands with a K.
- Then the cargo in port: an icon for each good across the top and a row
  for each colony, 17 to a page; 1000 and more are shown as "n K" in
  colour 0x0E.

## F6: the Colony Adviser

Nine colonies to a page, each with its picture, size and name.

- First the military garrisons: the armed land units in each colony,
  spread over 210 pixels from x 110 (at most 18 apart).
- Then the Sons of Liberty: the rebel percentage, the printing press or
  newspaper, the bells made each turn and the colonists working as
  statesmen.

## F7: the Naval Adviser

- Four columns 80 pixels wide from x 2 (the last 76): Ship, Cargo,
  Location and Destination, heads at y 27 in colour 0x92. Lines in colour
  0x77: across at y 40, 60, ... 180 from x 2 to 314, and down at x 82,
  162 and 242 from y 25 to 180.
- The rows are 20 pixels high from y 42, seven to a page; a key or click
  turns the page, and closes the report after the last.
- The units listed are the nation's ships, in the order of the unit list,
  and its land units standing on water (aboard). A ship is drawn as on the
  map at (2, row); a land unit stands in the cargo column at x 88.
- The cargo: one icon per hold from x 88, 12 pixels apart, 3 below the
  row: the full icon for 100, the small one for less.
- The name of the unit's type at the unit's x + 24, 6 below the row.
- **Location**, centred in its column: a colony's name; on the map
  elsewhere "(x, y)"; off the map the home port in port in Europe, else
  "High Seas".
- **Destination**: for a unit with orders to go somewhere (Go To, trade
  route) the place it heads for, a sea lane being the home port. A ship
  sailing for the New World shows the name of the nation's new land ("New
  Netherlands"), one sailing for Europe the home port. Otherwise nothing.

Land units in Europe are left out: the original looks at the terrain under
them, which off the map is not a square.

## F8: Foreign Affairs

After the declaration of independence it says FOREIGNNOTAVAIL instead.

- Lines across the screen at y 10, 55, 100 and 145; a block of 45 pixels
  for each of the four nations, from y 13.
- The leader's name and "'s " in colour 0x92, then the nation ("English:"),
  with "Free " before it when bit 0x04 of the nation record's first byte is
  set.
- With **Jan de Witt** two lines of figures follow, at x 2, 80 and 160:
  Colonies, Average Colony, Population; Military Power, Naval Power,
  Merchant Marine. They come from the count the original makes of every
  nation at the start of its turn (Viceroy counts the player's nation then
  too): the attack of its land units in eighths (shown divided by 8),
  privateers and frigates 8 each, and the holds of its ships.
- The nations it has met, at x 2, 80, 160: "England: " in colour 0x91 and
  Peace (white) or War (red).
- Without "Free", Rebels and Tories at x 2 and 80: the nation's people
  split by its rebel percentage.
- The nation that has withdrawn from the New World (header word 98) shows
  "(Withdrawn from New World)" instead and is left out of the others'
  lines.

## F9: the Indian Adviser

The tribes the nation has met, and those gone, one row of 21 pixels each
from y 25.

- A face at x 10 for the tribe's tension toward the nation (below 25,
  50, 75, and above; the angriest for a tribe gone).
- From x 30: the tribe's name and ": " in its colour (the Sioux in a
  brighter red, colour 0x0C), with a black edge one pixel right and below;
  "Extinct" after it for a tribe gone. The tribe's level (Semi-Nomadic to
  Civilized) ends at x 310.
- A second line in black, from x 40 in columns 56 pixels apart: its camps,
  villages or cities; the nation's missions there; its muskets (what it
  holds plus its armed braves, 50 each); its horse herds. The last three
  only when not zero (the herds can show negative, as in the original).
