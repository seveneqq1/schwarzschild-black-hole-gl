#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

// Tileable 3D noise baked once so shaders pay one filtered fetch instead of
// evaluating fbm per sample. Channels: R = Perlin fbm, G = inverted Worley F1
// (two octaves), B = ridged multifractal, A = decorrelated higher-frequency fbm.
namespace noise {

inline constexpr int VolumeSize = 64;

inline std::uint32_t hash(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t seed) {
    std::uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ z * 0xcb1ab31fu ^ seed * 0x165667b1u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

inline float unitFloat(std::uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }

inline glm::vec3 gradient(int x, int y, int z, int period, std::uint32_t seed) {
    const auto wrap = [period](int value) {
        return static_cast<std::uint32_t>(((value % period) + period) % period);
    };
    const std::uint32_t h = hash(wrap(x), wrap(y), wrap(z), seed);
    const float theta = 6.2831853f * unitFloat(h);
    const float cosine = 2.0f * unitFloat(h * 0x9e3779b9u + 1u) - 1.0f;
    const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
    return {sine * std::cos(theta), sine * std::sin(theta), cosine};
}

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Periodic Perlin noise with `period` lattice cells across the unit tile.
inline float perlin(const glm::vec3& p, int period, std::uint32_t seed) {
    const glm::vec3 scaled = p * static_cast<float>(period);
    const glm::ivec3 cell(glm::floor(scaled));
    const glm::vec3 f = scaled - glm::vec3(cell);
    float corners[8];
    for (int corner = 0; corner < 8; ++corner) {
        const glm::ivec3 offset(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        const glm::vec3 g = gradient(cell.x + offset.x, cell.y + offset.y, cell.z + offset.z,
                                     period, seed);
        corners[corner] = glm::dot(g, f - glm::vec3(offset));
    }
    const glm::vec3 u(fade(f.x), fade(f.y), fade(f.z));
    const float x00 = corners[0] + u.x * (corners[1] - corners[0]);
    const float x10 = corners[2] + u.x * (corners[3] - corners[2]);
    const float x01 = corners[4] + u.x * (corners[5] - corners[4]);
    const float x11 = corners[6] + u.x * (corners[7] - corners[6]);
    const float y0 = x00 + u.y * (x10 - x00);
    const float y1 = x01 + u.y * (x11 - x01);
    return y0 + u.z * (y1 - y0);
}

inline float worley(const glm::vec3& p, int period, std::uint32_t seed) {
    const glm::vec3 scaled = p * static_cast<float>(period);
    const glm::ivec3 cell(glm::floor(scaled));
    float nearest = 4.0f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const glm::ivec3 neighbor = cell + glm::ivec3(dx, dy, dz);
                const auto wrap = [period](int value) {
                    return static_cast<std::uint32_t>(((value % period) + period) % period);
                };
                const std::uint32_t h = hash(wrap(neighbor.x), wrap(neighbor.y), wrap(neighbor.z), seed);
                const glm::vec3 feature = glm::vec3(neighbor) +
                    glm::vec3(unitFloat(h), unitFloat(h * 0x27d4eb2du + 7u), unitFloat(h * 0x165667b1u + 13u));
                const glm::vec3 delta = feature - scaled;
                nearest = std::min(nearest, glm::dot(delta, delta));
            }
        }
    }
    return std::sqrt(nearest);
}

inline std::vector<std::uint8_t> buildVolume() {
    constexpr int size = VolumeSize;
    std::vector<std::uint8_t> texels(static_cast<std::size_t>(size) * size * size * 4u);
    const auto toByte = [](float value) {
        return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const glm::vec3 p = (glm::vec3(x, y, z) + 0.5f) / static_cast<float>(size);

                float fbm = 0.0f;
                float ridged = 0.0f;
                float fine = 0.0f;
                float amplitude = 0.5f;
                float ridgeWeight = 1.0f;
                for (int octave = 0; octave < 4; ++octave) {
                    const int period = 4 << octave;
                    fbm += amplitude * perlin(p, period, 11u);
                    float ridge = 1.0f - std::abs(perlin(p, period, 29u) * 1.6f);
                    ridge *= ridge * ridgeWeight;
                    ridgeWeight = std::clamp(ridge * 1.6f, 0.0f, 1.0f);
                    ridged += amplitude * ridge;
                    fine += amplitude * perlin(p, period * 2, 47u);
                    amplitude *= 0.5f;
                }
                const float cells = 0.65f * (1.0f - worley(p, 8, 5u)) +
                                    0.35f * (1.0f - worley(p, 16, 17u));

                const std::size_t index = (static_cast<std::size_t>(z) * size * size +
                                           static_cast<std::size_t>(y) * size +
                                           static_cast<std::size_t>(x)) * 4u;
                texels[index + 0] = toByte(0.5f + 0.95f * fbm);
                texels[index + 1] = toByte(cells * 1.15f - 0.1f);
                texels[index + 2] = toByte(ridged * 1.05f);
                texels[index + 3] = toByte(0.5f + 0.95f * fine);
            }
        }
    }
    return texels;
}

} // namespace noise
