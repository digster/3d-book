//
// main.cpp — SDL3 application entry + lifecycle, input handling, and the frame
// loop. The heavy lifting lives in Renderer / Scene / IsometricCamera; this file
// just wires them together and translates input events into camera moves.
//
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "render/renderer.h"
#include "scene/camera.h"
#include "scene/scene.h"

#include <cstdlib>
#include <cstring>

using book::IsometricCamera;
using book::Renderer;
using book::Scene;

namespace {

// Everything that persists between frames. Allocated in SDL_AppInit, freed in
// SDL_AppQuit.
struct AppState {
    Renderer renderer;
    Scene scene;
    IsometricCamera camera;

    bool dragging = false;             // left mouse button held -> orbiting
    long maxFrames = -1;               // --frames N: exit after N frames (-1 = forever)
    long frame = 0;
    const char* screenshotPath = nullptr; // --screenshot PATH: save one frame + exit
};

// Keep the camera's aspect ratio in sync with the (possibly resized) window.
void updateAspect(AppState* st) {
    int w = 0, h = 0;
    SDL_GetWindowSize(st->renderer.window(), &w, &h);
    if (h > 0) st->camera.setAspect(static_cast<float>(w) / static_cast<float>(h));
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    SDL_SetAppMetadata("3d-book", "0.1.0", "com.digster.3dbook");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    AppState* st = new AppState();
    *appstate = st; // store early so SDL_AppQuit can always clean up

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            st->maxFrames = std::atol(argv[++i]);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            st->screenshotPath = argv[++i];
        }
    }

    if (!st->renderer.init("3d-book — isometric staging", 1000, 720)) {
        return SDL_APP_FAILURE;
    }
    if (!st->scene.build(st->renderer.device())) {
        return SDL_APP_FAILURE;
    }

    st->camera.setTarget(st->scene.target());
    updateAspect(st);

    // One-shot screenshot mode: render a single frame to a file and exit.
    if (st->screenshotPath) {
        int w = 0, h = 0;
        SDL_GetWindowSize(st->renderer.window(), &w, &h);
        bool ok = st->renderer.saveScreenshot(st->scene, st->camera, st->screenshotPath, w, h);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    SDL_Log("controls: left-drag = orbit, scroll = zoom, R = reseed models, Esc = quit");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* e) {
    AppState* st = static_cast<AppState*>(appstate);

    switch (e->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;

        case SDL_EVENT_KEY_DOWN:
            if (e->key.scancode == SDL_SCANCODE_ESCAPE) return SDL_APP_SUCCESS;
            if (e->key.scancode == SDL_SCANCODE_R) st->scene.reseed();
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e->button.button == SDL_BUTTON_LEFT) st->dragging = true;
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e->button.button == SDL_BUTTON_LEFT) st->dragging = false;
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (st->dragging) {
                // Drag horizontally to swing around; drag up to tilt toward a
                // more top-down view (note: SDL's +Y is downward, hence -yrel).
                st->camera.orbit(e->motion.xrel * 0.01f, -e->motion.yrel * 0.01f);
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            // Wheel up zooms in (smaller orthographic extent).
            st->camera.zoom(e->wheel.y > 0.0f ? 0.9f : 1.0f / 0.9f);
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            updateAspect(st);
            break;

        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* st = static_cast<AppState*>(appstate);

    if (!st->renderer.draw(st->scene, st->camera)) return SDL_APP_FAILURE;

    if (st->maxFrames >= 0 && ++st->frame >= st->maxFrames) return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/) {
    AppState* st = static_cast<AppState*>(appstate);
    if (!st) return;

    // Scene owns GPU buffers created from the renderer's device, so release them
    // before the device is destroyed.
    st->scene.destroy(st->renderer.device());
    st->renderer.destroy();
    delete st;
    // SDL_MAIN_USE_CALLBACKS calls SDL_Quit() for us after this returns.
}
