#version 410 core

#include "common.glsl"

// Backward ray tracing of exact Schwarzschild null geodesics. Every photon orbit
// is planar, so instead of integrating 3D positions we integrate the Binet
// equation u'' = -u + (3/2) u^2 (u = r_s / r) in the orbital angle phi and map
// u(phi) back into the ray's plane. Between successive points the path is a
// short chord, which is where the disk, debris volumes and bodies are sampled.

out vec4 outColor;

uniform vec2 uResolution;
uniform vec3 uCameraPosition;
uniform mat4 uInverseViewProjection;
uniform float uSimulationTime;
uniform float uMarchRadius;
uniform float uPixelAngle;

uniform sampler3D uNoise;
uniform sampler2D uDiskLight;
uniform float uDiskTemperature;
uniform float uDiskOpacity;
uniform float uFlare;

uniform int uDebrisActive;
uniform int uDebugDebris;
uniform float uDebrisOpacity;
uniform sampler3D uNearComposition;
uniform sampler3D uNearMomentum;
uniform sampler3D uFarComposition;
uniform sampler3D uFarMomentum;
uniform sampler3D uOccupancy;
uniform vec3 uNearHalfExtent;
uniform vec3 uFarHalfExtent;
uniform vec3 uOccupancyResolution;

const int MAX_BODIES = 16;
uniform int uBodyCount;
uniform vec4 uBodyCenter[MAX_BODIES];   // centre, bounding radius
uniform vec4 uBodyAxisX[MAX_BODIES];    // long (radial) axis / a1
uniform vec4 uBodyAxisY[MAX_BODIES];    // along-track axis / a2
uniform vec4 uBodyAxisZ[MAX_BODIES];    // orbit-normal axis / a3
uniform vec4 uBodySurface[MAX_BODIES];  // albedo, roughness
uniform vec4 uBodyState[MAX_BODIES];    // material, seed, shedding, temperature / 1e4

uniform float uStarBrightness;
uniform float uGalaxyBrightness;

const float NEAR_STEP = 0.07;
const float FAR_STEP = 0.2;

vec3 gColor = vec3(0.0);
float gTransmittance = 1.0;
float gObserverLapse = 1.0;
float gImpact = 0.0;
float gPlaneNormalY = 0.0;
float gJitter = 0.5;
float gPathLength = 0.0;
int gVolumeBudget = 240;

// ---------------------------------------------------------------- disk light

vec3 diskIrradiance(vec3 p) {
    const vec2 range = vec2(24.0, 10.0);
    float radius = length(p.xz);
    float height = p.y;
    float scale = 1.0;
    vec2 clamped = vec2(min(radius, range.x), clamp(height, -range.y, range.y));
    if (clamped.x != radius || clamped.y != height) {
        scale = dot(clamped, clamped) / max(radius * radius + height * height, 1e-4);
    }
    vec2 uv = vec2(clamped.x / range.x,
                   0.5 + 0.5 * sign(clamped.y) * sqrt(abs(clamped.y) / range.y));
    return textureLod(uDiskLight, uv, 0.0).rgb * (scale * (1.0 + 0.7 * uFlare));
}

// ---------------------------------------------------------------- debris volume

bool clipToBox(vec3 origin, vec3 direction, vec3 halfExtent, inout float s0, inout float s1) {
    vec3 inverse = 1.0 / direction;
    vec3 t0 = (-halfExtent - origin) * inverse;
    vec3 t1 = (halfExtent - origin) * inverse;
    vec3 tNear = min(t0, t1);
    vec3 tFar = max(t0, t1);
    s0 = max(s0, max(max(tNear.x, tNear.y), tNear.z));
    s1 = min(s1, min(min(tFar.x, tFar.y), tFar.z));
    return s0 < s1;
}

float henyeyGreenstein(float cosine, float g) {
    float denominator = 1.0 + g * g - 2.0 * g * cosine;
    return (1.0 - g * g) / (4.0 * PI * denominator * sqrt(denominator));
}

void shadeDebrisSample(vec3 p, vec3 direction, float ds, bool nearCascade) {
    vec4 composition;
    vec4 momentum;
    if (nearCascade) {
        vec3 tc = (p / uNearHalfExtent * 0.5 + 0.5).xzy;
        composition = textureLod(uNearComposition, tc, 0.0);
        if (composition.x < 2.0e-4) return;
        momentum = textureLod(uNearMomentum, tc, 0.0);
    } else {
        vec3 tc = (p / uFarHalfExtent * 0.5 + 0.5).xzy;
        composition = textureLod(uFarComposition, tc, 0.0);
        if (composition.x < 2.0e-4) return;
        momentum = textureLod(uFarMomentum, tc, 0.0);
    }

    float density = composition.x;
    if (uDebugDebris != 0) {
        gColor += gTransmittance * vec3(0.0, 1.0, 0.2) * density * ds * (nearCascade ? 1.0 : 0.0) +
                  gTransmittance * vec3(1.0, 0.0, 0.5) * density * ds * (nearCascade ? 0.0 : 1.0);
        return;
    }
    float inverseDensity = 1.0 / density;
    float temperature = composition.y * inverseDensity * 1.0e4;
    float dust = clamp(composition.z * inverseDensity, 0.0, 1.0);
    float ice = clamp(composition.w * inverseDensity, 0.0, 1.0);
    vec3 velocity = momentum.xyz * inverseDensity;

    // Sub-kernel structure advected with the local debris velocity. Two flow-map
    // phases bound the shear before the pattern is cross-faded.
    const float flowPeriod = 1.6;
    const float flowScale = 0.9;
    float phase0 = fract(uSimulationTime / flowPeriod);
    float phase1 = fract(uSimulationTime / flowPeriod + 0.5);
    float flowBlend = 1.0 - abs(2.0 * phase0 - 1.0);
    float lod = log2(max(ds * flowScale * 64.0, 1.0));
    vec4 n0 = textureLod(uNoise, (p - velocity * (phase0 * flowPeriod)) * flowScale, lod);
    vec4 n1 = textureLod(uNoise, (p - velocity * (phase1 * flowPeriod)) * flowScale + 0.43, lod);
    vec4 structure = mix(n1, n0, flowBlend);
    float knots = smoothstep(0.25, 0.85, 0.55 * structure.g + 0.45 * structure.r + 0.25 * (structure.b - 0.4));
    density *= 0.25 + 1.5 * knots;
    temperature *= 0.88 + 0.24 * structure.a;

    float radius = length(p);
    float speed = length(velocity);
    float cosine = speed > 1.0e-4 ? -dot(velocity, direction) / speed : 0.0;
    float shift = frequencyShift(radius, speed, cosine, gObserverLapse);

    // Condensed grains and ice dominate the extinction; hot rock vapour keeps a
    // large molecular and bound-free opacity and radiates at its own temperature.
    float solid = clamp(dust + ice * (1.0 - dust), 0.0, 1.0);
    float extinction = uDebrisOpacity * density * (0.55 + 0.45 * solid) / max(shift, 0.2);
    // Regolith-like rock dust is dark (albedo ~0.12); water ice is bright.
    float albedo = mix(0.12, 0.75, ice) * solid;

    vec3 thermal = thermalRadiance(temperature * shift);
    vec3 light = diskIrradiance(p);
    float scatterCosine = dot(normalize(p), -direction);
    float phase = 4.0 * PI * henyeyGreenstein(scatterCosine, mix(0.4, 0.25, ice));
    vec3 tint = mix(vec3(1.0, 0.82, 0.66), vec3(0.85, 0.93, 1.0), ice);
    vec3 scattered = light * tint * mix(1.0, phase, 0.6);

    vec3 source = mix(thermal, scattered, albedo);
    float opacity = 1.0 - exp(-extinction * ds);
    gColor += gTransmittance * source * opacity;
    gTransmittance *= 1.0 - opacity;
}

void marchDebris(vec3 origin, vec3 direction, float s0, float s1) {
    if (uDebrisActive == 0 || gVolumeBudget <= 0 || gTransmittance < 0.004) {
        return;
    }
    if (!clipToBox(origin, direction, uFarHalfExtent, s0, s1)) {
        return;
    }
    vec3 cellSize = 2.0 * uFarHalfExtent / uOccupancyResolution;
    vec3 safeDirection = mix(direction, vec3(1e-6), lessThan(abs(direction), vec3(1e-6)));
    vec3 nearLimit = uNearHalfExtent - vec3(0.25);
    float s = s0;
    for (int guard = 0; guard < 512 && s < s1; ++guard) {
        vec3 p = origin + direction * s;
        vec3 cellCoordinate = (p + uFarHalfExtent) / cellSize;
        ivec3 cell = clamp(ivec3(floor(cellCoordinate)), ivec3(0), ivec3(uOccupancyResolution) - 1);
        if (texelFetch(uOccupancy, cell.xzy, 0).r < 0.5) {
            vec3 cellMin = -uFarHalfExtent + vec3(cell) * cellSize;
            vec3 boundary = cellMin + step(vec3(0.0), safeDirection) * cellSize;
            vec3 exitDistance = (boundary - p) / safeDirection;
            s += max(min(min(exitDistance.x, exitDistance.y), exitDistance.z), 0.0) + 1.0e-3;
            continue;
        }
        bool nearCascade = all(lessThan(abs(p), nearLimit));
        float stepLength = nearCascade ? NEAR_STEP : FAR_STEP;
        float ds = min(stepLength, s1 - s);
        shadeDebrisSample(origin + direction * (s + ds * gJitter), direction, ds, nearCascade);
        s += ds;
        if (--gVolumeBudget <= 0 || gTransmittance < 0.004) {
            return;
        }
    }
}

// ---------------------------------------------------------------- accretion disk

void shadeDisk(vec3 hit, vec3 direction) {
    float radius = length(hit.xz);
    if (radius < 1.3 || radius > DISK_OUTER_EDGE) {
        return;
    }
    float mu = max(abs(direction.y), 0.012);
    float angle = atan(hit.z, hit.x);

    // Keplerian angular velocity; inside the ISCO the plunging gas keeps the
    // ISCO specific angular momentum, so Omega grows as r^-2.
    float omega = radius >= ISCO_RADIUS
        ? sqrt(BLACK_HOLE_MASS / (radius * radius * radius))
        : sqrt(BLACK_HOLE_MASS / 27.0) * 9.0 / (radius * radius);
    float logRadius = log(radius);
    // Two flow-map phases bound how far differential rotation can shear the
    // advected turbulence before it is cross-faded.
    const float period = 26.0;
    float coarseAngle = angle / TAU * 3.0;
    float rate = omega / TAU * 3.0;
    float phase0 = fract(uSimulationTime / period);
    float phase1 = fract(uSimulationTime / period + 0.5);
    float blend = 1.0 - abs(2.0 * phase0 - 1.0);
    vec3 coarse0 = vec3(coarseAngle - rate * phase0 * period, logRadius * 1.6, 0.11);
    vec3 coarse1 = vec3(coarseAngle - rate * phase1 * period, logRadius * 1.6, 0.48);
    vec3 fine0 = vec3(coarse0.x * 3.0, logRadius * 5.3, 0.73);
    vec3 fine1 = vec3(coarse1.x * 3.0, logRadius * 5.3, 0.29);
    // Footprint-based LOD: implicit derivatives are undefined inside the march loop.
    float footprint = uPixelAngle * gPathLength / sqrt(mu);
    float coarseLod = log2(max(footprint * 64.0 * max(3.0 / (TAU * radius), 1.6 / radius), 1.0));
    float fineLod = coarseLod + 1.7;
    vec4 coarse = mix(textureLod(uNoise, coarse1, coarseLod), textureLod(uNoise, coarse0, coarseLod), blend);
    vec4 fine = mix(textureLod(uNoise, fine1, fineLod), textureLod(uNoise, fine0, fineLod), blend);

    float clouds = clamp(coarse.r * 1.35 - 0.28 + 0.55 * (fine.b - 0.35) + 0.25 * (fine.r - 0.5), 0.0, 1.4);
    clouds = clouds * clouds;
    float outerTaper = 1.0 - smoothstep(DISK_FADE_START, DISK_OUTER_EDGE, radius);
    float plunge = radius < ISCO_RADIUS ? exp(-(ISCO_RADIUS - radius) * 2.2) * 0.35 : 1.0;
    float radialProfile = pow(4.5 / max(radius, 4.5), 1.4);
    float surfaceDensity = outerTaper * plunge * radialProfile * (0.9 + 2.6 * clouds);
    float tau = uDiskOpacity * surfaceDensity / mu;
    if (tau < 1.0e-3) {
        return;
    }

    float flareHeat = 1.0 + 0.16 * uFlare * exp(-max(radius - ISCO_RADIUS, 0.0) * 0.35);
    float temperature = radius >= ISCO_RADIUS
        ? uDiskTemperature * pageThorneTemperature(radius) * (0.78 + 0.42 * clouds)
        : uDiskTemperature * 0.42 * smoothstep(1.3, ISCO_RADIUS, radius);
    temperature *= flareHeat;

    float speed = radius >= ISCO_RADIUS ? circularOrbitSpeed(radius) : min(circularOrbitSpeed(ISCO_RADIUS) * ISCO_RADIUS / radius, 0.9);
    float cosine = gImpact * gPlaneNormalY * sqrt(1.0 - 1.0 / radius) / radius;
    float shift = frequencyShift(radius, speed, clamp(cosine, -1.0, 1.0), gObserverLapse);

    float limb = (1.0 + 2.06 * mu) / 2.03;
    vec3 emission = thermalRadiance(temperature * shift) * limb;
    float opacity = 1.0 - exp(-tau);
    gColor += gTransmittance * emission * opacity;
    gTransmittance *= 1.0 - opacity;
}

// ---------------------------------------------------------------- bodies

vec3 shadeBody(int index, vec3 hit, vec3 direction) {
    vec3 center = uBodyCenter[index].xyz;
    vec3 ax = uBodyAxisX[index].xyz;
    vec3 ay = uBodyAxisY[index].xyz;
    vec3 az = uBodyAxisZ[index].xyz;
    vec3 offset = hit - center;
    vec3 q = vec3(dot(offset, ax), dot(offset, ay), dot(offset, az));
    vec4 state = uBodyState[index];
    vec4 surface = uBodySurface[index];
    int material = int(state.x + 0.5);

    vec3 sampleCoordinate = q * 0.42 + vec3(state.y * 7.31, state.y * 3.17, state.y * 5.03);
    float size = 1.0 / max(length(ax), 1e-3);
    float lod = log2(max(uPixelAngle * gPathLength / size * 0.42 * 64.0, 1.0));
    vec4 n0 = textureLod(uNoise, sampleCoordinate, lod);
    vec4 n1 = textureLod(uNoise, sampleCoordinate * 3.0 + 0.5, lod + 1.6);
    const float e = 0.03;
    float hx = textureLod(uNoise, sampleCoordinate + vec3(e, 0.0, 0.0), lod).r - n0.r;
    float hy = textureLod(uNoise, sampleCoordinate + vec3(0.0, e, 0.0), lod).r - n0.r;
    float hz = textureLod(uNoise, sampleCoordinate + vec3(0.0, 0.0, e), lod).r - n0.r;
    vec3 bump = vec3(hx, hy, hz) / e;
    vec3 localNormal = normalize(q - 0.18 * (bump - q * dot(bump, q)) + 0.25 * (n1.rgb - 0.5));
    vec3 normal = normalize(ax * localNormal.x + ay * localNormal.y + az * localNormal.z);

    float craters = smoothstep(0.62, 0.9, n0.g) - 0.6 * smoothstep(0.9, 1.0, n0.g);
    vec3 albedo = surface.rgb * (0.62 + 0.55 * n0.r + 0.25 * n1.a) * (1.0 - 0.45 * craters);
    if (material == 1) {
        albedo = mix(albedo, vec3(0.92, 0.96, 1.0), smoothstep(0.55, 0.8, n1.b) * 0.6);
    }

    // Light arrives from the hot annulus near the flux peak; six samples give
    // a soft, correctly oriented terminator.
    vec3 irradiance = diskIrradiance(center);
    float directional = 0.0;
    float weightSum = 0.0;
    vec3 specularSum = vec3(0.0);
    vec3 reflected = reflect(direction, normal);
    float roughness = surface.a;
    float shininess = 2.0 / max(roughness * roughness * roughness * roughness, 1e-3);
    for (int k = 0; k < 6; ++k) {
        float a = float(k) * (TAU / 6.0) + 0.3;
        vec3 source = vec3(4.8 * cos(a), 0.0, 4.8 * sin(a));
        vec3 toLight = source - hit;
        float distanceSquared = dot(toLight, toLight);
        vec3 l = toLight * inversesqrt(distanceSquared);
        float along = clamp(dot(-hit, l), 0.0, sqrt(distanceSquared));
        vec3 closest = hit + l * along;
        float visible = smoothstep(1.4, 1.7, length(closest));
        float w = (abs(l.y) + 0.05) / distanceSquared;
        weightSum += w;
        directional += w * visible * max(dot(normal, l), 0.0);
        specularSum += w * visible * pow(max(dot(reflected, l), 0.0), shininess) * (shininess + 2.0) / TAU * 0.25;
    }
    float lambert = directional / max(weightSum, 1e-6);
    float specular = dot(specularSum, vec3(1.0)) / max(weightSum, 1e-6);
    vec3 diffuse = 4.0 * albedo * irradiance * (lambert + 0.015);
    vec3 metal = material == 2 ? mix(vec3(0.95, 0.9, 0.82), albedo, 0.4) : vec3(0.04);
    float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(normal, -direction), 0.0), 5.0);
    vec3 specularColor = irradiance * 4.0 * PI * specular * mix(metal, vec3(1.0), fresnel) * 0.25;
    if (material == 2) {
        diffuse *= 0.35;
    }

    // Irradiation heats the dayside; tidal heating peaks in the bulges and
    // around the L1/L2 overflow points, where fresh hot interior is exposed.
    float shedding = state.z;
    float bulge = q.x * q.x;
    float hotSpot = smoothstep(0.75, 1.0, abs(q.x)) * shedding;
    float day = clamp(lambert * 2.5, 0.0, 1.0);
    float cracks = smoothstep(0.55, 0.85, n0.g) * smoothstep(0.35, 0.7, n1.b);
    float temperature = state.w * 1.0e4 * (0.5 + 0.5 * sqrt(sqrt(day))) *
                            (1.0 + 0.5 * bulge * shedding) * (0.8 + 0.45 * cracks) +
                        3200.0 * hotSpot * (0.6 + 0.8 * n0.b);
    vec3 thermal = thermalRadiance(temperature);
    return diffuse + specularColor + thermal;
}

bool intersectBody(int index, vec3 origin, vec3 delta, inout float tHit) {
    vec4 bounds = uBodyCenter[index];
    vec3 toCenter = bounds.xyz - origin;
    float deltaSquared = dot(delta, delta);
    float along = clamp(dot(toCenter, delta) / deltaSquared, 0.0, 1.0);
    vec3 closest = toCenter - delta * along;
    if (dot(closest, closest) > bounds.w * bounds.w) {
        return false;
    }
    vec3 relative = origin - bounds.xyz;
    vec3 q0 = vec3(dot(relative, uBodyAxisX[index].xyz), dot(relative, uBodyAxisY[index].xyz),
                   dot(relative, uBodyAxisZ[index].xyz));
    vec3 qd = vec3(dot(delta, uBodyAxisX[index].xyz), dot(delta, uBodyAxisY[index].xyz),
                   dot(delta, uBodyAxisZ[index].xyz));
    float a = dot(qd, qd);
    float b = dot(q0, qd);
    float c = dot(q0, q0) - 1.0;
    float discriminant = b * b - a * c;
    if (discriminant < 0.0) {
        return false;
    }
    float t = (-b - sqrt(discriminant)) / a;
    if (c < 0.0) {
        t = 0.0;
    }
    if (t >= 0.0 && t <= 1.0 && t < tHit) {
        tHit = t;
        return true;
    }
    return false;
}

// Samples everything along one chord. Returns true if an opaque body ends the ray.
bool traceChord(vec3 a, vec3 b) {
    vec3 delta = b - a;
    float chordLength = length(delta);
    if (chordLength < 1.0e-6) {
        return false;
    }
    vec3 direction = delta / chordLength;
    float pathAtStart = gPathLength;

    float tBody = 2.0;
    int body = -1;
    for (int i = 0; i < uBodyCount; ++i) {
        if (intersectBody(i, a, delta, tBody)) {
            body = i;
        }
    }
    float tEnd = min(tBody, 1.0);

    float tDisk = 2.0;
    if (a.y * b.y < 0.0) {
        tDisk = a.y / (a.y - b.y);
    }
    if (tDisk < tEnd) {
        marchDebris(a, direction, 0.0, tDisk * chordLength);
        gPathLength = pathAtStart + tDisk * chordLength;
        shadeDisk(a + delta * tDisk, direction);
        marchDebris(a, direction, tDisk * chordLength, tEnd * chordLength);
    } else {
        marchDebris(a, direction, 0.0, tEnd * chordLength);
    }

    if (body >= 0) {
        gPathLength = pathAtStart + tBody * chordLength;
        gColor += gTransmittance * shadeBody(body, a + delta * tBody, direction);
        gTransmittance = 0.0;
        return true;
    }
    gPathLength = pathAtStart;
    return false;
}

// ---------------------------------------------------------------- sky

vec3 faceCoordinates(vec3 d, out float face) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        face = d.x > 0.0 ? 0.0 : 1.0;
        return vec3(d.y, d.z, a.x);
    }
    if (a.y >= a.z) {
        face = d.y > 0.0 ? 2.0 : 3.0;
        return vec3(d.x, d.z, a.y);
    }
    face = d.z > 0.0 ? 4.0 : 5.0;
    return vec3(d.x, d.y, a.z);
}

vec3 directionFromFace(vec2 uv, float face) {
    vec3 d;
    if (face < 1.5) {
        d = vec3(face < 0.5 ? 1.0 : -1.0, uv.x, uv.y);
    } else if (face < 3.5) {
        d = vec3(uv.x, face < 2.5 ? 1.0 : -1.0, uv.y);
    } else {
        d = vec3(uv.x, uv.y, face < 4.5 ? 1.0 : -1.0);
    }
    return normalize(d);
}

// Point-source stars filtered by the pixel's footprint on the celestial sphere.
// Dividing each star's flux by the footprint solid angle makes lensed images
// brighten exactly by the gravitational magnification.
vec3 starLayer(vec3 direction, float sigma, float cells, float layerSeed, float flux, float density) {
    float face;
    vec3 fc = faceCoordinates(direction, face);
    vec2 uv = fc.xy / fc.z;
    vec2 grid = (uv * 0.5 + 0.5) * cells;
    vec2 cell = floor(grid);
    float cellAngle = 2.0 / cells;
    float kernelSigma = max(sigma, 6.0e-5);
    float normalisation = 1.0 / (TAU * kernelSigma * kernelSigma);

    vec3 sum = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 neighbor = cell + vec2(x, y);
            vec3 h = hash33(vec3(neighbor, face * 31.0 + layerSeed));
            if (h.z > density) {
                continue;
            }
            vec2 starUv = ((neighbor + 0.15 + 0.7 * h.xy) / cells) * 2.0 - 1.0;
            vec3 starDirection = directionFromFace(starUv, face);
            vec3 separation = direction - starDirection;
            float distanceSquared = dot(separation, separation);
            float brightness = flux * pow(fract(h.x * 13.7 + h.y * 7.1), 7.0) * 6.0 + flux * 0.02;
            float temperature = 2900.0 + 13000.0 * pow(fract(h.y * 17.3 + h.z * 3.9), 2.6);
            vec3 tint = mix(vec3(1.0), blackbodyChroma(temperature), 0.75);
            sum += tint * brightness * normalisation * exp(-0.5 * distanceSquared / (kernelSigma * kernelSigma));
        }
    }
    // Once a pixel spans many cells, replace sparse samples with their mean radiance.
    float mean = flux * (6.0 / 8.0 + 0.02) * density / (cellAngle * cellAngle);
    float unresolved = smoothstep(0.25, 0.8, sigma / cellAngle);
    return mix(sum, vec3(1.0, 0.97, 0.93) * mean, unresolved);
}

vec3 galaxy(vec3 direction, float sigma) {
    const vec3 galacticNorth = vec3(0.4226, -0.2588, -0.8686);
    const vec3 galacticCenter = vec3(-0.8160, -0.5130, -0.2440);
    float sinLatitude = dot(direction, galacticNorth);
    vec3 inPlane = normalize(direction - galacticNorth * sinLatitude + 1e-5);
    float cosLongitude = dot(inPlane, normalize(galacticCenter - galacticNorth * dot(galacticCenter, galacticNorth)));
    float latitude = abs(sinLatitude);
    float bulge = exp(-(1.0 - cosLongitude) * 2.8) * exp(-latitude / 0.16);
    float band = exp(-latitude / 0.075) * (0.45 + 0.55 * exp(-(1.0 - cosLongitude) * 0.9)) +
                 0.18 * exp(-latitude / 0.3);

    float lod = log2(max(sigma * 64.0 * 2.2, 1.0));
    vec4 coarse = textureLod(uNoise, direction * 1.1, lod);
    vec4 fine = textureLod(uNoise, direction * 3.7 + 0.31, lod + 1.5);
    float lanes = smoothstep(0.42, 0.72, coarse.r * 0.7 + fine.b * 0.45) * exp(-latitude / 0.05);
    float mottling = 0.55 + 0.9 * fine.a * coarse.g;
    vec3 diskColor = vec3(0.72, 0.8, 1.0);
    vec3 bulgeColor = vec3(1.0, 0.8, 0.58);
    vec3 color = (diskColor * band * mottling + bulgeColor * bulge * 1.6) * (1.0 - 0.85 * lanes);
    vec3 nebula = vec3(0.9, 0.35, 0.4) * smoothstep(0.7, 0.95, fine.g * coarse.a + 0.25) * band * 0.4;
    return (color + nebula) * uGalaxyBrightness;
}

vec3 sky(vec3 direction, float sigma) {
    float sinLatitude = dot(direction, vec3(0.4226, -0.2588, -0.8686));
    float crowding = 1.0 + 3.0 * exp(-abs(sinLatitude) / 0.12);
    vec3 stars = starLayer(direction, sigma, 60.0, 1.0, 3.0e-6, 0.45) +
                 starLayer(direction, sigma, 170.0, 2.0, 2.0e-7, 0.35) * crowding +
                 starLayer(direction, sigma, 480.0, 3.0, 2.5e-8, 0.6) * crowding;
    return stars * uStarBrightness + galaxy(direction, sigma);
}

// ---------------------------------------------------------------- integrator

vec2 binet(vec2 s) {
    return vec2(s.y, -s.x + 1.5 * s.x * s.x);
}

vec2 rk4(vec2 s, float h) {
    vec2 k1 = binet(s);
    vec2 k2 = binet(s + 0.5 * h * k1);
    vec2 k3 = binet(s + 0.5 * h * k2);
    vec2 k4 = binet(s + h * k3);
    return s + (h / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}

// Remaining orbital angle until u = 0 for an outbound photon.
float angleToInfinity(vec2 s) {
    float total = 0.0;
    for (int i = 0; i < 3; ++i) {
        float estimate = atan(s.x, -s.y);
        float h = 0.85 * estimate;
        s = rk4(s, h);
        total += h;
    }
    return total + atan(s.x, -s.y);
}

void main() {
    vec2 pixel = gl_FragCoord.xy;
    vec2 ndc = pixel / uResolution * 2.0 - 1.0;
    vec4 farPoint = uInverseViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 rayDirection = normalize(farPoint.xyz / farPoint.w - uCameraPosition);
    gJitter = interleavedGradientNoise(pixel);

    float r0 = length(uCameraPosition);
    vec3 e1 = uCameraPosition / r0;
    vec3 perpendicular = rayDirection - e1 * dot(rayDirection, e1);
    float sinPsi = length(perpendicular);
    vec3 e2 = sinPsi > 1.0e-6
        ? perpendicular / sinPsi
        : normalize(cross(e1, abs(e1.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    sinPsi = max(sinPsi, 1.0e-6);
    float cosPsi = dot(rayDirection, e1);

    // The camera is a static observer: its local angles map to the Binet initial
    // data through the lapse sqrt(1 - r_s/r).
    gObserverLapse = sqrt(1.0 - 1.0 / r0);
    vec2 state = vec2(1.0 / r0, -(1.0 / r0) * gObserverLapse * cosPsi / sinPsi);
    gImpact = r0 * sinPsi / gObserverLapse;
    gPlaneNormalY = cross(e1, e2).y;

    float phi = 0.0;
    vec3 previous = uCameraPosition;
    float previousRadius = r0;
    float escapeRadius = uMarchRadius + 0.5;
    bool escaped = false;
    float escapeAngle = 0.0;

    for (int step = 0; step < 360; ++step) {
        float u = state.x;
        float radius = 1.0 / u;
        float h = radius > escapeRadius ? 0.25 : (radius < 3.2 ? 0.055 : 0.1);
        h = min(h, (0.2 * u + 0.004) / max(abs(state.y), 1.0e-6));
        vec2 next = rk4(state, h);
        if (next.x <= 0.0) {
            escaped = true;
            escapeAngle = phi + h * state.x / (state.x - next.x);
            break;
        }
        phi += h;
        state = next;
        float nextRadius = 1.0 / next.x;
        vec3 position = (cos(phi) * e1 + sin(phi) * e2) * nextRadius;

        if (min(previousRadius, nextRadius) < escapeRadius) {
            if (traceChord(previous, position)) {
                break;
            }
        }
        gPathLength += distance(previous, position);
        if (next.x > 1.0 || gTransmittance < 0.004) {
            break;
        }
        if (nextRadius > escapeRadius && next.y < 0.0) {
            escaped = true;
            escapeAngle = phi + angleToInfinity(next);
            break;
        }
        previous = position;
        previousRadius = nextRadius;
    }

    // Derivatives are taken in uniform control flow, so captured rays still
    // provide a (meaningless) direction for their neighbours' footprints.
    float skyAngle = escaped ? escapeAngle : phi;
    vec3 skyDirection = cos(skyAngle) * e1 + sin(skyAngle) * e2;
    vec3 footprint = cross(dFdx(skyDirection), dFdy(skyDirection));
    float sigma = clamp(0.5 * sqrt(length(footprint)), 2.0e-5, 0.08);
    if (escaped && gTransmittance > 0.0) {
        gColor += gTransmittance * sky(skyDirection, sigma);
    }

    outColor = vec4(gColor, 1.0);
}
