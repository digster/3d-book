//
// scene_loader.cpp — parse `book.json` into the in-memory BookData the Scene
// turns into render instances. Parsing is defensive by design: a bad file should
// degrade to "no scenes" (an empty book) rather than crash, and a single bad
// object should be skipped rather than sinking the whole spread.
//
#include "io/scene_loader.h"

#include <SDL3/SDL.h>

// nlohmann/json is a large vendored single header; isolate its include here (the
// only TU that parses JSON) and silence any third-party warnings so our own
// -Wall/-Wextra output stays meaningful.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
#include "json.hpp"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace book {

using json = nlohmann::json;

namespace {

// Lower-case a copy of `s` for case-insensitive keyword matching.
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Read a 2-component (u, v) array. Returns false if it isn't a 2-number array.
bool parsePosition(const json& j, glm::vec2& out) {
    if (!j.is_array() || j.size() != 2) return false;
    if (!j[0].is_number() || !j[1].is_number()) return false;
    out = glm::vec2(j[0].get<float>(), j[1].get<float>());
    return true;
}

// Parse one object entry. Returns nullopt (and logs) when a required field is
// missing or malformed, so the caller can skip just this object.
std::optional<ObjectPlacement> parseObject(const json& j, size_t sceneIdx, size_t objIdx) {
    if (!j.is_object()) {
        SDL_Log("book.json: scene %zu object %zu is not an object — skipping", sceneIdx, objIdx);
        return std::nullopt;
    }

    ObjectPlacement obj;

    // model (required, non-empty string).
    auto modelIt = j.find("model");
    if (modelIt == j.end() || !modelIt->is_string() || modelIt->get<std::string>().empty()) {
        SDL_Log("book.json: scene %zu object %zu missing/invalid 'model' — skipping", sceneIdx, objIdx);
        return std::nullopt;
    }
    obj.model = modelIt->get<std::string>();

    // page (required, "left" | "right").
    auto pageIt = j.find("page");
    if (pageIt == j.end() || !pageIt->is_string()) {
        SDL_Log("book.json: object '%s' missing/invalid 'page' — skipping", obj.model.c_str());
        return std::nullopt;
    }
    const std::string page = toLower(pageIt->get<std::string>());
    if (page == "left") obj.page = PageSide::Left;
    else if (page == "right") obj.page = PageSide::Right;
    else {
        SDL_Log("book.json: object '%s' has unknown page '%s' (want left/right) — skipping",
                obj.model.c_str(), page.c_str());
        return std::nullopt;
    }

    // position (required, [u, v]).
    auto posIt = j.find("position");
    if (posIt == j.end() || !parsePosition(*posIt, obj.position)) {
        SDL_Log("book.json: object '%s' missing/invalid 'position' [u,v] — skipping", obj.model.c_str());
        return std::nullopt;
    }

    // rotation / scale (optional, with defaults).
    obj.rotationDeg = j.value("rotation", 0.0f);
    obj.scale = j.value("scale", 1.0f);

    return obj;
}

} // namespace

std::optional<BookData> parseBook(const std::string& text) {
    json root = json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        SDL_Log("book.json: malformed JSON — ignoring");
        return std::nullopt;
    }
    if (!root.is_object()) {
        SDL_Log("book.json: top level must be an object");
        return std::nullopt;
    }

    // Version gate: refuse formats we don't understand rather than guess.
    const int version = root.value("version", -1);
    if (version != kBookSchemaVersion) {
        SDL_Log("book.json: unsupported version %d (expected %d)", version, kBookSchemaVersion);
        return std::nullopt;
    }

    BookData book;
    book.title = root.value("title", std::string{});

    auto scenesIt = root.find("scenes");
    if (scenesIt == root.end() || !scenesIt->is_array()) {
        SDL_Log("book.json: missing or non-array 'scenes'");
        return std::nullopt;
    }

    for (size_t si = 0; si < scenesIt->size(); ++si) {
        const json& js = (*scenesIt)[si];
        if (!js.is_object()) {
            SDL_Log("book.json: scene %zu is not an object — skipping", si);
            continue;
        }

        SceneData scene;
        scene.name = js.value("name", std::string{});

        auto objsIt = js.find("objects");
        if (objsIt != js.end() && objsIt->is_array()) {
            for (size_t oi = 0; oi < objsIt->size(); ++oi) {
                if (auto obj = parseObject((*objsIt)[oi], si, oi)) {
                    scene.objects.push_back(std::move(*obj));
                }
            }
        }
        book.scenes.push_back(std::move(scene));
    }

    if (book.scenes.empty()) {
        SDL_Log("book.json: parsed 0 scenes");
    }
    return book;
}

std::optional<BookData> loadBook(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        SDL_Log("book.json not found at %s", path.c_str());
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseBook(ss.str());
}

} // namespace book
