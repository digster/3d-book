#pragma once
//
// camera.h — an orthographic isometric camera with orbit + zoom.
//
// Orthographic (not perspective) projection is what gives the classic isometric
// "no vanishing point" look. The camera orbits around a fixed target on a
// sphere defined by an azimuth + elevation angle; zoom changes the orthographic
// half-height (how many world units fill the viewport).
//
#include <glm/glm.hpp>

namespace book {

class IsometricCamera {
public:
    void setAspect(float aspect) { aspect_ = aspect; }
    void setTarget(glm::vec3 target) { target_ = target; }

    // Rotate around the target. Elevation is clamped to avoid gimbal flip at the
    // poles (where the view direction becomes parallel to the up vector).
    void orbit(float dAzimuthRad, float dElevationRad);

    // Multiply the orthographic half-height by `factor` (clamped). <1 zooms in.
    void zoom(float factor);

    glm::mat4 view() const;
    glm::mat4 proj() const;
    glm::mat4 viewProj() const { return proj() * view(); }

    float orthoSize() const { return orthoSize_; }

private:
    // Classic true-isometric starting angles: 45° around, ~35.264° up.
    float azimuth_ = 0.785398163f;    // 45°  in radians
    float elevation_ = 0.615479709f;  // atan(1/sqrt(2)) ≈ 35.264°
    float orthoSize_ = 3.4f;          // half-height of the view volume (world units)
    float aspect_ = 1.4f;
    glm::vec3 target_ = glm::vec3(0.0f, 0.35f, 0.0f);

    // For an orthographic camera the eye distance does not change the projected
    // size; it only needs to be large enough to keep the scene inside the
    // near/far planes.
    static constexpr float kDistance = 30.0f;
    static constexpr float kNear = 0.1f;
    static constexpr float kFar = 100.0f;
};

} // namespace book
