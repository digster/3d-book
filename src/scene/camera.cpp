//
// camera.cpp — isometric orthographic camera math.
//
#include "scene/camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace book {

void IsometricCamera::orbit(float dAzimuthRad, float dElevationRad) {
    azimuth_ += dAzimuthRad;
    elevation_ = std::clamp(elevation_ + dElevationRad,
                            0.174532925f /* 10° */, 1.396263402f /* 80° */);
}

void IsometricCamera::zoom(float factor) {
    orthoSize_ = std::clamp(orthoSize_ * factor, 1.2f, 12.0f);
}

glm::mat4 IsometricCamera::view() const {
    // Spherical -> Cartesian direction from the target to the eye.
    float ce = std::cos(elevation_);
    glm::vec3 dir(ce * std::cos(azimuth_), std::sin(elevation_), ce * std::sin(azimuth_));
    glm::vec3 eye = target_ + dir * kDistance;
    return glm::lookAt(eye, target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 IsometricCamera::proj() const {
    float halfW = orthoSize_ * aspect_;
    float halfH = orthoSize_;
    // GLM_FORCE_DEPTH_ZERO_TO_ONE (set in CMake) makes this emit a [0,1] depth
    // range, which is what SDL_GPU expects.
    return glm::ortho(-halfW, halfW, -halfH, halfH, kNear, kFar);
}

} // namespace book
