#version 450
//
// scene.frag — base-color-textured shading for every object in the scene.
//
// A single hard-coded directional light plus a constant ambient term gives the
// shapes readable 3D form under the isometric camera. The object's base color is
// the per-object uniform color multiplied by a sampled base-color texture; for
// untextured objects (the book parts, models with no texture) a 1x1 white
// texture is bound, so the multiply leaves the per-object color untouched.
//
// Resource binding note: SDL_GPU maps *fragment-stage sampled textures* to
// descriptor set 2 (the vertex uniform block lives in set 1 — see scene.vert).
// Declaring the sampler anywhere else makes the runtime bind it as the wrong
// resource, so `set = 2` here is load bearing.

layout(set = 2, binding = 0) uniform sampler2D uBaseColor;

layout(location = 0) in vec3 vNormal;  // world-space normal (not yet normalized)
layout(location = 1) in vec4 vColor;   // object base color / tint (rgb) + opacity (a)
layout(location = 2) in vec2 vUV;      // texture coordinate

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNormal);

    // External models can have arbitrary face winding, and we render with back
    // faces enabled so none of their surfaces vanish. Flip the normal toward the
    // viewer on back faces so those surfaces are lit instead of appearing dark.
    if (!gl_FrontFacing) N = -N;

    // Light comes from the upper-front-right — chosen so the isometric faces
    // each catch a different amount of light and the forms read clearly.
    vec3 L = normalize(vec3(0.5, 1.0, 0.6));

    float diffuse = max(dot(N, L), 0.0);
    float ambient = 0.35;                       // floor so shadowed faces aren't black
    float light   = ambient + (1.0 - ambient) * diffuse;

    vec4 base = texture(uBaseColor, vUV);       // white (1,1,1,1) when untextured
    // Alpha carries the per-object opacity (vColor.a) for the page-turn cross-
    // fade; it is 1.0 for everything outside a transition.
    outColor = vec4(base.rgb * vColor.rgb * light, base.a * vColor.a);
}
