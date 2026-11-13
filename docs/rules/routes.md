# Routes

How a unit with a destination (Go To, and the braves on their way to a
colony) picks its next step. Read from the original with a debugger and
checked against it: a colonist, a scout and two ships given Go To in
`COLONY00.SAV` took the same steps, and used the same moves, in the
original and in Viceroy (`tests/test_routes.cpp`).

The destination is in bytes 9 and 10 of the unit's record; the step taken
is written to byte 11 (0xFF for none). Directions are numbered from north
clockwise, 0 to 7.

## The search

1. A destination outside the map's inner part (one square from every
   edge), or the square the unit is on, gives no step. A destination next
   to the unit gives the step straight to it, without looking at the square.
2. **Near** (less than 7 squares away each way): a cost flood over the
   15 × 15 squares centred on the destination. The destination costs 1;
   each square costs its cheapest neighbour plus the cost of stepping from
   there: 1 along a road (a road or a town on both squares), 1 along a river
   followed straight, 3 for a unit with a single move, else 3 × the
   terrain's movement cost. The flood works through a queue of at most 225
   squares and stops when the queue runs dry. The unit then steps to the
   neighbour with a lower cost than its own square for which that cost
   plus the step is the least; of equals, the one nearest the destination.
   Only a route cheaper than 999 counts.
3. **Far**, or when the near search finds nothing: a search over the map's
   4 × 4 blocks. The save keeps after the map one byte per block for the
   sea and one for the land (15 blocks across, 18 down, the sea's first),
   with bit d set when the block is joined to its neighbour in direction d.
   The unit's block (or a joined neighbour block whose corner square it
   can reach within 18) and the destination's are found the same way; a
   breadth-first search over the joined blocks from the destination's
   counts the blocks to every other. The next block is the joined
   neighbour of the unit's with the lowest count, the one whose corner is
   nearer the destination of equals; its corner square (the first of the
   kind the unit moves on in the 2 × 2 at block × 4 + 1) is the new goal of
   a flood as above, now cheaper than 998. If that fails the corner of the
   unit's own block is tried.
4. When both fail, a European unit takes a **greedy** step: the neighbour
   for which 4 × (the longer side + both sides) of the distance left to
   the goal, plus both sides once more, plus the cost of the step, is
   least. The player's units never take a step that leaves them farther
   away by that measure. At the goal a
   greedy step clears the orders and ends the unit's turn. A step straight
   back where the unit came from is not taken: the unit waits (a computer
   player's Go To tries up to eight random directions first).

The tribes' units take whatever the flood gave, without the greedy step.

## What the search walks on

- Land units walk on land, ships on water whose region number (the low four
  bits of the path layer) is 1; the other kind is entered only at a colony
  that is where the route ends.
- Braves (types 19 and up) keep off lost city rumours.
- With Go To the unit's own nation is given: squares with another's units
  or town are left out, and so is the land (roads or fields) of a nation it
  has flagged as hostile (bit 0x40 of its relation byte); the player may
  cross that land at 8 more. The tribes go without a nation.
- The finder keeps its last flood: asked again for the same goal from a
  square the flood reached, it uses the old costs, as the original does.
- The block search leaves the mover counted as a caravel or a soldier for
  the rest of that search, as in the original.

## Moves

A unit's moves used (byte 5) take the whole cost of each step, even past
its allowance: a colonist that walks into a forest has used 6 thirds.
