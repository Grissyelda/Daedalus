@order 7
# Your games

**Your games** lists the id Tech 4 games installed on this computer, and what Daedalus can do with each. Open it from **View > Your games**, or from the Getting started window.

Daedalus reads a game where you installed it. It never copies, uploads or shares any of the game's files, and none come with Daedalus: you bring the game. When you change a game, your changes will go in a mod folder of your own, so the install is never touched.

## Finding your games

Daedalus looks in Steam and GOG by itself (untick **Look in Steam and GOG** to stop that). For anything else, add a folder:

- **Add a folder** opens a folder picker. Pick a game's own folder (the one with **base** or **q4base** inside), or a folder that holds several games.
- Or type or paste a folder into the box and press **Add**.
- Or drop a folder from your file browser onto the Daedalus window.

**Search again** looks once more, for games installed since. Under **Your folders**, the cross stops Daedalus looking in a folder; the games stay where they are.

## What each game can do

- **Play and edit** (green): DOOM 3 BFG Edition, DOOM 3 and Resurrection of Evil. Daedalus is built on DOOM 3's own game code, so it can run them.
- **Edit; play with a port** (yellow): Quake 4 and Prey (2006). Daedalus edits their levels and assets; to play them, install a port of that game and Daedalus hands the level to it.
- **Look inside** (grey): Wolfenstein (2009). Its files differ more from id Tech 4's, so Daedalus can show what's in them; editing comes later.

## Opening a game

DOOM 3 BFG Edition, DOOM 3 and Resurrection of Evil have **Open in Daedalus**. It restarts Daedalus into a project made for that game (named like `mod_doom3`, next to Daedalus). The project reads the game's levels, models, materials and sounds from where you installed it: open a level from **Content** to look around it in the editor.

Whatever you save goes into that project's own folder, never into the game's install. That folder is your mod: it holds only your files, so you can share it without any of the game's. The top bar shows which game's mod is open.

Playing the game from Daedalus comes next on the roadmap; Quake 4 and Prey open in a later step.

## From the console

`listGames` lists what was found, `addGameFolder <folder>` and `removeGameFolder <folder>` add and remove folders, and `games_steamPath` adds a Steam folder Daedalus didn't find by itself.
