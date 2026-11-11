# Rule tables in `NAMES.TXT`

`NAMES.TXT` (see `docs/formats/text.md` for the file format) holds most of
the numbers the game plays with. Viceroy reads them from the player's own
file (`src/core/src/rules/ruleset.cpp`), so this page describes the columns
but does not repeat the values. Meanings marked *checked* were confirmed
against the printed manual and reference cards; the rest are still working
names until they are confirmed against the original program.

A table ends at the first empty line of its section.

## Terrain: `@UNFORESTED`, `@FORESTED`, `@OTHER`

`name, movement, defense, improvement, value, yield × 9`

- **movement**: movement points needed to enter the square. *Checked.*
- **defense**: defense bonus in steps of 25 % of the defender's strength
  (2 = +50 %, 6 = +150 %). *Checked.*
- **improvement**: how much work plowing or clearing takes.
- **value**: an overall worth of the square, used when judging sites.
- **yield**: what one free colonist produces per turn, in this order:
  farmer, sugar, tobacco, cotton, furs, lumber, ore, silver, fish.

Each forested type is the forest of the unforested type in the same row.

## `@BUILDING`

`name, hammers, tools ÷ 10, slot, minimum colony size, upkeep`

Rows that share a name (such as the three town hall rows) are the levels of
one building. The tools column is stored divided by ten.

## `@JOB`

`name, expert name, skill level, price in Europe`

Skill level 4 means the job cannot be taught in a school. A price of -1
means the expert cannot be bought in Europe.

## `@CARGO`

`name, start price low, start price high, drift low, drift high, burden,
rise, fall, attrition, volatility`

These drive the market in Europe: the starting price range, the range the
price may move in, the gap between buying and selling price, the traffic
levels at which the price goes up or down, how fast traffic recovers each
turn, and how strongly traffic counts. Rows with only a name (hammers,
crosses, liberty bells, flags) are not traded.

## `@UNIT`

`name, icon, movement, attack, combat, cargo slots, size, cost, tools, guns,
hull, AI roles`

The AI roles are eight flags written as 0s and 1s, from left to right:
invade, settle, explore, attack, defend, escort, transport, naval.

## `@CLASS`

`name, cost to bring over from Europe`

## `@FATHERS`

`name, category, weight 1500s, weight 1600s, weight 1700s and later`

The weight is how likely the father is to be offered in that century. The
categories are listed in `@FOUNDING`.

## `@TRIBES`

`name, adjective, treasure name, level, colour`

Only the first block (up to the first empty line) are the playable tribes;
the lines after it are spare names.
