#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

// Planck spectra integrated against the CIE 1931 2-degree observer, using the
// multi-lobe Gaussian fit of Wyman, Sloan & Shirley (JCGT 2, 2013).
namespace blackbody {

inline constexpr int LutSize = 512;
inline constexpr float MinimumTemperature = 400.0f;
inline constexpr float MaximumTemperature = 120000.0f;
inline constexpr float ReferenceTemperature = 6500.0f;

inline float lobe(float wavelength, float mean, float below, float above) {
    const float t = (wavelength - mean) / (wavelength < mean ? below : above);
    return std::exp(-0.5f * t * t);
}

inline glm::vec3 cieObserver(float nm) {
    return {
        1.056f * lobe(nm, 599.8f, 37.9f, 31.0f) + 0.362f * lobe(nm, 442.0f, 16.0f, 26.7f) -
            0.065f * lobe(nm, 501.1f, 20.4f, 26.2f),
        0.821f * lobe(nm, 568.8f, 46.9f, 40.5f) + 0.286f * lobe(nm, 530.9f, 16.3f, 31.1f),
        1.217f * lobe(nm, 437.0f, 11.8f, 36.0f) + 0.681f * lobe(nm, 459.0f, 26.0f, 13.8f)};
}

// CIE XYZ of a blackbody, in arbitrary but temperature-consistent units.
inline glm::dvec3 planckXyz(double temperature) {
    constexpr double secondRadiation = 1.4387769e-2; // hc/k in m K
    glm::dvec3 xyz(0.0);
    for (int nm = 380; nm <= 780; nm += 2) {
        const double lambda = nm * 1.0e-9;
        const double planck = 1.0 / (std::pow(lambda * 1.0e6, 5.0) *
                                     std::expm1(secondRadiation / (lambda * temperature)));
        xyz += glm::dvec3(cieObserver(static_cast<float>(nm))) * planck;
    }
    return xyz;
}

inline glm::dvec3 xyzToLinearSrgb(const glm::dvec3& xyz) {
    return {3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z,
            -0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z,
            0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z};
}

inline float lutTemperature(int index) {
    const float t = static_cast<float>(index) / static_cast<float>(LutSize - 1);
    return MinimumTemperature * std::pow(MaximumTemperature / MinimumTemperature, t);
}

// RGBA per entry: linear-sRGB chromaticity scaled to unit luminance, and
// log2 of the photopic luminance relative to a 6500 K blackbody.
inline std::vector<float> buildLut() {
    std::vector<float> lut(static_cast<std::size_t>(LutSize) * 4u);
    const double reference = planckXyz(ReferenceTemperature).y;
    for (int index = 0; index < LutSize; ++index) {
        const glm::dvec3 xyz = planckXyz(lutTemperature(index));
        glm::dvec3 rgb = glm::max(xyzToLinearSrgb(xyz / std::max(xyz.y, 1.0e-300)), glm::dvec3(0.0));
        const double luminance = 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
        rgb /= std::max(luminance, 1.0e-9);
        const std::size_t base = static_cast<std::size_t>(index) * 4u;
        lut[base + 0] = static_cast<float>(rgb.r);
        lut[base + 1] = static_cast<float>(rgb.g);
        lut[base + 2] = static_cast<float>(rgb.b);
        lut[base + 3] = static_cast<float>(std::log2(std::max(xyz.y / reference, 1.0e-30)));
    }
    return lut;
}

} // namespace blackbody
