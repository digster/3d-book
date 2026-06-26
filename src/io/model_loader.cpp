//
// model_loader.cpp — OBJ (tinyobjloader) + glTF (cgltf) loading, plus the shared
// normal-fixup and normalization steps that make any model placeable on a page.
//
#include "io/model_loader.h"

#include "tiny_obj_loader.h"
#include "cgltf.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <tuple>

namespace book {

namespace {

// Largest local half-extent a normalized model is scaled to. Roughly matches the
// footprint of the old built-in primitives so seating/insetting stays sensible
// and a handful of models share a page without crowding too much.
constexpr float kTargetHalfExtent = 0.4f;

// Directory part of a path, including the trailing separator ("" if none).
std::string dirOf(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
}

// Lowercased copy (for case-insensitive extension matching).
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string& s, const char* suffix) {
    const std::string suf = suffix;
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// Resolve a (possibly relative) asset reference against the model's directory.
std::string resolveRelative(const std::string& dir, std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/'); // normalize Windows paths
    if (!name.empty() && name[0] == '/') return name;   // already absolute
    return dir + name;
}

// Minimal base64 decoder for glTF "data:...;base64,<payload>" image URIs.
std::vector<uint8_t> base64Decode(const char* in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1; // padding '=' or whitespace/terminator
    };
    std::vector<uint8_t> out;
    int buf = 0, bits = 0;
    for (const char* p = in; *p; ++p) {
        int v = val(*p);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// Recompute per-vertex normals as the normalized sum of adjacent face normals.
// Used when a loaded model omits normals (smooth shading falls out of vertex
// sharing; unshared vertices end up flat-shaded, which is correct either way).
void computeSmoothNormals(MeshData& m) {
    for (Vertex& v : m.vertices) v.normal = glm::vec3(0.0f);
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        glm::vec3 fn = glm::cross(m.vertices[b].pos - m.vertices[a].pos,
                                  m.vertices[c].pos - m.vertices[a].pos);
        m.vertices[a].normal += fn;
        m.vertices[b].normal += fn;
        m.vertices[c].normal += fn;
    }
    for (Vertex& v : m.vertices) {
        float len = glm::length(v.normal);
        v.normal = (len > 1e-8f) ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

// Recenter every sub-mesh's geometry on the combined AABB center and uniformly
// scale it so the largest half-extent becomes kTargetHalfExtent, then derive the
// seating numbers (halfHeight / footprintRadius) from the result. This is what
// lets an arbitrary external model slot into the page-placement math built for
// the old fixed-size primitives.
void normalizeModel(ModelData& model) {
    constexpr float inf = std::numeric_limits<float>::max();
    glm::vec3 lo(inf), hi(-inf);
    for (const SubMeshData& sm : model.submeshes)
        for (const Vertex& v : sm.mesh.vertices) {
            lo = glm::min(lo, v.pos);
            hi = glm::max(hi, v.pos);
        }
    if (lo.x > hi.x) return; // no vertices

    glm::vec3 center = (lo + hi) * 0.5f;
    glm::vec3 half = (hi - lo) * 0.5f;
    float maxHalf = std::max({half.x, half.y, half.z});
    float scale = (maxHalf > 1e-8f) ? (kTargetHalfExtent / maxHalf) : 1.0f;

    float footprint = 0.0f;
    for (SubMeshData& sm : model.submeshes)
        for (Vertex& v : sm.mesh.vertices) {
            v.pos = (v.pos - center) * scale;
            footprint = std::max(footprint, std::sqrt(v.pos.x * v.pos.x + v.pos.z * v.pos.z));
        }

    model.halfHeight = half.y * scale;
    model.footprintRadius = footprint;
}

// --- OBJ ------------------------------------------------------------------
std::optional<ModelData> loadObj(const std::string& path) {
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;          // fan-triangulate any n-gons for us
    config.mtl_search_path = dirOf(path); // find the .mtl next to the .obj

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config)) {
        SDL_Log("OBJ load failed (%s): %s", path.c_str(), reader.Error().c_str());
        return std::nullopt;
    }
    if (!reader.Warning().empty())
        SDL_Log("OBJ warning (%s): %s", path.c_str(), reader.Warning().c_str());

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();
    const std::vector<tinyobj::material_t>& materials = reader.GetMaterials();

    // Group triangles by material id (-1 == no material) so each group becomes a
    // sub-mesh with its own texture. Dedup by the (v, vn, vt) index triple.
    struct Group {
        MeshData mesh;
        std::map<std::tuple<int, int, int>, uint32_t> dedup;
        bool missingNormals = false;
    };
    std::map<int, Group> groups;

    for (const tinyobj::shape_t& shape : shapes) {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            int fv = shape.mesh.num_face_vertices[f]; // 3 after triangulation
            int matId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;
            Group& g = groups[matId];

            for (int v = 0; v < fv; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                auto key = std::make_tuple(idx.vertex_index, idx.normal_index, idx.texcoord_index);
                auto it = g.dedup.find(key);
                if (it != g.dedup.end()) {
                    g.mesh.indices.push_back(it->second);
                    continue;
                }

                Vertex vert{};
                vert.pos = {attrib.vertices[3 * idx.vertex_index + 0],
                            attrib.vertices[3 * idx.vertex_index + 1],
                            attrib.vertices[3 * idx.vertex_index + 2]};
                if (idx.normal_index >= 0) {
                    vert.normal = {attrib.normals[3 * idx.normal_index + 0],
                                   attrib.normals[3 * idx.normal_index + 1],
                                   attrib.normals[3 * idx.normal_index + 2]};
                } else {
                    g.missingNormals = true;
                }
                if (idx.texcoord_index >= 0) {
                    // OBJ's V origin is bottom-left; flip to match the top-left
                    // convention used when we upload the texture image.
                    vert.uv = {attrib.texcoords[2 * idx.texcoord_index + 0],
                               1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]};
                }

                uint32_t vi = static_cast<uint32_t>(g.mesh.vertices.size());
                g.mesh.vertices.push_back(vert);
                g.dedup.emplace(key, vi);
                g.mesh.indices.push_back(vi);
            }
            indexOffset += fv;
        }
    }

    ModelData model;
    for (auto& [matId, g] : groups) {
        if (g.mesh.indices.empty()) continue;
        if (g.missingNormals) computeSmoothNormals(g.mesh);

        SubMeshData sm;
        sm.mesh = std::move(g.mesh);
        if (matId >= 0 && matId < static_cast<int>(materials.size())) {
            const tinyobj::material_t& mat = materials[matId];
            sm.baseColorFactor = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]};
            if (!mat.diffuse_texname.empty())
                sm.imagePath = resolveRelative(dirOf(path), mat.diffuse_texname);
        }
        model.submeshes.push_back(std::move(sm));
    }

    if (model.empty()) {
        SDL_Log("OBJ has no triangles: %s", path.c_str());
        return std::nullopt;
    }
    normalizeModel(model);
    return model;
}

// --- glTF -----------------------------------------------------------------
std::optional<ModelData> loadGltf(const std::string& path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        SDL_Log("glTF parse failed: %s", path.c_str());
        return std::nullopt;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        SDL_Log("glTF buffer load failed: %s", path.c_str());
        cgltf_free(data);
        return std::nullopt;
    }

    const std::string dir = dirOf(path);
    ModelData model;

    // Walk every node; cgltf_node_transform_world bakes the full ancestor chain,
    // so flattening the hierarchy here is just "for each node with a mesh".
    for (size_t n = 0; n < data->nodes_count; ++n) {
        const cgltf_node* node = &data->nodes[n];
        if (!node->mesh) continue;

        cgltf_float wm[16];
        cgltf_node_transform_world(node, wm);   // column-major, matches glm
        glm::mat4 world = glm::make_mat4(wm);
        glm::mat3 normalMat = glm::mat3(world);  // rotation/scale part for normals

        for (size_t p = 0; p < node->mesh->primitives_count; ++p) {
            const cgltf_primitive* prim = &node->mesh->primitives[p];
            if (prim->type != cgltf_primitive_type_triangles) continue;

            const cgltf_accessor* posA = nullptr;
            const cgltf_accessor* nrmA = nullptr;
            const cgltf_accessor* uvA = nullptr;
            for (size_t a = 0; a < prim->attributes_count; ++a) {
                const cgltf_attribute* at = &prim->attributes[a];
                if (at->type == cgltf_attribute_type_position) posA = at->data;
                else if (at->type == cgltf_attribute_type_normal) nrmA = at->data;
                else if (at->type == cgltf_attribute_type_texcoord && at->index == 0) uvA = at->data;
            }
            if (!posA) continue;

            SubMeshData sm;
            const size_t vcount = posA->count;
            sm.mesh.vertices.reserve(vcount);
            for (size_t i = 0; i < vcount; ++i) {
                Vertex vert{};
                cgltf_float tmp[4] = {0, 0, 0, 0};

                cgltf_accessor_read_float(posA, i, tmp, 3);
                vert.pos = glm::vec3(world * glm::vec4(tmp[0], tmp[1], tmp[2], 1.0f));

                if (nrmA) {
                    cgltf_accessor_read_float(nrmA, i, tmp, 3);
                    glm::vec3 nn = normalMat * glm::vec3(tmp[0], tmp[1], tmp[2]);
                    float len = glm::length(nn);
                    vert.normal = (len > 1e-8f) ? nn / len : glm::vec3(0, 1, 0);
                }
                if (uvA) {
                    cgltf_accessor_read_float(uvA, i, tmp, 2);
                    vert.uv = glm::vec2(tmp[0], tmp[1]); // glTF UV origin is top-left
                }
                sm.mesh.vertices.push_back(vert);
            }

            if (prim->indices) {
                const size_t icount = prim->indices->count;
                sm.mesh.indices.reserve(icount);
                for (size_t k = 0; k < icount; ++k)
                    sm.mesh.indices.push_back(
                        static_cast<uint32_t>(cgltf_accessor_read_index(prim->indices, k)));
            } else {
                sm.mesh.indices.reserve(vcount);
                for (uint32_t k = 0; k < static_cast<uint32_t>(vcount); ++k)
                    sm.mesh.indices.push_back(k);
            }

            if (!nrmA) computeSmoothNormals(sm.mesh);

            // Base color: factor (tint) + optional base-color texture.
            if (prim->material && prim->material->has_pbr_metallic_roughness) {
                const cgltf_pbr_metallic_roughness& pbr = prim->material->pbr_metallic_roughness;
                sm.baseColorFactor = {pbr.base_color_factor[0], pbr.base_color_factor[1],
                                      pbr.base_color_factor[2]};
                const cgltf_texture* tex = pbr.base_color_texture.texture;
                if (tex && tex->image) {
                    const cgltf_image* img = tex->image;
                    if (img->buffer_view) { // embedded (always the case for .glb)
                        const cgltf_buffer_view* bv = img->buffer_view;
                        const uint8_t* base =
                            static_cast<const uint8_t*>(bv->buffer->data) + bv->offset;
                        sm.embeddedImage.assign(base, base + bv->size);
                    } else if (img->uri) {
                        if (std::strncmp(img->uri, "data:", 5) == 0) {
                            const char* comma = std::strchr(img->uri, ',');
                            if (comma) sm.embeddedImage = base64Decode(comma + 1);
                        } else {
                            std::string uri = img->uri;
                            cgltf_size newlen = cgltf_decode_uri(&uri[0]); // percent-decode
                            uri.resize(newlen);
                            sm.imagePath = resolveRelative(dir, uri);
                        }
                    }
                }
            }

            model.submeshes.push_back(std::move(sm));
        }
    }

    cgltf_free(data);
    if (model.empty()) {
        SDL_Log("glTF has no triangle geometry: %s", path.c_str());
        return std::nullopt;
    }
    normalizeModel(model);
    return model;
}

} // namespace

std::optional<ModelData> loadModel(const std::string& path) {
    const std::string lower = toLower(path);
    if (endsWith(lower, ".obj")) return loadObj(path);
    if (endsWith(lower, ".gltf") || endsWith(lower, ".glb")) return loadGltf(path);
    SDL_Log("Unsupported model format (skipping): %s", path.c_str());
    return std::nullopt;
}

} // namespace book
