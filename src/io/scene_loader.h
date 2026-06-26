#pragma once
//
// scene_loader.h — load the book's *scenes* from an external JSON document.
//
// A "scene" is one two-page spread: the (fixed) book plus a list of objects
// placed on its left/right pages. The book file (`book.json`) holds an ordered
// array of these spreads, so flipping scenes is exactly like turning to a new
// page. This mirrors the model_loader split: parsing is pure CPU work (no GPU
// device) so it stays unit-testable, and the Scene turns the parsed data into
// concrete render instances.
//
// Positions are authored *page-relative*: each object names a page and a
// normalized (u, v) in [-1, 1] (with (0,0) the page center). The renderer-side
// Scene resolves that against the page rectangle and seats the object on the
// surface — the author never deals in world coordinates.
//
#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace book {

// Which page surface an object sits on.
enum class PageSide { Left, Right };

// One authored object on a page. `model` is a filename key into the loaded model
// registry (e.g. "textured_cube.glb"); `position` is page-relative (u, v) in
// [-1, 1]; `rotationDeg` is about the Y axis (keeps the footprint flat on the
// page); `scale` is a uniform multiplier on the model's normalized size. The Y
// position is not authored — the object is auto-seated on the page surface.
struct ObjectPlacement {
    std::string model;
    PageSide    page = PageSide::Left;
    glm::vec2   position{0.0f};
    float       rotationDeg = 0.0f;
    float       scale = 1.0f;
};

// One page-spread: an optional label plus the objects staged on it.
struct SceneData {
    std::string                  name;
    std::vector<ObjectPlacement> objects;
};

// A whole book: an optional title and the ordered spreads to flip through.
struct BookData {
    std::string            title;
    std::vector<SceneData> scenes;
    bool empty() const { return scenes.empty(); }
};

// The only schema version this loader understands. Bumped if the format changes
// incompatibly; a file declaring a different version is rejected.
inline constexpr int kBookSchemaVersion = 1;

// Read and parse a book JSON file. Returns nullopt (and logs a warning) on a
// missing file, malformed JSON, or an unsupported `version`, so the caller can
// fall back to an empty book and keep running.
std::optional<BookData> loadBook(const std::string& path);

// Parse a book document already held in memory. Same contract as loadBook; split
// out so the parsing logic can be unit-tested without touching the filesystem.
std::optional<BookData> parseBook(const std::string& json);

} // namespace book
