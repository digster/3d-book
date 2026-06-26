//
// scene.cpp — load external models from disk and scatter them on the book pages.
//
#include "scene/scene.h"

#include "io/model_loader.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstring>
#include <optional>
#include <random>
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

std::vector<Placement> generatePlacements(const PageRect& left, const PageRect& right,
                                          uint32_t seed, const std::vector<ModelInfo>& models) {
    std::vector<Placement> out;
    if (models.empty()) return out;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::uniform_int_distribution<int> modelDist(0, static_cast<int>(models.size()) - 1);
    std::uniform_int_distribution<int> countDist(4, 6);

    const PageRect pages[2] = {left, right};
    for (const PageRect& page : pages) {
        int n = countDist(rng);
        for (int i = 0; i < n; ++i) {
            int idx = modelDist(rng);
            const ModelInfo& info = models[idx];

            float scale = 0.7f + u01(rng) * 0.6f;          // [0.7, 1.3]
            float foot = info.footprintRadius * scale + 0.04f;

            // Inset the spawn rectangle by the footprint so the model stays on
            // the page (clamped to 0 in case a big model barely fits).
            float availX = std::max(0.0f, page.halfX - foot);
            float availZ = std::max(0.0f, page.halfZ - foot);
            float x = page.center.x + (u01(rng) * 2.0f - 1.0f) * availX;
            float z = page.center.z + (u01(rng) * 2.0f - 1.0f) * availZ;
            float ty = page.topY + info.halfHeight * scale; // seat base on the page

            float angle = u01(rng) * 6.28318530718f;

            // model = T * R * S (applied right-to-left: scale, then rotate, then
            // translate). Y-rotation keeps the footprint flat on the page.
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, ty, z));
            model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(scale));

            out.push_back({idx, model});
        }
    }
    return out;
}

bool Scene::build(SDL_GPUDevice* device) {
    if (!book_.build(device)) return false;

    // The shared white texture + sampler back the "always sample a base-color
    // texture" path: untextured objects bind white so their color is unchanged.
    if (!whiteTexture_.uploadWhite(device)) return false;
    sampler_ = createDefaultSampler(device);
    if (!sampler_) return false;

    loadModelsFromDir(device); // non-fatal: an empty registry just means bare pages
    if (models_.empty()) {
        SDL_Log("No external models loaded — drop .obj/.gltf/.glb files in models/ "
                "(next to the executable). Showing the empty book.");
    }

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
        models_.push_back(std::move(gpu));
        modelInfos_.push_back(info);
        SDL_Log("loaded model: %s (%zu sub-mesh(es))", name.c_str(),
                models_.back().submeshes.size());
    }

    SDL_free(files);
    return !models_.empty();
}

void Scene::rebuildInstances() {
    instances_.clear();
    // Book parts are untextured: stamp them with the shared white texture so the
    // renderer's per-instance sampler binding always has a valid texture.
    book_.appendInstances(instances_, whiteTexture_.handle());

    if (models_.empty()) return;

    auto placements = generatePlacements(book_.leftPage(), book_.rightPage(), seed_, modelInfos_);
    for (const Placement& p : placements) {
        const Model& m = models_[p.modelIndex];
        for (const GpuSubMesh& sm : m.submeshes)
            instances_.push_back({&sm.mesh, p.model, sm.tint, sm.texture});
    }
}

void Scene::reseed() {
    // Advance via a simple LCG step so each press gives a visibly different roll.
    seed_ = seed_ * 1664525u + 1013904223u;
    rebuildInstances();
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
    textures_.clear();
    instances_.clear();
}

} // namespace book
