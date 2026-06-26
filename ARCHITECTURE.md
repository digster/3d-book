# Architecture

This document explains the "big picture" of `3d-book` — the pieces that only
make sense when you read several files together, and the *why* behind the
structural decisions.

## The one-paragraph mental model

The app is a thin SDL3 shell around a small forward renderer. `main.cpp` owns
the SDL lifecycle and input; it drives a `Renderer` (all the SDL_GPU plumbing),
a `Scene` (a flat list of drawable instances: the book parts plus the staged
models), and an `IsometricCamera` (the view/projection matrices). Each frame the
renderer walks the scene and, for every instance, pushes a small uniform block
(`mvp`, `model`, `color`), binds the instance's base-color texture, and issues an
indexed draw. The **book geometry** is generated in code (`makeBox`); the
**staged models** are loaded at startup from external files (OBJ / glTF) in the
`models/` directory and normalized into a common page-placement system. **What is
placed where** comes from data: a `book.json` file (see `io/scene_loader.cpp`)
holds an ordered list of *scenes* (page spreads), and the `Scene` renders one
spread at a time, flipping between them on a key press — the rest of the pipeline
is unchanged because a scene is just a different set of `{model, page, transform}`
entries feeding the same instance list.

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

SDL_GPU maps shader resources to fixed descriptor sets. Two matter here:
**vertex-stage uniform buffers live in descriptor set 1**, and **fragment-stage
sampled textures live in descriptor set 2**. So `shaders/scene.vert` declares its
uniform block as `layout(set = 1, binding = 0)` (fed with
`SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, ...)`), and `shaders/scene.frag`
declares its base-color sampler as `layout(set = 2, binding = 0)` (fed with
`SDL_BindGPUFragmentSamplers(pass, /*slot=*/0, ...)`). Declaring either in the
wrong set makes the runtime bind it as the wrong resource type.

### Texturing and two-sided rendering

The fragment shader always samples a base-color texture and multiplies it by the
per-object color. Untextured objects (the book parts, models with no material
texture) bind a shared **1×1 white texture**, so the multiply is a no-op and the
solid color shows through — this avoids any "is this textured?" branching in the
draw loop. Because external models can carry arbitrary face winding, the pipeline
disables back-face culling (`SDL_GPU_CULLMODE_NONE`) and the fragment shader
flips the normal toward the viewer on back faces (`gl_FrontFacing`) so both sides
stay correctly lit; depth testing resolves occlusion.

### Coordinate conventions

SDL_GPU uses a [0,1] clip-space depth range (like Vulkan/Metal/D3D), so the code
is compiled with `GLM_FORCE_DEPTH_ZERO_TO_ONE` and uses `glslangValidator -V`
(Vulkan semantics). SDL_GPU applies the Y-flip between backends for us, so the
same matrices produce a correctly-oriented image everywhere.

## Module map

```
src/
  main.cpp            SDL_AppInit/Event/Iterate/Quit lifecycle, CLI, input.
  third_party_impl.cpp  One TU that compiles the vendored single-header libs.
  gpu/
    gpu_common.h      fail() error helper; shared small includes.
    shader.*          .spv file -> SDL_GPUShader (via shadercross).
    mesh.*            Vertex/MeshData; makeBox generator; GPU Mesh upload.
    texture.*         Image decode (stb_image) + GPU Texture + shared sampler.
  io/
    model_loader.*    OBJ (tinyobjloader) + glTF (cgltf) -> ModelData; normalize.
    scene_loader.*    book.json (nlohmann/json) -> BookData (scenes of objects).
  scene/
    camera.*          IsometricCamera: orthographic view/projection + orbit/zoom.
    book.*            Builds the book meshes and exposes the two page rectangles.
    scene.*           Loads models/ + book.json; seats the current spread; instances.
  render/
    renderer.*        Device/window/depth/pipeline; per-frame draw of the scene.

third_party/          Vendored: tiny_obj_loader.h, cgltf.h, stb_image.h, json.hpp.
models/               Model files loaded at runtime (copied to build/models/).
book.json             The scenes (page spreads), loaded at runtime (copied to build/).
tools/                make_sample_models.py (regenerates the sample/test models).
```

`book_core` (in `CMakeLists.txt`) is a static library containing everything
except `main.cpp`. The app links it, and so does the test target — which lets
the headless tests exercise the pure logic (box generator, camera math, page
placement, the scene-file parser, and the OBJ/glTF loaders + image decode)
without the SDL entry point.

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
             ├─ SDL_BindGPUFragmentSamplers(slot 0, instance.texture + sampler)
             └─ bind mesh vbuf/ibuf + SDL_DrawGPUIndexedPrimitives
```

## Geometry: why boxes for the book

The book is intentionally built from axis-aligned boxes (`makeBox`): a dark
cover, two cream "paper-stack" boxes whose **top faces are the pages**, and a
dark spine. Because each page is a known axis-aligned rectangle (center,
half-extents, top Y — see `PageRect` in `book.h`), seating a model "on the page"
is just: map the authored page-relative `(u, v)` onto the rectangle (inset by the
model footprint), and set y so the model's base rests on the page top. Keeping the
pages flat (no tilt) keeps that placement math trivial and the staging area
usable.

## Loading & normalizing external models

`io/model_loader.cpp` turns a file into a `ModelData`: a list of textured
`SubMeshData` (grouped by OBJ material / glTF primitive) plus the seating numbers
`{halfHeight, footprintRadius}`. OBJ goes through tinyobjloader (triangulated,
grouped by `usemtl`, V flipped); glTF/GLB through cgltf (node world transforms
baked into vertices, base-color texture taken from a buffer-view, data-URI, or
external file). Both then run a shared **normalize** step that recenters the
combined AABB on the origin and uniformly scales it so the largest half-extent is
~0.4 — which is what lets a model authored at any scale/origin slot into the
page-placement math built for the old fixed-size primitives. Loading is pure CPU
work (no GPU device), so the loaders are unit-tested headlessly.

`Scene::build` enumerates `<exe dir>/models/` with `SDL_GlobDirectory`, loads
each supported file into a runtime **registry** (`std::vector<Model>` of GPU
sub-meshes + textures, plus a parallel `std::vector<ModelInfo>` and a
`filename → index` map so `book.json` can reference models by name), and uploads a
shared 1×1 white texture + sampler. If nothing loads, the book still renders.

## Scenes: data-driven page spreads

A **scene** is one two-page spread: the book plus a list of objects placed on its
pages. `io/scene_loader.cpp` parses `book.json` (nlohmann/json) into a `BookData`
— an ordered `std::vector<SceneData>`, each a list of `ObjectPlacement`
(`{model, page, position (u,v), rotationDeg, scale}`). Parsing is pure CPU work
and deliberately forgiving: a malformed document or unsupported `version` yields
`nullopt` (the app shows the bare book), and an individual bad object is skipped
rather than failing the whole spread — so a typo never crashes the app. The
parser is split into `loadBook(path)` (filesystem) and `parseBook(text)`
(string), and the string form is what the unit tests exercise.

The pure function `placeOnPage(page, uv, rotationDeg, scale, info)` (glm + std
only, no GPU) turns one authored object into a seated world-space model matrix: it
maps the page-relative `(u, v) ∈ [-1, 1]` across the `PageRect` (inset by the
scaled footprint and clamped, so the object never hangs off the page), seats Y on
the surface using the model's `halfHeight`, and applies a Y-axis rotation. The
unit tests check exactly these invariants (center maps to `(0,0)`, corners stay on
the page, the base rests on the surface at any scale).

`Scene` holds the parsed `bookData_` and a `currentScene_` index. `nextScene` /
`prevScene` / `setScene` clamp to `[0, sceneCount)` (a book doesn't wrap) and call
`rebuildInstances`, which stamps the book parts then, for each object in the
current spread, looks the model up by name, calls `placeOnPage`, and pushes one
`Instance` per sub-mesh (sharing the transform). Switching scenes is therefore
just rebuilding that flat list from a different spread; the renderer is untouched.

## Build & test workflow

- `cmake -B build && cmake --build build` — the `shaders` target compiles GLSL →
  SPIR-V, `model_assets` copies `models/` → `build/models/`, and `scene_assets`
  copies `book.json` → `build/book.json`, all as part of the normal build.
- `./build/3d_book --frames N` — renders N frames and exits; the NULL-swapchain
  guard keeps this valid even with no visible window, which makes it a good
  smoke test for the device/pipeline/shader/texture path.
- `./build/3d_book --screenshot out.bmp` — renders one frame offscreen to a BMP;
  the best end-to-end check that loaded models appear textured and seated.
  Combine with `--scene PATH` / `--scene-index N` to screenshot a specific spread.
- `ctest --test-dir build` — `tests/test_geometry.cpp` validates the box
  generator, camera matrices, `placeOnPage` seating/bounds, the `parseBook` scene
  parser (valid, partial, and malformed inputs), and the OBJ/glTF loaders + image
  decode (using the fixtures in `tests/assets/`) with no GPU involved.
