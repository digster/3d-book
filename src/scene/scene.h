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

// --- Page-turn math (pure, GPU-free, so it is directly unit-testable) -------
// These drive the leaf as it sweeps about the spine during a page flip.

// Ease raw progress t in [0,1] with a smoothstep so the leaf accelerates out of
// and decelerates into rest rather than moving at a constant rate.
float pageTurnEase(float t);

// Hinge angle (degrees) from eased progress `e` and turn direction `dir`. The
// leaf is built along +X (flat right = 0deg) and rotates about the spine (local
// Z); forward turns sweep 0 -> 180deg (right -> up -> left), backward turns the
// mirror (180 -> 0deg). `dir >= 0` is forward.
float pageTurnAngleDeg(float e, int dir);

// Curl curvature (1/radius) from raw progress `t`, peaking at mid-turn (t=0.5)
// and zero at both ends so the page lies flat when settled. `kMax` is the peak.
float pageTurnCurvature(float t, float kMax);

// Model matrix that hinges the leaf at the spine: a rotation of `angleDeg` about
// the Z axis, translated up to the page surface at `hingeY`. The leaf mesh's
// local origin is its hinge edge, so this pivots cleanly around the gutter.
glm::mat4 leafTransform(float angleDeg, float hingeY);

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

    // Page-spread navigation. next/prev start an animated page-turn toward the
    // neighbouring spread (a no-op past either end, since a real book doesn't
    // wrap, or while a turn is already running). setScene jumps instantly and is
    // used for the initial spread / the --scene-index CLI flag.
    void nextScene();
    void prevScene();
    void setScene(int index);

    // Advance any in-progress page-turn by `dt` seconds, rebuilding the render
    // list for this frame. A no-op when no turn is running. Call once per frame
    // before draw().
    void update(float dt);
    bool isTurning() const { return turn_.active; }

    // Pose a (frozen) page-turn from the current spread toward its neighbour at
    // progress `t` in [0,1], without advancing it. For deterministic headless
    // capture/testing (the --turn-preview CLI flag); returns false at the ends of
    // the book where no neighbour exists. `dir >= 0` flips forward.
    bool poseTurn(int dir, float t);

    int  sceneCount() const { return static_cast<int>(bookData_.scenes.size()); }
    int  currentScene() const { return currentScene_; }
    // The spread to report in the UI: the turn's destination while flipping (so
    // the title leads the animation), otherwise the settled current spread.
    int  displayScene() const { return turn_.active ? turn_.toScene : currentScene_; }
    const std::string& currentSceneName() const;
    const std::string& bookTitle() const { return bookData_.title; }

    const std::vector<Instance>& instances() const { return instances_; }
    SDL_GPUSampler* sampler() const { return sampler_; }
    glm::vec3 target() const { return glm::vec3(0.0f, 0.35f, 0.0f); }

private:
    // An in-progress page-turn. `dir` is +1 forward / -1 backward; `t` runs 0->1
    // over `duration` seconds, driving the leaf sweep and the from/to cross-fade.
    struct PageTurn {
        bool  active = false;
        int   fromScene = 0;
        int   toScene = 0;
        int   dir = 1;
        float t = 0.0f;
        float duration = 0.6f;
    };

    // Load every supported model file from `<exe dir>/models/` into the registry.
    bool loadModelsFromDir(SDL_GPUDevice* device);
    // Load the book of scenes (default `<exe dir>/book.json` when path is empty).
    void loadBookFile(const std::string& scenePath);
    // Rebuild the render list from the book parts + the current scene's objects
    // (the settled, non-animating state).
    void rebuildInstances();
    // Rebuild the render list for the current frame of an in-progress turn: book
    // parts + the from/to spreads cross-faded + the curling, sweeping leaf.
    void buildTransitionInstances();
    // Append one spread's staged objects to `out` at the given opacity. `logMissing`
    // is true only on a settled rebuild so a turn (which calls this every frame)
    // doesn't spam the log for an unresolved model name.
    void appendSceneObjects(int sceneIndex, float opacity, std::vector<Instance>& out,
                            bool logMissing) const;
    // Begin a turn toward `target` (no-op if out of range or one is already running).
    void startTurn(int target, int dir);

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
    PageTurn               turn_;        // the active page-turn animation, if any
};

} // namespace book
