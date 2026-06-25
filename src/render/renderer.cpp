//
// renderer.cpp — SDL_GPU device/pipeline setup and the per-frame draw loop.
//
#include "render/renderer.h"

#include "gpu/gpu_common.h"
#include "gpu/mesh.h"
#include "gpu/shader.h"

#include <SDL3_shadercross/SDL_shadercross.h>
#include <glm/glm.hpp>

#include <cstring>

namespace book {

namespace {
constexpr SDL_GPUTextureFormat kDepthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

// Per-draw uniform block. Layout matches `UBO` in shaders/scene.vert: two mat4s
// followed by a vec4, all naturally 16-byte aligned (std140-compatible).
struct VertexUBO {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::vec4 color;
};

// Pick the SDL surface pixel format that matches a GPU color format, so a
// downloaded texture's bytes are interpreted correctly when saved.
SDL_PixelFormat surfaceFormatFor(SDL_GPUTextureFormat gpuFormat) {
    switch (gpuFormat) {
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return SDL_PIXELFORMAT_BGRA32;
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return SDL_PIXELFORMAT_RGBA32;
        default:                                   return SDL_PIXELFORMAT_RGBA32;
    }
}
} // namespace

bool Renderer::init(const char* title, int width, int height) {
    // SDL_shadercross transpiles our SPIR-V to the device-native format at load
    // time; it must be initialized before any shader is created.
    if (!SDL_ShaderCross_Init()) return fail("SDL_ShaderCross_Init");

    // Advertise every shader format we could supply so this stays valid if moved
    // to another platform; SDL picks whichever the backend wants (MSL on Metal).
    SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL |
                                  SDL_GPU_SHADERFORMAT_METALLIB | SDL_GPU_SHADERFORMAT_DXIL |
                                  SDL_GPU_SHADERFORMAT_DXBC;
    device_ = SDL_CreateGPUDevice(formats, /*debug_mode=*/true, /*name=*/nullptr);
    if (!device_) return fail("SDL_CreateGPUDevice");

    window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
    if (!window_) return fail("SDL_CreateWindow");

    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        return fail("SDL_ClaimWindowForGPUDevice");
    }

    return createPipeline();
}

bool Renderer::createPipeline() {
    // One uniform buffer in the vertex stage (the MVP/model/color block); none
    // in the fragment stage.
    SDL_GPUShader* vs = loadShader(device_, "scene.vert.spv",
                                   SDL_SHADERCROSS_SHADERSTAGE_VERTEX, /*ubos=*/1);
    if (!vs) return false;
    SDL_GPUShader* fs = loadShader(device_, "scene.frag.spv",
                                   SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, /*ubos=*/0);
    if (!fs) {
        SDL_ReleaseGPUShader(device_, vs);
        return false;
    }

    // Vertex layout: one interleaved buffer of {vec3 pos; vec3 normal}.
    SDL_GPUVertexBufferDescription vbDesc{};
    vbDesc.slot = 0;
    vbDesc.pitch = sizeof(Vertex);
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbDesc.instance_step_rate = 0;

    SDL_GPUVertexAttribute attrs[2]{};
    attrs[0].location = 0;
    attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attrs[0].offset = offsetof(Vertex, pos);
    attrs[1].location = 1;
    attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attrs[1].offset = offsetof(Vertex, normal);

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device_, window_);

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pci.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.vertex_input_state.num_vertex_attributes = 2;

    // Cull back faces; generators wind front faces counter-clockwise.
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

    // Standard "closer fragments win" depth testing.
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;

    pci.target_info.color_target_descriptions = &colorTarget;
    pci.target_info.num_color_targets = 1;
    pci.target_info.has_depth_stencil_target = true;
    pci.target_info.depth_stencil_format = kDepthFormat;

    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pci);

    // The pipeline owns compiled copies of the stages now; release the handles.
    SDL_ReleaseGPUShader(device_, vs);
    SDL_ReleaseGPUShader(device_, fs);

    if (!pipeline_) return fail("SDL_CreateGPUGraphicsPipeline");
    return true;
}

bool Renderer::ensureDepthTexture(Uint32 width, Uint32 height) {
    // (Re)create the depth target only when the size actually changes — this is
    // what keeps depth testing correct across window resizes.
    if (depthTexture_ && depthW_ == width && depthH_ == height) return true;

    if (depthTexture_) {
        SDL_ReleaseGPUTexture(device_, depthTexture_);
        depthTexture_ = nullptr;
    }

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = kDepthFormat;
    tci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    tci.width = width;
    tci.height = height;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    depthTexture_ = SDL_CreateGPUTexture(device_, &tci);
    if (!depthTexture_) return fail("SDL_CreateGPUTexture(depth)");
    depthW_ = width;
    depthH_ = height;
    return true;
}

void Renderer::recordScene(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* color,
                           SDL_GPUTexture* depth, const Scene& scene,
                           const IsometricCamera& camera) {
    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = color;
    colorInfo.clear_color = SDL_FColor{0.10f, 0.11f, 0.14f, 1.0f};
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depthInfo{};
    depthInfo.texture = depth;
    depthInfo.clear_depth = 1.0f;
    depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    depthInfo.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    glm::mat4 viewProj = camera.viewProj();

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorInfo, 1, &depthInfo);
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);

    for (const Instance& inst : scene.instances()) {
        if (!inst.mesh || inst.mesh->indexCount() == 0) continue;

        VertexUBO ubo;
        ubo.mvp = viewProj * inst.model;
        ubo.model = inst.model;
        ubo.color = glm::vec4(inst.color, 1.0f);
        SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(ubo));

        SDL_GPUBufferBinding vb{};
        vb.buffer = inst.mesh->vertexBuffer();
        vb.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

        SDL_GPUBufferBinding ib{};
        ib.buffer = inst.mesh->indexBuffer();
        ib.offset = 0;
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(pass, inst.mesh->indexCount(), 1, 0, 0, 0);
    }

    SDL_EndGPURenderPass(pass);
}

bool Renderer::draw(const Scene& scene, const IsometricCamera& camera) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) return fail("SDL_AcquireGPUCommandBuffer");

    SDL_GPUTexture* swap = nullptr;
    Uint32 sw = 0, sh = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swap, &sw, &sh)) {
        return fail("SDL_WaitAndAcquireGPUSwapchainTexture");
    }

    // No swapchain image (minimized/occluded): nothing to draw, but we must
    // still submit the command buffer we acquired.
    if (!swap) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return true;
    }

    if (!ensureDepthTexture(sw, sh)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return false;
    }

    recordScene(cmd, swap, depthTexture_, scene, camera);

    if (!SDL_SubmitGPUCommandBuffer(cmd)) return fail("SDL_SubmitGPUCommandBuffer");
    return true;
}

bool Renderer::saveScreenshot(const Scene& scene, IsometricCamera camera, const char* path,
                              int width, int height) {
    const Uint32 w = static_cast<Uint32>(width);
    const Uint32 h = static_cast<Uint32>(height);
    camera.setAspect(static_cast<float>(w) / static_cast<float>(h));

    // The offscreen color target must use the same format the pipeline was built
    // for (the swapchain format), or the render pass is incompatible.
    const SDL_GPUTextureFormat colorFormat = SDL_GetGPUSwapchainTextureFormat(device_, window_);

    SDL_GPUTextureCreateInfo cci{};
    cci.type = SDL_GPU_TEXTURETYPE_2D;
    cci.format = colorFormat;
    cci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    cci.width = w;
    cci.height = h;
    cci.layer_count_or_depth = 1;
    cci.num_levels = 1;
    cci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* colorTex = SDL_CreateGPUTexture(device_, &cci);
    if (!colorTex) return fail("SDL_CreateGPUTexture(screenshot color)");

    SDL_GPUTextureCreateInfo dci{};
    dci.type = SDL_GPU_TEXTURETYPE_2D;
    dci.format = kDepthFormat;
    dci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    dci.width = w;
    dci.height = h;
    dci.layer_count_or_depth = 1;
    dci.num_levels = 1;
    dci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* depthTex = SDL_CreateGPUTexture(device_, &dci);
    if (!depthTex) {
        SDL_ReleaseGPUTexture(device_, colorTex);
        return fail("SDL_CreateGPUTexture(screenshot depth)");
    }

    const Uint32 byteCount = w * h * 4;
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbci.size = byteCount;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tbci);
    if (!transfer) {
        SDL_ReleaseGPUTexture(device_, depthTex);
        SDL_ReleaseGPUTexture(device_, colorTex);
        return fail("SDL_CreateGPUTransferBuffer(screenshot)");
    }

    // Render the scene, then download the color texture into the transfer buffer.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    recordScene(cmd, colorTex, depthTex, scene, camera);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region{};
    region.texture = colorTex;
    region.w = w;
    region.h = h;
    region.d = 1;
    SDL_GPUTextureTransferInfo tinfo{};
    tinfo.transfer_buffer = transfer;
    tinfo.offset = 0;
    tinfo.pixels_per_row = w;
    tinfo.rows_per_layer = h;
    SDL_DownloadFromGPUTexture(copy, &region, &tinfo);
    SDL_EndGPUCopyPass(copy);

    // Submit + block until the GPU has finished, so the bytes are ready to read.
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(device_, true, &fence, 1);
        SDL_ReleaseGPUFence(device_, fence);
    }

    bool ok = false;
    void* pixels = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (pixels) {
        SDL_Surface* surf = SDL_CreateSurface(static_cast<int>(w), static_cast<int>(h),
                                              surfaceFormatFor(colorFormat));
        if (surf) {
            for (Uint32 y = 0; y < h; ++y) {
                std::memcpy(static_cast<Uint8*>(surf->pixels) + y * surf->pitch,
                            static_cast<Uint8*>(pixels) + y * w * 4, w * 4);
            }
            ok = SDL_SaveBMP(surf, path);
            SDL_DestroySurface(surf);
        }
        SDL_UnmapGPUTransferBuffer(device_, transfer);
    }

    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    SDL_ReleaseGPUTexture(device_, depthTex);
    SDL_ReleaseGPUTexture(device_, colorTex);

    if (!ok) return fail("SDL_SaveBMP");
    SDL_Log("screenshot saved: %s (%ux%u)", path, w, h);
    return true;
}

void Renderer::destroy() {
    if (device_) {
        if (depthTexture_) SDL_ReleaseGPUTexture(device_, depthTexture_);
        if (pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        if (window_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_ShaderCross_Quit();
}

} // namespace book
