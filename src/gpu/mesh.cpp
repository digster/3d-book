//
// mesh.cpp — procedural primitive generators + GPU buffer upload.
//
#include "gpu/mesh.h"
#include "gpu/gpu_common.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace book {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Append a quad (4 corners tracing the perimeter) with a single flat normal.
// The winding is auto-corrected so the front face matches `n` regardless of the
// order the corners are given in — which makes the box/pyramid code trivial to
// read without hand-verifying every face's orientation.
void addFlatQuad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                 glm::vec3 n) {
    uint32_t base = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({a, n});
    m.vertices.push_back({b, n});
    m.vertices.push_back({c, n});
    m.vertices.push_back({d, n});

    if (glm::dot(glm::cross(b - a, c - a), n) >= 0.0f) {
        const uint32_t idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        m.indices.insert(m.indices.end(), idx, idx + 6);
    } else {
        const uint32_t idx[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
        m.indices.insert(m.indices.end(), idx, idx + 6);
    }
}

// Append a triangle, computing a flat normal and winding it to face away from
// the local origin (every primitive here is centered on the origin, so "away
// from origin" is a reliable proxy for "outward").
void addOutwardTri(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    glm::vec3 centroid = (a + b + c) / 3.0f;
    if (glm::dot(n, centroid) < 0.0f) {
        n = -n;
        std::swap(b, c);
    }
    uint32_t base = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({a, n});
    m.vertices.push_back({b, n});
    m.vertices.push_back({c, n});
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
}

// Emit two triangles for a grid cell whose 4 vertices already carry analytic
// normals (used by the curved surfaces). Winding is chosen to agree with those
// normals so front faces point outward.
void emitSmoothQuad(MeshData& m, uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3) {
    const glm::vec3& p0 = m.vertices[i0].pos;
    const glm::vec3& p1 = m.vertices[i1].pos;
    const glm::vec3& p2 = m.vertices[i2].pos;
    glm::vec3 faceN = glm::cross(p1 - p0, p2 - p0);
    glm::vec3 ref = m.vertices[i0].normal + m.vertices[i1].normal +
                    m.vertices[i2].normal + m.vertices[i3].normal;
    if (glm::dot(faceN, ref) >= 0.0f) {
        m.indices.insert(m.indices.end(), {i0, i1, i2, i0, i2, i3});
    } else {
        m.indices.insert(m.indices.end(), {i0, i2, i1, i0, i3, i2});
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

// --- Pyramid (square base, apex up) ---------------------------------------
MeshData makePyramid(float baseSize, float height) {
    const float hb = baseSize * 0.5f, hy = height * 0.5f;

    const glm::vec3 b00(-hb, -hy, -hb), b10(hb, -hy, -hb);
    const glm::vec3 b11(hb, -hy, hb),   b01(-hb, -hy, hb);
    const glm::vec3 apex(0.0f, hy, 0.0f);

    MeshData m;
    addFlatQuad(m, b00, b10, b11, b01, {0, -1, 0}); // base
    addOutwardTri(m, b00, b10, apex);               // four slanted faces
    addOutwardTri(m, b10, b11, apex);
    addOutwardTri(m, b11, b01, apex);
    addOutwardTri(m, b01, b00, apex);
    return m;
}

// --- UV sphere ------------------------------------------------------------
MeshData makeUVSphere(float radius, int sectors, int rings) {
    MeshData m;
    const int cols = sectors + 1;

    for (int i = 0; i <= rings; ++i) {
        float theta = static_cast<float>(i) / rings * kPi; // polar angle from +Y
        float y = std::cos(theta) * radius;
        float rs = std::sin(theta) * radius;
        for (int j = 0; j <= sectors; ++j) {
            float phi = static_cast<float>(j) / sectors * 2.0f * kPi;
            glm::vec3 p(rs * std::cos(phi), y, rs * std::sin(phi));
            m.vertices.push_back({p, glm::normalize(p + glm::vec3(0, 1e-6f, 0))});
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sectors; ++j) {
            uint32_t a = static_cast<uint32_t>(i * cols + j);
            uint32_t b = a + cols;
            emitSmoothQuad(m, a, a + 1, b + 1, b);
        }
    }
    return m;
}

// --- Torus (lies flat in the XZ plane) ------------------------------------
MeshData makeTorus(float majorRadius, float minorRadius, int majorSeg, int minorSeg) {
    MeshData m;
    const int cols = minorSeg + 1;

    for (int i = 0; i <= majorSeg; ++i) {
        float u = static_cast<float>(i) / majorSeg * 2.0f * kPi;
        float cu = std::cos(u), su = std::sin(u);
        for (int j = 0; j <= minorSeg; ++j) {
            float v = static_cast<float>(j) / minorSeg * 2.0f * kPi;
            float cv = std::cos(v), sv = std::sin(v);
            glm::vec3 p((majorRadius + minorRadius * cv) * cu,
                        minorRadius * sv,
                        (majorRadius + minorRadius * cv) * su);
            glm::vec3 n(cv * cu, sv, cv * su); // direction from tube center outward
            m.vertices.push_back({p, glm::normalize(n)});
        }
    }
    for (int i = 0; i < majorSeg; ++i) {
        for (int j = 0; j < minorSeg; ++j) {
            uint32_t a = static_cast<uint32_t>(i * cols + j);
            uint32_t b = a + cols;
            emitSmoothQuad(m, a, a + 1, b + 1, b);
        }
    }
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
