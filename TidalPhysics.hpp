#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace physics {

// Geometrized units: G = c = 1 and the Schwarzschild radius r_s = 2M = 1.
inline constexpr float BlackHoleMass = 0.5f;
inline constexpr float HorizonRadius = 1.0f;
inline constexpr float PhotonSphereRadius = 1.5f;
inline constexpr float IscoRadius = 3.0f;
inline constexpr float DiskOuterRadius = 9.5f;

// Chandrasekhar's synchronous Roche ellipsoids end at Omega^2/(pi G rho) = 0.09009
// with axes close to 1 : 0.51 : 0.47. Expressed through the tidal parameter
// chi = 3 M R^3 / (m r^3) = (9/4) Omega^2/(pi G rho), the limit is chi = 0.2027.
inline constexpr float RocheCriticalChi = 0.2027f;
inline constexpr float MaximumElongation = 2.6f;

// Specific energy (c^2 units) to temperature for the visual thermal model.
inline constexpr float HeatPerSpecificEnergy = 1.6e6f;
inline constexpr float MaximumTemperature = 60000.0f;

inline float cube(float value) { return value * value * value; }

// Binet form of Schwarzschild timelike orbits, u'' + u = M/h^2 + 3 M u^2, written as
// a central force. It reproduces the exact orbit shapes, apsidal precession, the
// ISCO at 6M and capture for h < 4M.
inline glm::vec3 geodesicAcceleration(const glm::vec3& position, const glm::vec3& velocity) {
    const float radiusSquared = std::max(glm::dot(position, position), 1.0e-6f);
    const float radius = std::sqrt(radiusSquared);
    const glm::vec3 angularMomentum = glm::cross(position, velocity);
    const float angularMomentumSquared = glm::dot(angularMomentum, angularMomentum);
    return -BlackHoleMass * (1.0f + 3.0f * angularMomentumSquared / radiusSquared) *
           position / (radiusSquared * radius);
}

// Conserved energy of the Binet potential: v^2/2 - M/r - M h^2/r^3.
inline float orbitalEnergy(const glm::vec3& position, const glm::vec3& velocity) {
    const float radius = glm::length(position);
    const float angularMomentumSquared =
        glm::dot(glm::cross(position, velocity), glm::cross(position, velocity));
    return 0.5f * glm::dot(velocity, velocity) - BlackHoleMass / radius -
           BlackHoleMass * angularMomentumSquared / cube(radius);
}

// Circular speed in the same potential: v^2 = M / (r - 3M).
inline float circularSpeed(float radius) {
    return std::sqrt(BlackHoleMass / std::max(radius - 3.0f * BlackHoleMass, 0.05f));
}

inline glm::vec3 diskRotationDirection(const glm::vec3& position) {
    const glm::vec3 tangent(-position.z, 0.0f, position.x);
    const float length = glm::length(tangent);
    return length > 1.0e-5f ? tangent / length : glm::vec3(0.0f);
}

// Must match diskScaleHeight() in common.glsl.
inline float diskScaleHeight(float cylindricalRadius) {
    return 0.024f * cylindricalRadius + 0.025f;
}

// Relative midplane density of the background accretion flow.
inline float diskDensity(const glm::vec3& position) {
    const float cylindricalRadius = std::sqrt(position.x * position.x + position.z * position.z);
    if (cylindricalRadius < IscoRadius * 0.9f || cylindricalRadius > DiskOuterRadius + 0.8f) {
        return 0.0f;
    }
    const float height = diskScaleHeight(cylindricalRadius);
    const float vertical = std::exp(-0.5f * position.y * position.y / (height * height));
    const float inner = std::clamp((cylindricalRadius - IscoRadius * 0.9f) / 0.4f, 0.0f, 1.0f);
    const float outer = std::clamp((DiskOuterRadius + 0.8f - cylindricalRadius) / 1.4f, 0.0f, 1.0f);
    return vertical * inner * outer;
}

inline float hillRadius(float distance, float mass) {
    return distance * std::cbrt(std::max(mass, 0.0f) / (3.0f * BlackHoleMass));
}

inline float tidalParameter(float distance, float radius, float mass) {
    return 3.0f * BlackHoleMass * cube(radius) / (std::max(mass, 1.0e-12f) * cube(distance));
}

// Equilibrium long-axis stretch a1/R of a synchronously rotating body. The linear
// term is the homogeneous-fluid Love-number response (h2 = 5/2); the quartic term
// reaches Chandrasekhar's critical Roche ellipsoid (a1/R ~ 1.6) at chi_crit.
// Rigidity in [0, 1) suppresses the response as in the solid-body Love number.
inline float equilibriumElongation(float chi, float rigidity) {
    const float scaled = chi / RocheCriticalChi;
    const float fluid = 1.0f + 0.169f * scaled + 0.431f * scaled * scaled * scaled * scaled;
    const float response = (std::min(fluid, MaximumElongation) - 1.0f) * (1.0f - rigidity);
    return 1.0f + response;
}

// Volume fraction of an ellipsoid beyond |x| > u * a1 on both ends.
inline float ellipsoidCapFraction(float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    return (1.0f - u) * (1.0f - u) * (2.0f + u) * 0.5f;
}

// Page-Thorne peak temperature of the disk; the renderer uses the same value.
inline constexpr float DiskPeakTemperature = 4200.0f;

// Radiative-equilibrium temperature of a blackbody grain irradiated by the disk,
// T_eq = W^(1/4) T_disk. The dilution factor W treats the bright inner disk as
// a source of radius ~5 r_s, halved because the disk radiates only from its faces.
inline float irradiationTemperature(const glm::vec3& position) {
    constexpr float sourceRadius = 5.0f;
    const float distance = std::max(glm::length(position), 1.0f);
    const float ratio = std::min(sourceRadius / distance, 1.0f);
    const float dilution = 0.25f * (1.0f - std::sqrt(1.0f - ratio * ratio));
    return DiskPeakTemperature * std::sqrt(std::sqrt(dilution));
}

// Radiative cooling dT/dt = -k T^4, integrated exactly over dt.
inline float radiativeCooling(float temperature, float floor, float coefficient, float dt) {
    const float t4 = temperature * 1.0e-4f;
    const float cooled = t4 / std::cbrt(1.0f + 3.0f * coefficient * t4 * t4 * t4 * dt);
    return std::max(floor, cooled * 1.0e4f);
}

} // namespace physics
