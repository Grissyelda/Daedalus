@order 3
# Undo and saving

- `Ctrl+Z` undoes, `Ctrl+Y` redoes. The arrows at the top right do the same, and their tooltips say what they'll undo.
- `Ctrl+S` saves the level. A dot after the level's name at the top means it has unsaved changes.
- Play and Build save first, so the game always gets what you see.
- Opening another level saves the one that's open, so work is never lost by switching.

Undo also covers [pieces](pieces/pieces): if a change wrote a piece, undoing it puts the old piece back. The version before each save is also kept beside it as a `.bak` file.
