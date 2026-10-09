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
    float cohesion = 1.0f;
    float seed = 0.0f;
    int fractureLevel = 0;
};

class SceneObjects {
public:
    static constexpr std::size_t MaxObjects = 24;

    SceneObjects() { reset(); }

    // Public API for adding a custom body from application code.
    bool addObject(const SceneObject& object) {
        if (objects_.size() >= MaxObjects || object.radius <= 0.0f) {
            return false;
        }
        SceneObject copy = object;
        if (copy.seed == 0.0f) {
            copy.seed = fract(0.173f + static_cast<float>(spawnCounter_) * 0.6180339f);
        }
        objects_.push_back(copy);
        return true;
    }

    bool spawnInfallingObject() {
        static constexpr std::array<glm::vec3, 6> palette{{
            {0.52f, 0.13f, 0.035f}, {0.13f, 0.31f, 0.55f},
            {0.47f, 0.43f, 0.37f}, {0.18f, 0.47f, 0.31f},
            {0.46f, 0.19f, 0.43f}, {0.62f, 0.43f, 0.09f}}};

        const std::size_t sequenceIndex = spawnCounter_++;
        const float sequence = static_cast<float>(sequenceIndex);
        const float angle = sequence * 2.39996323f + 0.4f;
        const float distance = 7.2f + std::fmod(sequence * 1.37f, 3.2f);
        const glm::vec3 radial(std::cos(angle), 0.0f, std::sin(angle));
        const glm::vec3 tangent(-radial.z, 0.0f, radial.x);

        SceneObject object;
        object.position = radial * distance;
        object.position.y = 0.35f * std::sin(sequence * 1.71f);
        object.velocity = tangent * (0.13f + 0.015f * std::fmod(sequence, 3.0f))
                        - radial * (0.055f + 0.012f * std::fmod(sequence, 2.0f));
        object.radius = 0.24f + 0.045f * std::fmod(sequence, 4.0f);
        object.albedo = palette[sequenceIndex % palette.size()];
        object.roughness = 0.48f + 0.1f * std::fmod(sequence, 4.0f);
        object.cohesion = 0.78f + 0.2f * fract(sequence * 0.37f);
        object.seed = fract(0.31f + sequence * 0.6180339f);
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
                object.velocity += acceleration(object) * dt;
                object.position += object.velocity * dt;
            }
        }

        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(), [](const SceneObject& object) {
                return glm::length(object.position) <= EventHorizonRadius + object.radius * 0.18f;
            }),
            objects_.end());
        fractureObjects();
    }

    void reset() {
        objects_.clear();
        spawnCounter_ = 0;
        paused_ = false;

        SceneObject rockyBody;
        rockyBody.position = {7.8f, 0.45f, 0.0f};
        rockyBody.velocity = {-0.055f, -0.005f, 0.155f};
        rockyBody.radius = 0.38f;
        rockyBody.albedo = {0.44f, 0.12f, 0.032f};
        rockyBody.roughness = 0.82f;
        rockyBody.cohesion = 0.72f;
        rockyBody.seed = 0.21f;
        addObject(rockyBody);

        SceneObject icyBody;
        icyBody.position = {-6.6f, -0.3f, 4.4f};
        icyBody.velocity = {0.11f, 0.012f, 0.12f};
        icyBody.radius = 0.29f;
        icyBody.albedo = {0.11f, 0.32f, 0.56f};
        icyBody.roughness = 0.38f;
        icyBody.cohesion = 0.88f;
        icyBody.seed = 0.67f;
        addObject(icyBody);
    }

    void clear() { objects_.clear(); }
    void togglePaused() { paused_ = !paused_; }

    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] std::size_t size() const { return objects_.size(); }
    [[nodiscard]] const std::vector<SceneObject>& objects() const { return objects_; }

    [[nodiscard]] static float tidalStretch(const SceneObject& object) {
        const float distance = glm::length(object.position);
        const float proximity = std::clamp((4.8f - distance) / 3.55f, 0.0f, 1.0f);
        return std::clamp(proximity + 0.17f * static_cast<float>(object.fractureLevel),
                          0.0f, 1.35f);
    }

private:
    static constexpr float GM = 0.5f;
    static constexpr float EventHorizonRadius = 1.0f;
    static constexpr float Pi = 3.14159265358979323846f;

    std::vector<SceneObject> objects_;
    std::size_t spawnCounter_ = 0;
    bool paused_ = false;
    float simulationSpeed_ = 3.0f;

    static float fract(float value) { return value - std::floor(value); }

    static glm::vec3 acceleration(const SceneObject& object) {
        const float radius = std::max(glm::length(object.position), 1.02f);
        const float radiusSquared = radius * radius;
        const glm::vec3 angularMomentum = glm::cross(object.position, object.velocity);
        const float angularMomentumSquared = glm::dot(angularMomentum, angularMomentum);
        const float relativisticCorrection = 1.0f + 3.0f * angularMomentumSquared / radiusSquared;
        glm::vec3 result = -GM * relativisticCorrection * object.position /
                           (radiusSquared * radius);
        const float nearHorizon = std::exp(-(radius - EventHorizonRadius) * 0.55f);
        result -= object.velocity * (0.004f + 0.055f * nearHorizon);
        return result;
    }

    void fractureObjects() {
        std::vector<SceneObject> next;
        next.reserve(MaxObjects);

        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex) {
            const SceneObject& object = objects_[objectIndex];
            const float distance = glm::length(object.position);
            const float threshold = object.fractureLevel == 0
                ? 3.75f + (1.0f - object.cohesion) * 0.8f
                : 2.2f + (1.0f - object.cohesion) * 0.25f;
            const std::size_t unprocessed = objects_.size() - objectIndex - 1;
            const bool hasCapacity = next.size() + 3 + unprocessed <= MaxObjects;
            const bool shouldFracture = object.fractureLevel < 2 &&
                                        object.radius > 0.075f &&
                                        distance < threshold && hasCapacity;
            if (!shouldFracture) {
                next.push_back(object);
                continue;
            }

            const glm::vec3 radial = glm::normalize(object.position);
            glm::vec3 tangent = object.velocity - radial * glm::dot(object.velocity, radial);
            if (glm::dot(tangent, tangent) < 0.0001f) {
                tangent = glm::cross(radial, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::dot(tangent, tangent) < 0.0001f) {
                    tangent = glm::cross(radial, glm::vec3(1.0f, 0.0f, 0.0f));
                }
            }
            tangent = glm::normalize(tangent);
            const glm::vec3 bitangent = glm::normalize(glm::cross(radial, tangent));

            for (int fragmentIndex = 0; fragmentIndex < 3; ++fragmentIndex) {
                const float phase = 2.0f * Pi * static_cast<float>(fragmentIndex) / 3.0f +
                                    object.seed * Pi;
                const glm::vec3 spread = tangent * std::cos(phase) + bitangent * std::sin(phase);
                const float radialLayer = static_cast<float>(fragmentIndex - 1);

                SceneObject fragment = object;
                fragment.position += spread * object.radius * 0.58f +
                                     radial * radialLayer * object.radius * 0.28f;
                fragment.velocity += spread * (0.024f + 0.008f * object.fractureLevel) +
                                     radial * radialLayer * 0.012f;
                fragment.radius *= 0.655f;
                fragment.roughness = std::min(1.0f, object.roughness + 0.06f);
                fragment.cohesion *= 0.76f;
                fragment.seed = fract(object.seed * 3.17f +
                                      static_cast<float>(fragmentIndex) * 0.337f + 0.11f);
                fragment.fractureLevel = object.fractureLevel + 1;
                next.push_back(fragment);
            }
        }

        objects_.swap(next);
    }
};
