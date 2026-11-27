# GrizzTech4

An all-in-one game editor and engine built on id Tech 4: one executable that opens into an editor where you build, light, script and play a level without leaving it.

GrizzTech4 is a fork of [RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG), the modernised Doom 3 BFG Edition source port. That gives it DX12 and Vulkan through NVRHI, physically based rendering, baked global illumination, soft shadows and glTF 2 model support.

> **Status: pre-alpha (milestone 0).** GrizzTech4 now runs standalone, with its own small engine content and no Doom 3 data. It opens into an editor shell whose Play button runs a level in the same window. Level editing, the inspector and the other tools arrive from milestone 1 onward, so it isn't ready to make a game with yet.

## Goals

- **One executable.** It starts in the editor, and Play runs the game in the same window. Shipped games are built without the editor.
- **Approachable tools.** Brush blockout and mesh placement, a property inspector, a content browser, node-graph scripting, a conversation editor and a visual UI editor, with undo everywhere and readable error messages.
- **Live iteration.** Changes to maps, scripts, definitions, UI and imported models show up in the running game within seconds.
- **Standalone.** New projects need no Doom 3 game data.
- **Two looks.** A stylized material path and a PBR material path, switchable per project and per material.
- **Platforms:** Windows, Linux and Steam Deck (Steam Deck is the performance floor).
- **Single-player** games.

## Building

Clone with submodules:

```
git clone --recurse-submodules https://github.com/Grissyelda/GrizzTech4.git
```

You need CMake 3.25 or newer, [ISPC](https://github.com/ispc/ispc/releases) on your `PATH`, and:

- **Windows:** Visual Studio 2022. For the Vulkan renderer, the [Vulkan SDK](https://vulkan.lunarg.com/) as well; otherwise add `-DUSE_VULKAN=OFF` to build DirectX 12 only.
- **Linux / Steam Deck:** GCC or Clang, Ninja, SDL2, OpenAL and the Vulkan headers and loader (on Ubuntu: `ninja-build libsdl2-dev libopenal-dev libvulkan-dev`), plus [DXC](https://github.com/microsoft/DirectXShaderCompiler/releases) for compiling shaders.

Then, from the `neo` folder, pick a preset (`cmake --list-presets` shows them all):

| Preset | Builds |
| --- | --- |
| `windows-vs2022` | Windows, with the editor |
| `windows-game` | Windows, game only (what you ship) |
| `linux-release` | Linux, with the editor |
| `linux-debug` | Linux, with the editor and debug info |
| `steamdeck-release` | Steam Deck / Linux, game only |

```
cmake --preset linux-release
cmake --build --preset linux-release
```

Shaders compile into `content/renderprogs2` as part of the build. Every push also builds Windows and Linux packages on GitHub Actions (see the **Build** workflow's artifacts).

## Running

Run the executable from the repository root (or a package folder), next to `content/`. It opens the editor:

- The level fills the middle. The **tool dock** at the bottom has five tabs: **Build** (move, turn, resize, draw, cut, corners, and shapes to drag in), **Paint** (click surfaces to paint them), **Logic** (doors and the player start), **Light** and **Kits** (ready-made things to drop in). Keys 1 to 5 switch tabs; hold **Space** over the level for a wheel of every tool.
- The **Outliner**, **Inspector** (settings for the selection, as cards, with the raw keys under Advanced), **Content** and **Problems** panels dock around it. Drag a panel's tab to rearrange them, or out of the window onto another monitor (`editor_detachablePanels 0` turns that off). **View → Reset layout** puts them back.
- **Play** (top centre, **F5**) saves and runs the open level. **Esc** pauses it and brings the editor back with the game still loaded: changes go into the game, and brush changes rebuild the level around you when you carry on (**F5**). **F10** stops.
- Levels are compiled by `rbdmap`, which ships next to the editor and runs in the background (the bar left of Play shows how far it's got), so the editor never freezes. Play builds when the level needs it; **File → Build** (**Ctrl+B**) builds without playing. What the build says shows up in **Problems**, with the full build log a click away.
- **Pieces** (prefabs): select things and press **Make piece** to save them in `maps/prefabs/`, then place copies from **Kits**. Copies stay linked: **Edit piece** changes the piece right where a copy sits (the rest of the level turns to ghosts) and every copy follows. A double-click picks one part of a copy; changing it changes only that copy, and the Inspector marks it with **Revert** and **Apply to all copies**. Pieces can contain pieces.
- **File → New Project** creates a project folder from `template/` and asks whether it starts with the stylized or the PBR look. **File → Open Project** switches to another project.
- Start with `+set editor 0` to run as the game, without the editor. `editor_uiScale` makes the editor's text and controls bigger or smaller.
- **Help → Getting started** walks through building a first level.

`content/` holds the engine's own content: default materials, the `grizzsans` font (generated from DejaVu Sans), prototype grid textures, the default player and a test room. The editor's UI fonts are [Inter](https://rsms.me/inter/) (SIL Open Font License) and [Lucide](https://lucide.dev) icons (ISC license), in `content/editor/fonts`. The scripts in `scripts/content/` regenerate the fonts, textures and test map.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). In short: enable the hygiene hook, and commit with your GitHub noreply address.

## License

GrizzTech4's code is licensed under the GNU General Public License v3 with id Software's additional terms. See [LICENSE.md](LICENSE.md) and [LICENSE_EXCEPTIONS.md](LICENSE_EXCEPTIONS.md) for third-party components under other licenses.

Games made with GrizzTech4 must publish their engine and game code under the same terms. Their art, maps, audio and writing are not covered by the GPL and can stay proprietary.

GrizzTech4 contains no Doom 3 game data, which remains covered by its original EULA.

## Credits

GrizzTech4 stands on the work of id Software (Doom 3 and Doom 3 BFG Edition), Robert Beckebans and the RBDOOM-3-BFG contributors, and the wider id Tech 4 community. See [docs/UPSTREAM.md](docs/UPSTREAM.md) for the exact upstream version.
