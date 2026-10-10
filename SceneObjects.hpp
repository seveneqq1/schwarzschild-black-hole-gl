#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "DebrisSystem.hpp"
#include "TidalPhysics.hpp"

enum class BodyMaterial : int { Rocky = 0, Icy = 1, Metallic = 2 };

struct SceneObject {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float radius = 0.35f;      // unperturbed radius of the intact remnant
    glm::vec3 albedo{0.5f};
    float roughness = 0.7f;
    BodyMaterial material = BodyMaterial::Rocky;

    // Mean density in G = c = 1 units, where the fluid Roche limit is
    // r_Roche = (1.764 / density)^(1/3): 0.011 disrupts near 5.4 r_s.
    float density = 0.011f;
    float mass = 0.0f;         // derived from density and radius when left at zero
    float cohesion = 0.3f;     // 0 = strengthless rubble pile, 1 = strong monolith
    float seed = 0.0f;

    // Evolving tidal response.
    float elongation = 1.0f;   // long-axis stretch a1/R
    float elongationRate = 0.0f;
    float temperature = 280.0f;
    float shedRate = 0.0f;     // fractional mass-loss rate through L1/L2
    float initialMass = 0.0f;
    float initialDensity = 0.0f;
    float pendingShedMass = 0.0f;
    float particleMass = 0.0f;
    int leadingClump = -1;
    int trailingClump = -1;
    float leadingClumpMass = 0.0f;
    float trailingClumpMass = 0.0f;
};

struct OrbitState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
};

// Starts a body inbound at startRadius on the Binet orbit with the given apsides.
// The ascending node lies at nodeAzimuth; pericenterArgument is measured from it.
// Values in (0, pi/2) for positive inclination (or pi more for negative) put
// pericentre a few scale heights above the disk and keep the young outbound
// stream on the camera side of the disk plane.
inline OrbitState orbitFromApsides(float pericenter, float apocenter, float startRadius,
                                   float nodeAzimuth, float inclination,
                                   float pericenterArgument, bool prograde) {
    const float m = physics::BlackHoleMass;
    const float rp = pericenter;
    const float ra = std::max(apocenter, pericenter + 0.01f);
    const float h2 = m * (1.0f / rp - 1.0f / ra) /
        (0.5f / (rp * rp) - 0.5f / (ra * ra) - m * (1.0f / physics::cube(rp) - 1.0f / physics::cube(ra)));
    const float r0 = std::clamp(startRadius, rp, ra);

    const glm::vec3 node(std::cos(nodeAzimuth), 0.0f, std::sin(nodeAzimuth));
    glm::vec3 ahead(-node.z, 0.0f, node.x);
    if (!prograde) {
        ahead = -ahead;
    }
    ahead = std::cos(inclination) * ahead + std::sin(inclination) * glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 radial = std::cos(pericenterArgument) * node + std::sin(pericenterArgument) * ahead;
    const glm::vec3 tangent = -std::sin(pericenterArgument) * node + std::cos(pericenterArgument) * ahead;

    // Relativistic apsidal precession near r_p ~ 4 r_s is ~1.7 rad per orbit, so
    // a Newtonian anomaly would misplace pericentre. Instead start exactly at
    // pericentre and run the (time-reversible) geodesic backwards to startRadius.
    glm::vec3 position = radial * rp;
    glm::vec3 velocity = -tangent * (std::sqrt(h2) / rp);
    const float h = 0.004f;
    for (int step = 0; step < 400000 && glm::length(position) < r0; ++step) {
        velocity += physics::geodesicAcceleration(position, velocity) * (0.5f * h);
        position += velocity * h;
        velocity += physics::geodesicAcceleration(position, velocity) * (0.5f * h);
    }

    OrbitState state;
    state.position = position;
    state.velocity = -velocity;
    return state;
}

class SceneObjects {
public:
    static constexpr std::size_t MaxObjects = 16;
    static constexpr std::size_t ParticlesPerBody = 5200;

    SceneObjects() { reset(); }

    bool addObject(const SceneObject& object) {
        if (objects_.size() >= MaxObjects || object.radius <= 0.0f || object.density <= 0.0f) {
            return false;
        }
        SceneObject copy = object;
        copy.cohesion = std::clamp(copy.cohesion, 0.0f, 1.0f);
        if (copy.mass <= 0.0f) {
            copy.mass = 4.18879f * copy.density * physics::cube(copy.radius);
        } else {
            copy.radius = std::cbrt(copy.mass / (4.18879f * copy.density));
        }
        copy.initialMass = copy.mass;
        copy.initialDensity = copy.density;
        copy.particleMass = copy.mass / static_cast<float>(ParticlesPerBody);
        if (copy.seed == 0.0f) {
            copy.seed = fract(0.173f + static_cast<float>(spawnCounter_) * 0.6180339f);
        }
        objects_.push_back(copy);
        return true;
    }

    bool spawnInfallingObject() {
        const std::size_t sequenceIndex = spawnCounter_++;
        const float sequence = static_cast<float>(sequenceIndex);
        static constexpr std::array<BodyMaterial, 5> materials{{
            BodyMaterial::Rocky, BodyMaterial::Icy, BodyMaterial::Metallic,
            BodyMaterial::Icy, BodyMaterial::Rocky}};
        const BodyMaterial material = materials[sequenceIndex % materials.size()];

        const float pericenter = 3.2f + 1.8f * fract(0.37f + sequence * 0.7548777f);
        const float apocenter = 11.0f + 3.0f * fract(0.11f + sequence * 0.5698403f);
        const float azimuth = sequence * 2.39996323f + 0.9f;
        const float inclinationSign = (sequenceIndex % 2 == 0) ? 1.0f : -1.0f;
        const float inclination = inclinationSign *
            (0.18f + 0.2f * fract(0.29f + sequence * 0.4142136f));
        const float pericenterArgument = 0.7f + 0.4f * fract(0.61f + sequence * 0.3247f) +
                                         (inclination < 0.0f ? 3.14159265f : 0.0f);
        const bool prograde = sequenceIndex % 4 != 3;
        const OrbitState orbit = orbitFromApsides(pericenter, apocenter, apocenter * 0.86f,
                                                  azimuth, inclination, pericenterArgument,
                                                  prograde);

        SceneObject object = makeBody(material, sequence);
        object.position = orbit.position;
        object.velocity = orbit.velocity;
        return addObject(object);
    }

    void update(float realDeltaSeconds) {
        if (paused_) {
            return;
        }
        const float frameTime = std::clamp(realDeltaSeconds, 0.0f, 0.04f);
        const float dt = frameTime * timeScale_;
        simulatedTime_ += dt;

        for (SceneObject& object : objects_) {
            integrateOrbit(object, dt);
            updateTidalResponse(object, dt);
            shedThroughLagrangePoints(object, dt);
        }
        removeDestroyedBodies();
        debris_.update(dt);
        accretionLuminosity_ = 2.4f * (1.0f - std::exp(-debris_.accretionRate() / 1.4e-4f));
    }

    void reset() {
        objects_.clear();
        debris_.clear();
        accretionLuminosity_ = 0.0f;
        spawnCounter_ = 0;
        paused_ = false;
        simulatedTime_ = 0.0f;

        // A differentiated rocky planetesimal on a deep (beta ~ 1.5) bound orbit.
        SceneObject rocky = makeBody(BodyMaterial::Rocky, 0.0f);
        rocky.radius = 0.4f;
        rocky.seed = 0.21f;
        const OrbitState rockyOrbit = orbitFromApsides(3.6f, 12.0f, 7.6f, 0.35f, 0.3f, 0.95f, true);
        rocky.position = rockyOrbit.position;
        rocky.velocity = rockyOrbit.velocity;
        addObject(rocky);

        // A low-density icy rubble pile that breaks into a chain of clumps.
        SceneObject icy = makeBody(BodyMaterial::Icy, 1.0f);
        icy.radius = 0.33f;
        icy.seed = 0.67f;
        const OrbitState icyOrbit = orbitFromApsides(4.6f, 13.0f, 12.0f, 2.75f, -0.26f, 4.05f, true);
        icy.position = icyOrbit.position;
        icy.velocity = icyOrbit.velocity;
        addObject(icy);
        spawnCounter_ = 2;
    }

    void clear() {
        objects_.clear();
        debris_.clear();
        accretionLuminosity_ = 0.0f;
    }

    void togglePaused() { paused_ = !paused_; }
    void scaleTime(float factor) { timeScale_ = std::clamp(timeScale_ * factor, 0.25f, 40.0f); }

    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] float timeScale() const { return timeScale_; }
    [[nodiscard]] float simulatedTime() const { return simulatedTime_; }
    [[nodiscard]] std::size_t size() const { return objects_.size(); }
    [[nodiscard]] const std::vector<SceneObject>& objects() const { return objects_; }
    [[nodiscard]] const DebrisSystem& debris() const { return debris_; }
    [[nodiscard]] float accretionLuminosity() const { return accretionLuminosity_; }

    // Mass still in the simulation plus everything accreted, circularized or ejected.
    [[nodiscard]] float accountedMass() const {
        return totalMass() + debris_.accretedMass() + debris_.escapedMass() +
               debris_.diskReservoirMass();
    }

    [[nodiscard]] float totalMass() const {
        float result = debris_.particleMass();
        for (const SceneObject& object : objects_) {
            result += object.mass + object.pendingShedMass;
        }
        return result;
    }

    [[nodiscard]] glm::vec3 totalMomentum() const {
        glm::vec3 result = debris_.particleMomentum();
        for (const SceneObject& object : objects_) {
            result += object.velocity * (object.mass + object.pendingShedMass);
        }
        return result;
    }

    static float rigidity(const SceneObject& object) {
        const float base = object.material == BodyMaterial::Metallic ? 0.45f
                         : object.material == BodyMaterial::Rocky   ? 0.3f
                                                                     : 0.08f;
        return std::clamp(base + 0.35f * object.cohesion, 0.0f, 0.85f);
    }

    // Distance at which the Roche lobe first cuts into the body (shedding onset).
    static float sheddingRadius(const SceneObject& object) {
        const float lobeBoost = 1.0f + 0.35f * object.cohesion;
        float low = 1.2f;
        float high = 40.0f;
        for (int iteration = 0; iteration < 40; ++iteration) {
            const float middle = 0.5f * (low + high);
            const float chi = physics::tidalParameter(middle, object.radius, object.mass);
            const float tip = physics::equilibriumElongation(chi, rigidity(object)) * object.radius;
            const float lobe = 0.94f * physics::hillRadius(middle, object.mass) * lobeBoost;
            (tip > lobe ? low : high) = middle;
        }
        return 0.5f * (low + high);
    }

private:
    std::vector<SceneObject> objects_;
    DebrisSystem debris_;
    std::size_t spawnCounter_ = 0;
    bool paused_ = false;
    float timeScale_ = 5.0f;
    float simulatedTime_ = 0.0f;
    float accretionLuminosity_ = 0.0f;
    std::uint32_t random_ = 0x9e3779b9u;

    static float fract(float value) { return value - std::floor(value); }

    float random01() {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return static_cast<float>(random_ >> 8) * (1.0f / 16777216.0f);
    }

    static SceneObject makeBody(BodyMaterial material, float sequence) {
        SceneObject object;
        object.material = material;
        object.seed = fract(0.31f + sequence * 0.6180339f);
        switch (material) {
        case BodyMaterial::Icy:
            // Cometary rubble: rho ~ 0.6 g/cm^3 scaled to r_Roche ~ 6.8 r_s.
            object.radius = 0.28f + 0.06f * fract(sequence * 0.37f);
            object.albedo = {0.62f, 0.7f, 0.78f};
            object.roughness = 0.35f;
            object.density = 0.0056f;
            object.cohesion = 0.05f;
            object.temperature = 140.0f;
            break;
        case BodyMaterial::Metallic:
            // Differentiated body whose dense core resists complete disruption.
            object.radius = 0.3f + 0.05f * fract(sequence * 0.53f);
            object.albedo = {0.36f, 0.33f, 0.3f};
            object.roughness = 0.42f;
            object.density = 0.017f;
            object.cohesion = 0.5f;
            object.temperature = 300.0f;
            break;
        case BodyMaterial::Rocky:
        default:
            object.radius = 0.33f + 0.07f * fract(sequence * 0.29f);
            object.albedo = {0.38f, 0.24f, 0.16f};
            object.roughness = 0.82f;
            object.density = 0.011f;
            object.cohesion = 0.25f;
            object.temperature = 280.0f;
            break;
        }
        return object;
    }

    static void integrateOrbit(SceneObject& object, float dt) {
        const float radius = std::max(glm::length(object.position), 1.0f);
        const int substeps = std::clamp(
            static_cast<int>(std::ceil(dt / (0.02f * radius * std::sqrt(radius)))), 1, 64);
        const float h = dt / static_cast<float>(substeps);
        for (int step = 0; step < substeps; ++step) {
            object.velocity += physics::geodesicAcceleration(object.position, object.velocity) * (0.5f * h);
            object.position += object.velocity * h;
            object.velocity += physics::geodesicAcceleration(object.position, object.velocity) * (0.5f * h);
        }
    }

    static void updateTidalResponse(SceneObject& object, float dt) {
        const float distance = std::max(glm::length(object.position), 1.01f);
        const float chi = physics::tidalParameter(distance, object.radius, object.mass);
        const float target = physics::equilibriumElongation(chi, rigidity(object));

        // Kelvin's l = 2 f-mode, omega^2 = (16 pi / 15) G rho, driven toward the
        // instantaneous equilibrium. The lag behind the tide is what makes most
        // mass leave just after pericentre, as in hydrodynamic disruptions.
        const float omega = std::sqrt(3.351f * object.density);
        const float damping = object.material == BodyMaterial::Icy ? 0.45f : 0.3f;
        const int substeps = std::clamp(static_cast<int>(std::ceil(dt * omega / 0.15f)), 1, 32);
        const float h = dt / static_cast<float>(substeps);
        for (int step = 0; step < substeps; ++step) {
            const float acceleration = omega * omega * (target - object.elongation) -
                                       2.0f * damping * omega * object.elongationRate;
            object.elongationRate += acceleration * h;
            object.elongation += object.elongationRate * h;
            // Homologous ellipsoid mode: kinetic energy per mass ~ 0.15 R^2 Ldot^2.
            const float dissipated = 0.6f * damping * omega * object.radius * object.radius *
                                     object.elongationRate * object.elongationRate * h;
            object.temperature += dissipated * physics::HeatPerSpecificEnergy;
        }
        if (object.elongation < 1.0f) {
            object.elongation = 1.0f;
            object.elongationRate = std::max(object.elongationRate, 0.0f);
        }
        object.elongation = std::min(object.elongation, physics::MaximumElongation);
        // A large body's surface heats far more slowly than dust (thermal inertia),
        // and only its dayside faces the disk.
        const float equilibrium = 0.85f * physics::irradiationTemperature(object.position);
        if (object.temperature < equilibrium) {
            object.temperature += (equilibrium - object.temperature) * (1.0f - std::exp(-dt / 6.0f));
        }
        object.temperature = physics::radiativeCooling(object.temperature,
                                                       std::max(120.0f, equilibrium), 0.02f, dt);
    }

    void shedThroughLagrangePoints(SceneObject& object, float dt) {
        const float distance = std::max(glm::length(object.position), 1.01f);
        const float lobe = 0.94f * physics::hillRadius(distance, object.mass) *
                           (1.0f + 0.35f * object.cohesion);
        const float tip = object.elongation * object.radius;
        object.shedRate = 0.0f;
        if (tip > lobe) {
            const float fraction = physics::ellipsoidCapFraction(lobe / tip);
            const float dynamicalRate = std::sqrt(object.density);
            object.shedRate = 1.6f * fraction * dynamicalRate;
            const float lost = object.mass * (1.0f - std::exp(-object.shedRate * dt));
            object.mass -= lost;
            object.pendingShedMass += lost;

            if (object.material == BodyMaterial::Metallic) {
                // Mantle leaves first; the remnant converges toward iron-core density.
                const float stripped = object.initialMass / std::max(object.mass, 1.0e-9f);
                object.density = object.initialDensity * std::min(2.6f, std::pow(stripped, 0.45f));
            }
            object.radius = std::cbrt(object.mass / (4.18879f * object.density));
        }

        const float pairMass = 2.0f * object.particleMass;
        const float cutU = std::clamp(lobe / std::max(tip, 1.0e-5f), 0.0f, 0.98f);
        while (object.pendingShedMass >= pairMass && debris_.capacityLeft() >= 2) {
            emitPair(object, cutU, object.particleMass);
            object.pendingShedMass -= pairMass;
        }
        if (debris_.capacityLeft() < 2 && object.pendingShedMass > pairMass) {
            // Pool exhausted: keep the mass on the body rather than losing it.
            object.mass += object.pendingShedMass;
            object.pendingShedMass = 0.0f;
        }
    }

    void removeDestroyedBodies() {
        auto iterator = objects_.begin();
        while (iterator != objects_.end()) {
            SceneObject& object = *iterator;
            const float distance = glm::length(object.position);
            const bool swallowed = distance <= physics::HorizonRadius + object.radius * 0.2f;
            const bool shredded = object.mass < 0.035f * object.initialMass;
            const bool escaped = distance > DebrisSystem::EscapeRadius &&
                                 glm::dot(object.position, object.velocity) > 0.0f;
            if (shredded && !swallowed) {
                // Remaining rubble disperses as the final pairs of the stream.
                float remaining = object.mass + object.pendingShedMass;
                const float pairParticle = std::max(object.particleMass, remaining / 64.0f);
                while (remaining >= 2.0f * pairParticle && debris_.capacityLeft() >= 2) {
                    emitPair(object, 0.0f, pairParticle);
                    remaining -= 2.0f * pairParticle;
                }
                if (remaining > 0.0f && debris_.capacityLeft() >= 2) {
                    emitPair(object, 0.0f, 0.5f * remaining);
                    remaining = 0.0f;
                }
                object.mass = remaining;
                object.pendingShedMass = 0.0f;
            }
            if (swallowed || escaped || object.mass <= 0.0f) {
                if (swallowed) {
                    // Feeds the accretion rate exactly like captured debris.
                    DebrisParticle plunge;
                    plunge.position = glm::normalize(object.position) * 0.5f;
                    plunge.mass = object.mass + object.pendingShedMass;
                    debris_.emit(plunge);
                }
                iterator = objects_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    void emitPair(SceneObject& object, float cutU, float particleMass) {
        const glm::vec3 radial = glm::normalize(object.position);
        glm::vec3 tangent = object.velocity - radial * glm::dot(object.velocity, radial);
        if (glm::dot(tangent, tangent) < 1.0e-8f) {
            tangent = glm::cross(radial, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        tangent = glm::normalize(tangent);
        const glm::vec3 normal = glm::cross(radial, tangent);

        const float a1 = object.elongation * object.radius;
        const float a2 = object.radius * std::pow(object.elongation, -0.47f);
        const float a3 = object.radius * std::pow(object.elongation, -0.53f);

        // Uniform sample of the cap |x| > cutU * a1 (rejection on the parabolic profile).
        float x = 0.0f;
        for (int attempt = 0; attempt < 8; ++attempt) {
            x = cutU + (1.0f - cutU) * random01();
            if (random01() <= 1.0f - x * x) {
                break;
            }
        }
        const float lateral = std::sqrt(std::max(0.0f, 1.0f - x * x)) * std::sqrt(random01());
        const float angle = 6.2831853f * random01();
        const glm::vec3 offset = radial * (x * a1) +
                                 tangent * (a2 * lateral * std::cos(angle)) +
                                 normal * (a3 * lateral * std::sin(angle));

        // Synchronous spin (Omega = orbital rate) plus slow L1/L2 overflow.
        const float distanceSquared = glm::dot(object.position, object.position);
        const glm::vec3 spin = glm::cross(object.position, object.velocity) / distanceSquared;
        const float escapeSpeed = std::sqrt(2.0f * object.initialMass / std::max(object.radius, 0.02f));
        const glm::vec3 corotation = glm::cross(spin, offset);
        const glm::vec3 overflow = radial * (0.12f * escapeSpeed);

        const float particleSize = std::clamp(0.45f * object.radius *
                                              std::cbrt(particleMass / object.initialMass) * 6.0f,
                                              0.018f, 0.06f);
        const float ice = object.material == BodyMaterial::Icy ? 0.9f
                        : object.material == BodyMaterial::Metallic ? 0.0f : 0.08f;

        for (int side = 0; side < 2; ++side) {
            const float sign = side == 0 ? 1.0f : -1.0f;
            const bool trailing = sign > 0.0f; // far side (L2) is less bound
            int& clump = trailing ? object.trailingClump : object.leadingClump;
            float& clumpMass = trailing ? object.trailingClumpMass : object.leadingClumpMass;
            if (clump < 0 || clumpMass > object.initialMass / 18.0f) {
                clump = debris_.createClump(object.initialDensity);
                clumpMass = 0.0f;
            }
            clumpMass += particleMass;

            DebrisParticle particle;
            particle.position = object.position + sign * offset;
            particle.velocity = object.velocity + sign * (corotation + overflow);
            particle.mass = particleMass;
            particle.temperature = object.temperature;
            particle.dust = 1.0f;
            particle.ice = ice;
            particle.size = particleSize;
            particle.clump = clump;
            debris_.emit(particle);
        }
    }
};
