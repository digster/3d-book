#pragma once
//
// book.h — the open book itself: a few boxes plus the two page rectangles that
// act as staging surfaces for placed models.
//
#include "gpu/mesh.h"

#include <glm/glm.hpp>
#include <vector>

namespace book {

// An axis-aligned page surface. Models are placed within [center ± half] in X/Z
// and seated so their base rests at `topY`.
struct PageRect {
    glm::vec3 center; // center of the page-top surface (center.y == topY)
    float halfX;
    float halfZ;
    float topY;
};

// Builds and owns the meshes that make up the book (cover, two pages, spine)
// and reports the two page surfaces for model placement.
class Book {
public:
    bool build(SDL_GPUDevice* device);
    void destroy(SDL_GPUDevice* device);

    // Append the book's own parts (cover/pages/spine) to a render list. The book
    // is untextured, so each appended instance is stamped with `whiteTexture`
    // (the shared fallback) to satisfy the renderer's per-instance texture bind.
    void appendInstances(std::vector<Instance>& out, SDL_GPUTexture* whiteTexture) const;

    const PageRect& leftPage() const { return left_; }
    const PageRect& rightPage() const { return right_; }

private:
    Mesh coverMesh_;
    Mesh pageMesh_;
    Mesh spineMesh_;
    std::vector<Instance> parts_;
    PageRect left_{};
    PageRect right_{};
};

} // namespace book
