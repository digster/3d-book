//
// scene.cpp — load external models from disk and stage them on the book pages
// according to the authored scenes in book.json. Switching scenes re-selects
// which spread's objects feed the flat render list.
//
#include "scene/scene.h"

#include "io/model_loader.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

namespace book {

namespace {
constexpr float kPi = 3.14159265358979323846f;
// Peak page-curl curvature (1/radius) at mid-turn. Tuned for a gentle paper bow
// across the leaf's length; larger = a tighter curl.
constexpr float kCurlMax = 0.5f;

// True if `name` ends with one of the model extensions we can load.
bool isModelFile(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto ends = [&](const char* ext) {
        size_t n = std::strlen(ext);
        return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
    };
    return ends(".obj") || ends(".gltf") || ends(".glb");
}
} // namespace

glm::mat4 placeOnPage(const PageRect& page, glm::vec2 uv, float rotationDeg,
                      float scale, const ModelInfo& info) {
    // Inset the page rectangle by the (scaled) footprint plus a small margin so an
    // object at the edge (u/v = ±1) still rests fully on the page. Clamp uv so an
    // out-of-range authored position can't push the object off the surface.
    float foot = info.footprintRadius * scale + 0.04f;
    float availX = std::max(0.0f, page.halfX - foot);
    float availZ = std::max(0.0f, page.halfZ - foot);

    float u = std::clamp(uv.x, -1.0f, 1.0f);
    float v = std::clamp(uv.y, -1.0f, 1.0f);
    float x = page.center.x + u * availX;
    float z = page.center.z + v * availZ;
    float ty = page.topY + info.halfHeight * scale; // seat base on the page surface

    float angle = glm::radians(rotationDeg);

    // model = T * R * S (applied right-to-left: scale, then rotate, then
    // translate). Y-rotation keeps the footprint flat on the page.
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, ty, z));
    model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(scale));
    return model;
}

// --- Page-turn math (pure) -------------------------------------------------
float pageTurnEase(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t); // smoothstep: 0 and 1 derivatives at the ends
}

float pageTurnAngleDeg(float e, int dir) {
    // Forward sweeps 0 -> 180deg; backward is the mirror so the leaf comes from
    // the opposite side, reusing the same +X leaf mesh.
    return dir >= 0 ? 180.0f * e : 180.0f * (1.0f - e);
}

float pageTurnCurvature(float t, float kMax) {
    // sin(pi*t) is 0 at t=0/1 (page flat when grabbed and when laid down) and
    // peaks at t=0.5 (most curled at the top of the lift).
    return kMax * std::sin(kPi * std::clamp(t, 0.0f, 1.0f));
}

glm::mat4 leafTransform(float angleDeg, float hingeY) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, hingeY, 0.0f));
    m = glm::rotate(m, glm::radians(angleDeg), glm::vec3(0.0f, 0.0f, 1.0f));
    return m;
}

bool Scene::build(SDL_GPUDevice* device, const std::string& scenePath) {
    if (!book_.build(device)) return false;

    // The shared white texture + sampler back the "always sample a base-color
    // texture" path: untextured objects bind white so their color is unchanged.
    if (!whiteTexture_.uploadWhite(device)) return false;
    sampler_ = createDefaultSampler(device);
    if (!sampler_) return false;

    loadModelsFromDir(device); // non-fatal: an empty registry just means bare pages
    if (models_.empty()) {
        SDL_Log("No external models loaded — drop .obj/.gltf/.glb files in models/ "
                "(next to the executable). Scenes referencing them will be skipped.");
    }

    loadBookFile(scenePath); // non-fatal: a missing/bad book just shows the bare book
    currentScene_ = 0;

    rebuildInstances();
    return true;
}

bool Scene::loadModelsFromDir(SDL_GPUDevice* device) {
    const char* base = SDL_GetBasePath();
    const std::string dir = std::string(base ? base : "") + "models/";

    int count = 0;
    char** files = SDL_GlobDirectory(dir.c_str(), nullptr, 0, &count);
    if (!files) {
        SDL_Log("models/ directory not found at %s", dir.c_str());
        return false;
    }

    for (int i = 0; i < count; ++i) {
        const std::string name = files[i];
        if (!isModelFile(name)) continue; // skip .mtl/.png/etc. that sit alongside

        std::optional<ModelData> md = loadModel(dir + name);
        if (!md) continue;

        Model gpu;
        gpu.info = {md->halfHeight, md->footprintRadius};
        for (SubMeshData& sm : md->submeshes) {
            GpuSubMesh gs;
            if (!gs.mesh.upload(device, sm.mesh)) continue; // skip a bad sub-mesh

            // Decode + upload this sub-mesh's base-color texture, if any; fall
            // back to the shared white texture otherwise.
            SDL_GPUTexture* tex = whiteTexture_.handle();
            ImageData img;
            if (!sm.imagePath.empty()) img = loadImageRGBA(sm.imagePath);
            else if (!sm.embeddedImage.empty())
                img = decodeImageRGBA(sm.embeddedImage.data(), sm.embeddedImage.size());

            if (img.valid()) {
                textures_.emplace_back();
                if (textures_.back().upload(device, img.pixels.data(), img.width, img.height)) {
                    tex = textures_.back().handle();
                } else {
                    textures_.back().destroy(device);
                    textures_.pop_back();
                }
            }

            gs.texture = tex;
            gs.tint = sm.baseColorFactor;
            gpu.submeshes.push_back(std::move(gs));
        }
        if (gpu.submeshes.empty()) continue;

        const ModelInfo info = gpu.info;
        const int index = static_cast<int>(models_.size());
        models_.push_back(std::move(gpu));
        modelInfos_.push_back(info);
        // Key the registry by filename so book.json can reference models by name.
        modelByName_[name] = index;
        SDL_Log("loaded model: %s (%zu sub-mesh(es))", name.c_str(),
                models_.back().submeshes.size());
    }

    SDL_free(files);
    return !models_.empty();
}

void Scene::loadBookFile(const std::string& scenePath) {
    std::string path = scenePath;
    if (path.empty()) {
        const char* base = SDL_GetBasePath();
        path = std::string(base ? base : "") + "book.json";
    }

    if (auto bk = loadBook(path)) {
        bookData_ = std::move(*bk);
        SDL_Log("loaded book '%s' (%d scene(s)) from %s", bookData_.title.c_str(),
                static_cast<int>(bookData_.scenes.size()), path.c_str());
    } else {
        SDL_Log("no usable book at %s — showing the bare book", path.c_str());
        bookData_ = BookData{};
    }
}

void Scene::appendSceneObjects(int sceneIndex, float opacity, std::vector<Instance>& out,
                              bool logMissing) const {
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(bookData_.scenes.size()))
        return;

    const SceneData& scene = bookData_.scenes[sceneIndex];
    for (const ObjectPlacement& obj : scene.objects) {
        auto it = modelByName_.find(obj.model);
        if (it == modelByName_.end()) {
            if (logMissing)
                SDL_Log("scene '%s': model '%s' not loaded — skipping",
                        scene.name.c_str(), obj.model.c_str());
            continue;
        }
        const int idx = it->second;
        const PageRect& page = (obj.page == PageSide::Left) ? book_.leftPage() : book_.rightPage();
        const glm::mat4 m = placeOnPage(page, obj.position, obj.rotationDeg, obj.scale,
                                        modelInfos_[idx]);
        for (const GpuSubMesh& sm : models_[idx].submeshes)
            out.push_back({&sm.mesh, m, sm.tint, sm.texture, opacity, 0.0f});
    }
}

void Scene::rebuildInstances() {
    instances_.clear();
    // Book parts are untextured: stamp them with the shared white texture so the
    // renderer's per-instance sampler binding always has a valid texture.
    book_.appendInstances(instances_, whiteTexture_.handle());
    appendSceneObjects(currentScene_, 1.0f, instances_, /*logMissing=*/true);
}

void Scene::buildTransitionInstances() {
    instances_.clear();
    book_.appendInstances(instances_, whiteTexture_.handle());

    // Cross-fade: the outgoing spread fades out as the incoming one fades in.
    const float e = pageTurnEase(turn_.t);
    const float outAlpha = 1.0f - e;
    const float inAlpha = e;
    if (outAlpha > 0.02f) appendSceneObjects(turn_.fromScene, outAlpha, instances_, false);
    if (inAlpha > 0.02f) appendSceneObjects(turn_.toScene, inAlpha, instances_, false);

    // The turning leaf: hinged at the spine, swept by `angle` and bowed by `curv`
    // (curvature uploaded per-instance and applied in the vertex shader). Opaque,
    // so it cleanly hides whatever it passes over.
    const float angle = pageTurnAngleDeg(e, turn_.dir);
    const float curv = pageTurnCurvature(turn_.t, kCurlMax);
    instances_.push_back({&book_.leafMesh(), leafTransform(angle, book_.leafHingeY()),
                          book_.leafColor(), whiteTexture_.handle(), 1.0f, curv});
}

void Scene::update(float dt) {
    if (!turn_.active) return;

    turn_.t += dt / turn_.duration;
    if (turn_.t >= 1.0f) {
        // Landed: commit the destination spread and drop back to the static path.
        turn_.t = 1.0f;
        turn_.active = false;
        currentScene_ = turn_.toScene;
        rebuildInstances();
        return;
    }
    buildTransitionInstances();
}

void Scene::startTurn(int target, int dir) {
    // Ignore input mid-turn and don't wrap past either end of the book.
    if (turn_.active) return;
    if (target < 0 || target >= static_cast<int>(bookData_.scenes.size())) return;
    if (target == currentScene_) return;

    turn_ = PageTurn{};
    turn_.active = true;
    turn_.fromScene = currentScene_;
    turn_.toScene = target;
    turn_.dir = dir;
    turn_.t = 0.0f;
    buildTransitionInstances(); // show frame 0 immediately
}

bool Scene::poseTurn(int dir, float t) {
    const int target = currentScene_ + (dir >= 0 ? 1 : -1);
    if (target < 0 || target >= static_cast<int>(bookData_.scenes.size())) return false;

    turn_ = PageTurn{};
    turn_.active = true;
    turn_.fromScene = currentScene_;
    turn_.toScene = target;
    turn_.dir = dir >= 0 ? 1 : -1;
    turn_.t = std::clamp(t, 0.0f, 1.0f);
    buildTransitionInstances();
    return true;
}

void Scene::setScene(int index) {
    if (bookData_.scenes.empty()) return;
    const int last = static_cast<int>(bookData_.scenes.size()) - 1;
    const int clamped = std::max(0, std::min(index, last));
    if (clamped == currentScene_) return; // already there (or flipping past an end)
    currentScene_ = clamped;
    rebuildInstances();
}

void Scene::nextScene() { startTurn(currentScene_ + 1, +1); }
void Scene::prevScene() { startTurn(currentScene_ - 1, -1); }

const std::string& Scene::currentSceneName() const {
    static const std::string kEmpty;
    const int idx = displayScene(); // lead the title with the turn's destination
    if (idx < 0 || idx >= static_cast<int>(bookData_.scenes.size()))
        return kEmpty;
    return bookData_.scenes[idx].name;
}

void Scene::destroy(SDL_GPUDevice* device) {
    for (Model& m : models_)
        for (GpuSubMesh& sm : m.submeshes) sm.mesh.destroy(device);
    for (Texture& t : textures_) t.destroy(device);
    whiteTexture_.destroy(device);
    if (sampler_) {
        SDL_ReleaseGPUSampler(device, sampler_);
        sampler_ = nullptr;
    }
    book_.destroy(device);

    models_.clear();
    modelInfos_.clear();
    modelByName_.clear();
    textures_.clear();
    instances_.clear();
    bookData_ = BookData{};
    currentScene_ = 0;
    turn_ = PageTurn{};
}

} // namespace book
