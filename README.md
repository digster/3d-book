# 3d-book

A small native C++ app that renders an **open book in an isometric view**. The
two facing pages are a flat "staging area" onto which the app drops a handful of
**procedurally generated 3D models** (cubes, pyramids, spheres, a torus) at
random positions, sizes, rotations and colors.

It is built on **SDL3** and its modern **`SDL_GPU`** API (which backends onto
Metal on macOS), with **GLM** for math and **SDL_shadercross** to turn one set
of GLSL shaders into the Metal-native form the GPU wants at runtime.

![concept: an isometric open book with little 3D shapes scattered across both pages]

## Controls

| Input              | Action                                  |
| ------------------ | --------------------------------------- |
| Left-drag          | Orbit the camera around the book        |
| Scroll wheel       | Zoom in / out (orthographic scale)      |
| `R`                | Re-roll the random models on the pages  |
| `Esc` / close      | Quit                                    |

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
./build/3d_book --frames 120          # render N frames then exit (headless smoke run)
./build/3d_book --screenshot out.bmp  # render one frame to a BMP and exit
ctest --test-dir build                # geometry / camera / placement unit tests
```

`--screenshot` renders a single frame to an offscreen texture and saves it with
SDL's built-in BMP writer — no screen-recording permission needed. Convert with
`sips -s format png out.bmp --out out.png` on macOS if you want a PNG.

## How it works (short version)

- **Shaders** are authored once in GLSL (`shaders/scene.vert`, `scene.frag`),
  compiled to SPIR-V at build time by `glslangValidator`, then transpiled to
  Metal at load time by `SDL_ShaderCross_CompileGraphicsShaderFromSPIRV`.
- **Geometry** is generated in code (`src/gpu/mesh.cpp`) as indexed triangle
  meshes and uploaded once to GPU buffers.
- The **book** (`src/scene/book.cpp`) is a few boxes — a cover, two cream
  "paper stacks" whose top faces are the pages, and a spine.
- The **camera** (`src/scene/camera.cpp`) is an orthographic isometric camera
  with orbit + zoom.
- The **scene** (`src/scene/scene.cpp`) seats random primitives on the two page
  rectangles and hands every object to the renderer as an instance.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full picture.

## License

MIT — see [LICENSE](LICENSE).
