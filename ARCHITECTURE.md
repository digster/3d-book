# Architecture

This document explains the "big picture" of `3d-book` — the pieces that only
make sense when you read several files together, and the *why* behind the
structural decisions.

## The one-paragraph mental model

The app is a thin SDL3 shell around a small forward renderer. `main.cpp` owns
the SDL lifecycle and input; it drives a `Renderer` (all the SDL_GPU plumbing),
a `Scene` (a flat list of drawable instances: the book parts plus the random
models), and an `IsometricCamera` (the view/projection matrices). Each frame the
renderer walks the scene and, for every instance, pushes a small uniform block
(`mvp`, `model`, `color`) and issues an indexed draw. Geometry is generated in
code, not loaded from files.

## Rendering pipeline: GLSL → SPIR-V → Metal

This is the most important cross-cutting detail. SDL_GPU is a backend-agnostic
GPU API; on macOS it runs on Metal, which wants **MSL** (Metal Shading
Language), not GLSL or SPIR-V. We bridge that gap with two steps:

1. **Build time** — `glslangValidator -V` compiles `shaders/*.vert|*.frag`
   (GLSL) to `build/shaders/*.spv` (SPIR-V). This is wired up in
   `CMakeLists.txt` as a per-shader `add_custom_command`.
2. **Load time** — `src/gpu/shader.cpp` reads a `.spv` file and calls
   `SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(...)`, which transpiles
   SPIR-V → MSL and creates the `SDL_GPUShader`. That one call also handles the
   MSL entry-point renaming and resource reflection that would otherwise be
   fiddly to do by hand.

This mirrors the proven path in the sibling project
`../sdl3-2d/examples/cpp-gpu-shadercross`. Authoring in GLSL (not HLSL) is
deliberate: `glslangValidator` needs no DirectXShaderCompiler, which is awkward
to obtain on macOS.

### Shader resource binding convention (a real gotcha)

SDL_GPU maps shader resources to fixed descriptor sets. The one that matters
here: **vertex-stage uniform buffers live in descriptor set 1**. So
`shaders/scene.vert` declares its uniform block as
`layout(set = 1, binding = 0)`. At draw time we feed it with
`SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, ...)`.

### Coordinate conventions

SDL_GPU uses a [0,1] clip-space depth range (like Vulkan/Metal/D3D), so the code
is compiled with `GLM_FORCE_DEPTH_ZERO_TO_ONE` and uses `glslangValidator -V`
(Vulkan semantics). SDL_GPU applies the Y-flip between backends for us, so the
same matrices produce a correctly-oriented image everywhere.

## Module map

```
src/
  main.cpp            SDL_AppInit/Event/Iterate/Quit lifecycle, CLI, input.
  gpu/
    gpu_common.h      fail() error helper; shared small includes.
    shader.*          .spv file -> SDL_GPUShader (via shadercross).
    mesh.*            Vertex/MeshData; primitive generators; GPU Mesh upload.
  scene/
    camera.*          IsometricCamera: orthographic view/projection + orbit/zoom.
    book.*            Builds the book meshes and exposes the two page rectangles.
    scene.*           Random placement of primitives on the pages; instance list.
  render/
    renderer.*        Device/window/depth/pipeline; per-frame draw of the scene.
```

`book_core` (in `CMakeLists.txt`) is a static library containing everything
except `main.cpp`. The app links it, and so does the test target — which lets
the headless tests exercise the pure logic (mesh generators, camera math,
placement) without the SDL entry point.

## Data flow per frame

```
main.cpp (SDL_AppIterate)
   └─ Renderer::draw(scene, camera)
        ├─ acquire command buffer + swapchain texture (skip if minimized)
        ├─ begin render pass: clear color + clear depth=1.0
        ├─ bind the single graphics pipeline
        └─ for each Instance in scene:
             ├─ ubo.mvp   = camera.viewProj() * instance.model
             ├─ ubo.model = instance.model
             ├─ ubo.color = instance.color
             ├─ SDL_PushGPUVertexUniformData(slot 0)
             └─ bind mesh vbuf/ibuf + SDL_DrawGPUIndexedPrimitives
```

## Geometry: why boxes for the book

The book is intentionally built from axis-aligned boxes (`makeBox`): a dark
cover, two cream "paper-stack" boxes whose **top faces are the pages**, and a
dark spine. Because each page is a known axis-aligned rectangle (center,
half-extents, top Y — see `PageRect` in `scene.h`), seating a model "on the
page" is just: pick a random (x, z) inside the rectangle (with a margin) and set
y so the model's base rests on the page top. Keeping the pages flat (no tilt)
keeps that placement math trivial and the staging area usable.

## Scene & determinism

`generatePlacements(left, right, seed)` is a **pure function** (glm + std only,
no GPU) that returns a list of `{primitive type, model matrix, color}`. This is
what the unit tests check (every placement lands within its page rect and rests
on the surface). `Scene` turns those placements into renderable `Instance`s by
binding each to the matching shared `Mesh`. Placement is seeded by
`std::mt19937`; pressing `R` advances the seed and rebuilds the instances.

## Build & test workflow

- `cmake -B build && cmake --build build` — the `shaders` custom target compiles
  GLSL → SPIR-V as part of the normal build.
- `./build/3d_book --frames N` — renders N frames and exits; the NULL-swapchain
  guard keeps this valid even with no visible window, which makes it a good
  smoke test for the device/pipeline/shader path.
- `ctest --test-dir build` — `tests/test_geometry.cpp` validates mesh
  generators, camera matrices, and placement bounds with no GPU involved.
