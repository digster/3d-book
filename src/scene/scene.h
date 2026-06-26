#pragma once
//
// scene.h — the set of things to draw: the book plus externally loaded models
// scattered across its two pages.
//
// `generatePlacements` is a *pure* function (no GPU) so the placement rules stay
// unit-testable. It works against a runtime registry of models — each described
// by a `ModelInfo` (the same {halfHeight, footprintRadius} seating contract the
// old built-in primitives used) — rather than a fixed compile-time set, which is
// what lets arbitrary user models drop straight into the staging math.
//
#include "gpu/mesh.h"
#include "gpu/texture.h"
#include "scene/book.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace book {

// Seating info a model needs to be placed on a page: half its height (to rest
// the base on the surface) and its XZ footprint radius (to stay within bounds).
struct ModelInfo {
    float halfHeight;
    float footprintRadius;
};

// One placed model: which registry entry, and its full model transform.
struct Placement {
    int       modelIndex;
    glm::mat4 model;
};

// Deterministically scatter a handful of models across each page. Every
// placement lands fully within its page rectangle and is seated so its base
// rests on the page surface. Returns empty if there are no models.
std::vector<Placement> generatePlacements(const PageRect& left, const PageRect& right,
                                          uint32_t seed, const std::vector<ModelInfo>& models);

// GPU-resident sub-mesh: geometry, the base-color texture to sample (the shared
// white texture when untextured), and a color tint (the material base color).
struct GpuSubMesh {
    Mesh            mesh;
    SDL_GPUTexture* texture = nullptr; // borrowed: owned by Scene (loaded or white)
    glm::vec3       tint{1.0f};
};

// A loaded model: one or more textured sub-meshes plus its placement seating.
struct Model {
    std::vector<GpuSubMesh> submeshes;
    ModelInfo               info{};
};

// Owns the book + the loaded model registry (meshes, textures, sampler) and the
// flat list of instances to render.
class Scene {
public:
    bool build(SDL_GPUDevice* device);
    void destroy(SDL_GPUDevice* device);

    // Re-roll the random placement of the loaded models (bound to `R` in main).
    void reseed();

    const std::vector<Instance>& instances() const { return instances_; }
    SDL_GPUSampler* sampler() const { return sampler_; }
    glm::vec3 target() const { return glm::vec3(0.0f, 0.35f, 0.0f); }

private:
    // Load every supported model file from `<exe dir>/models/` into the registry.
    bool loadModelsFromDir(SDL_GPUDevice* device);
    void rebuildInstances();

    Book book_;
    std::vector<Model>     models_;     // the registry of loaded models
    std::vector<ModelInfo> modelInfos_; // parallel to models_, fed to placement
    std::vector<Texture>   textures_;   // owns every loaded base-color texture
    Texture                whiteTexture_;          // untextured fallback
    SDL_GPUSampler*        sampler_ = nullptr;     // shared by every textured draw
    std::vector<Instance>  instances_;
    uint32_t               seed_ = 1337u;
};

} // namespace book
