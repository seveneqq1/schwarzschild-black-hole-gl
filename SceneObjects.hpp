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

    // Dimensionless material properties in the visualization's G = c = 1 units.
    float density = 1.0f;
    float mass = 0.0f;       // Derived from density * radius^3 when left at zero.
    float cohesion = 0.8f;   // 0 = rubble pile, 1 = strong differentiated body.
    float seed = 0.0f;

    // Evolving response to the Schwarzschild tidal tensor.
    float tidalStrain = 0.0f;
    float strainRate = 0.0f;
    float damage = 0.0f;
    float internalEnergy = 0.0f;
    float ageSinceFracture = 0.0f;
    int fractureLevel = 0;
};

class SceneObjects {
public:
    static constexpr std::size_t MaxObjects = 24;

    SceneObjects() { reset(); }

    bool addObject(const SceneObject& object) {
        if (objects_.size() >= MaxObjects || object.radius <= 0.0f || object.density <= 0.0f) {
            return false;
        }
        SceneObject copy = object;
        copy.cohesion = std::clamp(copy.cohesion, 0.0f, 1.0f);
        if (copy.mass <= 0.0f) {
            copy.mass = copy.density * copy.radius * copy.radius * copy.radius;
        }
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
        object.density = 0.82f + 0.24f * fract(sequence * 0.29f);
        object.cohesion = 0.55f + 0.35f * fract(sequence * 0.37f);
        object.seed = fract(0.31f + sequence * 0.6180339f);
        return addObject(object);
    }

    void update(float realDeltaSeconds) {
        if (paused_) {
            return;
        }

        constexpr int substeps = 6;
        const float frameTime = std::clamp(realDeltaSeconds, 0.0f, 0.04f);
        const float simulatedFrameTime = frameTime * simulationSpeed_;
        const float dt = simulatedFrameTime / static_cast<float>(substeps);
        updateFallback(simulatedFrameTime);

        for (int substep = 0; substep < substeps; ++substep) {
            for (SceneObject& object : objects_) {
                updateMaterialResponse(object, dt);
                object.velocity += acceleration(object) * dt;
                object.position += object.velocity * dt;
                object.ageSinceFracture += dt;
            }
        }

        resolveFragmentCollisions();
        fractureObjects();
        removeCapturedAndEscaped();
    }

    void reset() {
        objects_.clear();
        fallbackPulses_.clear();
        fallbackLuminosity_ = 0.0f;
        spawnCounter_ = 0;
        paused_ = false;

        SceneObject rockyBody;
        rockyBody.position = {7.8f, 0.45f, 0.0f};
        rockyBody.velocity = {-0.055f, -0.005f, 0.155f};
        rockyBody.radius = 0.38f;
        rockyBody.albedo = {0.44f, 0.12f, 0.032f};
        rockyBody.roughness = 0.82f;
        rockyBody.density = 1.08f;
        rockyBody.cohesion = 0.62f;
        rockyBody.seed = 0.21f;
        addObject(rockyBody);

        SceneObject icyBody;
        icyBody.position = {-6.6f, -0.3f, 4.4f};
        icyBody.velocity = {0.11f, 0.012f, 0.12f};
        icyBody.radius = 0.29f;
        icyBody.albedo = {0.11f, 0.32f, 0.56f};
        icyBody.roughness = 0.38f;
        icyBody.density = 0.76f;
        icyBody.cohesion = 0.48f;
        icyBody.seed = 0.67f;
        addObject(icyBody);
    }

    void clear() {
        objects_.clear();
        fallbackPulses_.clear();
        fallbackLuminosity_ = 0.0f;
    }
    void togglePaused() { paused_ = !paused_; }

    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] std::size_t size() const { return objects_.size(); }
    [[nodiscard]] const std::vector<SceneObject>& objects() const { return objects_; }
    [[nodiscard]] float fallbackLuminosity() const { return fallbackLuminosity_; }

    [[nodiscard]] float totalMass() const {
        float result = 0.0f;
        for (const SceneObject& object : objects_) {
            result += object.mass;
        }
        return result;
    }

    [[nodiscard]] glm::vec3 totalMomentum() const {
        glm::vec3 result(0.0f);
        for (const SceneObject& object : objects_) {
            result += object.velocity * object.mass;
        }
        return result;
    }

private:
    struct FallbackPulse {
        float mass = 0.0f;
        float age = 0.0f;
    };

    static constexpr float GM = 0.5f;
    static constexpr float EventHorizonRadius = 1.0f;

    std::vector<SceneObject> objects_;
    std::vector<FallbackPulse> fallbackPulses_;
    std::size_t spawnCounter_ = 0;
    bool paused_ = false;
    float simulationSpeed_ = 3.0f;
    float fallbackLuminosity_ = 0.0f;

    static float fract(float value) { return value - std::floor(value); }

    static float materialStrength(const SceneObject& object) {
        // The first term behaves like tensile strength; the second is a small
        // self-gravity contribution that favors survival of dense central cores.
        const float tensile = 0.0012f + object.cohesion * 0.0042f;
        const float selfGravity = 0.0025f * object.density * object.mass /
                                  std::max(object.radius, 0.03f);
        return tensile + selfGravity;
    }

    static float tidalStress(const SceneObject& object) {
        const float distance = std::max(glm::length(object.position), 1.01f);
        return 2.0f * GM * object.density * object.radius * object.radius /
               (distance * distance * distance);
    }

    static void updateMaterialResponse(SceneObject& object, float dt) {
        const float distance = std::max(glm::length(object.position), 1.01f);
        const float tidalEigenvalue = 2.0f * GM / (distance * distance * distance);
        const float strength = materialStrength(object);
        const float elasticFrequencySquared = strength /
            std::max(object.density * object.radius * object.radius, 0.0001f);
        const float forcing = std::max(0.0f, tidalEigenvalue - elasticFrequencySquared);

        // Kelvin-Voigt-like response to the radial (+2) eigenvalue. The two
        // transverse (-1, -1) axes are derived volume-conservingly in the shader.
        object.strainRate += forcing * dt * 5.5f;
        object.strainRate *= std::exp(-dt * (0.28f + object.cohesion * 0.18f));
        object.tidalStrain = std::clamp(object.tidalStrain + object.strainRate * dt,
                                        0.0f, 1.8f);

        const float stressRatio = tidalStress(object) / std::max(strength, 0.00001f);
        if (stressRatio > 0.72f) {
            object.damage += (stressRatio - 0.72f) * dt * 1.35f;
            object.internalEnergy += (stressRatio - 0.55f) *
                                     std::abs(object.strainRate) * dt * 0.55f;
        }
        object.damage = std::clamp(object.damage, 0.0f, 2.0f);
        object.internalEnergy = std::clamp(object.internalEnergy * std::exp(-0.025f * dt),
                                           0.0f, 1.5f);
    }

    static glm::vec3 acceleration(const SceneObject& object) {
        const float radius = std::max(glm::length(object.position), 1.02f);
        const float radiusSquared = radius * radius;
        const glm::vec3 angularMomentum = glm::cross(object.position, object.velocity);
        const float angularMomentumSquared = glm::dot(angularMomentum, angularMomentum);
        const float relativisticCorrection = 1.0f + 3.0f * angularMomentumSquared / radiusSquared;
        glm::vec3 result = -GM * relativisticCorrection * object.position /
                           (radiusSquared * radius);

        // Once disrupted, debris is approximately ballistic. Intact bodies retain
        // a weak phenomenological orbital-loss term so interactive launches capture.
        const float nearHorizon = std::exp(-(radius - EventHorizonRadius) * 0.7f);
        const float drag = object.fractureLevel > 0
            ? 0.0005f + 0.004f * nearHorizon
            : 0.003f + 0.025f * nearHorizon;
        result -= object.velocity * drag;
        return result;
    }

    void fractureObjects() {
        std::vector<SceneObject> next;
        next.reserve(MaxObjects);

        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex) {
            const SceneObject& object = objects_[objectIndex];
            const std::size_t unprocessed = objects_.size() - objectIndex - 1;
            const bool hasCapacity = next.size() + 3 + unprocessed <= MaxObjects;
            const bool failed = object.damage >= 1.0f || object.tidalStrain >= 0.72f;
            if (object.fractureLevel >= 2 || !failed || !hasCapacity || object.radius < 0.055f) {
                next.push_back(object);
                continue;
            }

            const glm::vec3 radial = glm::normalize(object.position);
            const float speed = std::max(glm::length(object.velocity), 0.04f);
            const glm::vec3 orbitalDirection = object.velocity / speed;
            const float distance = std::max(glm::length(object.position), 1.05f);
            const float frozenEnergySpread = GM * object.radius / (distance * distance);
            const float velocitySpread = std::min(0.09f, frozenEnergySpread / speed);
            const std::array<float, 3> massFractions = object.fractureLevel == 0
                ? std::array<float, 3>{0.20f, 0.60f, 0.20f}
                : std::array<float, 3>{0.30f, 0.40f, 0.30f};

            for (int fragmentIndex = 0; fragmentIndex < 3; ++fragmentIndex) {
                const float streamCoordinate = static_cast<float>(fragmentIndex - 1);
                const float massFraction = massFractions[fragmentIndex];
                SceneObject fragment = object;
                fragment.mass = object.mass * massFraction;
                fragment.radius = object.radius * std::cbrt(massFraction);
                fragment.position += radial * streamCoordinate * object.radius * 0.78f;
                fragment.velocity += orbitalDirection * streamCoordinate * velocitySpread;
                fragment.velocity += radial * streamCoordinate *
                                     object.strainRate * object.radius * 0.32f;
                fragment.seed = fract(object.seed * 3.17f +
                                      static_cast<float>(fragmentIndex) * 0.337f + 0.11f);
                fragment.damage = 0.0f;
                fragment.tidalStrain = std::min(1.35f, object.tidalStrain * 0.72f +
                                                        std::abs(streamCoordinate) * 0.12f);
                fragment.strainRate = object.strainRate *
                                      (0.75f + 0.18f * std::abs(streamCoordinate));
                fragment.internalEnergy = std::min(1.5f, object.internalEnergy +
                                                          0.14f + 0.10f *
                                                          std::abs(streamCoordinate));
                fragment.ageSinceFracture = 0.0f;

                const bool centralCore = fragmentIndex == 1 && object.fractureLevel == 0;
                fragment.fractureLevel = centralCore ? 1 : 2;
                if (centralCore) {
                    fragment.cohesion = std::min(1.0f, object.cohesion + 0.16f);
                    fragment.density *= 1.08f;
                    // Preserve mass after raising the central concentration.
                    fragment.radius = std::cbrt(fragment.mass / fragment.density);
                } else {
                    fragment.cohesion *= 0.68f;
                }
                next.push_back(fragment);
            }
        }

        objects_.swap(next);
    }

    void resolveFragmentCollisions() {
        for (std::size_t first = 0; first < objects_.size(); ++first) {
            if (objects_[first].fractureLevel == 0 || objects_[first].ageSinceFracture < 0.35f) {
                continue;
            }
            for (std::size_t second = first + 1; second < objects_.size(); ++second) {
                if (objects_[second].fractureLevel == 0 ||
                    objects_[second].ageSinceFracture < 0.35f) {
                    continue;
                }
                SceneObject& a = objects_[first];
                SceneObject& b = objects_[second];
                glm::vec3 separation = b.position - a.position;
                const float distance = glm::length(separation);
                const float contactDistance = (a.radius + b.radius) * 0.46f;
                if (distance <= 0.0001f || distance >= contactDistance) {
                    continue;
                }

                const glm::vec3 normal = separation / distance;
                const float relativeNormalVelocity = glm::dot(b.velocity - a.velocity, normal);
                if (relativeNormalVelocity >= 0.0f) {
                    continue;
                }

                constexpr float restitution = 0.12f;
                const float inverseMassSum = 1.0f / a.mass + 1.0f / b.mass;
                const float impulseMagnitude = -(1.0f + restitution) *
                                               relativeNormalVelocity / inverseMassSum;
                a.velocity -= normal * (impulseMagnitude / a.mass);
                b.velocity += normal * (impulseMagnitude / b.mass);

                const float reducedMass = a.mass * b.mass / (a.mass + b.mass);
                const float dissipatedEnergy = 0.5f * reducedMass *
                    relativeNormalVelocity * relativeNormalVelocity *
                    (1.0f - restitution * restitution);
                const float specificHeat = dissipatedEnergy / (a.mass + b.mass) * 18.0f;
                a.internalEnergy = std::min(1.5f, a.internalEnergy + specificHeat);
                b.internalEnergy = std::min(1.5f, b.internalEnergy + specificHeat);

                const float overlap = contactDistance - distance;
                a.position -= normal * overlap * (b.mass / (a.mass + b.mass));
                b.position += normal * overlap * (a.mass / (a.mass + b.mass));
            }
        }
    }

    void removeCapturedAndEscaped() {
        auto iterator = objects_.begin();
        while (iterator != objects_.end()) {
            const float distance = glm::length(iterator->position);
            if (distance <= EventHorizonRadius + iterator->radius * 0.12f) {
                fallbackPulses_.push_back({iterator->mass, 0.0f});
                if (fallbackPulses_.size() > 8) {
                    fallbackPulses_.erase(fallbackPulses_.begin());
                }
                iterator = objects_.erase(iterator);
            } else if (distance > 45.0f && glm::dot(iterator->position, iterator->velocity) > 0.0f) {
                iterator = objects_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    void updateFallback(float dt) {
        fallbackLuminosity_ = 0.0f;
        for (FallbackPulse& pulse : fallbackPulses_) {
            pulse.age += dt;
            // Canonical late-time fallback envelope: Mdot proportional to t^-5/3.
            fallbackLuminosity_ += pulse.mass * 18.0f *
                std::pow(1.0f + pulse.age / 1.4f, -5.0f / 3.0f);
        }
        fallbackPulses_.erase(
            std::remove_if(fallbackPulses_.begin(), fallbackPulses_.end(),
                           [](const FallbackPulse& pulse) { return pulse.age > 35.0f; }),
            fallbackPulses_.end());
        fallbackLuminosity_ = std::min(fallbackLuminosity_, 2.5f);
    }
};
