//
// texture.cpp — image decode (stb_image) + GPU texture upload.
//
#include "gpu/texture.h"
#include "gpu/gpu_common.h"

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace book {

namespace {
// Number of mip levels for a width x height texture: the full chain down to 1x1.
Uint32 mipLevelsFor(int width, int height) {
    int maxDim = std::max(width, height);
    Uint32 levels = 1;
    while (maxDim > 1) {
        maxDim >>= 1;
        ++levels;
    }
    return levels;
}

// Copy a decoded stb_image buffer (which stb owns) into an ImageData and free it.
ImageData adopt(stbi_uc* data, int w, int h) {
    ImageData img;
    if (!data) return img;
    img.width = w;
    img.height = h;
    img.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return img;
}
} // namespace

ImageData loadImageRGBA(const std::string& path) {
    int w = 0, h = 0, channels = 0;
    // req_comp = 4 forces RGBA output regardless of the source's channel count.
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        SDL_Log("stbi_load(%s): %s", path.c_str(), stbi_failure_reason());
    }
    return adopt(data, w, h);
}

ImageData decodeImageRGBA(const uint8_t* bytes, size_t len) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load_from_memory(bytes, static_cast<int>(len), &w, &h, &channels, 4);
    if (!data) {
        SDL_Log("stbi_load_from_memory: %s", stbi_failure_reason());
    }
    return adopt(data, w, h);
}

// --- GPU Texture ----------------------------------------------------------
Texture::Texture(Texture&& other) noexcept : texture_(other.texture_) {
    other.texture_ = nullptr;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        // Caller must have destroy()'d any prior handle; we just adopt the new one.
        texture_ = other.texture_;
        other.texture_ = nullptr;
    }
    return *this;
}

bool Texture::upload(SDL_GPUDevice* device, const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return fail("Texture::upload(invalid image)");

    const Uint32 w = static_cast<Uint32>(width);
    const Uint32 h = static_cast<Uint32>(height);
    const Uint32 levels = mipLevelsFor(width, height);
    const Uint32 byteCount = w * h * 4;

    // COLOR_TARGET is required in addition to SAMPLER so the driver can render
    // the down-sampled mip levels with SDL_GenerateMipmapsForGPUTexture.
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    tci.width = w;
    tci.height = h;
    tci.layer_count_or_depth = 1;
    tci.num_levels = levels;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_ = SDL_CreateGPUTexture(device, &tci);
    if (!texture_) return fail("SDL_CreateGPUTexture");

    // Stage the level-0 pixels in a CPU-visible transfer buffer.
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = byteCount;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tbci);
    if (!transfer) return fail("SDL_CreateGPUTransferBuffer(texture)");

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) return fail("SDL_MapGPUTransferBuffer(texture)");
    std::memcpy(mapped, rgba, byteCount);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    // Copy into mip 0, then have the GPU build the rest of the chain.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) return fail("SDL_AcquireGPUCommandBuffer(texture)");
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = transfer;
    src.offset = 0;
    src.pixels_per_row = w;
    src.rows_per_layer = h;

    SDL_GPUTextureRegion dst{};
    dst.texture = texture_;
    dst.mip_level = 0;
    dst.layer = 0;
    dst.w = w;
    dst.h = h;
    dst.d = 1;

    SDL_UploadToGPUTexture(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);

    if (levels > 1) SDL_GenerateMipmapsForGPUTexture(cmd, texture_);

    if (!SDL_SubmitGPUCommandBuffer(cmd)) return fail("SDL_SubmitGPUCommandBuffer(texture)");
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

bool Texture::uploadWhite(SDL_GPUDevice* device) {
    const uint8_t white[4] = {255, 255, 255, 255};

    // A 1x1 sampler-only texture: no mip chain, so no COLOR_TARGET needed.
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = 1;
    tci.height = 1;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_ = SDL_CreateGPUTexture(device, &tci);
    if (!texture_) return fail("SDL_CreateGPUTexture(white)");

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = sizeof(white);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tbci);
    if (!transfer) return fail("SDL_CreateGPUTransferBuffer(white)");

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) return fail("SDL_MapGPUTransferBuffer(white)");
    std::memcpy(mapped, white, sizeof(white));
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd) return fail("SDL_AcquireGPUCommandBuffer(white)");
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = transfer;
    src.pixels_per_row = 1;
    src.rows_per_layer = 1;

    SDL_GPUTextureRegion dst{};
    dst.texture = texture_;
    dst.w = 1;
    dst.h = 1;
    dst.d = 1;

    SDL_UploadToGPUTexture(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);
    if (!SDL_SubmitGPUCommandBuffer(cmd)) return fail("SDL_SubmitGPUCommandBuffer(white)");
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

void Texture::destroy(SDL_GPUDevice* device) {
    if (texture_) {
        SDL_ReleaseGPUTexture(device, texture_);
        texture_ = nullptr;
    }
}

SDL_GPUSampler* createDefaultSampler(SDL_GPUDevice* device) {
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sci.max_lod = 1000.0f; // let the full mip chain be used
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &sci);
    if (!sampler) SDL_Log("SDL_CreateGPUSampler: %s", SDL_GetError());
    return sampler;
}

} // namespace book
