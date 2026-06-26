//
// mesh.cpp — procedural primitive generators + GPU buffer upload.
//
#include "gpu/mesh.h"
#include "gpu/gpu_common.h"

#include <cstring>

namespace book {

namespace {
// Append a quad (4 corners tracing the perimeter) with a single flat normal.
// The winding is auto-corrected so the front face matches `n` regardless of the
// order the corners are given in — which makes the box/pyramid code trivial to
// read without hand-verifying every face's orientation.
void addFlatQuad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                 glm::vec3 n) {
    // The book boxes are untextured (a white texture is bound at draw time), so
    // their uv is irrelevant — set it to (0,0) explicitly to keep the vertex
    // fully initialized.
    const glm::vec2 uv(0.0f);
    uint32_t base = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({a, n, uv});
    m.vertices.push_back({b, n, uv});
    m.vertices.push_back({c, n, uv});
    m.vertices.push_back({d, n, uv});

    if (glm::dot(glm::cross(b - a, c - a), n) >= 0.0f) {
        const uint32_t idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        m.indices.insert(m.indices.end(), idx, idx + 6);
    } else {
        const uint32_t idx[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
        m.indices.insert(m.indices.end(), idx, idx + 6);
    }
}

} // namespace

float MeshData::minY() const {
    float lo = 0.0f;
    bool first = true;
    for (const Vertex& v : vertices) {
        if (first || v.pos.y < lo) {
            lo = v.pos.y;
            first = false;
        }
    }
    return lo;
}

// --- Box ------------------------------------------------------------------
MeshData makeBox(float width, float height, float depth) {
    const float hx = width * 0.5f, hy = height * 0.5f, hz = depth * 0.5f;

    // Eight corners, named by sign of (x,y,z).
    const glm::vec3 p000(-hx, -hy, -hz), p001(-hx, -hy, hz);
    const glm::vec3 p010(-hx, hy, -hz),  p011(-hx, hy, hz);
    const glm::vec3 p100(hx, -hy, -hz),  p101(hx, -hy, hz);
    const glm::vec3 p110(hx, hy, -hz),   p111(hx, hy, hz);

    MeshData m;
    addFlatQuad(m, p100, p101, p111, p110, {1, 0, 0});   // +X
    addFlatQuad(m, p000, p010, p011, p001, {-1, 0, 0});  // -X
    addFlatQuad(m, p010, p110, p111, p011, {0, 1, 0});   // +Y (top)
    addFlatQuad(m, p000, p001, p101, p100, {0, -1, 0});  // -Y (bottom)
    addFlatQuad(m, p001, p011, p111, p101, {0, 0, 1});   // +Z
    addFlatQuad(m, p000, p100, p110, p010, {0, 0, -1});  // -Z
    return m;
}

// --- GPU Mesh -------------------------------------------------------------
Mesh::Mesh(Mesh&& other) noexcept
    : vertexBuffer_(other.vertexBuffer_),
      indexBuffer_(other.indexBuffer_),
      indexCount_(other.indexCount_) {
    other.vertexBuffer_ = nullptr;
    other.indexBuffer_ = nullptr;
    other.indexCount_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        // Caller is responsible for having destroy()'d any prior GPU buffers;
        // we just take ownership of the incoming ones.
        vertexBuffer_ = other.vertexBuffer_;
        indexBuffer_ = other.indexBuffer_;
        indexCount_ = other.indexCount_;
        other.vertexBuffer_ = nullptr;
        other.indexBuffer_ = nullptr;
        other.indexCount_ = 0;
    }
    return *this;
}

bool Mesh::upload(SDL_GPUDevice* device, const MeshData& data) {
    const Uint32 vbytes = static_cast<Uint32>(data.vertices.size() * sizeof(Vertex));
    const Uint32 ibytes = static_cast<Uint32>(data.indices.size() * sizeof(uint32_t));
    indexCount_ = static_cast<uint32_t>(data.indices.size());

    // 1. Create the GPU-resident vertex and index buffers.
    SDL_GPUBufferCreateInfo vbci{};
    vbci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vbci.size = vbytes;
    vertexBuffer_ = SDL_CreateGPUBuffer(device, &vbci);
    if (!vertexBuffer_) return fail("SDL_CreateGPUBuffer(vertex)");

    SDL_GPUBufferCreateInfo ibci{};
    ibci.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    ibci.size = ibytes;
    indexBuffer_ = SDL_CreateGPUBuffer(device, &ibci);
    if (!indexBuffer_) return fail("SDL_CreateGPUBuffer(index)");

    // 2. Stage the data in a CPU-visible transfer buffer (vertices then indices).
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = vbytes + ibytes;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tbci);
    if (!transfer) return fail("SDL_CreateGPUTransferBuffer");

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) return fail("SDL_MapGPUTransferBuffer");
    std::memcpy(mapped, data.vertices.data(), vbytes);
    std::memcpy(static_cast<char*>(mapped) + vbytes, data.indices.data(), ibytes);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    // 3. Record + submit a one-shot copy pass that moves the staged bytes into
    //    the GPU buffers.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) return fail("SDL_AcquireGPUCommandBuffer(upload)");
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation vsrc{};
    vsrc.transfer_buffer = transfer;
    vsrc.offset = 0;
    SDL_GPUBufferRegion vdst{};
    vdst.buffer = vertexBuffer_;
    vdst.offset = 0;
    vdst.size = vbytes;
    SDL_UploadToGPUBuffer(pass, &vsrc, &vdst, false);

    SDL_GPUTransferBufferLocation isrc{};
    isrc.transfer_buffer = transfer;
    isrc.offset = vbytes;
    SDL_GPUBufferRegion idst{};
    idst.buffer = indexBuffer_;
    idst.offset = 0;
    idst.size = ibytes;
    SDL_UploadToGPUBuffer(pass, &isrc, &idst, false);

    SDL_EndGPUCopyPass(pass);
    if (!SDL_SubmitGPUCommandBuffer(cmd)) return fail("SDL_SubmitGPUCommandBuffer(upload)");

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

void Mesh::destroy(SDL_GPUDevice* device) {
    if (vertexBuffer_) {
        SDL_ReleaseGPUBuffer(device, vertexBuffer_);
        vertexBuffer_ = nullptr;
    }
    if (indexBuffer_) {
        SDL_ReleaseGPUBuffer(device, indexBuffer_);
        indexBuffer_ = nullptr;
    }
    indexCount_ = 0;
}

} // namespace book
