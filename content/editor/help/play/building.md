@order 3
# Building

The game doesn't play the level you edit directly: building turns it into what the game loads, and checks it on the way. Play builds when the level needs it; `Ctrl+B` (or the hammer left of Play) builds without playing.

Building runs in the background: the bar left of Play shows how far it has got, and you can keep working. The cross on the bar cancels it.

What the build says goes to [Problems](panels/problems), with the full build log a click away.

### Leaks
The level has to be sealed: no gap between the inside and the empty void outside it. If there's a gap, the build says the level **leaks** and stops. Press **Show** in Problems: a red line runs from inside the level out through the gap. Close it (move a wall, add a brush) and build again.

> Entities outside the level can cause a leak too: a light or player start sitting in the void.
