# 0005 Games people own are read where they're installed; changes go in a mod folder

Status: Accepted
Date: 2026-11-27

## Context

Daedalus should open id Tech 4 games people own (DOOM 3 BFG Edition, DOOM 3 and Resurrection of Evil, Quake 4, Prey, Wolfenstein 2009) so they can play them with modern comforts and mod them with the editor. Their data is copyrighted and is not ours to ship. Some games' code is only available as SDKs under licenses that don't allow mixing with GPL code (Quake 4, Prey), and some has never been released (Wolfenstein 2009). Installs live in many places: Steam libraries, GOG, copies on other drives.

## Decision

1. **Nothing from a game is shipped, copied into the repository, or uploaded.** Daedalus reads a game's files from the user's install. Tests use stand-in files with the right names and no content.
2. **Finding games** (framework/GameLibrary): a game is recognised by files its install always has (`base/_common.resources`, `q4base/`, `SP/base/assets.pk4`, Prey's and DOOM 3's own executables and paks). Places searched: folders the user adds (a game's folder, a folder of games, or a data folder picked by mistake), Steam libraries listed in `libraryfolders.vdf`, and GOG installs. Added folders are kept in `daedalus_games.txt` in the save folder, never in a project. Names are matched without regard to case.
3. **Reading** (next step): a game's data folder is added read-only, below a mod folder of the user's own and above Daedalus' engine files. Edits are only ever written to the mod folder, so the install is never changed and a mod can be shared without any of the game's files.
4. **Support levels, per game:**
   - *Play and edit* for the DOOM 3 family, whose game code is the GPL code Daedalus is built on.
   - *Edit; play with a port* for Quake 4 and Prey: the editor reads and writes their levels and assets; playing hands the level to a separately installed port. No Quake 4 or Prey SDK code goes into Daedalus.
   - *Look inside* for Wolfenstein 2009, whose formats differ further; editing it is later work.

## Consequences

- A mod made in Daedalus is only the user's own files; packaging can check that no file from a game's install is included.
- The game list is per computer; projects don't carry paths to anyone's installs.
- Playing Quake 4 and Prey depends on ports outside this repository, and on how well they read what the editor writes.
- `Sys_ListFiles` on Windows walks files recursively and ignored the "folders only" request; it now returns the folders directly inside for `"/"`, as on Linux. `Sys_ListFolder` lists one level (files and folders) on every platform, for code that must not walk a whole drive.
