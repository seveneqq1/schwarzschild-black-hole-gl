#version 410 core

in vec2 vUV;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uCameraPosition;
uniform mat4 uInverseViewProjection;

const int MAX_OBJECTS = 24;
uniform int uObjectCount;
uniform vec4 uObjectPositionRadius[MAX_OBJECTS];
uniform vec4 uObjectColorRoughness[MAX_OBJECTS];
uniform vec4 uObjectVelocity[MAX_OBJECTS];
uniform vec4 uObjectProperties[MAX_OBJECTS]; // fracture level, seed, tidal stretch, cohesion

const float PI = 3.141592653589793;
const float SCHWARZSCHILD_RADIUS = 1.0;
const float PHOTON_SPHERE_RADIUS = 1.5;
const float GM = 0.5; // c = 1, therefore r_s = 2GM.
const float DISK_INNER_RADIUS = 3.0;
const float DISK_OUTER_RADIUS = 9.0;
const float ESCAPE_RADIUS = 34.0;
const int MAX_STEPS = 420;

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
        mix(mix(hash31(i), hash31(i + vec3(1, 0, 0)), f.x),
            mix(hash31(i + vec3(0, 1, 0)), hash31(i + vec3(1, 1, 0)), f.x), f.y),
        mix(mix(hash31(i + vec3(0, 0, 1)), hash31(i + vec3(1, 0, 1)), f.x),
            mix(hash31(i + vec3(0, 1, 1)), hash31(i + vec3(1)), f.x), f.y), f.z);
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

float fbm4(vec3 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 4; ++octave) {
        value += amplitude * valueNoise(p);
        p = p * 2.03 + vec3(17.1, 9.2, 13.7);
        amplitude *= 0.5;
    }
    return value;
}

vec3 blackbodyColor(float kelvin) {
    // Tanner Helland's blackbody approximation, converted from display to linear light.
    float temperature = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    float red = temperature <= 66.0
        ? 1.0
        : clamp(1.292936 * pow(temperature - 60.0, -0.133205), 0.0, 1.0);
    float green = temperature <= 66.0
        ? clamp(0.390082 * log(temperature) - 0.631841, 0.0, 1.0)
        : clamp(1.129891 * pow(temperature - 60.0, -0.075515), 0.0, 1.0);
    float blue = temperature >= 66.0
        ? 1.0
        : (temperature <= 19.0
            ? 0.0
            : clamp(0.543207 * log(temperature - 10.0) - 1.196254, 0.0, 1.0));
    return pow(vec3(red, green, blue), vec3(2.2));
}

vec3 starfield(vec3 direction) {
    direction = normalize(direction);
    vec2 spherical = vec2(
        atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
        asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);

    vec3 color = vec3(0.0012, 0.0018, 0.0045);
    vec3 galacticNormal = normalize(vec3(0.18, 0.93, -0.32));
    float latitude = abs(dot(direction, galacticNormal));
    float band = exp(-13.0 * latitude);
    float core = exp(-45.0 * latitude);
    float nebula = fbm(direction * 4.3 + vec3(2.0, -7.0, 3.0));
    float detail = fbm(direction * 13.0 - vec3(4.0, 1.0, 8.0));
    float dust = smoothstep(0.38, 0.72, detail);
    color += band * mix(vec3(0.012, 0.018, 0.045),
                        vec3(0.14, 0.045, 0.19), nebula) * (1.0 - 0.72 * dust);
    color += core * vec3(0.11, 0.07, 0.035) * (0.35 + nebula);

    for (int layer = 0; layer < 4; ++layer) {
        float scale = 155.0 + float(layer) * 117.0;
        vec2 grid = spherical * vec2(scale, scale * 0.5);
        vec2 cell = floor(grid);
        vec2 local = fract(grid) - 0.5;
        vec2 offset = vec2(hash12(cell + 3.7), hash12(cell + 19.1)) - 0.5;
        float distanceToStar = length(local - offset * 0.76);
        float randomValue = hash12(cell + float(layer) * 47.0);
        float presence = step(0.958 + float(layer) * 0.009, randomValue);
        float radius = mix(0.018, 0.055, randomValue);
        float star = presence * smoothstep(radius, radius * 0.18, distanceToStar);
        star *= star * (0.45 + 3.2 * randomValue);
        vec3 tint = mix(vec3(0.48, 0.66, 1.0), vec3(1.0, 0.66, 0.35),
                        hash12(cell + 91.0));
        color += star * tint;
    }
    return color;
}

vec3 rayAcceleration(vec3 position, vec3 velocity) {
    float radiusSquared = dot(position, position);
    float radius = sqrt(radiusSquared);
    vec3 angularMomentum = cross(position, velocity);
    float angularMomentumSquared = dot(angularMomentum, angularMomentum);
    return -3.0 * GM * angularMomentumSquared * position /
           max(radiusSquared * radiusSquared * radius, 0.00001);
}

void integrateGeodesic(inout vec3 position, inout vec3 velocity, float stepSize) {
    // Curvature is negligible in the far field: midpoint integration halves the
    // acceleration work there, while full RK4 preserves the photon-ring geometry.
    if (dot(position, position) > 64.0) {
        vec3 firstAcceleration = rayAcceleration(position, velocity);
        vec3 midpointVelocity = normalize(velocity + firstAcceleration * (0.5 * stepSize));
        vec3 midpointPosition = position + velocity * (0.5 * stepSize);
        velocity = normalize(velocity + rayAcceleration(midpointPosition, midpointVelocity) *
                                        stepSize);
        position += midpointVelocity * stepSize;
        return;
    }

    vec3 k1p = velocity;
    vec3 k1v = rayAcceleration(position, velocity);

    vec3 k2p = normalize(velocity + k1v * (0.5 * stepSize));
    vec3 k2v = rayAcceleration(position + k1p * (0.5 * stepSize), k2p);

    vec3 k3p = normalize(velocity + k2v * (0.5 * stepSize));
    vec3 k3v = rayAcceleration(position + k2p * (0.5 * stepSize), k3p);

    vec3 k4p = normalize(velocity + k3v * stepSize);
    vec3 k4v = rayAcceleration(position + k3p * stepSize, k4p);

    position += stepSize * (k1p + 2.0 * k2p + 2.0 * k3p + k4p) / 6.0;
    velocity = normalize(velocity + stepSize *
                         (k1v + 2.0 * k2v + 2.0 * k3v + k4v) / 6.0);
}

struct MediumSample {
    vec3 emission;
    vec3 extinction;
};

MediumSample sampleAccretionFlow(vec3 point, vec3 rayDirection) {
    float cylindricalRadius = length(point.xz);
    if (cylindricalRadius < 1.08 || cylindricalRadius > DISK_OUTER_RADIUS + 0.8 ||
        abs(point.y) > 1.1) {
        return MediumSample(vec3(0.0), vec3(0.0));
    }

    float angle = atan(point.z, point.x);
    float normalizedRadius = clamp((cylindricalRadius - DISK_INNER_RADIUS) /
                                   (DISK_OUTER_RADIUS - DISK_INNER_RADIUS), 0.0, 1.0);
    float thickness = mix(0.12, 0.42, normalizedRadius);
    float verticalProfile = exp(-2.8 * point.y * point.y / (thickness * thickness));
    float innerEdge = smoothstep(DISK_INNER_RADIUS - 0.18,
                                 DISK_INNER_RADIUS + 0.22, cylindricalRadius);
    float outerEdge = 1.0 - smoothstep(DISK_OUTER_RADIUS - 0.9,
                                      DISK_OUTER_RADIUS + 0.25, cylindricalRadius);

    float orbitalRate = 2.2 / pow(max(cylindricalRadius, 1.0), 1.5);
    vec3 flowCoordinates = vec3(
        cos(angle + uTime * orbitalRate),
        sin(angle + uTime * orbitalRate),
        normalizedRadius * 3.4 - uTime * 0.035);
    float turbulence = fbm(flowCoordinates * vec3(6.0, 6.0, 2.2) +
                           vec3(normalizedRadius * 11.0, 0.0, 0.0));
    float spiral = 0.5 + 0.5 * sin(angle * 18.0 - uTime * 1.65 +
                                  normalizedRadius * 72.0 + turbulence * 9.0);
    float knots = smoothstep(0.34, 0.86, turbulence * 0.7 + spiral * 0.48);

    float dustNoise = fbm4(flowCoordinates * vec3(3.1, 3.1, 5.5) + vec3(8.7, 2.1, 4.3));
    float dustLane = smoothstep(0.48, 0.76, dustNoise);
    float diskMask = innerEdge * outerEdge * verticalProfile;
    float innerHeat = pow(1.0 - normalizedRadius, 1.55);
    float temperature = mix(1900.0, 11800.0, pow(innerHeat, 0.72));
    vec3 emissionColor = blackbodyColor(temperature);

    vec3 tangent = normalize(vec3(-point.z, 0.0, point.x));
    float orbitalSpeed = min(0.57, sqrt(GM / max(cylindricalRadius, 1.01)));
    float doppler = clamp(1.0 + 2.35 * orbitalSpeed * dot(tangent, -rayDirection), 0.28, 2.35);
    float gravitationalRedshift = sqrt(max(0.0, 1.0 -
                                           SCHWARZSCHILD_RADIUS / cylindricalRadius));
    float density = diskMask * (0.42 + 2.15 * knots + 1.35 * dustLane);
    vec3 emission = emissionColor * gravitationalRedshift * pow(doppler, 2.4) *
                    (0.55 + 3.0 * knots) * (1.0 - 0.62 * dustLane);

    // Sparse orbiting grains create resolved incandescent particle highlights.
    float rotation = uTime * orbitalRate * 0.72;
    float cosineRotation = cos(rotation);
    float sineRotation = sin(rotation);
    vec3 particlePoint = vec3(
        cosineRotation * point.x - sineRotation * point.z,
        point.y,
        sineRotation * point.x + cosineRotation * point.z);
    vec3 particleCoordinates = particlePoint * vec3(7.5, 16.0, 7.5);
    vec3 particleCell = floor(particleCoordinates);
    vec3 particleLocal = fract(particleCoordinates) - 0.5;
    vec3 particleOffset = vec3(hash31(particleCell + 1.7),
                               hash31(particleCell + 7.1),
                               hash31(particleCell + 15.3)) - 0.5;
    float particleDistance = length(particleLocal - particleOffset * 0.68);
    float particlePresent = step(0.86, hash31(particleCell + 31.7));
    float particle = particlePresent * smoothstep(0.15, 0.025, particleDistance) * diskMask;
    vec3 coolerParticleColor = mix(emissionColor, vec3(1.0, 0.09, 0.004), 0.16);
    emission += coolerParticleColor * particle * 13.0;
    density += particle * 0.3;

    // A faint, optically thin inner corona adds depth without filling the shadow.
    float coronaRadial = smoothstep(1.12, 1.55, cylindricalRadius) *
                         (1.0 - smoothstep(2.7, 4.2, cylindricalRadius));
    float corona = coronaRadial * exp(-abs(point.y) * 2.4) * 0.045;
    emission += blackbodyColor(18000.0) * corona * 2.2;
    density += corona;
    vec3 wavelengthExtinction = vec3(0.62, 0.92, 1.34);
    vec3 extinction = density * mix(vec3(1.0), wavelengthExtinction,
                                    clamp(dustLane * 0.85 + 0.1, 0.0, 1.0));
    return MediumSample(emission, extinction);
}

void objectFrame(int index, out vec3 radial, out vec3 tangent,
                 out vec3 bitangent, out vec3 axes) {
    vec3 center = uObjectPositionRadius[index].xyz;
    float radius = uObjectPositionRadius[index].w;
    vec3 velocity = uObjectVelocity[index].xyz;
    float stretch = uObjectProperties[index].z;
    radial = normalize(center);
    tangent = velocity - radial * dot(velocity, radial);
    if (dot(tangent, tangent) < 0.0001) {
        tangent = cross(radial, vec3(0.0, 1.0, 0.0));
        if (dot(tangent, tangent) < 0.0001) {
            tangent = cross(radial, vec3(1.0, 0.0, 0.0));
        }
    }
    tangent = normalize(tangent);
    bitangent = normalize(cross(radial, tangent));
    axes = radius * vec3(1.0 + 1.15 * stretch,
                         max(0.46, 1.0 - 0.34 * stretch),
                         max(0.52, 1.0 - 0.28 * stretch));
}

bool segmentObject(vec3 start, vec3 end, int index, out float hitT) {
    vec3 radial;
    vec3 tangent;
    vec3 bitangent;
    vec3 axes;
    objectFrame(index, radial, tangent, bitangent, axes);
    vec3 offset = start - uObjectPositionRadius[index].xyz;
    vec3 segment = end - start;
    vec3 localOffset = vec3(dot(offset, radial), dot(offset, tangent),
                            dot(offset, bitangent)) / axes;
    vec3 localSegment = vec3(dot(segment, radial), dot(segment, tangent),
                             dot(segment, bitangent)) / axes;
    float a = dot(localSegment, localSegment);
    float b = 2.0 * dot(localOffset, localSegment);
    float c = dot(localOffset, localOffset) - 1.0;
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0 || a < 0.0000001) {
        return false;
    }
    float root = sqrt(discriminant);
    float first = (-b - root) / (2.0 * a);
    float second = (-b + root) / (2.0 * a);
    hitT = first >= 0.0 ? first : second;
    return hitT >= 0.0 && hitT <= 1.0;
}

bool nearestObjectHit(vec3 start, vec3 end, out int objectIndex, out float hitT,
                      out vec3 debrisGlow) {
    objectIndex = -1;
    hitT = 2.0;
    debrisGlow = vec3(0.0);
    vec3 midpoint = (start + end) * 0.5;
    float halfSegmentLength = length(end - start) * 0.5;
    for (int index = 0; index < MAX_OBJECTS; ++index) {
        if (index >= uObjectCount) {
            break;
        }
        float candidateT = 0.0;
        float broadRadius = uObjectPositionRadius[index].w *
                            (1.0 + 1.15 * uObjectProperties[index].z) +
                            halfSegmentLength;
        vec3 broadOffset = midpoint - uObjectPositionRadius[index].xyz;
        if (dot(broadOffset, broadOffset) <= broadRadius * broadRadius &&
            segmentObject(start, end, index, candidateT) && candidateT < hitT) {
            objectIndex = index;
            hitT = candidateT;
        }

        float fractureLevel = uObjectProperties[index].x;
        if (fractureLevel > 0.5) {
            float radius = uObjectPositionRadius[index].w;
            vec3 debrisPosition = (midpoint - uObjectPositionRadius[index].xyz) /
                                  max(radius * 4.5, 0.001);
            float envelope = max(0.0, 1.0 - dot(debrisPosition, debrisPosition));
            if (envelope > 0.0) {
                vec3 particleCoordinates = debrisPosition * 11.0 +
                                           vec3(uObjectProperties[index].y * 37.0);
                vec3 cell = floor(particleCoordinates);
                vec3 local = fract(particleCoordinates) - 0.5;
                vec3 offset = vec3(hash31(cell + 2.3), hash31(cell + 9.7),
                                   hash31(cell + 21.1)) - 0.5;
                float speck = step(0.73, hash31(cell + 41.0)) *
                              smoothstep(0.2, 0.035, length(local - offset * 0.7));
                vec3 debrisColor = mix(uObjectColorRoughness[index].rgb,
                                       vec3(1.0, 0.22, 0.025),
                                       uObjectProperties[index].z * 0.65);
                debrisGlow += debrisColor * speck * envelope *
                              (2.0 + 1.8 * fractureLevel);
            }
        }
    }
    return objectIndex >= 0;
}

vec3 shadeObject(int index, vec3 hitPoint, vec3 rayDirection) {
    vec3 center = uObjectPositionRadius[index].xyz;
    float objectRadius = uObjectPositionRadius[index].w;
    vec3 radial;
    vec3 tangent;
    vec3 bitangent;
    vec3 axes;
    objectFrame(index, radial, tangent, bitangent, axes);
    vec3 localPoint = vec3(dot(hitPoint - center, radial),
                           dot(hitPoint - center, tangent),
                           dot(hitPoint - center, bitangent));
    vec3 normal = normalize(radial * localPoint.x / (axes.x * axes.x) +
                            tangent * localPoint.y / (axes.y * axes.y) +
                            bitangent * localPoint.z / (axes.z * axes.z));
    vec3 viewDirection = normalize(-rayDirection);
    vec4 material = uObjectColorRoughness[index];
    vec3 velocity = uObjectVelocity[index].xyz;
    float fractureLevel = uObjectProperties[index].x;
    float seed = uObjectProperties[index].y;
    float stretch = uObjectProperties[index].z;

    float surfaceNoise = fbm(normal * 4.5 + vec3(seed * 17.0));
    float craters = smoothstep(0.31, 0.52, valueNoise(normal * 13.0 + vec3(seed * 29.0)));
    vec3 albedo = material.rgb * mix(0.48, 1.22, surfaceNoise) * mix(0.72, 1.0, craters);

    vec3 lightDirection = normalize(-hitPoint + vec3(0.0, 0.18, 0.0));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float halfLambert = 0.32 + 0.68 * diffuse;
    vec3 halfVector = normalize(lightDirection + viewDirection);
    float specularPower = mix(72.0, 8.0, material.a);
    float specular = pow(max(dot(normal, halfVector), 0.0), specularPower) *
                     mix(0.8, 0.12, material.a);
    float rim = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.2);

    float distanceToHole = length(hitPoint);
    float tidalHeat = 1.0 - smoothstep(1.2, 4.5, distanceToHole);
    float redshift = sqrt(max(0.0, 1.0 - SCHWARZSCHILD_RADIUS / distanceToHole));
    float relativeMotion = dot(velocity, viewDirection);
    float doppler = clamp(1.0 + relativeMotion * 1.6, 0.55, 1.65);

    float crackWave = abs(sin((normal.x * 1.7 + normal.y * 2.3 + normal.z) *
                              (12.0 + fractureLevel * 4.0) +
                              fbm(normal * 7.0 + vec3(seed * 11.0)) * 7.0));
    float cracks = (1.0 - smoothstep(0.025, 0.115, crackWave)) *
                   clamp(fractureLevel * 0.38 + stretch * 0.58, 0.0, 1.0);
    vec3 heatedRock = blackbodyColor(mix(1200.0, 6200.0, tidalHeat));
    vec3 warmDiskLight = blackbodyColor(5200.0) * halfLambert;
    vec3 color = albedo * (vec3(0.018, 0.026, 0.055) + warmDiskLight * 1.4);
    color += blackbodyColor(7200.0) * specular;
    color += albedo * vec3(0.12, 0.21, 0.42) * rim * 0.45;
    color += heatedRock * tidalHeat * tidalHeat *
             (0.35 + 0.65 * surfaceNoise) * 2.4;
    color += heatedRock * cracks * (1.5 + 3.0 * tidalHeat);
    return color * redshift * doppler / max(objectRadius * 0.08 + 0.92, 0.1);
}

vec3 traceRay(vec3 origin, vec3 direction) {
    vec3 position = origin;
    vec3 velocity = normalize(direction);
    vec3 radiance = vec3(0.0);
    vec3 transmittance = vec3(1.0);
    float photonOrbitGlow = 0.0;

    for (int stepIndex = 0; stepIndex < MAX_STEPS; ++stepIndex) {
        float radius = length(position);
        if (radius <= SCHWARZSCHILD_RADIUS * 1.002) {
            return radiance;
        }
        if (radius > ESCAPE_RADIUS && dot(position, velocity) > 0.0) {
            vec3 background = starfield(velocity);
            vec3 ringEmission = vec3(1.0, 0.41, 0.085) * photonOrbitGlow * 0.016;
            return radiance + transmittance * (background + ringEmission);
        }

        float stepSize = radius < 4.0
            ? clamp(radius * 0.026, 0.008, 0.105)
            : clamp(radius * 0.045, 0.03, 0.30);
        float cylindricalRadius = length(position.xz);
        if (cylindricalRadius < DISK_OUTER_RADIUS + 1.0 && abs(position.y) < 1.15) {
            stepSize = min(stepSize, 0.075);
        }
        vec3 previousPosition = position;
        vec3 previousVelocity = velocity;
        integrateGeodesic(position, velocity, stepSize);

        vec3 angularMomentum = cross(previousPosition, previousVelocity);
        float angularWeight = smoothstep(0.65, 1.45, length(angularMomentum));
        photonOrbitGlow += exp(-18.0 * abs(radius - PHOTON_SPHERE_RADIUS)) *
                           angularWeight * stepSize;

        int objectIndex = -1;
        float objectT = 1.0;
        bool hitObject = false;
        vec3 debrisGlow = vec3(0.0);
        if (uObjectCount > 0 && radius < 12.5) {
            hitObject = nearestObjectHit(previousPosition, position, objectIndex, objectT,
                                         debrisGlow);
        }
        float traversedFraction = hitObject ? objectT : 1.0;
        vec3 samplePoint = mix(previousPosition, position, traversedFraction * 0.5);
        vec3 sampleDirection = normalize(mix(previousVelocity, velocity, traversedFraction * 0.5));
        MediumSample flow = sampleAccretionFlow(samplePoint, sampleDirection);
        vec3 opticalDepth = flow.extinction * stepSize * traversedFraction * 2.7;
        vec3 absorbed = vec3(1.0) - exp(-opticalDepth);
        radiance += transmittance * flow.emission * absorbed;
        radiance += transmittance * debrisGlow * stepSize * 0.85;
        transmittance *= vec3(1.0) - absorbed * 0.92;

        if (hitObject) {
            vec3 hitPoint = mix(previousPosition, position, objectT);
            return radiance + transmittance * shadeObject(objectIndex, hitPoint, sampleDirection);
        }
        if (max(transmittance.r, max(transmittance.g, transmittance.b)) < 0.008) {
            return radiance;
        }
    }
    return radiance;
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec4 farPoint = uInverseViewProjection * vec4(ndc, 1.0, 1.0);
    farPoint /= farPoint.w;
    vec3 rayDirection = normalize(farPoint.xyz - uCameraPosition);
    vec3 color = traceRay(uCameraPosition, rayDirection);

    // ACES-inspired filmic curve preserves the white-hot inner disk highlights.
    color = max(color, vec3(0.0));
    color = (color * (2.51 * color + 0.03)) /
            (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, 0.0, 1.0);

    vec2 centered = vUV - 0.5;
    color *= 1.0 - 0.2 * dot(centered, centered);
    float dither = hash12(gl_FragCoord.xy + fract(uTime) * 73.0) - 0.5;
    color += dither / max(255.0, max(uResolution.x, uResolution.y) * 0.35);
    fragColor = vec4(color, 1.0);
}
