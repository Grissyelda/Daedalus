# GrizzTech4

An all-in-one game editor and engine built on id Tech 4: one executable that opens into an editor where you build, light, script and play a level without leaving it.

GrizzTech4 is a fork of [RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG), the modernised Doom 3 BFG Edition source port. That gives it DX12 and Vulkan through NVRHI, physically based rendering, baked global illumination, soft shadows and glTF 2 model support.

> **Status: pre-alpha.** This repository currently holds the unmodified RBDOOM-3-BFG code base plus repository setup. The GrizzTech4 editor, standalone base content and engine changes land in the coming milestones. Nothing here is ready to make a game with yet.

## Goals

- **One executable.** It starts in the editor, and Play runs the game in the same window. Shipped games are built without the editor.
- **Approachable tools.** Brush blockout and mesh placement, a property inspector, a content browser, node-graph scripting, a conversation editor and a visual UI editor, with undo everywhere and readable error messages.
- **Live iteration.** Changes to maps, scripts, definitions, UI and imported models show up in the running game within seconds.
- **Standalone.** New projects need no Doom 3 game data.
- **Two looks.** A stylized material path and a PBR material path, switchable per project and per material.
- **Platforms:** Windows, Linux and Steam Deck (Steam Deck is the performance floor).
- **Single-player** games.

## Building

Until the GrizzTech4 build changes land, build it exactly as RBDOOM-3-BFG is built. See [docs/upstream/RBDOOM-3-BFG-README.md](docs/upstream/RBDOOM-3-BFG-README.md) for compilers, the Vulkan SDK, ISPC and DXC. Clone with submodules:

```
git clone --recurse-submodules https://github.com/Grissyelda/GrizzTech4.git
```

Running it currently needs Doom 3 BFG Edition game data, as RBDOOM-3-BFG does.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). In short: enable the hygiene hook, and commit with your GitHub noreply address.

## License

GrizzTech4's code is licensed under the GNU General Public License v3 with id Software's additional terms. See [LICENSE.md](LICENSE.md) and [LICENSE_EXCEPTIONS.md](LICENSE_EXCEPTIONS.md) for third-party components under other licenses.

Games made with GrizzTech4 must publish their engine and game code under the same terms. Their art, maps, audio and writing are not covered by the GPL and can stay proprietary.

GrizzTech4 contains no Doom 3 game data, which remains covered by its original EULA.

## Credits

GrizzTech4 stands on the work of id Software (Doom 3 and Doom 3 BFG Edition), Robert Beckebans and the RBDOOM-3-BFG contributors, and the wider id Tech 4 community. See [docs/UPSTREAM.md](docs/UPSTREAM.md) for the exact upstream version.
