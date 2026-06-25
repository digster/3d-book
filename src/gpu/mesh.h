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

// One vertex: position + normal. Color is *per object* (supplied through the
// uniform block), not per vertex, so a single mesh can be reused by many
// instances with different colors.
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
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

// --- Procedural primitive generators --------------------------------------
// All are centered on the local origin and wound so that front faces are
// counter-clockwise when viewed from outside (matches the pipeline's cull
// state). Normals are per-face for the box/pyramid and analytic for the curved
// surfaces.
MeshData makeBox(float width, float height, float depth);
MeshData makePyramid(float baseSize, float height);
MeshData makeUVSphere(float radius, int sectors, int rings);
MeshData makeTorus(float majorRadius, float minorRadius, int majorSeg, int minorSeg);

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

// A single thing to draw: which mesh, where (model matrix), and what color.
struct Instance {
    const Mesh* mesh;
    glm::mat4   model;
    glm::vec3   color;
};

} // namespace book
