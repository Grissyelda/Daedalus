@order 2
# Editing a piece

Select a copy and press **Edit piece** in the [Inspector](panels/inspector). The rest of the level turns into see-through ghosts you can't click, so you only change the piece. Anything you add meanwhile becomes part of it.

When you're done, choose above the view:
- **Save to every copy** changes the piece itself: every copy, in every level, gets the change. Other levels catch up when they open.
- **Save to this copy** keeps the changes in this copy only; the piece and its other copies stay as they were. What this copy does differently is marked in the Inspector.
- **Cancel** throws the changes away.

Changes a copy already had of its own stay its own when you save to every copy.

> **Undo** takes back a saved piece too, and the version before each save is kept as `maps/prefabs/<name>.bak`.
