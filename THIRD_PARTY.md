# Third-party components

Daedalus is built on the Doom 3 BFG Edition GPL source and RBDOOM-3-BFG, and bundles the components below.
Full license texts are in [LICENSE_EXCEPTIONS.md](LICENSE_EXCEPTIONS.md) and next to each component.

This file is generated from `third_party.json` by `scripts/hygiene/third_party.py write`; edit the JSON, not this file.

| Component | License | Used | Where |
|---|---|---|---|
| [Doom 3 BFG Edition GPL source and RBDOOM-3-BFG](https://github.com/RobertBeckebans/RBDOOM-3-BFG) | GPL-3.0 with id Software's additional terms | The engine Daedalus is built on. See LICENSE.md and docs/UPSTREAM.md | `neo` |
| [Dear ImGui (with ImGuizmo)](https://github.com/ocornut/imgui) | MIT | Built in: the editor's interface and gizmos | `neo/libs/imgui` |
| [ImTui (with PDCurses)](https://github.com/ggerganov/imtui) | MIT; PDCurses public domain | Not built | `neo/libs/imtui` |
| [BinPack2D](https://github.com/chris-stones/BinPack2D) | BSD-2-Clause | Built in: packing rectangles into texture atlases | `neo/libs/binpack2d` |
| [FFmpeg (prebuilt for Windows)](https://ffmpeg.org) | GPL-3.0 build | Optional, off by default (FFMPEG): Bink videos use libbinkdec instead | `neo/libs/ffmpeg-win64` |
| [irrXML](https://irrlicht.sourceforge.io) | zlib | Built in: reading XML | `neo/libs/irrxml` |
| [Intel ISPC Texture Compressor](https://github.com/GameTechDev/ISPCTextureCompressor) | MIT | Built in: compressing textures | `neo/libs/ispc_texcomp` |
| libbinkdec | LGPL-2.1 | Built in: playing Bink videos. LGPL code may be used under the GPL, as here | `neo/libs/libbinkdec` |
| [Mesa R11G11B10 helpers](https://www.mesa3d.org) | MIT | Built in: packing HDR colours | `neo/libs/mesa` |
| [MinGW compatibility headers](https://www.portaudio.com) | MIT (sal.h, from PortAudio); LGPL-2.1+ (Wine dsound and XInput headers) | Only when building for Windows with MinGW (not our builds) | `neo/libs/mingw-hacks` |
| [Mikkelsen tangent space (MikkTSpace)](https://github.com/mmikk/MikkTSpace) | zlib | Built in: normal-map tangents for models | `neo/libs/mikktspace` |
| [Intel Masked Software Occlusion Culling](https://github.com/GameTechDev/MaskedOcclusionCulling) | Apache-2.0 | Built in: skipping hidden objects when drawing | `neo/libs/moc` |
| [Ogg Vorbis](https://xiph.org) | BSD-3-Clause | Built in: playing .ogg sounds | `neo/libs/oggvorbis` |
| [OpenAL Soft (prebuilt for Windows)](https://openal-soft.org) | LGPL-2.0 | Optional, off by default on Windows (OPENAL): Windows uses XAudio2; Linux links the system's OpenAL | `neo/libs/openal-soft` |
| [Optick](https://github.com/bombomby/optick) | MIT | Built in, switched off (USE_OPTICK=0): profiler hooks | `neo/libs/optick` |
| [RapidJSON](https://github.com/Tencent/rapidjson) | MIT (parts BSD and JSON License) | Built in: reading and writing JSON (glTF, model import) | `neo/libs/rapidjson` |
| [RenderDoc in-application API header](https://renderdoc.org) | MIT | Built in: lets RenderDoc capture frames | `neo/libs/renderdoc` |
| [stb (image, image write, image resize)](https://github.com/nothings/stb) | Public domain or MIT | Built in: reading and writing pictures | `neo/libs/stb` |
| [TiMidity](http://www.cgs.fi/~tt/discontinued.html) | GPL, LGPL or Artistic License (your choice) | Not built (only with the classic Doom code, which Daedalus removed) | `neo/libs/timidity` |
| [TinyEXR (with OpenEXR parts)](https://github.com/syoyo/tinyexr) | BSD-3-Clause | Built in: reading and writing EXR pictures | `neo/libs/tinyexr` |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | MIT | Built in: Vulkan memory | `neo/libs/vma` |
| [zlib and minizip](https://zlib.net) | zlib | Built in: compression and .zip files | `neo/libs/zlib` |
| [NVRHI](https://github.com/RobertBeckebans/nvrhi) | MIT | Built in: the DX12 and Vulkan renderer layer. A git submodule; its own third-party notices are in ThirdPartyLicenses.txt | `neo/extern/nvrhi` |
| [ShaderMake](https://github.com/RobertBeckebans/ShaderMake) | MIT | Build tool: compiles the shaders. A git submodule | `neo/extern/ShaderMake` |
| [AlchemyAO and Deep G-Buffer shaders (NVIDIA Research)](https://research.nvidia.com) | BSD | Built in: ambient occlusion and screen-space indirect light | `neo/shaders/builtin/SSAO`<br>`neo/shaders/builtin/SSGI` |
| Hash functions (Base64, MD4, MD5, CRC32) | BSD-style (Base64), RSA (MD4), public domain (MD5), zlib (CRC32) | Built in | `neo/idlib/Base64.cpp`<br>`neo/idlib/hashing/MD4.cpp`<br>`neo/idlib/hashing/MD5.cpp`<br>`neo/idlib/hashing/CRC32.cpp` |
| [Inter](https://github.com/rsms/inter) | OFL-1.1 | Font: the editor's text (a Latin subset, made by scripts/content/make_editor_fonts.py) | `content/editor/fonts/Inter-Regular.ttf`<br>`content/editor/fonts/Inter-SemiBold.ttf` |
| [Lucide icons](https://lucide.dev) | ISC | Font: the editor's icons (a subset, made by scripts/content/make_editor_fonts.py) | `content/editor/fonts/lucide.ttf` |
| [DejaVu Sans](https://dejavu-fonts.github.io) | Bitstream Vera license (and public domain changes) | Font: the in-game font daedalussans (rendered by scripts/content/make_font.py) | `content/newfonts/daedalussans` |
