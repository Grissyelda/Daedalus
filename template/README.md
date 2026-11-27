# Your Daedalus project

This folder was created by **File → New Project** in the Daedalus editor. Everything your game adds or replaces lives here. Daedalus's own engine content (`content/`) stays underneath it, so anything you put here with the same name wins.

| Folder | What goes in it |
| --- | --- |
| `maps/` | Your levels. `start.map` is a small lit room to begin from. |
| `materials/` | Material definitions (`.mtr`) for your textures. |
| `textures/` | Your textures. |
| `models/` | Models exported from Blender as `.glb`. |
| `def/` | Entity definitions (`.def`): the player, items, characters. |
| `script/` | Game scripts. |
| `sound/` | Sounds and sound definitions. |

`project.daedalus` records the project's name and starting look (stylized or PBR). `autoexec.cfg` holds settings applied every time the project opens.

Remember that the GPL covers your game's **code**, not your art, maps, audio or writing. Keep your project in its own repository rather than in the Daedalus engine repository.
