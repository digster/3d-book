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
entries feeding the same instance list. A key press doesn't swap spreads
instantly; it starts a timed **page-turn animation** (a curling leaf + a
cross-fade of the two spreads) that the `Scene` advances each frame — but this too
is just the same instance list, rebuilt per frame from a blend of both spreads.

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

### Alpha blending (the page-turn cross-fade)

The color target enables standard "over" alpha blending
(`SRC_ALPHA / ONE_MINUS_SRC_ALPHA`). Outside a page-turn every draw is opaque
(alpha = 1), which reduces to the source color passing straight through, so
blending is a no-op in the steady state — there is no second pipeline. During a
turn the staged objects carry a fractional opacity (the uniform block's
`color.a`, fed from `Instance::opacity`) so the outgoing and incoming spreads
cross-fade. The cross-fading objects still write depth; this is acceptable
because the staged props are small and well separated, so the brief, transient
ordering imperfections are not noticeable.

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
    mesh.*            Vertex/MeshData; makeBox + makeGridPanel (leaf); GPU Mesh upload.
    texture.*         Image decode (stb_image) + GPU Texture + shared sampler.
  io/
    model_loader.*    OBJ (tinyobjloader) + glTF (cgltf) -> ModelData; normalize.
    scene_loader.*    book.json (nlohmann/json) -> BookData (scenes of objects).
  scene/
    camera.*          IsometricCamera: orthographic view/projection + orbit/zoom.
    book.*            Builds the book meshes + the turning-page leaf; page rectangles.
    scene.*           Loads models/ + book.json; seats the current spread; instances;
                      owns the page-turn animation (update / leaf transform / cross-fade).
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
   ├─ dt = clamp(now - lastTick, 0, 0.1s)
   ├─ Scene::update(dt)              # advance an in-progress page-turn, if any
   │     └─ rebuild the instance list for this frame (book + cross-faded spreads + leaf)
   └─ Renderer::draw(scene, camera)
        ├─ acquire command buffer + swapchain texture (skip if minimized)
        ├─ begin render pass: clear color + clear depth=1.0
        ├─ bind the single graphics pipeline
        └─ for each Instance in scene:
             ├─ ubo.mvp   = camera.viewProj() * instance.model
             ├─ ubo.model = instance.model
             ├─ ubo.color = vec4(instance.color, instance.opacity)   # a = page-turn fade
             ├─ ubo.turn  = vec4(instance.curl, 0, 0, 0)             # x = leaf curl curvature
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

`Scene` holds the parsed `bookData_` and a `currentScene_` index. `setScene`
clamps to `[0, sceneCount)` (a book doesn't wrap) and calls `rebuildInstances`,
which stamps the book parts then, for each object in the current spread, looks the
model up by name, calls `placeOnPage`, and pushes one `Instance` per sub-mesh
(sharing the transform) — all via the reusable `appendSceneObjects(index, opacity,
out)` helper. The settled state is just that flat list from one spread; the
renderer is untouched.

## Page-turn animation

`nextScene` / `prevScene` no longer jump instantly — they start an animated
**page-turn** toward the neighbouring spread (`setScene` remains the instant path,
used for the initial spread and the `--scene-index` flag). A turn is a small piece
of state on the `Scene` (`PageTurn`: `from`/`to` spread, direction, progress `t`,
duration). The main loop feeds a **clamped per-frame delta-time** into
`Scene::update(dt)`, which advances `t` and rebuilds the instance list for the
frame from three groups:

1. the **book** parts (always opaque);
2. the **two spreads cross-faded** — `appendSceneObjects(from, 1−e)` and
   `appendSceneObjects(to, e)`, where `e = smoothstep(t)` drives per-instance opacity;
3. the **turning leaf** — a hinge-anchored subdivided panel (`makeGridPanel`, owned
   by `Book`) rotated about the spine by `leafTransform(angle, hingeY)`.

The leaf's paper **curl** is the one piece that can't be a matrix: it's a
per-vertex *cylindrical bend* applied in `scene.vert` from a single curvature
scalar (`Instance::curl` → `ubo.turn.x`). A vertex at local x rotates by `θ = k·x`,
mapping the flat sheet onto an arc of radius `1/k`; the formula collapses to the
identity as `k → 0`, so the **same shader runs for every draw** with the bend
branch inert for non-leaf geometry (no second pipeline, no CPU re-tessellation —
just one scalar per draw). Curvature follows `kMax·sin(π t)`, peaking mid-turn and
flat at both ends so a settled page lies flat. The leaf is hinge-anchored (its
local origin is the spine edge) so a plain `rotateZ` about that origin pivots it
cleanly around the gutter; forward turns sweep `0 → 180°` and backward turns the
mirror, reusing the one `+X` leaf mesh.

The animation math is factored into **pure helpers** (`pageTurnEase`,
`pageTurnAngleDeg`, `pageTurnCurvature`, `leafTransform`) — GPU-free like
`placeOnPage`, so they're unit-tested headlessly. Input during a turn is ignored
and turns past either end are no-ops. `poseTurn(dir, t)` freezes a turn at a given
progress for deterministic capture (the `--turn-preview` flag).

## Build & test workflow

- `cmake -B build && cmake --build build` — the `shaders` target compiles GLSL →
  SPIR-V, `model_assets` copies `models/` → `build/models/`, and `scene_assets`
  copies `book.json` → `build/book.json`, all as part of the normal build.
- `./build/3d_book --frames N` — renders N frames and exits; the NULL-swapchain
  guard keeps this valid even with no visible window, which makes it a good
  smoke test for the device/pipeline/shader/texture path.
- `./build/3d_book --screenshot out.bmp` — renders one frame offscreen to a BMP;
  the best end-to-end check that loaded models appear textured and seated.
  Combine with `--scene PATH` / `--scene-index N` to screenshot a specific spread,
  or with `--turn-preview T` (T ∈ [0,1]) to capture a frozen mid-flip frame — the
  way to eyeball the leaf curl + cross-fade without recording the live animation.
- `ctest --test-dir build` — `tests/test_geometry.cpp` validates the box
  generator, camera matrices, `placeOnPage` seating/bounds, the **page-turn math**
  (easing, sweep angle, curl curvature, the hinge-pinned leaf transform, and the
  `makeGridPanel` leaf mesh), the `parseBook` scene parser (valid, partial, and
  malformed inputs), and the OBJ/glTF loaders + image decode (using the fixtures in
  `tests/assets/`) with no GPU involved.
