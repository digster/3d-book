//
// test_geometry.cpp — headless checks for the pure logic: the box generator,
// camera matrices, model placement, and the OBJ/glTF model loaders + image
// decode. No GPU device is created.
//
// Tiny assert-style harness: each CHECK records a failure; the process exits
// non-zero if any failed (which is what CTest keys on).
//
#include "gpu/mesh.h"
#include "gpu/texture.h"
#include "io/model_loader.h"
#include "scene/book.h"
#include "scene/camera.h"
#include "scene/scene.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

using namespace book;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static bool finite4x4(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(m[c][r])) return false;
    return true;
}

// A well-formed indexed triangle mesh: non-empty, a whole number of triangles,
// and every index in range.
static void checkMeshValid(const MeshData& m, const char* name) {
    CHECK(!m.vertices.empty());
    CHECK(!m.indices.empty());
    CHECK(m.indices.size() % 3 == 0);
    bool inRange = true;
    for (uint32_t idx : m.indices)
        if (idx >= m.vertices.size()) inRange = false;
    CHECK(inRange);
    if (!inRange) std::printf("  (%s had out-of-range indices)\n", name);
}

static void testBoxGenerator() {
    MeshData box = makeBox(0.6f, 0.6f, 0.6f);
    checkMeshValid(box, "box");
    // A 0.6 cube centered on the origin reaches down to y = -0.3.
    CHECK(std::fabs(box.minY() + 0.3f) < 1e-3f);
    // Vertices carry a uv (defaults to 0,0 for the untextured book boxes).
    CHECK(box.vertices[0].uv.x == 0.0f && box.vertices[0].uv.y == 0.0f);
}

static void testCamera() {
    IsometricCamera cam;
    cam.setAspect(1.4f);
    cam.setTarget(glm::vec3(0.0f, 0.35f, 0.0f));

    CHECK(finite4x4(cam.view()));
    CHECK(finite4x4(cam.proj()));
    CHECK(finite4x4(cam.viewProj()));

    // Orthographic projection has no perspective divide: bottom-right is 1 and
    // the w-from-z term is 0.
    glm::mat4 p = cam.proj();
    CHECK(std::fabs(p[3][3] - 1.0f) < 1e-6f);
    CHECK(std::fabs(p[2][3] - 0.0f) < 1e-6f);

    // Zoom is clamped; hammering it shouldn't blow past the bounds.
    for (int i = 0; i < 100; ++i) cam.zoom(0.5f);
    CHECK(cam.orthoSize() >= 1.2f - 1e-4f);
    for (int i = 0; i < 100; ++i) cam.zoom(2.0f);
    CHECK(cam.orthoSize() <= 12.0f + 1e-4f);
}

static bool withinPage(const PageRect& page, float x, float z) {
    return x >= page.center.x - page.halfX - 1e-3f &&
           x <= page.center.x + page.halfX + 1e-3f &&
           z >= page.center.z - page.halfZ - 1e-3f &&
           z <= page.center.z + page.halfZ + 1e-3f;
}

static void testPlacement() {
    // Page rectangles mirroring Book's layout.
    PageRect left{glm::vec3(-1.10f, 0.22f, 0.0f), 1.0f, 1.5f, 0.22f};
    PageRect right{glm::vec3(1.10f, 0.22f, 0.0f), 1.0f, 1.5f, 0.22f};

    // A small registry of model seating info (as the scene would build it).
    std::vector<ModelInfo> models = {{0.5f, 0.7f}, {0.3f, 0.45f}, {0.25f, 0.5f}};

    auto placements = generatePlacements(left, right, 1337u, models);

    // 4..6 models per page, two pages.
    CHECK(placements.size() >= 8 && placements.size() <= 12);

    for (const Placement& pl : placements) {
        CHECK(finite4x4(pl.model));
        CHECK(pl.modelIndex >= 0 && pl.modelIndex < static_cast<int>(models.size()));

        float x = pl.model[3][0];
        float y = pl.model[3][1];
        float z = pl.model[3][2];
        float scale = glm::length(glm::vec3(pl.model[0])); // |scaled basis vector|
        float halfH = models[pl.modelIndex].halfHeight;

        // Lands on one of the two pages...
        CHECK(withinPage(left, x, z) || withinPage(right, x, z));

        // ...and is seated so its base rests on the page top (y = 0.22).
        float base = y - halfH * scale;
        CHECK(std::fabs(base - 0.22f) < 1e-2f);
    }

    // Determinism: same seed -> identical result.
    auto again = generatePlacements(left, right, 1337u, models);
    CHECK(again.size() == placements.size());
    bool identical = again.size() == placements.size();
    for (size_t i = 0; i < again.size() && identical; ++i)
        identical = again[i].modelIndex == placements[i].modelIndex &&
                    again[i].model == placements[i].model;
    CHECK(identical);

    // No models -> no placements.
    CHECK(generatePlacements(left, right, 1337u, {}).empty());
}

// Combined AABB across all of a model's sub-meshes.
static void modelBounds(const ModelData& md, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(std::numeric_limits<float>::max());
    hi = glm::vec3(-std::numeric_limits<float>::max());
    for (const SubMeshData& sm : md.submeshes)
        for (const Vertex& v : sm.mesh.vertices) {
            lo = glm::min(lo, v.pos);
            hi = glm::max(hi, v.pos);
        }
}

static void checkLoadedModel(const ModelData& md, const char* name) {
    CHECK(!md.empty());
    for (const SubMeshData& sm : md.submeshes) {
        checkMeshValid(sm.mesh, name);
        // Normals are present and unit length.
        for (const Vertex& v : sm.mesh.vertices) {
            float len = glm::length(v.normal);
            CHECK(len > 0.9f && len < 1.1f);
        }
    }
    // Seating numbers are sane. kTargetHalfExtent (0.4) is the size the largest
    // half-extent is normalized to; halfHeight can't exceed it.
    constexpr float kTargetHalfExtent = 0.4f;
    CHECK(md.halfHeight > 0.0f && md.halfHeight <= kTargetHalfExtent + 1e-3f);
    CHECK(md.footprintRadius > 0.0f);

    // Normalization recenters the combined AABB on the origin.
    glm::vec3 lo, hi;
    modelBounds(md, lo, hi);
    glm::vec3 center = (lo + hi) * 0.5f;
    CHECK(glm::length(center) < 1e-3f);
    // Largest half-extent was scaled to the target.
    glm::vec3 half = (hi - lo) * 0.5f;
    CHECK(std::fabs(std::max({half.x, half.y, half.z}) - kTargetHalfExtent) < 1e-3f);
}

static void testLoaders() {
    const std::string dir = TEST_ASSET_DIR;

    // --- OBJ (external texture via map_Kd) ---
    auto obj = loadModel(dir + "/cube.obj");
    CHECK(obj.has_value());
    if (obj) {
        checkLoadedModel(*obj, "cube.obj");
        // The cube has UVs; at least one is non-zero (and V-flipped on load).
        bool anyUV = false;
        for (const SubMeshData& sm : obj->submeshes)
            for (const Vertex& v : sm.mesh.vertices)
                if (v.uv.x != 0.0f || v.uv.y != 0.0f) anyUV = true;
        CHECK(anyUV);
        // The material references an external image file that decodes to 64x64.
        CHECK(!obj->submeshes.empty() && !obj->submeshes[0].imagePath.empty());
        if (!obj->submeshes.empty() && !obj->submeshes[0].imagePath.empty()) {
            ImageData img = loadImageRGBA(obj->submeshes[0].imagePath);
            CHECK(img.valid() && img.width == 64 && img.height == 64);
        }
    }

    // --- glTF / GLB (embedded texture via bufferView) ---
    auto glb = loadModel(dir + "/cube.glb");
    CHECK(glb.has_value());
    if (glb) {
        checkLoadedModel(*glb, "cube.glb");
        // The base-color image is embedded; it decodes to a 64x64 RGBA image.
        CHECK(!glb->submeshes.empty() && !glb->submeshes[0].embeddedImage.empty());
        if (!glb->submeshes.empty() && !glb->submeshes[0].embeddedImage.empty()) {
            const auto& bytes = glb->submeshes[0].embeddedImage;
            ImageData img = decodeImageRGBA(bytes.data(), bytes.size());
            CHECK(img.valid() && img.width == 64 && img.height == 64);
        }
    }

    // Unsupported / missing files fail gracefully (no crash, no value).
    CHECK(!loadModel(dir + "/does_not_exist.obj").has_value());
    CHECK(!loadModel(dir + "/cube.png").has_value());
}

int main() {
    testBoxGenerator();
    testCamera();
    testPlacement();
    testLoaders();

    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
