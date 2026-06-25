//
// shader.cpp — SPIR-V file -> SDL_GPUShader via SDL_shadercross.
//
#include "gpu/shader.h"
#include "gpu/gpu_common.h"

#include <SDL3/SDL.h>

#include <string>

namespace book {

SDL_GPUShader* loadShader(SDL_GPUDevice* device,
                          const char* spvFilename,
                          SDL_ShaderCross_ShaderStage stage,
                          Uint32 numUniformBuffers) {
    // Compiled shaders live in `shaders/` next to the executable (the build
    // copies them there). SDL_GetBasePath() makes this independent of CWD.
    const char* base = SDL_GetBasePath();
    std::string path = std::string(base ? base : "") + "shaders/" + spvFilename;

    size_t size = 0;
    void* bytes = SDL_LoadFile(path.c_str(), &size);
    if (!bytes) {
        SDL_Log("SDL_LoadFile(%s): %s", path.c_str(), SDL_GetError());
        return nullptr;
    }

    // Describe the SPIR-V we just loaded...
    SDL_ShaderCross_SPIRV_Info info{};
    info.bytecode = static_cast<const Uint8*>(bytes);
    info.bytecode_size = size;
    info.entrypoint = "main";
    info.shader_stage = stage;
    info.props = 0;

    // ...and the resources it binds. Everything but uniform buffers is zero for
    // this app (no textures/samplers/storage).
    SDL_ShaderCross_GraphicsShaderResourceInfo resources{};
    resources.num_samplers = 0;
    resources.num_storage_textures = 0;
    resources.num_storage_buffers = 0;
    resources.num_uniform_buffers = numUniformBuffers;

    SDL_GPUShader* shader =
        SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(device, &info, &resources, 0);
    SDL_free(bytes); // the shader has consumed the bytecode now

    if (!shader) {
        SDL_Log("SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(%s): %s",
                spvFilename, SDL_GetError());
    }
    return shader;
}

} // namespace book
