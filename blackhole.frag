#version 410 core

in vec2 vUV;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uCameraPosition;
uniform mat4 uInverseViewProjection;

const float PI = 3.141592653589793;
const float SCHWARZSCHILD_RADIUS = 1.0;
const float GM = 0.5; // c = 1, therefore r_s = 2GM.
const float DISK_INNER_RADIUS = 3.0;
const float DISK_OUTER_RADIUS = 8.0;
const float ESCAPE_RADIUS = 32.0;
const int MAX_STEPS = 360;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(mix(hash31(i + vec3(0, 0, 0)), hash31(i + vec3(1, 0, 0)), f.x),
            mix(hash31(i + vec3(0, 1, 0)), hash31(i + vec3(1, 1, 0)), f.x), f.y),
        mix(mix(hash31(i + vec3(0, 0, 1)), hash31(i + vec3(1, 0, 1)), f.x),
            mix(hash31(i + vec3(0, 1, 1)), hash31(i + vec3(1, 1, 1)), f.x), f.y),
        f.z);
}

float fbm(vec3 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 5; ++octave) {
        value += amplitude * valueNoise(p);
        p = p * 2.03 + vec3(17.1, 9.2, 13.7);
        amplitude *= 0.5;
    }
    return value;
}

vec3 starfield(vec3 direction) {
    direction = normalize(direction);
    vec2 spherical = vec2(
        atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
        asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);

    vec3 color = vec3(0.002, 0.003, 0.008);

    // A soft galactic band gives lensing an extended feature to distort.
    vec3 galacticNormal = normalize(vec3(0.18, 0.93, -0.32));
    float band = exp(-18.0 * abs(dot(direction, galacticNormal)));
    float cloudy = fbm(direction * 5.0 + vec3(0.0, 0.0, uTime * 0.002));
    color += band * mix(vec3(0.025, 0.018, 0.05), vec3(0.12, 0.055, 0.16), cloudy);

    // Layered equirectangular cells provide a dense, stable procedural sky.
    for (int layer = 0; layer < 3; ++layer) {
        float scale = 180.0 + float(layer) * 113.0;
        vec2 grid = spherical * vec2(scale, scale * 0.5);
        vec2 cell = floor(grid);
        vec2 local = fract(grid) - 0.5;
        vec2 offset = vec2(hash12(cell + 3.7), hash12(cell + 19.1)) - 0.5;
        float distanceToStar = length(local - offset * 0.72);
        float randomValue = hash12(cell + float(layer) * 47.0);
        float presence = step(0.965 + float(layer) * 0.008, randomValue);
        float star = presence * smoothstep(0.055, 0.0, distanceToStar);
        star *= star * (0.55 + 2.5 * randomValue);
        vec3 tint = mix(vec3(0.55, 0.7, 1.0), vec3(1.0, 0.72, 0.45),
                        hash12(cell + 91.0));
        color += star * tint;
    }

    return color;
}

vec3 diskEmission(vec3 point, vec3 rayDirection) {
    float radius = length(point.xz);
    float angle = atan(point.z, point.x);
    float normalizedRadius = (radius - DISK_INNER_RADIUS) /
                             (DISK_OUTER_RADIUS - DISK_INNER_RADIUS);

    float orbitalRate = 1.8 / pow(radius, 1.5);
    vec3 noisePosition = vec3(
        cos(angle + uTime * orbitalRate),
        sin(angle + uTime * orbitalRate),
        normalizedRadius * 2.7);
    float turbulence = fbm(noisePosition * vec3(5.0, 5.0, 2.0) +
                           vec3(normalizedRadius * 9.0, 0.0, -uTime * 0.13));
    float filaments = 0.5 + 0.5 * sin(angle * 24.0 - uTime * 2.2 +
                                      normalizedRadius * 55.0 + turbulence * 8.0);
    float structure = smoothstep(0.12, 0.92, turbulence * 0.72 + filaments * 0.42);

    float innerHeat = pow(1.0 - normalizedRadius, 1.7);
    vec3 coolColor = vec3(1.0, 0.055, 0.003);
    vec3 hotColor = vec3(1.0, 0.62, 0.12);
    vec3 whiteHot = vec3(1.0, 0.94, 0.72);
    vec3 color = mix(coolColor, hotColor, innerHeat);
    color = mix(color, whiteHot, pow(innerHeat, 3.2));

    // Approximate special-relativistic beaming from the orbiting plasma.
    vec3 tangent = normalize(vec3(-point.z, 0.0, point.x));
    float orbitalSpeed = min(0.52, sqrt(GM / radius));
    float doppler = clamp(1.0 + 1.8 * orbitalSpeed * dot(tangent, -rayDirection), 0.45, 1.9);

    // Gravitational redshift dims material close to the hole.
    float redshift = sqrt(max(0.0, 1.0 - SCHWARZSCHILD_RADIUS / radius));
    float edgeFade = smoothstep(DISK_INNER_RADIUS, DISK_INNER_RADIUS + 0.22, radius) *
                     (1.0 - smoothstep(DISK_OUTER_RADIUS - 0.65, DISK_OUTER_RADIUS, radius));
    float brightness = edgeFade * redshift * doppler * (0.35 + 2.6 * structure);
    return color * brightness * 2.2;
}

vec3 acceleration(vec3 position, vec3 velocity) {
    float radiusSquared = dot(position, position);
    float radius = sqrt(radiusSquared);
    float angularMomentumSquared = dot(cross(position, velocity),
                                       cross(position, velocity));
    // Weak-field Schwarzschild null-geodesic equation from the task statement.
    return -3.0 * GM * angularMomentumSquared * position /
           max(radiusSquared * radiusSquared * radius, 0.00001);
}

vec3 traceRay(vec3 origin, vec3 direction) {
    vec3 position = origin;
    vec3 velocity = normalize(direction);
    vec3 radiance = vec3(0.0);
    float transmittance = 1.0;

    for (int stepIndex = 0; stepIndex < MAX_STEPS; ++stepIndex) {
        float radius = length(position);
        if (radius <= SCHWARZSCHILD_RADIUS * 1.003) {
            // Background light is absorbed; any disk light accumulated in front remains.
            return radiance;
        }
        if (radius > ESCAPE_RADIUS && dot(position, velocity) > 0.0) {
            return radiance + transmittance * starfield(velocity);
        }

        float stepSize = clamp(radius * 0.032, 0.012, 0.17);
        vec3 previousPosition = position;

        // Midpoint (RK2) integration is stable enough for real-time fragment shading
        // and accurately produces the photon-ring behavior near 1.5 r_s.
        vec3 a0 = acceleration(position, velocity);
        vec3 midpointVelocity = normalize(velocity + a0 * (0.5 * stepSize));
        vec3 midpointPosition = position + velocity * (0.5 * stepSize);
        vec3 aMidpoint = acceleration(midpointPosition, midpointVelocity);
        velocity = normalize(velocity + aMidpoint * stepSize);
        position += midpointVelocity * stepSize;

        // A sign change crosses the infinitesimally thin disk plane.
        if (previousPosition.y * position.y <= 0.0 &&
            abs(previousPosition.y - position.y) > 0.000001) {
            float crossing = previousPosition.y /
                             (previousPosition.y - position.y);
            vec3 diskPoint = mix(previousPosition, position, clamp(crossing, 0.0, 1.0));
            float diskRadius = length(diskPoint.xz);
            if (diskRadius >= DISK_INNER_RADIUS && diskRadius <= DISK_OUTER_RADIUS) {
                vec3 emission = diskEmission(diskPoint, velocity);
                float opacity = clamp(0.48 + dot(emission, vec3(0.12)), 0.48, 0.92);
                radiance += transmittance * emission * opacity;
                transmittance *= 1.0 - opacity;
                if (transmittance < 0.015) {
                    return radiance;
                }
            }
        }
    }

    // Rays still trapped after the integration budget form the dark photon region.
    return radiance;
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec4 farPoint = uInverseViewProjection * vec4(ndc, 1.0, 1.0);
    farPoint /= farPoint.w;
    vec3 rayDirection = normalize(farPoint.xyz - uCameraPosition);

    vec3 color = traceRay(uCameraPosition, rayDirection);

    // Filmic tone mapping; the sRGB framebuffer performs the transfer function.
    color = color / (color + vec3(1.0));
    color = pow(max(color, vec3(0.0)), vec3(0.92));

    // Subtle vignette anchors the image without obscuring the lensing.
    vec2 centered = vUV - 0.5;
    color *= 1.0 - 0.16 * dot(centered, centered);
    fragColor = vec4(color, 1.0);
}
