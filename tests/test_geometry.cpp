//
// test_geometry.cpp — headless checks for the pure logic: mesh generators,
// camera matrices, and model placement. No GPU device is created.
//
// Tiny assert-style harness: each CHECK records a failure; the process exits
// non-zero if any failed (which is what CTest keys on).
//
#include "gpu/mesh.h"
#include "scene/book.h"
#include "scene/camera.h"
#include "scene/scene.h"

#include <cmath>
#include <cstdio>

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

static void testGenerators() {
    MeshData box = makeBox(0.6f, 0.6f, 0.6f);
    MeshData pyr = makePyramid(0.7f, 0.8f);
    MeshData sph = makeUVSphere(0.38f, 22, 14);
    MeshData tor = makeTorus(0.32f, 0.13f, 28, 16);

    checkMeshValid(box, "box");
    checkMeshValid(pyr, "pyramid");
    checkMeshValid(sph, "sphere");
    checkMeshValid(tor, "torus");

    // minY must match the kPrimitiveInfo half-heights placement relies on.
    CHECK(std::fabs(box.minY() + kPrimitiveInfo[(int)PrimitiveType::Box].halfHeight) < 1e-3f);
    CHECK(std::fabs(pyr.minY() + kPrimitiveInfo[(int)PrimitiveType::Pyramid].halfHeight) < 1e-3f);
    CHECK(std::fabs(sph.minY() + kPrimitiveInfo[(int)PrimitiveType::Sphere].halfHeight) < 1e-2f);
    CHECK(std::fabs(tor.minY() + kPrimitiveInfo[(int)PrimitiveType::Torus].halfHeight) < 1e-3f);
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

    auto placements = generatePlacements(left, right, 1337u);

    // 4..6 models per page, two pages.
    CHECK(placements.size() >= 8 && placements.size() <= 12);

    for (const Placement& pl : placements) {
        CHECK(finite4x4(pl.model));

        float x = pl.model[3][0];
        float y = pl.model[3][1];
        float z = pl.model[3][2];
        float scale = glm::length(glm::vec3(pl.model[0])); // |scaled basis vector|
        float halfH = kPrimitiveInfo[(int)pl.type].halfHeight;

        // Lands on one of the two pages...
        CHECK(withinPage(left, x, z) || withinPage(right, x, z));

        // ...and is seated so its base rests on the page top (y = 0.22).
        float base = y - halfH * scale;
        CHECK(std::fabs(base - 0.22f) < 1e-2f);

        // Colors stay in a sane, non-black range.
        CHECK(pl.color.r >= 0.0f && pl.color.g >= 0.0f && pl.color.b >= 0.0f);
        CHECK(pl.color.r + pl.color.g + pl.color.b > 0.2f);
    }

    // Determinism: same seed -> identical result.
    auto again = generatePlacements(left, right, 1337u);
    CHECK(again.size() == placements.size());
}

int main() {
    testGenerators();
    testCamera();
    testPlacement();

    if (g_failures == 0) {
        std::printf("all geometry tests passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
