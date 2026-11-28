@order 6
# Areas and portals

The game splits a level into **areas** and only draws the ones the player can see. The doorways between areas are **portals**: invisible, see-through panes that fill a doorway from wall to wall. A level with no portals is one big area, so the game draws all of it all the time. That's fine for a small level, but big levels run faster with a portal in each doorway between rooms.

## Making a portal

1. Draw a thin brush that fills a doorway completely: from wall to wall and from floor to ceiling of the opening, a few units thick.
2. With it selected, open the **Logic** tab and click **Portal**. Its faces turn into the portal material.
3. Build the level (Ctrl+B).

## Seeing them

Turn on **Areas** in the view bar (top left of the view). The portals from the last build show as cyan outlines with a cross, and the status line says how many areas and portals the level has. If a portal you made doesn't show up, it doesn't seal its doorway: make it reach every edge of the opening, and check that the walls around it are solid.
