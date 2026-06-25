#pragma once
//
// scene.h — the set of things to draw: the book plus randomly placed models.
//
// `generatePlacements` is a *pure* function (no GPU) so the placement rules can
// be unit-tested headlessly. `Scene` turns those placements into renderable
// instances by binding each to a shared primitive mesh.
//
#include "gpu/mesh.h"
#include "scene/book.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace book {

// The four primitive kinds we scatter on the pages.
enum class PrimitiveType : int { Box = 0, Pyramid, Sphere, Torus, Count };

// Canonical local geometry of each primitive *before* per-instance scaling.
// Shared between mesh construction and placement so the two never drift apart.
struct PrimitiveInfo {
    float halfHeight;      // -minY of the local mesh (used to seat it on a page)
    float footprintRadius; // local XZ radius (used to keep it within the page)
};

inline constexpr PrimitiveInfo kPrimitiveInfo[static_cast<int>(PrimitiveType::Count)] = {
    {0.30f, 0.43f}, // Box     — 0.6 cube: half-height 0.3, half-diagonal ~0.424
    {0.40f, 0.50f}, // Pyramid — base 0.7, height 0.8
    {0.38f, 0.38f}, // Sphere  — radius 0.38
    {0.13f, 0.45f}, // Torus   — outer radius 0.45, tube half-height 0.13
};

// One placed model: which primitive, its full model transform, and its color.
struct Placement {
    PrimitiveType type;
    glm::mat4 model;
    glm::vec3 color;
};

// Deterministically place a handful of primitives on each page. Every placement
// lands fully within its page rectangle and is seated so its base rests on the
// page surface.
std::vector<Placement> generatePlacements(const PageRect& left, const PageRect& right,
                                          uint32_t seed);

// Owns the book + primitive meshes and the flat list of instances to render.
class Scene {
public:
    bool build(SDL_GPUDevice* device);
    void destroy(SDL_GPUDevice* device);

    // Re-roll the random models (bound to the `R` key in main).
    void reseed();

    const std::vector<Instance>& instances() const { return instances_; }
    glm::vec3 target() const { return glm::vec3(0.0f, 0.35f, 0.0f); }

private:
    void rebuildInstances();

    Book book_;
    Mesh meshes_[static_cast<int>(PrimitiveType::Count)];
    std::vector<Instance> instances_;
    uint32_t seed_ = 1337u;
};

} // namespace book
