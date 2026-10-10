#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "TidalPhysics.hpp"

struct DebrisParticle {
    glm::vec3 position{0.0f};
    float mass = 0.0f;
    glm::vec3 velocity{0.0f};
    float temperature = 280.0f; // Kelvin
    float dust = 1.0f;          // 1 = condensed grains and rubble, 0 = vapour or plasma
    float ice = 0.0f;           // volatile fraction; sets the scattering albedo
    float size = 0.03f;         // smoothing radius in r_s
    float diskTime = 0.0f;      // time spent co-rotating with the accretion flow
    std::int32_t clump = -1;
    bool bound = false;         // inside its clump's Hill sphere this frame
};

// A self-gravitating aggregate inside the stream. Membership is fixed at birth,
// but only members inside the Hill sphere contribute to, and feel, its gravity.
struct DebrisClump {
    glm::vec3 center{0.0f};
    glm::vec3 velocity{0.0f};
    float boundMass = 0.0f;
    float totalMass = 0.0f;
    float density = 0.01f;
    float hillRadius = 0.0f;
    float solidRadius = 0.0f;
    bool binding = false;
    bool fresh = true;
    bool active = false;
};

class DebrisSystem {
public:
    static constexpr std::size_t MaxParticles = 32768;
    static constexpr std::size_t MaxClumps = 160;
    static constexpr float EscapeRadius = 34.0f;

    [[nodiscard]] const std::vector<DebrisParticle>& particles() const { return particles_; }
    [[nodiscard]] const std::vector<DebrisClump>& clumps() const { return clumps_; }
    [[nodiscard]] std::size_t size() const { return particles_.size(); }
    [[nodiscard]] std::size_t capacityLeft() const { return MaxParticles - particles_.size(); }
    [[nodiscard]] float accretedMass() const { return accretedMass_; }
    [[nodiscard]] float escapedMass() const { return escapedMass_; }
    [[nodiscard]] float diskReservoirMass() const { return diskReservoir_; }
    [[nodiscard]] float maximumRadius() const { return maximumRadius_; }

    // Rest-mass accretion rate onto the hole, smoothed over a few dynamical times.
    [[nodiscard]] float accretionRate() const { return accretionRate_; }

    [[nodiscard]] float particleMass() const {
        float result = 0.0f;
        for (const DebrisParticle& particle : particles_) {
            result += particle.mass;
        }
        return result;
    }

    [[nodiscard]] glm::vec3 particleMomentum() const {
        glm::vec3 result(0.0f);
        for (const DebrisParticle& particle : particles_) {
            result += particle.velocity * particle.mass;
        }
        return result;
    }

    void clear() {
        particles_.clear();
        clumps_.clear();
        accretedMass_ = 0.0f;
        escapedMass_ = 0.0f;
        diskReservoir_ = 0.0f;
        accretionRate_ = 0.0f;
        maximumRadius_ = 0.0f;
    }

    bool emit(const DebrisParticle& particle) {
        if (particles_.size() >= MaxParticles || particle.mass <= 0.0f) {
            return false;
        }
        particles_.push_back(particle);
        if (particle.clump >= 0 && static_cast<std::size_t>(particle.clump) < clumps_.size()) {
            clumps_[static_cast<std::size_t>(particle.clump)].totalMass += particle.mass;
        }
        return true;
    }

    int createClump(float density) {
        std::size_t slot = clumps_.size();
        for (std::size_t index = 0; index < clumps_.size(); ++index) {
            if (!clumps_[index].active) {
                slot = index;
                break;
            }
        }
        if (slot == clumps_.size()) {
            if (clumps_.size() >= MaxClumps) {
                return -1;
            }
            clumps_.emplace_back();
        }
        DebrisClump clump;
        clump.density = density;
        clump.active = true;
        clumps_[slot] = clump;
        return static_cast<int>(slot);
    }

    void update(float dt) {
        if (dt <= 0.0f) {
            return;
        }
        float swallowed = 0.0f;
        if (!particles_.empty()) {
            measureClumps();
            integrate(dt);
            dissipate(dt);
            evolveThermalState(dt);
            swallowed = removeFinishedParticles();
        }

        // Circularized debris joins the flow and drains on a viscous time-scale.
        const float drained = diskReservoir_ * (1.0f - std::exp(-dt / ViscousTime));
        diskReservoir_ -= drained;
        accretedMass_ += drained;
        const float instantaneous = (drained + swallowed) / dt;
        accretionRate_ += (instantaneous - accretionRate_) * (1.0f - std::exp(-dt / 4.0f));
    }

private:
    static constexpr float ViscousTime = 30.0f;
    static constexpr float DiskCoupling = 0.2f;
    static constexpr float CollisionCell = 0.2f;
    static constexpr float CollisionSpeedFloor = 0.006f;
    static constexpr std::size_t HashBuckets = 1u << 17;
    static constexpr std::size_t MaxCellSample = 20;

    std::vector<DebrisParticle> particles_;
    std::vector<DebrisClump> clumps_;
    std::vector<std::uint32_t> bucketStart_;
    std::vector<std::uint32_t> sortedIndices_;
    std::vector<std::uint32_t> particleBucket_;
    float accretedMass_ = 0.0f;
    float escapedMass_ = 0.0f;
    float diskReservoir_ = 0.0f;
    float accretionRate_ = 0.0f;
    float maximumRadius_ = 0.0f;

    void measureClumps() {
        struct Sums {
            glm::vec3 boundPosition{0.0f};
            glm::vec3 boundVelocity{0.0f};
            glm::vec3 allPosition{0.0f};
            glm::vec3 allVelocity{0.0f};
            float boundMass = 0.0f;
            float allMass = 0.0f;
        };
        std::vector<Sums> sums(clumps_.size());

        for (DebrisParticle& particle : particles_) {
            if (particle.clump < 0) {
                particle.bound = false;
                continue;
            }
            const DebrisClump& clump = clumps_[static_cast<std::size_t>(particle.clump)];
            Sums& sum = sums[static_cast<std::size_t>(particle.clump)];
            const glm::vec3 offset = particle.position - clump.center;
            particle.bound = clump.fresh ||
                (clump.binding && glm::dot(offset, offset) < clump.hillRadius * clump.hillRadius);
            sum.allMass += particle.mass;
            sum.allPosition += particle.position * particle.mass;
            sum.allVelocity += particle.velocity * particle.mass;
            if (particle.bound) {
                sum.boundMass += particle.mass;
                sum.boundPosition += particle.position * particle.mass;
                sum.boundVelocity += particle.velocity * particle.mass;
            }
        }

        for (std::size_t index = 0; index < clumps_.size(); ++index) {
            DebrisClump& clump = clumps_[index];
            const Sums& sum = sums[index];
            if (!clump.active) {
                continue;
            }
            if (sum.allMass <= 0.0f) {
                clump.active = false;
                continue;
            }
            const bool hasCore = sum.boundMass > 0.04f * sum.allMass;
            clump.boundMass = hasCore ? sum.boundMass : 0.0f;
            clump.totalMass = sum.allMass;
            clump.center = hasCore ? sum.boundPosition / sum.boundMass : sum.allPosition / sum.allMass;
            clump.velocity = hasCore ? sum.boundVelocity / sum.boundMass : sum.allVelocity / sum.allMass;

            // The potential clump is weighed by its bound core, with a quarter of the
            // membership as a seed so a dispersed group can recollapse farther out.
            const float effectiveMass = std::max(clump.boundMass, 0.25f * sum.allMass);
            clump.solidRadius = std::cbrt(3.0f * effectiveMass / (4.0f * 3.14159265f * clump.density));
            clump.hillRadius = physics::hillRadius(glm::length(clump.center), effectiveMass);
            // Same fluid Roche criterion used for intact bodies: a critical Roche
            // ellipsoid (a1 ~ 1.6 R) just fills 0.94 of the Hill sphere.
            clump.binding = 0.94f * clump.hillRadius > 1.6f * clump.solidRadius;
            clump.fresh = false;
        }
    }

    glm::vec3 clumpAcceleration(const DebrisParticle& particle, const glm::vec3& position,
                                float elapsed) const {
        if (particle.clump < 0 || !particle.bound) {
            return glm::vec3(0.0f);
        }
        const DebrisClump& clump = clumps_[static_cast<std::size_t>(particle.clump)];
        if (!clump.binding || clump.boundMass <= 0.0f) {
            return glm::vec3(0.0f);
        }
        const glm::vec3 offset = position - (clump.center + clump.velocity * elapsed);
        const float distanceSquared = glm::dot(offset, offset);
        const float solidSquared = clump.solidRadius * clump.solidRadius;
        if (distanceSquared <= solidSquared) {
            return glm::vec3(0.0f);
        }
        const float distance = std::sqrt(distanceSquared);
        return -clump.boundMass * offset / (distanceSquared * distance);
    }

    void integrate(float dt) {
        maximumRadius_ = 0.0f;
        for (DebrisParticle& particle : particles_) {
            const float radius = std::max(glm::length(particle.position), 1.0f);
            // Resolve each local orbital time-scale with at least ~30 kicks.
            const int substeps = std::clamp(
                static_cast<int>(std::ceil(dt / (0.035f * radius * std::sqrt(radius)))), 1, 40);
            const float h = dt / static_cast<float>(substeps);
            glm::vec3 position = particle.position;
            glm::vec3 velocity = particle.velocity;
            float elapsed = 0.0f;
            glm::vec3 acceleration = physics::geodesicAcceleration(position, velocity) +
                                     clumpAcceleration(particle, position, elapsed);
            for (int step = 0; step < substeps; ++step) {
                velocity += acceleration * (0.5f * h);
                position += velocity * h;
                elapsed += h;
                acceleration = physics::geodesicAcceleration(position, velocity) +
                               clumpAcceleration(particle, position, elapsed);
                velocity += acceleration * (0.5f * h);
                if (glm::dot(position, position) < physics::HorizonRadius * physics::HorizonRadius) {
                    break;
                }
            }
            particle.position = position;
            particle.velocity = velocity;
            maximumRadius_ = std::max(maximumRadius_, glm::length(position));
        }
    }

    static void relax(DebrisParticle& particle, const glm::vec3& target, float fraction) {
        const glm::vec3 relative = particle.velocity - target;
        const float retained = 1.0f - fraction;
        particle.velocity = target + relative * retained;
        const float dissipated = 0.5f * glm::dot(relative, relative) * (1.0f - retained * retained);
        particle.temperature = std::min(physics::MaximumTemperature,
            particle.temperature + dissipated * physics::HeatPerSpecificEnergy);
    }

    static void collidePair(DebrisParticle& a, DebrisParticle& b, float dt) {
        const glm::vec3 separation = b.position - a.position;
        const float distanceSquared = glm::dot(separation, separation);
        if (distanceSquared < 1.0e-10f) {
            return;
        }
        const glm::vec3 normal = separation * (1.0f / std::sqrt(distanceSquared));
        const float approach = glm::dot(b.velocity - a.velocity, normal);
        if (approach > -CollisionSpeedFloor) {
            return;
        }
        // Collision probability grows with the closing speed across a cell.
        const float fraction = 1.0f - std::exp(approach / CollisionCell * 1.6f * dt);
        const float totalMass = a.mass + b.mass;
        const glm::vec3 impulse = normal * (approach * fraction);
        a.velocity += impulse * (b.mass / totalMass);
        b.velocity -= impulse * (a.mass / totalMass);
        const float reducedMass = a.mass * b.mass / totalMass;
        const float retained = 1.0f - fraction;
        const float heat = 0.5f * reducedMass * approach * approach * (1.0f - retained * retained);
        const float specificHeat = heat / totalMass * physics::HeatPerSpecificEnergy;
        a.temperature = std::min(physics::MaximumTemperature, a.temperature + specificHeat);
        b.temperature = std::min(physics::MaximumTemperature, b.temperature + specificHeat);
    }

    static glm::ivec3 cellOf(const glm::vec3& position) {
        return glm::ivec3(glm::floor(position / CollisionCell));
    }

    static std::uint32_t hashCell(const glm::ivec3& cell) {
        const std::uint32_t x = static_cast<std::uint32_t>(cell.x) * 73856093u;
        const std::uint32_t y = static_cast<std::uint32_t>(cell.y) * 19349663u;
        const std::uint32_t z = static_cast<std::uint32_t>(cell.z) * 83492791u;
        return (x ^ y ^ z) & static_cast<std::uint32_t>(HashBuckets - 1);
    }

    void dissipate(float dt) {
        // Inelastic contact inside bound clumps (rubble-pile restitution) settles
        // collapsing aggregates instead of letting them oscillate forever.
        for (DebrisParticle& particle : particles_) {
            if (particle.clump < 0 || !particle.bound) {
                continue;
            }
            const DebrisClump& clump = clumps_[static_cast<std::size_t>(particle.clump)];
            if (!clump.binding || clump.boundMass <= 0.0f) {
                continue;
            }
            const glm::vec3 offset = particle.position - clump.center;
            if (glm::dot(offset, offset) < 1.69f * clump.solidRadius * clump.solidRadius) {
                const float crossing = 2.0f * std::sqrt(physics::cube(clump.solidRadius) /
                                                        clump.boundMass);
                relax(particle, clump.velocity, 1.0f - std::exp(-dt / std::max(crossing, 0.5f)));
            }
        }

        // Converging streams: a counting sort into hashed cells finds particles that
        // share a cell, and a large velocity dispersion there means a shock.
        const std::size_t count = particles_.size();
        bucketStart_.assign(HashBuckets + 1, 0);
        particleBucket_.resize(count);
        sortedIndices_.resize(count);
        for (std::size_t index = 0; index < count; ++index) {
            const std::uint32_t bucket = hashCell(cellOf(particles_[index].position));
            particleBucket_[index] = bucket;
            ++bucketStart_[bucket + 1];
        }
        for (std::size_t bucket = 0; bucket < HashBuckets; ++bucket) {
            bucketStart_[bucket + 1] += bucketStart_[bucket];
        }
        std::vector<std::uint32_t> cursor(bucketStart_.begin(), bucketStart_.end() - 1);
        for (std::size_t index = 0; index < count; ++index) {
            sortedIndices_[cursor[particleBucket_[index]]++] = static_cast<std::uint32_t>(index);
        }

        // Only approaching pairs collide, so the laminar shear that stretches a young
        // stream is untouched while crossing streams and the pericentre "pancake"
        // compression dissipate their converging motion as heat.
        std::array<std::uint32_t, MaxCellSample> members{};
        for (std::size_t bucket = 0; bucket < HashBuckets; ++bucket) {
            const std::uint32_t begin = bucketStart_[bucket];
            const std::uint32_t end = bucketStart_[bucket + 1];
            if (end - begin < 2) {
                continue;
            }
            const glm::ivec3 cell = cellOf(particles_[sortedIndices_[begin]].position);
            std::size_t memberCount = 0;
            const std::uint32_t stride = std::max<std::uint32_t>(1u, (end - begin) / MaxCellSample);
            for (std::uint32_t slot = begin; slot < end && memberCount < MaxCellSample; slot += stride) {
                if (cellOf(particles_[sortedIndices_[slot]].position) == cell) {
                    members[memberCount++] = sortedIndices_[slot];
                }
            }
            for (std::size_t first = 0; first < memberCount; ++first) {
                for (std::size_t second = first + 1; second < memberCount; ++second) {
                    collidePair(particles_[members[first]], particles_[members[second]],
                                dt * static_cast<float>(stride));
                }
            }
        }

        // Ram pressure where the stream pierces the pre-existing accretion flow. A
        // dense stream outweighs the gas it sweeps up, so only its surface layer is
        // decelerated; the cell population stands in for the stream column density.
        for (std::size_t index = 0; index < count; ++index) {
            DebrisParticle& particle = particles_[index];
            const float density = physics::diskDensity(particle.position);
            if (density < 0.01f) {
                particle.diskTime = std::max(0.0f, particle.diskTime - dt);
                continue;
            }
            const std::uint32_t bucket = particleBucket_[index];
            const float crowding = static_cast<float>(bucketStart_[bucket + 1] - bucketStart_[bucket]);
            const float shielding = 1.0f / (1.0f + crowding / 3.0f);
            const float cylindricalRadius = std::sqrt(particle.position.x * particle.position.x +
                                                      particle.position.z * particle.position.z);
            const glm::vec3 keplerian = physics::circularSpeed(cylindricalRadius) *
                                        physics::diskRotationDirection(particle.position);
            const glm::vec3 relative = particle.velocity - keplerian;
            relax(particle, keplerian, 1.0f - std::exp(-DiskCoupling * density * shielding * dt));
            if (glm::length(relative) < 0.08f * glm::length(keplerian)) {
                particle.diskTime += dt * density;
            }
        }
    }

    void evolveThermalState(float dt) {
        const float heating = 1.0f - std::exp(-dt / 0.4f);
        for (DebrisParticle& particle : particles_) {
            // Optically thin T^4 cooling down to the disk-irradiation equilibrium,
            // which small fragments reach quickly from below; condensed grains
            // sublimate above ~1500 K.
            const float equilibrium = physics::irradiationTemperature(particle.position);
            if (particle.temperature < equilibrium) {
                particle.temperature += (equilibrium - particle.temperature) * heating;
            }
            particle.temperature = physics::radiativeCooling(
                particle.temperature, std::max(60.0f, equilibrium), 0.2f, dt);
            if (particle.temperature > 1500.0f) {
                // Fragments are boulders, not grains, so they ablate over several orbits.
                const float rate = (particle.temperature - 1500.0f) * 0.0003f;
                particle.dust *= std::exp(-rate * dt);
            }
        }
    }

    float removeFinishedParticles() {
        float swallowed = 0.0f;
        std::size_t index = 0;
        while (index < particles_.size()) {
            DebrisParticle& particle = particles_[index];
            const float radius = glm::length(particle.position);
            bool removed = false;
            if (radius <= physics::HorizonRadius * 1.02f) {
                swallowed += particle.mass;
                accretedMass_ += particle.mass;
                removed = true;
            } else if (radius > EscapeRadius &&
                       glm::dot(particle.position, particle.velocity) > 0.0f) {
                escapedMass_ += particle.mass;
                removed = true;
            } else if (particle.diskTime > 6.0f) {
                diskReservoir_ += particle.mass;
                removed = true;
            }
            if (removed) {
                particles_[index] = particles_.back();
                particles_.pop_back();
            } else {
                ++index;
            }
        }
        return swallowed;
    }
};
