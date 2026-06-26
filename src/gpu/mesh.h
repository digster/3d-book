#pragma once
//
// mesh.h — vertex data, procedural primitive generators, and a GPU-resident mesh.
//
// The generators return plain CPU-side `MeshData` (no GPU handles), which keeps
// them pure and unit-testable. `Mesh` is the GPU-resident counterpart that owns
// vertex/index buffers.

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace book {

// One vertex: position + normal + texture coordinate. Color is *per object*
// (supplied through the uniform block), not per vertex, so a single mesh can be
// reused by many instances. The uv is used to sample a base-color texture; for
// untextured objects a 1x1 white texture is bound so the uv has no visible
// effect and the per-object color shows through unchanged.
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

// CPU-side mesh: an indexed triangle list. Pure data — safe to build and test
// without a GPU device.
struct MeshData {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    // Lowest local-space Y across all vertices. Used to "seat" a model on a
    // page: lift it so this point rests on the page surface.
    float minY() const;
};

// --- Procedural box generator ---------------------------------------------
// Centered on the local origin with per-face normals. This is what the book
// (cover/pages/spine) is built from; staged models now come from external files
// (see io/model_loader.h) rather than procedural primitives.
MeshData makeBox(float width, float height, float depth);

// --- Procedural grid panel generator --------------------------------------
// A single flat sheet lying in the XZ plane (top normal +Y), used for the
// turning page leaf. Unlike makeBox, the panel is *hinge-anchored*: its local
// origin sits at the x=0 edge and it extends to x=width, so a rotation about
// the local Z axis pivots cleanly around that edge (the book's spine/gutter).
// It is subdivided into nx*nz cells (nx along x, nz along z) so the vertex
// shader can bend it into a smooth paper curl. UVs run u=x/width, v=(z+depth/2)/depth.
MeshData makeGridPanel(float width, float depth, int nx, int nz);

// GPU-resident mesh: vertex buffer + index buffer + index count. Owns its GPU
// buffers; call destroy() with the owning device before the device is gone.
class Mesh {
public:
    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    ~Mesh() = default; // buffers must be released explicitly via destroy()

    // Upload CPU data into freshly created GPU buffers. Returns false on error.
    bool upload(SDL_GPUDevice* device, const MeshData& data);

    // Release the GPU buffers. Safe to call on an empty/moved-from mesh.
    void destroy(SDL_GPUDevice* device);

    SDL_GPUBuffer* vertexBuffer() const { return vertexBuffer_; }
    SDL_GPUBuffer* indexBuffer()  const { return indexBuffer_; }
    uint32_t       indexCount()   const { return indexCount_; }

private:
    SDL_GPUBuffer* vertexBuffer_ = nullptr;
    SDL_GPUBuffer* indexBuffer_  = nullptr;
    uint32_t       indexCount_   = 0;
};

// A single thing to draw: which mesh, where (model matrix), its color tint, and
// the base-color texture to sample. `texture` is never null at draw time — the
// book parts and untextured models point at the scene's shared 1x1 white
// texture, which leaves `color` as the visible color.
//
// `opacity` drives the page-turn cross-fade (1 = fully opaque, the default; the
// renderer feeds it through the fragment alpha). `curl` is the cylindrical-bend
// curvature applied to this instance's vertices in the vertex shader; it is 0
// for everything except the turning leaf at the height of a page flip.
struct Instance {
    const Mesh*    mesh;
    glm::mat4      model;
    glm::vec3      color;
    SDL_GPUTexture* texture = nullptr;
    float          opacity = 1.0f;
    float          curl = 0.0f;
};

} // namespace book
