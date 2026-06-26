//
// third_party_impl.cpp — the single translation unit that compiles the bodies
// of our vendored header-only libraries.
//
// Each of these libraries ships as one header that holds both the declarations
// (always visible) and the implementation (only emitted where the matching
// ..._IMPLEMENTATION macro is defined). Defining them all here, exactly once,
// keeps the parsing code out of every other TU and avoids duplicate symbols at
// link time. Everywhere else just #includes the headers normally.
//

// cgltf is C99 but is written to compile cleanly as C++; keep it first so its
// own includes are pulled in before anything else fiddles with macros.
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
// We only ever feed stb_image RGBA8 output, and we don't need HDR/linear paths.
#include "stb_image.h"
