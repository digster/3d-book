#pragma once
//
// renderer.h — owns the SDL_GPU device/window/depth/pipeline and draws a scene.
//
#include "scene/camera.h"
#include "scene/scene.h"

#include <SDL3/SDL.h>

namespace book {

class Renderer {
public:
    // Create the GPU device, window, shaders, pipeline and depth target.
    bool init(const char* title, int width, int height);
    void destroy();

    // Render one frame. Returns false on an unrecoverable GPU error. A missing
    // swapchain (e.g. minimized window) is treated as a no-op success so the
    // headless `--frames N` smoke run stays valid.
    bool draw(const Scene& scene, const IsometricCamera& camera);

    // Render a single frame to an offscreen texture and save it as a BMP. Lets
    // us capture a deterministic image without screen-recording permission.
    bool saveScreenshot(const Scene& scene, IsometricCamera camera, const char* path,
                        int width, int height);

    SDL_GPUDevice* device() const { return device_; }
    SDL_Window* window() const { return window_; }

private:
    bool createPipeline();
    bool ensureDepthTexture(Uint32 width, Uint32 height);

    // Record the clear + depth-tested draw of every scene instance into the
    // given color/depth targets. Shared by draw() and saveScreenshot().
    void recordScene(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* color,
                     SDL_GPUTexture* depth, const Scene& scene,
                     const IsometricCamera& camera);

    SDL_GPUDevice* device_ = nullptr;
    SDL_Window* window_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUTexture* depthTexture_ = nullptr;
    Uint32 depthW_ = 0;
    Uint32 depthH_ = 0;
};

} // namespace book
