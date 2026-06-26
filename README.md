# 3d-book

A small native C++ app that renders an **open book in an isometric view**. The
two facing pages are a flat "staging area" onto which the app places **external
3D models** — loaded from a `models/` folder as **Wavefront OBJ** or **glTF 2.0**
(`.gltf` / `.glb`), **with their base-color textures**.

What goes where is **data-driven**: a `book.json` file describes an ordered list
of **scenes** (page spreads), each placing named models at specific positions on
the left/right pages. You flip between scenes like turning pages in a book.

It is built on **SDL3** and its modern **`SDL_GPU`** API (which backends onto
Metal on macOS), with **GLM** for math and **SDL_shadercross** to turn one set
of GLSL shaders into the Metal-native form the GPU wants at runtime. Model, image,
and scene loading use four vendored single-header libraries — **tinyobjloader**,
**cgltf**, **stb_image**, and **nlohmann/json** (under `third_party/`).

![concept: an isometric open book with textured 3D models scattered across both pages]

## Controls

| Input                       | Action                                       |
| --------------------------- | -------------------------------------------- |
| Left-drag                   | Orbit the camera around the book             |
| Scroll wheel                | Zoom in / out (orthographic scale)           |
| `→` / `PageDown`            | Flip to the next scene (page spread)         |
| `←` / `PageUp`              | Flip to the previous scene                   |
| `Esc` / close               | Quit                                         |

The window title shows the current scene's name and position, e.g.
`3d-book — Spread 2 — Corners (2/3)`.

## Authoring scenes (`book.json`)

A **scene** is one two-page spread: the book plus the objects staged on its two
pages. The app reads an ordered array of scenes from **`book.json`** (next to the
executable; the build copies the source-tree `book.json` into the build dir), and
the `←`/`→` keys flip between them.

```json
{
  "version": 1,
  "title": "Demo Book",
  "scenes": [
    {
      "name": "Spread 1 — One per page",
      "objects": [
        { "model": "textured_cube.obj", "page": "left",  "position": [0.0, 0.0], "rotation": 0,  "scale": 1.0 },
        { "model": "textured_cube.glb", "page": "right", "position": [0.4, -0.3], "rotation": 30, "scale": 0.8 }
      ]
    }
  ]
}
```

| Field        | Required | Meaning                                                                                   |
| ------------ | -------- | ----------------------------------------------------------------------------------------- |
| `version`    | yes      | Schema version. Must be `1`.                                                               |
| `title`      | no       | Book title (shown in the window title / logs).                                            |
| `scenes`     | yes      | Ordered list of page spreads. The index is the spread number.                             |
| `name`       | no       | Spread label (shown in the window title when active).                                     |
| `model`      | yes      | Filename as it appears in `models/`, e.g. `"textured_cube.glb"`.                           |
| `page`       | yes      | `"left"` or `"right"` (case-insensitive).                                                  |
| `position`   | yes      | Page-relative `[u, v]`, each in **[-1, 1]**. `(0,0)` = page center, `±1` = edge.           |
| `rotation`   | no       | Y-axis rotation in **degrees** (default `0`).                                              |
| `scale`      | no       | Uniform scale on top of the model's normalized size (default `1.0`).                       |

Positions are **page-relative and auto-seated**: you pick a page and a normalized
`(u, v)`; the app maps that onto the page rectangle (inset by the model's
footprint so it never hangs off the edge) and sets the height so the model's base
rests on the page surface — you never deal in world coordinates. The format is
forgiving: a malformed file or wrong `version` falls back to the bare book, and a
single bad object (missing `model`/`page`, or naming a model that isn't loaded) is
skipped with a log line rather than crashing.

## Adding your own 3D models

Drop model files into the **`models/`** directory next to the build (the build
copies `models/` from the source tree into `build/models/` automatically) and
rebuild or relaunch, then reference them by filename from `book.json`.

- **Formats:** Wavefront OBJ (with `.mtl` + `map_Kd` textures) and glTF 2.0,
  including binary `.glb` and textures embedded as buffer-views or data-URIs.
- **Textures:** the base-color texture / `map_Kd` (and the base-color factor /
  diffuse color as a tint) are used. Other PBR channels are ignored — the
  renderer is single base-color-textured with simple directional lighting.
- **Normalization:** each model is automatically recentered and uniformly scaled
  to a page-friendly size, so models authored at any scale or origin just work.
- If `models/` is empty (or `book.json` is missing), the book renders with bare
  pages and logs a hint.

The two sample cubes shipped in `models/` are generated by
`tools/make_sample_models.py` (standard-library Python only).

## Building

Prerequisites (all already present on the dev machine; install via Homebrew):

```sh
brew install sdl3 glm glslang
# SDL_shadercross is not in Homebrew — build it from source with the CLI/SPIRV
# paths enabled and `cmake --install` it (see ../sdl3-2d/examples/cpp-gpu-shadercross
# for the exact recipe). It must be discoverable via `pkg-config sdl3-shadercross`.
```

Then:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/3d_book
```

Useful extras:

```sh
./build/3d_book --frames 120              # render N frames then exit (headless smoke run)
./build/3d_book --screenshot out.bmp      # render one frame to a BMP and exit
./build/3d_book --scene path/to/book.json # load a specific scene file
./build/3d_book --scene-index 1           # open on spread N (0-based; clamped)
ctest --test-dir build                    # geometry / camera / scene-loader unit tests
```

`--screenshot` renders a single frame to an offscreen texture and saves it with
SDL's built-in BMP writer — no screen-recording permission needed. Convert with
`sips -s format png out.bmp --out out.png` on macOS if you want a PNG.

## How it works (short version)

- **Shaders** are authored once in GLSL (`shaders/scene.vert`, `scene.frag`),
  compiled to SPIR-V at build time by `glslangValidator`, then transpiled to
  Metal at load time by `SDL_ShaderCross_CompileGraphicsShaderFromSPIRV`. The
  fragment stage samples a base-color texture (a 1×1 white texture is bound for
  untextured objects, so their color shows through unchanged).
- **Models** are loaded from files (`src/io/model_loader.cpp`) into indexed
  triangle meshes, normalized, and uploaded to GPU buffers; their textures are
  decoded and uploaded by `src/gpu/texture.cpp`.
- The **book** (`src/scene/book.cpp`) is a few boxes (`makeBox`) — a cover, two
  cream "paper stacks" whose top faces are the pages, and a spine.
- The **camera** (`src/scene/camera.cpp`) is an orthographic isometric camera
  with orbit + zoom.
- The **scene** (`src/scene/scene.cpp`) loads the `models/` directory into a
  registry keyed by filename, parses `book.json` (`src/io/scene_loader.cpp`) into
  a list of spreads, and — for the current spread — seats each authored object on
  its page (`placeOnPage`), handing every sub-mesh to the renderer as an instance.
  Flipping scenes just re-selects which spread feeds the instance list.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full picture.

## License

MIT — see [LICENSE](LICENSE).
