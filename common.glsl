// Shared physics and colour helpers. Units: G = c = 1, Schwarzschild radius r_s = 1.

const float PI = 3.14159265359;
const float TAU = 6.28318530718;
const float BLACK_HOLE_MASS = 0.5;
const float ISCO_RADIUS = 3.0;
const float DISK_FADE_START = 8.5;
const float DISK_OUTER_EDGE = 12.0;

// Blackbody LUT: rgb = unit-luminance linear-sRGB chromaticity, a = log2 of the
// photopic luminance relative to 6500 K. Log-spaced over 400 K .. 120 000 K.
uniform sampler2D uBlackbody;
uniform float uLuminanceExponent;

vec4 blackbodySample(float temperature) {
    const float logMin = 8.6438562;   // log2(400)
    const float logRange = 8.2288187; // log2(120000 / 400)
    float coordinate = (log2(max(temperature, 1.0)) - logMin) / logRange;
    return textureLod(uBlackbody, vec2(clamp(coordinate, 0.0, 1.0) * (511.0 / 512.0) + 0.5 / 512.0, 0.5), 0.0);
}

// Visible radiance of a blackbody with luminance compressed as L^kappa so that
// a 1500 K ember and a 30 000 K shock share one displayable range.
vec3 thermalRadiance(float temperature) {
    if (temperature < 450.0) {
        return vec3(0.0);
    }
    vec4 bb = blackbodySample(temperature);
    float fadeIn = smoothstep(450.0, 900.0, temperature);
    return bb.rgb * (exp2(uLuminanceExponent * bb.a) * fadeIn);
}

vec3 blackbodyChroma(float temperature) {
    return blackbodySample(temperature).rgb;
}

// Must match physics::diskScaleHeight.
float diskScaleHeight(float cylindricalRadius) {
    return 0.024 * cylindricalRadius + 0.025;
}

// Novikov-Thorne / Page-Thorne effective temperature for a Schwarzschild hole
// with zero torque at the ISCO, normalised to 1 at its peak (r = 4.78 r_s).
float pageThorneTemperature(float radius) {
    if (radius <= ISCO_RADIUS + 1.0e-3) {
        return 0.0;
    }
    const float sqrt3 = 1.7320508;
    const float x0 = 2.4494897;
    float x = sqrt(2.0 * radius);
    float bracket = x - x0 - 0.5 * sqrt3 * log((x - sqrt3) / (x0 - sqrt3)) +
                    0.5 * sqrt3 * log((x + sqrt3) / (x0 + sqrt3));
    float flux = bracket / (x * x * x * x * x * (x * x - 3.0));
    return pow(max(flux, 0.0) / 1.1458903e-4, 0.25);
}

// Local speed of a circular geodesic seen by a static observer; 0.5 c at the ISCO.
float circularOrbitSpeed(float radius) {
    return sqrt(BLACK_HOLE_MASS / max(radius - 1.0, 0.05));
}

// Combined gravitational and Doppler frequency ratio nu_obs / nu_emit for an
// emitter at `radius` moving with local speed `speed`, where `cosine` is the
// angle between its velocity and the photon direction in the static frame.
float frequencyShift(float radius, float speed, float cosine, float observerLapse) {
    float lapse = sqrt(max(1.0 - 1.0 / radius, 1.0e-4));
    speed = min(speed, 0.97);
    return lapse * sqrt(1.0 - speed * speed) / ((1.0 - speed * cosine) * observerLapse);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 hash33(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

// Jimenez interleaved gradient noise.
float interleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}
