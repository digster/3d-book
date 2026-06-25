#version 450
//
// scene.frag — simple opaque shading for every object in the scene.
//
// A single hard-coded directional light plus a constant ambient term. That is
// enough to give the boxes and primitives readable 3D form under the isometric
// camera without any material/texture machinery. No uniforms here, so the
// fragment stage reports zero resources to SDL_GPU.

layout(location = 0) in vec3 vNormal;  // world-space normal (not yet normalized)
layout(location = 1) in vec3 vColor;   // object base color

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNormal);

    // Light comes from the upper-front-right — chosen so the isometric faces
    // each catch a different amount of light and the forms read clearly.
    vec3 L = normalize(vec3(0.5, 1.0, 0.6));

    float diffuse = max(dot(N, L), 0.0);
    float ambient = 0.35;                       // floor so shadowed faces aren't black
    float light   = ambient + (1.0 - ambient) * diffuse;

    outColor = vec4(vColor * light, 1.0);
}
