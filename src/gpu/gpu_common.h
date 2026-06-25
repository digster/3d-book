#pragma once
//
// gpu_common.h — tiny shared helpers for the SDL_GPU code.
//
// SDL reports failures through return values + SDL_GetError(), not exceptions,
// so we follow that style: a helper that logs an error with context and returns
// false, letting callers bubble the failure up cleanly.

#include <SDL3/SDL.h>

namespace book {

// Log `what` together with the current SDL error string, then return false.
// Usage: `if (!something) return fail("SDL_CreateGPUDevice");`
inline bool fail(const char* what) {
    SDL_Log("%s: %s", what, SDL_GetError());
    return false;
}

} // namespace book
