# ==============================================================================
# 3d-book — Isometric 3D book staging app (SDL3 + SDL_GPU)
# ==============================================================================
# Configure + build:
#   cmake -B build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
#   ./build/3d_book              # interactive
#   ./build/3d_book --frames 120 # headless smoke run (exits after N frames)
#   ctest --test-dir build       # headless geometry/camera/placement unit tests
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(3d-book VERSION 0.1.0 LANGUAGES CXX)

# --- C++ standard ---------------------------------------------------------
# C++17 gives us std::optional, structured bindings and <random> niceties.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# compile_commands.json for clangd / IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---------------------------------------------------------
# SDL3 and SDL_shadercross both ship pkg-config files (`sdl3`, `sdl3-shadercross`).
# We use IMPORTED_TARGET so the cflags/libs propagate as a normal CMake target.
# glm ships a CMake config package via Homebrew, so find_package works for it.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3        REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(SHADERCROSS REQUIRED IMPORTED_TARGET sdl3-shadercross)
find_package(glm REQUIRED)

# The shadercross dylib usually lives in /usr/local/lib, which is NOT on the
# default dyld search path nor in pkg-config's emitted rpath. Bake an rpath to
# its libdir so the binary finds libSDL3_shadercross at runtime.
pkg_get_variable(SHADERCROSS_LIBDIR sdl3-shadercross libdir)

# --- Core library (shared by the app and the tests) -----------------------
# Everything except main() lives here so the headless test target can link the
# pure-logic pieces (mesh generators, camera math, placement) without dragging
# in the SDL callback entry point.
add_library(book_core STATIC
    src/gpu/mesh.cpp
    src/gpu/shader.cpp
    src/gpu/texture.cpp
    src/io/model_loader.cpp
    src/io/scene_loader.cpp
    src/scene/camera.cpp
    src/scene/book.cpp
    src/scene/scene.cpp
    src/render/renderer.cpp
    src/third_party_impl.cpp
)

target_include_directories(book_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(book_core PUBLIC
    PkgConfig::SDL3
    PkgConfig::SHADERCROSS
    glm::glm
)

# GLM_FORCE_DEPTH_ZERO_TO_ONE: SDL_GPU (like Vulkan/Metal/D3D) uses a [0,1] clip
# depth range, not OpenGL's [-1,1]. This makes glm::ortho produce the right Z.
# GLM_ENABLE_EXPERIMENTAL: needed for some glm/gtx helpers.
target_compile_definitions(book_core PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
)

if(SHADERCROSS_LIBDIR)
    target_link_options(book_core PUBLIC "-Wl,-rpath,${SHADERCROSS_LIBDIR}")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(book_core PUBLIC -Wall -Wextra)
endif()

# The vendored single-header libraries (compiled in third_party_impl.cpp) are not
# our code and trip -Wall/-Wextra; silence just that one TU so our own warnings
# stay visible and meaningful.
set_source_files_properties(src/third_party_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")

# --- Application executable ------------------------------------------------
add_executable(3d_book src/main.cpp)
target_link_libraries(3d_book PRIVATE book_core)

# --- Shader compilation (GLSL -> SPIR-V via glslangValidator) --------------
# SDL_GPU on Metal cannot consume GLSL/SPIR-V directly, but SDL_shadercross
# transpiles SPIR-V -> MSL at load time. glslangValidator turns our GLSL into
# the SPIR-V the runtime loader hands to shadercross. No DXC required.
find_program(GLSLANG glslangValidator HINTS /opt/homebrew/bin REQUIRED)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
file(GLOB SHADER_SOURCES
    ${CMAKE_SOURCE_DIR}/shaders/*.vert
    ${CMAKE_SOURCE_DIR}/shaders/*.frag
)

set(SHADER_OUTPUTS "")
foreach(SHADER_SOURCE ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLANG} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling shader: ${SHADER_NAME} -> ${SHADER_NAME}.spv"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

# Compiled shaders land in build/shaders/, right next to the build/3d_book
# binary, so the runtime resolves them via SDL_GetBasePath() + "shaders/...".
add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(3d_book shaders)

# --- Model assets ----------------------------------------------------------
# External 3D models are loaded at runtime from a `models/` directory next to
# the executable (resolved via SDL_GetBasePath() + "models/"). Mirror the shader
# layout by copying the source-tree `models/` into the build dir on every build,
# so dropping a new file in models/ and rebuilding makes it appear in the app.
add_custom_target(model_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/models ${CMAKE_BINARY_DIR}/models
    COMMENT "Copying model assets -> build/models/"
)
add_dependencies(3d_book model_assets)

# --- Scene data ------------------------------------------------------------
# The book of scenes (book.json) is loaded at runtime from next to the binary
# (SDL_GetBasePath() + "book.json"). Copy it on every build like the models so an
# edit to the source book.json shows up in the app after a rebuild.
add_custom_target(scene_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/book.json ${CMAKE_BINARY_DIR}/book.json
    COMMENT "Copying scene data -> build/book.json"
)
add_dependencies(3d_book scene_assets)

# --- Tests -----------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
