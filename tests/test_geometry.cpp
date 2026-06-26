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
#include "io/scene_loader.h"
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

// Pull translation / uniform scale back out of a model matrix for checking.
static void decompose(const glm::mat4& m, float& x, float& y, float& z, float& scale) {
    x = m[3][0];
    y = m[3][1];
    z = m[3][2];
    scale = glm::length(glm::vec3(m[0])); // |scaled basis vector|
}

static void testScenePlacement() {
    // Page rectangles mirroring Book's layout.
    PageRect left{glm::vec3(-1.10f, 0.22f, 0.0f), 1.0f, 1.5f, 0.22f};
    PageRect right{glm::vec3(1.10f, 0.22f, 0.0f), 1.0f, 1.5f, 0.22f};
    ModelInfo info{0.4f, 0.3f}; // halfHeight, footprintRadius

    // (0,0) lands on the page center and seats the base on the surface.
    {
        glm::mat4 m = placeOnPage(left, glm::vec2(0.0f, 0.0f), 0.0f, 1.0f, info);
        CHECK(finite4x4(m));
        float x, y, z, scale;
        decompose(m, x, y, z, scale);
        CHECK(std::fabs(x - left.center.x) < 1e-4f);
        CHECK(std::fabs(z - left.center.z) < 1e-4f);
        CHECK(std::fabs(scale - 1.0f) < 1e-4f);
        CHECK(std::fabs((y - info.halfHeight * scale) - left.topY) < 1e-4f); // base on surface
        CHECK(withinPage(left, x, z));
    }

    // Corner / edge positions and out-of-range values stay on the page (the
    // footprint inset + clamp guarantee it) and stay seated, at any scale.
    const glm::vec2 uvs[] = {{1, 1}, {-1, -1}, {1, -1}, {-1, 1}, {2.5f, -3.0f}};
    const float scales[] = {0.5f, 1.0f, 1.5f};
    for (const glm::vec2& uv : uvs) {
        for (float s : scales) {
            glm::mat4 m = placeOnPage(right, uv, 37.0f, s, info);
            CHECK(finite4x4(m));
            float x, y, z, scale;
            decompose(m, x, y, z, scale);
            CHECK(std::fabs(scale - s) < 1e-4f);
            CHECK(withinPage(right, x, z));
            CHECK(std::fabs((y - info.halfHeight * scale) - right.topY) < 1e-3f);
        }
    }

    // Determinism: same inputs -> identical matrix.
    glm::mat4 a = placeOnPage(left, glm::vec2(0.3f, -0.2f), 45.0f, 0.9f, info);
    glm::mat4 b = placeOnPage(left, glm::vec2(0.3f, -0.2f), 45.0f, 0.9f, info);
    CHECK(a == b);
}

static void testPageTurn() {
    // --- Easing: pinned at the ends, symmetric, monotonic. ---
    CHECK(std::fabs(pageTurnEase(0.0f) - 0.0f) < 1e-6f);
    CHECK(std::fabs(pageTurnEase(1.0f) - 1.0f) < 1e-6f);
    CHECK(std::fabs(pageTurnEase(0.5f) - 0.5f) < 1e-6f);
    CHECK(pageTurnEase(0.25f) < 0.25f);   // eases in (slower than linear early)
    CHECK(pageTurnEase(0.75f) > 0.75f);   // eases out (faster than linear late)
    // Out-of-range progress is clamped, not extrapolated.
    CHECK(std::fabs(pageTurnEase(-1.0f) - 0.0f) < 1e-6f);
    CHECK(std::fabs(pageTurnEase(2.0f) - 1.0f) < 1e-6f);

    // --- Sweep angle: forward 0->180, backward the mirror. ---
    CHECK(std::fabs(pageTurnAngleDeg(0.0f, +1) - 0.0f) < 1e-4f);
    CHECK(std::fabs(pageTurnAngleDeg(1.0f, +1) - 180.0f) < 1e-4f);
    CHECK(std::fabs(pageTurnAngleDeg(0.0f, -1) - 180.0f) < 1e-4f);
    CHECK(std::fabs(pageTurnAngleDeg(1.0f, -1) - 0.0f) < 1e-4f);
    // At the midpoint both directions stand the leaf straight up (90deg).
    CHECK(std::fabs(pageTurnAngleDeg(0.5f, +1) - 90.0f) < 1e-4f);
    CHECK(std::fabs(pageTurnAngleDeg(0.5f, -1) - 90.0f) < 1e-4f);

    // --- Curl: flat at the ends, peaks at mid-turn. ---
    const float kMax = 0.5f;
    CHECK(std::fabs(pageTurnCurvature(0.0f, kMax)) < 1e-6f);
    CHECK(std::fabs(pageTurnCurvature(1.0f, kMax)) < 1e-6f);
    CHECK(std::fabs(pageTurnCurvature(0.5f, kMax) - kMax) < 1e-5f);
    CHECK(pageTurnCurvature(0.25f, kMax) > 0.0f &&
          pageTurnCurvature(0.25f, kMax) < kMax);

    // --- Leaf transform: hinge edge stays pinned to the gutter at every angle. ---
    const float hingeY = 0.226f;
    for (float a : {0.0f, 45.0f, 90.0f, 135.0f, 180.0f}) {
        glm::mat4 m = leafTransform(a, hingeY);
        CHECK(finite4x4(m));
        glm::vec4 hinge = m * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // local origin = hinge edge
        CHECK(std::fabs(hinge.x - 0.0f) < 1e-5f);
        CHECK(std::fabs(hinge.y - hingeY) < 1e-5f);
        CHECK(std::fabs(hinge.z - 0.0f) < 1e-5f);
    }
    // A 90deg turn stands a free-edge point straight up above the hinge.
    {
        const float L = 2.1f;
        glm::vec4 tip = leafTransform(90.0f, hingeY) * glm::vec4(L, 0.0f, 0.0f, 1.0f);
        CHECK(std::fabs(tip.x - 0.0f) < 1e-4f);
        CHECK(std::fabs(tip.y - (hingeY + L)) < 1e-4f);
    }

    // --- Leaf mesh: valid, hinge-anchored along +X, fully subdivided. ---
    const int nx = 32, nz = 1;
    MeshData leaf = makeGridPanel(2.1f, 3.0f, nx, nz);
    checkMeshValid(leaf, "leaf");
    CHECK(leaf.vertices.size() == static_cast<size_t>((nx + 1) * (nz + 1)));
    float minX = leaf.vertices[0].pos.x, maxX = minX;
    for (const Vertex& v : leaf.vertices) {
        minX = std::min(minX, v.pos.x);
        maxX = std::max(maxX, v.pos.x);
        CHECK(std::fabs(v.pos.y) < 1e-6f);          // flat sheet before bending
        CHECK(v.normal.y > 0.9f);                   // faces +Y
    }
    CHECK(std::fabs(minX - 0.0f) < 1e-6f);          // origin anchored at the hinge edge
    CHECK(std::fabs(maxX - 2.1f) < 1e-5f);          // extends to the page edge
}

static void testSceneLoader() {
    // A well-formed book with two scenes exercising required + optional fields.
    const std::string good = R"({
        "version": 1,
        "title": "Test Book",
        "scenes": [
            { "name": "One", "objects": [
                { "model": "a.obj", "page": "left",  "position": [0.0, 0.0] },
                { "model": "b.glb", "page": "RIGHT", "position": [0.5, -0.5], "rotation": 90, "scale": 0.5 }
            ]},
            { "name": "Two", "objects": [] }
        ]
    })";

    auto book = parseBook(good);
    CHECK(book.has_value());
    if (book) {
        CHECK(book->title == "Test Book");
        CHECK(book->scenes.size() == 2);
        CHECK(book->scenes[0].name == "One");
        CHECK(book->scenes[0].objects.size() == 2);
        CHECK(book->scenes[1].objects.empty());

        const ObjectPlacement& o0 = book->scenes[0].objects[0];
        CHECK(o0.model == "a.obj");
        CHECK(o0.page == PageSide::Left);
        CHECK(std::fabs(o0.position.x) < 1e-6f && std::fabs(o0.position.y) < 1e-6f);
        CHECK(std::fabs(o0.rotationDeg) < 1e-6f);   // default
        CHECK(std::fabs(o0.scale - 1.0f) < 1e-6f);  // default

        const ObjectPlacement& o1 = book->scenes[0].objects[1];
        CHECK(o1.page == PageSide::Right);          // case-insensitive
        CHECK(std::fabs(o1.rotationDeg - 90.0f) < 1e-6f);
        CHECK(std::fabs(o1.scale - 0.5f) < 1e-6f);
    }

    // A malformed object is skipped, but the rest of the book still parses.
    const std::string partial = R"({
        "version": 1,
        "scenes": [ { "objects": [
            { "model": "ok.obj", "page": "left", "position": [0, 0] },
            { "page": "left", "position": [0, 0] },
            { "model": "noPage.obj", "position": [0, 0] },
            { "model": "badPos.obj", "page": "left", "position": [0] }
        ]}]
    })";
    auto pb = parseBook(partial);
    CHECK(pb.has_value());
    if (pb) {
        CHECK(pb->scenes.size() == 1);
        CHECK(pb->scenes[0].objects.size() == 1); // only the valid object survives
        CHECK(pb->scenes[0].objects[0].model == "ok.obj");
    }

    // Wrong / missing version is rejected outright.
    CHECK(!parseBook(R"({ "version": 2, "scenes": [] })").has_value());
    CHECK(!parseBook(R"({ "scenes": [] })").has_value());
    // Malformed JSON fails gracefully (no throw, no value).
    CHECK(!parseBook("{ this is not json").has_value());
    // Missing scenes array.
    CHECK(!parseBook(R"({ "version": 1 })").has_value());
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
    testScenePlacement();
    testPageTurn();
    testSceneLoader();
    testLoaders();

    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
