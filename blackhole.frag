#version 410 core

in vec2 vUV;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uCameraPosition;
uniform mat4 uInverseViewProjection;

const int MAX_OBJECTS = 16;
uniform int uObjectCount;
uniform vec4 uObjectPositionRadius[MAX_OBJECTS];
uniform vec4 uObjectColorRoughness[MAX_OBJECTS];
uniform vec4 uObjectVelocity[MAX_OBJECTS];

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
    // Fourth-order Runge-Kutta integration of the null-geodesic approximation.
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

vec4 sampleAccretionFlow(vec3 point, vec3 rayDirection) {
    float cylindricalRadius = length(point.xz);
    if (cylindricalRadius < 1.08 || cylindricalRadius > DISK_OUTER_RADIUS + 0.8 ||
        abs(point.y) > 1.1) {
        return vec4(0.0);
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

    float diskMask = innerEdge * outerEdge * verticalProfile;
    float innerHeat = pow(1.0 - normalizedRadius, 1.55);
    vec3 emissionColor = mix(vec3(0.95, 0.018, 0.001),
                             vec3(1.0, 0.44, 0.035), innerHeat);
    emissionColor = mix(emissionColor, vec3(1.0, 0.96, 0.78), pow(innerHeat, 3.0));

    vec3 tangent = normalize(vec3(-point.z, 0.0, point.x));
    float orbitalSpeed = min(0.57, sqrt(GM / max(cylindricalRadius, 1.01)));
    float doppler = clamp(1.0 + 2.35 * orbitalSpeed * dot(tangent, -rayDirection), 0.28, 2.35);
    float gravitationalRedshift = sqrt(max(0.0, 1.0 -
                                           SCHWARZSCHILD_RADIUS / cylindricalRadius));
    float density = diskMask * (0.55 + 2.3 * knots);
    vec3 emission = emissionColor * gravitationalRedshift * pow(doppler, 2.4) *
                    (0.7 + 3.2 * knots);

    // A faint, optically thin inner corona adds depth without filling the shadow.
    float coronaRadial = smoothstep(1.12, 1.55, cylindricalRadius) *
                         (1.0 - smoothstep(2.7, 4.2, cylindricalRadius));
    float corona = coronaRadial * exp(-abs(point.y) * 2.4) * 0.045;
    emission += vec3(0.28, 0.43, 0.72) * corona * 3.0;
    density += corona;
    return vec4(emission, density);
}

bool segmentSphere(vec3 start, vec3 end, vec3 center, float radius, out float hitT) {
    vec3 segment = end - start;
    vec3 offset = start - center;
    float a = dot(segment, segment);
    float b = 2.0 * dot(offset, segment);
    float c = dot(offset, offset) - radius * radius;
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

bool nearestObjectHit(vec3 start, vec3 end, out int objectIndex, out float hitT) {
    objectIndex = -1;
    hitT = 2.0;
    for (int index = 0; index < MAX_OBJECTS; ++index) {
        if (index >= uObjectCount) {
            break;
        }
        float candidateT = 0.0;
        if (segmentSphere(start, end, uObjectPositionRadius[index].xyz,
                          uObjectPositionRadius[index].w, candidateT) &&
            candidateT < hitT) {
            objectIndex = index;
            hitT = candidateT;
        }
    }
    return objectIndex >= 0;
}

vec3 shadeObject(int index, vec3 hitPoint, vec3 rayDirection) {
    vec3 center = uObjectPositionRadius[index].xyz;
    float objectRadius = uObjectPositionRadius[index].w;
    vec3 normal = normalize(hitPoint - center);
    vec3 viewDirection = normalize(-rayDirection);
    vec4 material = uObjectColorRoughness[index];
    vec3 velocity = uObjectVelocity[index].xyz;

    float surfaceNoise = fbm(normal * 4.5 + vec3(float(index) * 2.7));
    float craters = smoothstep(0.31, 0.52, valueNoise(normal * 13.0 + float(index) * 9.0));
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

    vec3 warmDiskLight = vec3(1.0, 0.29, 0.045) * halfLambert;
    vec3 color = albedo * (vec3(0.018, 0.026, 0.055) + warmDiskLight * 1.4);
    color += vec3(1.0, 0.72, 0.36) * specular;
    color += albedo * vec3(0.12, 0.21, 0.42) * rim * 0.45;
    color += vec3(1.0, 0.055, 0.004) * tidalHeat * tidalHeat *
             (0.35 + 0.65 * surfaceNoise) * 2.4;
    return color * redshift * doppler / max(objectRadius * 0.08 + 0.92, 0.1);
}

vec3 traceRay(vec3 origin, vec3 direction) {
    vec3 position = origin;
    vec3 velocity = normalize(direction);
    vec3 radiance = vec3(0.0);
    float transmittance = 1.0;
    float photonOrbitGlow = 0.0;

    for (int stepIndex = 0; stepIndex < MAX_STEPS; ++stepIndex) {
        float radius = length(position);
        if (radius <= SCHWARZSCHILD_RADIUS * 1.002) {
            return radiance;
        }
        if (radius > ESCAPE_RADIUS && dot(position, velocity) > 0.0) {
            vec3 background = starfield(velocity);
            vec3 ringEmission = vec3(1.0, 0.32, 0.035) * photonOrbitGlow * 0.018;
            return radiance + transmittance * (background + ringEmission);
        }

        float stepSize = clamp(radius * 0.034, 0.009, 0.21);
        vec3 previousPosition = position;
        vec3 previousVelocity = velocity;
        integrateGeodesic(position, velocity, stepSize);

        vec3 angularMomentum = cross(previousPosition, previousVelocity);
        float angularWeight = smoothstep(0.65, 1.45, length(angularMomentum));
        photonOrbitGlow += exp(-18.0 * abs(radius - PHOTON_SPHERE_RADIUS)) *
                           angularWeight * stepSize;

        int objectIndex = -1;
        float objectT = 1.0;
        bool hitObject = nearestObjectHit(previousPosition, position, objectIndex, objectT);
        float traversedFraction = hitObject ? objectT : 1.0;
        vec3 samplePoint = mix(previousPosition, position, traversedFraction * 0.5);
        vec3 sampleDirection = normalize(mix(previousVelocity, velocity, traversedFraction * 0.5));
        vec4 flow = sampleAccretionFlow(samplePoint, sampleDirection);
        float opticalDepth = flow.a * stepSize * traversedFraction * 2.7;
        float absorbed = 1.0 - exp(-opticalDepth);
        radiance += transmittance * flow.rgb * absorbed;
        transmittance *= 1.0 - absorbed * 0.92;

        if (hitObject) {
            vec3 hitPoint = mix(previousPosition, position, objectT);
            return radiance + transmittance * shadeObject(objectIndex, hitPoint, sampleDirection);
        }
        if (transmittance < 0.008) {
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
