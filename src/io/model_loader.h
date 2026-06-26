#pragma once
//
// model_loader.h — load external 3D models (Wavefront OBJ and glTF 2.0) into the
// engine's CPU-side mesh representation.
//
// Like the procedural generators in mesh.h, loading is pure CPU work (no GPU
// device) so it stays unit-testable. A loaded model is a list of textured
// sub-meshes (one per material/primitive) plus the seating info the page
// placement system needs — the same {halfHeight, footprintRadius} contract the
// old built-in primitives exposed, so external models drop straight into the
// existing staging math.
//
#include "gpu/mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace book {

// One material group within a model: geometry plus where its base-color texture
// lives. A texture may be an external file (`imagePath`) or bytes embedded in the
// model (`embeddedImage`, e.g. a glTF buffer-view or data-uri). Both empty means
// untextured — `baseColorFactor` then acts as the flat color.
struct SubMeshData {
    MeshData             mesh;
    std::string          imagePath;
    std::vector<uint8_t> embeddedImage;
    glm::vec3            baseColorFactor{1.0f};
};

// A fully loaded, normalized model. Geometry has been recentered (combined AABB
// center at the local origin) and uniformly scaled to a page-friendly size, with
// the seating numbers derived from the result.
struct ModelData {
    std::vector<SubMeshData> submeshes;
    float halfHeight = 0.0f;      // half the Y extent (seat the base on the page)
    float footprintRadius = 0.0f; // max XZ distance from center (keep on the page)
    bool empty() const { return submeshes.empty(); }
};

// Load a model, dispatching on file extension (.obj / .gltf / .glb,
// case-insensitive). Returns nullopt (and logs a warning) on any failure so the
// caller can skip the file and carry on.
std::optional<ModelData> loadModel(const std::string& path);

} // namespace book
