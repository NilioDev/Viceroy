# Trade routes

The player names a route of up to four stops and says which goods to
unload and which to load at each. Ships and wagon trains are then set to
follow it (orders 2) and trade on their own. Seen in the original, read
from it with a debugger and checked against its savegames
(`tests/test_trade_routes.cpp`).

## In the savegame

- The number of routes (at most 12) is the header's word at offset 0x30.
- The routes are the last 12 × 74 bytes of the file. Each route has:
  - its name in 32 bytes, ended by a 0;
  - its type at byte 32 (1 sea, 0 land);
  - the number of stops at byte 33;
  - four stops of 10 bytes from byte 34.
- A stop has:
  - the colony as a word (999 for Europe);
  - a byte with the number of goods to unload (low nibble) and to load
    (high nibble);
  - three bytes holding the goods to load, a nibble each, low nibble first;
  - three bytes holding the goods to unload, in the same way;
  - one byte unused.

  Taking a good off a list leaves its old nibble behind the new end; Viceroy
  writes zeros there.
- A carrier's route is the low nibble of byte 23 of its record, and the
  stop it heads for is the high nibble.

## The TRADE menu

- **Edit Trade Route**: "Select a trade route:" lists the routes as
  "1. name". The route picked opens on the EDIT TRADE ROUTE screen. With no
  routes it says TRADENONE.
- **Create Trade Route**: with 12 routes it says TRADEMANY. Otherwise it
  asks, in order:
  1. "Select destination number 1 for route", over the player's colonies
     ten to a page.
  2. For a colony by the water, whether it is a sea or a land route
     (TRADETYPE). A colony inland makes a land route.
  3. The name (TRADENAME). The name offered is the colony's name and a
     random word of TRADENAMES ("Jamestown Ferry"). While another route has
     that name, " A" is added, then its letter goes up.
  4. "Select destination number 2"; for a sea route, only colonies by the
     water are listed.

  Escape at any step leaves no route. The route is added and shown on the
  screen.
- **Delete Trade Route**: TRADEDELETE lists the routes, and SUREDELETE asks
  first. The routes after it move up. Of the player's carriers:
  - those on a later route follow its new number;
  - those on the deleted route go back to route 0, stop 0, and lose their
    orders if they were following it.

## The EDIT TRADE ROUTE screen (measured)

- **Layout**:
  - A grey ground (colour 0x22) with white small type.
  - "EDIT TRADE ROUTE n" is centred at the top (y 5).
  - "Route Name:" and "Route Type:" are at x 10, y 25 and 33, with their
    values at x 55.
  - The column heads are at y 49: "Destination" at x 21, "Unload Cargo" at
    125 and "Load Cargo" at 208.
- **The table**: black lines at y 61, 81, 101, 121 and 141 across the
  screen, and at x 115 and 198 from y 61 to 141.
- **A stop's row**:
  - "1." stands at x 10 and the stop's name at x 22, 8 pixels below the
    row's line. Europe is named by its home port ("Amsterdam").
  - The goods are drawn as their icons 5 pixels below the line, from 10
    pixels right of the column's line. The icons stand two pixels apart,
    each as wide as it is.
- **OK**: a black frame at (280, 170), 30 × 20, with "OK" at (292, 177).
- **Clicks**:
  - A stop's name: "Select a port to sail to:" (a land route: "Select a
    colony to travel to:"). For a sea route the home port comes first and
    the bar starts on the first colony. For a stop already set, the list's
    last page ends with "(Delete Destination)", and the stops after it move
    up.
  - An empty row: the same list, which adds a stop (at most four).
  - A cargo column: CARGOUNLOAD or CARGOLOAD lists the sixteen goods. The
    good picked is added at the end, at most six.
  - A good's icon takes it off the list.

## Begin Trade Route

Orders menu, or T.

1. With no routes: TRADENONE.
2. A ship picks among the sea routes and a wagon train among the land
   routes: "Select a trade route:", with the routes keeping their numbers.
   When there is none of that kind it says TRADENONE2 ("You have not yet
   defined any Land trade routes.").
3. The route's stops are listed in "Select a port to sail to:" for the
   stop to make for first. The carrier gets orders 2 and sets off at once.

## Following the route

Each turn, before the player is asked, the player's carriers on routes
take their steps while they have moves:

- **Heading for a colony**: the destination (bytes 9 and 10) is set to the
  colony and the carrier steps along the original's routes (see
  [routes.md](routes.md)). With no way there it loses its orders.
- **Heading for Europe**: a ship heads for the nearest sea lane and sails
  for Europe on it. A ship in port in Europe that heads for a colony sets
  sail for the New World. The orders stay through the crossing.
- **At a colony**:
  1. Each good on the unload list is unloaded, every hold of it.
  2. Of the goods on the load list, the one with the most valuable stock
     (the price in Europe times the stock) is loaded, a hold (up to 100) at
     a time. This goes on until the holds are full or nothing is left to
     load.
- **In Europe**: the goods to unload are sold and one hold of each good to
  load is bought; boycotted goods are left alone. The ship's moves end there.
- **After each stop** the carrier heads for the next stop, round the
  route. When every stop is the same colony the original says ROUTELOOP
  ("our ... trade route has only one port on its itinerary!") and the
  carrier's moves end.
