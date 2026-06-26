#pragma once
//
// scene.h — the set of things to draw: the book plus externally loaded models
// staged across its two pages by an authored "scene" (a page spread).
//
// Scenes are data-driven: a book file (book.json -> BookData, see scene_loader.h)
// holds an ordered list of spreads, and the Scene resolves each authored object
// into a seated model matrix via `placeOnPage` — a *pure* helper (no GPU) so the
// page-staging math stays unit-testable, the same {halfHeight, footprintRadius}
// seating contract the built-in primitives used. Flipping spreads is just
// re-selecting which scene's objects feed the render list.
//
#include "gpu/mesh.h"
#include "gpu/texture.h"
#include "io/scene_loader.h"
#include "scene/book.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace book {

// Seating info a model needs to be placed on a page: half its height (to rest
// the base on the surface) and its XZ footprint radius (to stay within bounds).
struct ModelInfo {
    float halfHeight;
    float footprintRadius;
};

// One resolved placement: which registry entry, and its full model transform.
struct Placement {
    int       modelIndex;
    glm::mat4 model;
};

// Resolve a page-relative authored object into a seated world-space model matrix.
// `uv` (clamped to [-1, 1]) maps across the page rectangle, inset by the model's
// footprint so the object always stays fully on the page; Y seats the base on the
// page surface; `rotationDeg` rotates about the Y axis (which keeps the footprint
// flat on the page). Pure (no GPU), so it is directly unit-testable.
glm::mat4 placeOnPage(const PageRect& page, glm::vec2 uv, float rotationDeg,
                      float scale, const ModelInfo& info);

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

// Owns the book + the loaded model registry (meshes, textures, sampler), the
// parsed book of scenes, and the flat list of instances to render.
class Scene {
public:
    // Build GPU resources and load the book of scenes. `scenePath` overrides the
    // default `<exe dir>/book.json` (used by tests / the --scene CLI flag).
    bool build(SDL_GPUDevice* device, const std::string& scenePath = "");
    void destroy(SDL_GPUDevice* device);

    // Page-spread navigation. Each clamps to [0, sceneCount) and rebuilds the
    // render list; flipping past either end is a no-op (a real book doesn't wrap).
    void nextScene();
    void prevScene();
    void setScene(int index);

    int  sceneCount() const { return static_cast<int>(bookData_.scenes.size()); }
    int  currentScene() const { return currentScene_; }
    const std::string& currentSceneName() const;
    const std::string& bookTitle() const { return bookData_.title; }

    const std::vector<Instance>& instances() const { return instances_; }
    SDL_GPUSampler* sampler() const { return sampler_; }
    glm::vec3 target() const { return glm::vec3(0.0f, 0.35f, 0.0f); }

private:
    // Load every supported model file from `<exe dir>/models/` into the registry.
    bool loadModelsFromDir(SDL_GPUDevice* device);
    // Load the book of scenes (default `<exe dir>/book.json` when path is empty).
    void loadBookFile(const std::string& scenePath);
    // Rebuild the render list from the book parts + the current scene's objects.
    void rebuildInstances();

    Book book_;
    std::vector<Model>     models_;     // the registry of loaded models
    std::vector<ModelInfo> modelInfos_; // parallel to models_, fed to placement
    std::unordered_map<std::string, int> modelByName_; // filename -> index in models_
    std::vector<Texture>   textures_;   // owns every loaded base-color texture
    Texture                whiteTexture_;          // untextured fallback
    SDL_GPUSampler*        sampler_ = nullptr;     // shared by every textured draw
    std::vector<Instance>  instances_;
    BookData               bookData_;   // the parsed scenes (page spreads)
    int                    currentScene_ = 0;
};

} // namespace book
