@order 2
# Live edits

While playing, `Esc` pauses the game and brings the editor back, with its camera where you were looking. The game stays loaded.

What you change goes into the paused game where it can:
- Lights change in place: colour, reach, position.
- Other things are updated, added or removed straight away.
- Brushes and level settings are built into the level, so they need a rebuild. When you carry on (`F5`), the level rebuilds in the background and reloads with you where you were, facing the same way.
- Moving the player start only matters when Play starts again, since you're already in the level.

## What each change did

While paused, a strip over the view counts your changes by what they did:
- **In the game:** already in the running game.
- **Next time you play:** saved, and used when Play starts again.
- **Rebuilds when you resume:** the level is rebuilt and reloaded when you carry on.

**Changes** opens the list, one line per thing. Select something and the Inspector says what happened to it. When a rebuild is needed, **Resume** shows a build icon, and carrying on says how many changes were already in the game and how many are being rebuilt.

`F10` stops. Health and other player state start fresh after a rebuild.
