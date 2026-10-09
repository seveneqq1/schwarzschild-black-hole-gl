#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

struct SceneObject {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float radius = 0.3f;
    glm::vec3 albedo{0.7f};
    float roughness = 0.7f;
};

class SceneObjects {
public:
    static constexpr std::size_t MaxObjects = 16;

    SceneObjects() { reset(); }

    // Public API for adding a custom body from application code.
    bool addObject(const SceneObject& object) {
        if (objects_.size() >= MaxObjects || object.radius <= 0.0f) {
            return false;
        }
        objects_.push_back(object);
        return true;
    }

    bool spawnInfallingObject() {
        static constexpr std::array<glm::vec3, 6> palette{{
            {0.78f, 0.20f, 0.07f}, {0.16f, 0.42f, 0.82f},
            {0.66f, 0.62f, 0.55f}, {0.31f, 0.72f, 0.48f},
            {0.72f, 0.30f, 0.68f}, {0.86f, 0.66f, 0.16f}}};

        const float sequence = static_cast<float>(spawnCounter_++);
        const float angle = sequence * 2.39996323f + 0.4f;
        const float distance = 7.2f + std::fmod(sequence * 1.37f, 3.2f);
        const glm::vec3 radial(std::cos(angle), 0.0f, std::sin(angle));
        const glm::vec3 tangent(-radial.z, 0.0f, radial.x);

        SceneObject object;
        object.position = radial * distance;
        object.position.y = 0.35f * std::sin(sequence * 1.71f);
        // Sub-circular launch speed and an inward component make capture visible.
        object.velocity = tangent * (0.13f + 0.015f * std::fmod(sequence, 3.0f))
                        - radial * (0.055f + 0.012f * std::fmod(sequence, 2.0f));
        object.radius = 0.24f + 0.045f * std::fmod(sequence, 4.0f);
        object.albedo = palette[spawnCounter_ % palette.size()];
        object.roughness = 0.42f + 0.1f * std::fmod(sequence, 4.0f);
        return addObject(object);
    }

    void update(float realDeltaSeconds) {
        if (paused_ || objects_.empty()) {
            return;
        }

        constexpr int substeps = 6;
        const float frameTime = std::clamp(realDeltaSeconds, 0.0f, 0.04f);
        const float dt = frameTime * simulationSpeed_ / static_cast<float>(substeps);

        for (int substep = 0; substep < substeps; ++substep) {
            for (SceneObject& object : objects_) {
                // Symplectic Euler stays stable for the intentionally dissipative orbit.
                object.velocity += acceleration(object) * dt;
                object.position += object.velocity * dt;
            }
        }

        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(), [](const SceneObject& object) {
                return glm::length(object.position) <= EventHorizonRadius + object.radius * 0.25f;
            }),
            objects_.end());
    }

    void reset() {
        objects_.clear();
        spawnCounter_ = 0;
        paused_ = false;

        SceneObject rockyBody;
        rockyBody.position = {7.8f, 0.45f, 0.0f};
        rockyBody.velocity = {-0.055f, -0.005f, 0.155f};
        rockyBody.radius = 0.38f;
        rockyBody.albedo = {0.62f, 0.19f, 0.055f};
        rockyBody.roughness = 0.82f;
        addObject(rockyBody);

        SceneObject icyBody;
        icyBody.position = {-6.6f, -0.3f, 4.4f};
        icyBody.velocity = {0.11f, 0.012f, 0.12f};
        icyBody.radius = 0.29f;
        icyBody.albedo = {0.18f, 0.48f, 0.78f};
        icyBody.roughness = 0.38f;
        addObject(icyBody);
    }

    void clear() { objects_.clear(); }
    void togglePaused() { paused_ = !paused_; }

    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] std::size_t size() const { return objects_.size(); }
    [[nodiscard]] const std::vector<SceneObject>& objects() const { return objects_; }

private:
    static constexpr float GM = 0.5f;
    static constexpr float EventHorizonRadius = 1.0f;

    std::vector<SceneObject> objects_;
    std::size_t spawnCounter_ = 0;
    bool paused_ = false;
    float simulationSpeed_ = 3.0f;

    static glm::vec3 acceleration(const SceneObject& object) {
        const float radius = std::max(glm::length(object.position), 1.02f);
        const float radiusSquared = radius * radius;
        const glm::vec3 angularMomentum = glm::cross(object.position, object.velocity);
        const float angularMomentumSquared = glm::dot(angularMomentum, angularMomentum);

        // Newtonian attraction plus a first-order Schwarzschild correction that
        // produces orbital precession. Weak accretion drag gradually removes
        // angular momentum so bodies can cross the event horizon.
        const float relativisticCorrection = 1.0f + 3.0f * angularMomentumSquared / radiusSquared;
        glm::vec3 result = -GM * relativisticCorrection * object.position /
                           (radiusSquared * radius);
        const float nearHorizon = std::exp(-(radius - EventHorizonRadius) * 0.55f);
        const float drag = 0.004f + 0.055f * nearHorizon;
        result -= object.velocity * drag;
        return result;
    }
};
