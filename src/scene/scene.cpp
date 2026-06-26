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
#include <cstring>
#include <optional>
#include <string>

namespace book {

namespace {
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

void Scene::rebuildInstances() {
    instances_.clear();
    // Book parts are untextured: stamp them with the shared white texture so the
    // renderer's per-instance sampler binding always has a valid texture.
    book_.appendInstances(instances_, whiteTexture_.handle());

    if (currentScene_ < 0 || currentScene_ >= static_cast<int>(bookData_.scenes.size()))
        return;

    const SceneData& scene = bookData_.scenes[currentScene_];
    for (const ObjectPlacement& obj : scene.objects) {
        auto it = modelByName_.find(obj.model);
        if (it == modelByName_.end()) {
            SDL_Log("scene '%s': model '%s' not loaded — skipping",
                    scene.name.c_str(), obj.model.c_str());
            continue;
        }
        const int idx = it->second;
        const PageRect& page = (obj.page == PageSide::Left) ? book_.leftPage() : book_.rightPage();
        const glm::mat4 m = placeOnPage(page, obj.position, obj.rotationDeg, obj.scale,
                                        modelInfos_[idx]);
        for (const GpuSubMesh& sm : models_[idx].submeshes)
            instances_.push_back({&sm.mesh, m, sm.tint, sm.texture});
    }
}

void Scene::setScene(int index) {
    if (bookData_.scenes.empty()) return;
    const int last = static_cast<int>(bookData_.scenes.size()) - 1;
    const int clamped = std::max(0, std::min(index, last));
    if (clamped == currentScene_) return; // already there (or flipping past an end)
    currentScene_ = clamped;
    rebuildInstances();
}

void Scene::nextScene() { setScene(currentScene_ + 1); }
void Scene::prevScene() { setScene(currentScene_ - 1); }

const std::string& Scene::currentSceneName() const {
    static const std::string kEmpty;
    if (currentScene_ < 0 || currentScene_ >= static_cast<int>(bookData_.scenes.size()))
        return kEmpty;
    return bookData_.scenes[currentScene_].name;
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
}

} // namespace book
