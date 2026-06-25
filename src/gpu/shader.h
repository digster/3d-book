#pragma once
//
// shader.h — load a build-time-compiled SPIR-V file and turn it into an
// SDL_GPUShader via SDL_shadercross (which transpiles SPIR-V -> MSL on macOS).
//
#include <SDL3/SDL_gpu.h>
#include <SDL3_shadercross/SDL_shadercross.h>

namespace book {

// Load `<exe dir>/shaders/<spvFilename>`, transpile it to the device's native
// shader format, and create an SDL_GPUShader. The resource counts describe the
// bindings the shader declares (we pass them explicitly since we author the
// shaders ourselves). Returns nullptr (and logs) on failure.
SDL_GPUShader* loadShader(SDL_GPUDevice* device,
                          const char* spvFilename,
                          SDL_ShaderCross_ShaderStage stage,
                          Uint32 numUniformBuffers);

} // namespace book
