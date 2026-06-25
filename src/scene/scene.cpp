//
// scene.cpp — random model placement + scene assembly.
//
#include "scene/scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <random>

namespace book {

namespace {
// Canonical primitive geometry. These dimensions MUST stay consistent with the
// kPrimitiveInfo table in scene.h (half-height and footprint).
MeshData makeCanonical(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Box:     return makeBox(0.6f, 0.6f, 0.6f);
        case PrimitiveType::Pyramid: return makePyramid(0.7f, 0.8f);
        case PrimitiveType::Sphere:  return makeUVSphere(0.38f, 22, 14);
        // minorSeg is a multiple of 4 so the tube's lowest point (270°) lands
        // exactly on a vertex -> minY == -minorRadius (matters for seating).
        case PrimitiveType::Torus:   return makeTorus(0.32f, 0.13f, 28, 16);
        default:                     return makeBox(0.5f, 0.5f, 0.5f);
    }
}

// Convert HSV (all in [0,1]) to RGB. Used to pick pleasant, well-separated hues
// for the placed models without ever producing muddy or near-black colors.
glm::vec3 hsv2rgb(float h, float s, float v) {
    float r = std::clamp(std::abs(h * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
    float g = std::clamp(2.0f - std::abs(h * 6.0f - 2.0f), 0.0f, 1.0f);
    float b = std::clamp(2.0f - std::abs(h * 6.0f - 4.0f), 0.0f, 1.0f);
    glm::vec3 rgb(r, g, b);
    return ((rgb - 1.0f) * s + 1.0f) * v; // lerp toward white by (1-s), scaled by v
}
} // namespace

std::vector<Placement> generatePlacements(const PageRect& left, const PageRect& right,
                                          uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::uniform_int_distribution<int> typeDist(0, static_cast<int>(PrimitiveType::Count) - 1);
    std::uniform_int_distribution<int> countDist(4, 6);

    std::vector<Placement> out;
    const PageRect pages[2] = {left, right};
    for (const PageRect& page : pages) {
        int n = countDist(rng);
        for (int i = 0; i < n; ++i) {
            auto type = static_cast<PrimitiveType>(typeDist(rng));
            const PrimitiveInfo& info = kPrimitiveInfo[static_cast<int>(type)];

            float scale = 0.7f + u01(rng) * 0.6f;          // [0.7, 1.3]
            float foot = info.footprintRadius * scale + 0.04f;

            // Inset the spawn rectangle by the footprint so the model stays on
            // the page (clamped to 0 in case a big primitive barely fits).
            float availX = std::max(0.0f, page.halfX - foot);
            float availZ = std::max(0.0f, page.halfZ - foot);
            float x = page.center.x + (u01(rng) * 2.0f - 1.0f) * availX;
            float z = page.center.z + (u01(rng) * 2.0f - 1.0f) * availZ;
            float ty = page.topY + info.halfHeight * scale; // seat base on the page

            float angle = u01(rng) * 6.28318530718f;
            glm::vec3 color = hsv2rgb(u01(rng),
                                      0.55f + u01(rng) * 0.25f,
                                      0.78f + u01(rng) * 0.18f);

            // model = T * R * S (applied right-to-left: scale, then rotate, then
            // translate). Y-rotation keeps the footprint flat on the page.
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, ty, z));
            model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(scale));

            out.push_back({type, model, color});
        }
    }
    return out;
}

bool Scene::build(SDL_GPUDevice* device) {
    if (!book_.build(device)) return false;
    for (int i = 0; i < static_cast<int>(PrimitiveType::Count); ++i) {
        if (!meshes_[i].upload(device, makeCanonical(static_cast<PrimitiveType>(i)))) {
            return false;
        }
    }
    rebuildInstances();
    return true;
}

void Scene::rebuildInstances() {
    instances_.clear();
    book_.appendInstances(instances_);

    auto placements = generatePlacements(book_.leftPage(), book_.rightPage(), seed_);
    for (const Placement& p : placements) {
        instances_.push_back({&meshes_[static_cast<int>(p.type)], p.model, p.color});
    }
}

void Scene::reseed() {
    // Advance via a simple LCG step so each press gives a visibly different roll.
    seed_ = seed_ * 1664525u + 1013904223u;
    rebuildInstances();
}

void Scene::destroy(SDL_GPUDevice* device) {
    for (Mesh& m : meshes_) m.destroy(device);
    book_.destroy(device);
    instances_.clear();
}

} // namespace book
