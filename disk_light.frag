#version 410 core

#include "common.glsl"

// Mean intensity J (radiance averaged over 4 pi) of the Page-Thorne disk on an
// axisymmetric (R, z) grid, used to light dust, ice and intact bodies. Light that
// passes inside the photon sphere is treated as captured.

uniform vec2 uResolution;
uniform float uDiskTemperature;

const float LIGHT_RANGE_R = 24.0;
const float LIGHT_RANGE_Z = 10.0;

out vec4 outIrradiance;

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    float t = uv.y * 2.0 - 1.0;
    vec3 receiver = vec3(uv.x * LIGHT_RANGE_R, sign(t) * t * t * LIGHT_RANGE_Z, 0.0);
    receiver.y = abs(receiver.y) < 0.004 ? 0.004 : receiver.y;

    const int RADIAL = 44;
    const int AZIMUTHAL = 80;
    vec3 sum = vec3(0.0);
    vec2 flux = vec2(0.0);
    for (int i = 0; i < RADIAL; ++i) {
        // Sample densely near the hot inner edge.
        float s = (float(i) + 0.5) / float(RADIAL);
        float radius = ISCO_RADIUS + (DISK_OUTER_EDGE - ISCO_RADIUS) * s * s;
        float width = (DISK_OUTER_EDGE - ISCO_RADIUS) * 2.0 * s / float(RADIAL);
        float taper = 1.0 - smoothstep(DISK_FADE_START, DISK_OUTER_EDGE, radius);
        vec3 emission = thermalRadiance(uDiskTemperature * pageThorneTemperature(radius)) * taper;
        if (dot(emission, vec3(1.0)) <= 0.0) {
            continue;
        }
        float area = radius * width * TAU / float(AZIMUTHAL);
        for (int j = 0; j < AZIMUTHAL; ++j) {
            float angle = (float(j) + 0.5) * TAU / float(AZIMUTHAL);
            vec3 source = vec3(radius * cos(angle), 0.0, radius * sin(angle));
            vec3 delta = receiver - source;
            float distanceSquared = dot(delta, delta);
            float distance = sqrt(distanceSquared);
            vec3 direction = delta / distance;
            float along = clamp(-dot(source, direction), 0.0, distance);
            vec3 closest = source + direction * along;
            if (dot(closest, closest) < 2.25) {
                continue;
            }
            float mu = abs(direction.y);
            float limb = (1.0 + 2.06 * mu) / 2.03;
            float solidAngle = area * mu / distanceSquared;
            sum += emission * (limb * solidAngle);
            flux += vec2(dot(direction, normalize(vec3(receiver.x, 0.0, 0.0) + vec3(1e-4, 0.0, 0.0))),
                         direction.y) * (limb * solidAngle * dot(emission, vec3(0.2126, 0.7152, 0.0722)));
        }
    }
    float luminance = dot(sum, vec3(0.2126, 0.7152, 0.0722));
    // rgb = J, a = anisotropy of the net flux (0 isotropic .. 1 collimated)
    outIrradiance = vec4(sum / (4.0 * PI), clamp(length(flux) / max(luminance, 1e-6), 0.0, 1.0));
}
