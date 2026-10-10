#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "SceneObjects.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

// Integrates a test particle with the same kick-drift-kick scheme as the debris.
glm::vec3 orbit(glm::vec3 position, glm::vec3 velocity, float duration, float* minimumRadius) {
    const float h = 0.002f;
    *minimumRadius = glm::length(position);
    for (float t = 0.0f; t < duration && glm::length(position) > 1.0f; t += h) {
        velocity += physics::geodesicAcceleration(position, velocity) * (0.5f * h);
        position += velocity * h;
        velocity += physics::geodesicAcceleration(position, velocity) * (0.5f * h);
        *minimumRadius = std::min(*minimumRadius, glm::length(position));
    }
    return position;
}

} // namespace

int main() {
    // Chandrasekhar's critical Roche ellipsoid for a strengthless body: a1 ~ 1.6 R.
    const float critical = physics::equilibriumElongation(physics::RocheCriticalChi, 0.0f);
    expect(std::abs(critical - 1.6f) < 0.05f, "Roche-limit elongation is ~1.6");
    expect(physics::equilibriumElongation(0.0f, 0.0f) == 1.0f, "no tide, no elongation");

    // A circular geodesic at 6 r_s is stable; one inside the ISCO is not.
    {
        float minimum = 0.0f;
        const float r = 6.0f;
        const glm::vec3 end = orbit({r, 0.0f, 0.0f}, {0.0f, 0.0f, physics::circularSpeed(r)}, 400.0f, &minimum);
        expect(std::abs(glm::length(end) - r) < 0.05f * r, "circular orbit at 6 r_s stays circular");
    }
    {
        float minimum = 0.0f;
        const float r = 2.6f;
        orbit({r, 0.0f, 0.0f}, {0.0f, 0.0f, 0.995f * physics::circularSpeed(r)}, 400.0f, &minimum);
        expect(minimum <= 1.0f, "orbit inside the ISCO plunges");
    }
    {
        // Angular momentum below 2 (4M) is captured even from far away.
        float minimum = 0.0f;
        orbit({20.0f, 0.0f, 0.0f}, {-0.1f, 0.0f, 1.9f / 20.0f}, 600.0f, &minimum);
        expect(minimum <= 1.0f, "h < 4M is captured");
    }

    // Mass bookkeeping over two minutes of the default scene with extra spawns.
    SceneObjects simulation;
    simulation.spawnInfallingObject();
    simulation.spawnInfallingObject();
    const float initialMass = simulation.accountedMass();
    float rockyMinimumFraction = 1.0f;
    float icyMinimumFraction = 1.0f;
    std::size_t peakParticles = 0;
    for (int frame = 0; frame < 60 * 120; ++frame) {
        simulation.update(1.0f / 60.0f);
        peakParticles = std::max(peakParticles, simulation.debris().size());
        expect(simulation.size() <= SceneObjects::MaxObjects, "body pool bound");
        for (const SceneObject& object : simulation.objects()) {
            const float fraction = object.mass / object.initialMass;
            if (object.material == BodyMaterial::Rocky && object.seed == 0.21f) {
                rockyMinimumFraction = std::min(rockyMinimumFraction, fraction);
            }
            if (object.material == BodyMaterial::Icy && object.seed == 0.67f) {
                icyMinimumFraction = std::min(icyMinimumFraction, fraction);
            }
        }
        if (frame == 60 * 20) {
            // First pericentre passages: the dense rocky body is only partially
            // stripped, the icy rubble pile loses most of its mass.
            expect(rockyMinimumFraction < 0.9f && rockyMinimumFraction > 0.1f,
                   "rocky body is partially disrupted on its first passage");
            expect(icyMinimumFraction < 0.5f, "icy body is mostly disrupted on its first passage");
        }
    }
    const float drift = std::abs(simulation.accountedMass() - initialMass) / initialMass;
    expect(drift < 1.0e-3f, "mass is conserved across bodies, debris, disk and hole");
    expect(peakParticles > 1000, "disruptions produce a resolved debris stream");
    expect(peakParticles <= DebrisSystem::MaxParticles, "debris pool bound");
    expect(simulation.debris().accretedMass() > 0.0f, "some debris is accreted");

    if (failures == 0) {
        std::printf("All physics checks passed (mass drift %.2e, peak debris %zu)\n",
                    static_cast<double>(drift), peakParticles);
    }
    return failures == 0 ? 0 : 1;
}
