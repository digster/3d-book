#pragma once
//
// texture.h — image decoding and GPU-resident base-color textures.
//
// Mirrors the split in mesh.h: decoding (loadImageRGBA / decodeImageRGBA) is
// pure CPU work producing an `ImageData`, and `Texture` is the GPU-resident
// counterpart that owns an SDL_GPUTexture. A single shared sampler and a 1x1
// white fallback texture (see Scene) keep the draw loop free of per-object
// "is this textured?" branching.
//
#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <string>
#include <vector>

namespace book {

// A decoded, tightly packed RGBA8 image (width*height*4 bytes). `valid()` is
// false when decoding failed or the source was empty.
struct ImageData {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    bool valid() const { return !pixels.empty() && width > 0 && height > 0; }
};

// Decode an image file (PNG/JPG/...) from disk to RGBA8. Logs + returns an
// invalid ImageData on failure.
ImageData loadImageRGBA(const std::string& path);

// Decode an image held in memory (e.g. a glTF buffer-view or data-uri) to RGBA8.
ImageData decodeImageRGBA(const uint8_t* bytes, size_t len);

// GPU-resident 2D RGBA8 texture. Owns its SDL_GPUTexture; call destroy() with
// the owning device before the device is gone.
class Texture {
public:
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    ~Texture() = default; // handle must be released explicitly via destroy()

    // Upload RGBA8 pixels (width*height*4 bytes) into a new sampled texture with
    // a full mip chain. Returns false on error.
    bool upload(SDL_GPUDevice* device, const uint8_t* rgba, int width, int height);

    // Upload a single white texel — the fallback bound for untextured objects so
    // their per-object color shows through the base-color multiply unchanged.
    bool uploadWhite(SDL_GPUDevice* device);

    void destroy(SDL_GPUDevice* device);
    SDL_GPUTexture* handle() const { return texture_; }

private:
    SDL_GPUTexture* texture_ = nullptr;
};

// Create the single sampler shared by every textured draw: linear min/mag,
// linear mipmaps, repeat addressing. Returns nullptr (and logs) on failure.
SDL_GPUSampler* createDefaultSampler(SDL_GPUDevice* device);

} // namespace book
