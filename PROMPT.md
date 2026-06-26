# Prompts

A running log of the prompts that shaped this project.

## 2026-06-25 — Initial build

- I want to create a 3D app where it looks like a book in isometric view where
  the two sides of the opened pages are the main staging area.
- I should be able to place 3D models on these pages. For now, just place some
  random 3D models on these pages.
- Build it using SDL3 and any other required libraries in c++.

## 2026-06-25 — External 3D models

- Enable adding external 3D models.
- (clarified) Support both OBJ and glTF; external libraries are fine. The
  renderer should support textures. The existing procedural primitives were only
  a proof of concept — replace them. For now, load models from a `models/`
  directory (richer discovery comes later).

## 2026-06-25 — Data-driven scenes (page spreads)

- The two pages and the objects on the pages along with their specific positions
  should each constitute a scene.
- New pages constitute new scenes, like the content on new pages in a normal book.
- All this scene data should be backed by some data format, which the application
  processes and renders. We should be able to switch between the different pages
  (scenes).
- Decide on this data format file before proceeding.
- (decided with the user) Format: **JSON via nlohmann/json** (vendored single
  header). Layout: a **single `book.json`** with an ordered `scenes` array.
  Positioning: **page-relative `(u,v)` + auto-seat**. The previous random
  placement is **replaced entirely** (purely data-driven).

## 2026-06-25 — Page-turn transition animation

- Implement book-like page turn transition animation when switching between the
  pages.
- (clarified with the user) Page fidelity: **curled leaf** — a subdivided page
  mesh that bends in the vertex shader as it sweeps about the spine (not a rigid
  board). Objects during the turn: **cross-fade** — the outgoing spread's models
  fade out and the incoming spread's fade in (enables pipeline alpha blending +
  per-instance opacity).
