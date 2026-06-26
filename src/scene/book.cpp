//
// book.cpp — construct the book geometry from a handful of boxes.
//
// Layout (looking down the +Y axis):
//   - a wide, thin COVER whose top sits at y = 0;
//   - two cream PAGE boxes resting on the cover, one either side of center —
//     their flat top faces (y = pageTopY) are the staging surfaces;
//   - a thin dark SPINE box down the middle, the binding ridge.
//
#include "scene/book.h"

#include <glm/gtc/matrix_transform.hpp>

namespace book {

namespace {
// --- Dimensions (world units) ---------------------------------------------
constexpr float kCoverW = 4.7f, kCoverH = 0.30f, kCoverD = 3.5f;
constexpr float kPageW = 2.0f, kPageH = 0.22f, kPageD = 3.0f;
constexpr float kSpineW = 0.18f, kSpineH = 0.26f, kSpineD = 3.0f;

constexpr float kPageGap = 0.10f;            // half-gap between the pages (the gutter)
constexpr float kPageCenterX = kPageW * 0.5f + kPageGap; // 1.10
constexpr float kPageTopY = kPageH;          // cover top is y=0, so page top = kPageH

// --- Turning-page leaf ----------------------------------------------------
// Length reaches from the spine (x=0) to the outer page edge; depth matches a
// page. Hinge sits a hair above the page surface to avoid z-fighting when flat.
constexpr float kLeafLength = kPageCenterX + kPageW * 0.5f; // 2.10 (spine -> edge)
constexpr float kLeafDepth = kPageD;
constexpr float kLeafHingeY = kPageTopY + 0.006f;
constexpr int   kLeafSegmentsX = 32; // subdivisions along the bend; plenty for a smooth curl
constexpr int   kLeafSegmentsZ = 1;  // curl is uniform along z, so one row suffices

// --- Colors ---------------------------------------------------------------
const glm::vec3 kCoverColor(0.40f, 0.22f, 0.14f); // brown leather
const glm::vec3 kPageColor(0.92f, 0.89f, 0.80f);  // cream paper
const glm::vec3 kSpineColor(0.28f, 0.15f, 0.09f); // darker binding
const glm::vec3 kLeafColor(0.96f, 0.94f, 0.86f);  // turning leaf, a touch brighter than the page
} // namespace

glm::vec3 Book::leafColor() const { return kLeafColor; }
float Book::leafHingeY() const { return kLeafHingeY; }

bool Book::build(SDL_GPUDevice* device) {
    if (!coverMesh_.upload(device, makeBox(kCoverW, kCoverH, kCoverD))) return false;
    if (!pageMesh_.upload(device, makeBox(kPageW, kPageH, kPageD))) return false;
    if (!spineMesh_.upload(device, makeBox(kSpineW, kSpineH, kSpineD))) return false;
    if (!leafMesh_.upload(device,
            makeGridPanel(kLeafLength, kLeafDepth, kLeafSegmentsX, kLeafSegmentsZ)))
        return false;

    auto translate = [](glm::vec3 p) { return glm::translate(glm::mat4(1.0f), p); };

    parts_.clear();
    // Cover: centered so its top face is at y = 0.
    parts_.push_back({&coverMesh_, translate({0.0f, -kCoverH * 0.5f, 0.0f}), kCoverColor});
    // Pages: boxes centered at y = kPageH/2 so they sit on the cover.
    parts_.push_back({&pageMesh_, translate({-kPageCenterX, kPageH * 0.5f, 0.0f}), kPageColor});
    parts_.push_back({&pageMesh_, translate({+kPageCenterX, kPageH * 0.5f, 0.0f}), kPageColor});
    // Spine: straddles the gutter, poking up between the pages.
    parts_.push_back({&spineMesh_, translate({0.0f, kSpineH * 0.5f - 0.04f, 0.0f}), kSpineColor});

    // Expose the two page-top surfaces for model placement.
    left_ = {glm::vec3(-kPageCenterX, kPageTopY, 0.0f), kPageW * 0.5f, kPageD * 0.5f, kPageTopY};
    right_ = {glm::vec3(+kPageCenterX, kPageTopY, 0.0f), kPageW * 0.5f, kPageD * 0.5f, kPageTopY};
    return true;
}

void Book::appendInstances(std::vector<Instance>& out, SDL_GPUTexture* whiteTexture) const {
    for (Instance part : parts_) {
        part.texture = whiteTexture;
        out.push_back(part);
    }
}

void Book::destroy(SDL_GPUDevice* device) {
    coverMesh_.destroy(device);
    pageMesh_.destroy(device);
    spineMesh_.destroy(device);
    leafMesh_.destroy(device);
    parts_.clear();
}

} // namespace book
