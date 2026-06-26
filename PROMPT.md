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
