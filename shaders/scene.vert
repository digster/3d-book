#version 450
//
// scene.vert — the only vertex shader in the app.
//
// Authored in GLSL, compiled to SPIR-V at build time by glslangValidator (-V,
// Vulkan semantics), then transpiled to Metal at load time by SDL_shadercross.
//
// Resource binding note: SDL_GPU maps *vertex-stage uniform buffers* to
// descriptor set 1 (HLSL "space1"). Declaring the block anywhere else makes the
// runtime bind it as the wrong resource type, so the `set = 1` here is load
// bearing, not decoration.

// Per-draw uniform block. The renderer fills this once per object and pushes it
// with SDL_PushGPUVertexUniformData(cmd, slot 0, ...).
layout(set = 1, binding = 0) uniform UBO {
    mat4 mvp;    // viewProj * model — clip-space transform
    mat4 model;  // model matrix — used to rotate the normal into world space
    vec4 color;  // object base color (rgb); a is unused
} ubo;

// Interleaved vertex attributes (see Vertex in src/gpu/mesh.h):
//   location 0 = position, location 1 = normal, location 2 = texcoord.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

// Outputs handed to the fragment stage (interpolated across the triangle).
layout(location = 0) out vec3 vNormal;  // world-space normal
layout(location = 1) out vec3 vColor;   // object color, flat per object
layout(location = 2) out vec2 vUV;      // texture coordinate

void main() {
    gl_Position = ubo.mvp * vec4(inPos, 1.0);

    // We only ever apply uniform scale + rotation + translation to objects, so
    // the upper-left 3x3 of the model matrix rotates normals correctly without
    // needing a separate inverse-transpose normal matrix.
    vNormal = mat3(ubo.model) * inNormal;
    vColor  = ubo.color.rgb;
    vUV     = inUV;
}
