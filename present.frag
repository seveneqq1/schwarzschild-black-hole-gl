#version 410 core

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform vec2 uSceneSize;
uniform vec2 uOutputSize;
uniform float uExposure;
uniform float uBloomStrength;

in vec2 vUv;
out vec4 outColor;

// Bicubic Catmull-Rom in nine bilinear taps, clamped to the 2x2 neighbourhood
// so bright stars cannot ring into dark halos.
vec3 sampleCatmullRom(vec2 uv) {
    vec2 position = uv * uSceneSize;
    vec2 center = floor(position - 0.5) + 0.5;
    vec2 f = position - center;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;
    vec2 texel = 1.0 / uSceneSize;
    vec2 t0 = (center - 1.0) * texel;
    vec2 t3 = (center + 2.0) * texel;
    vec2 t12 = (center + offset12) * texel;

    vec3 result =
        (textureLod(uScene, vec2(t0.x, t0.y), 0.0).rgb * w0.x +
         textureLod(uScene, vec2(t12.x, t0.y), 0.0).rgb * w12.x +
         textureLod(uScene, vec2(t3.x, t0.y), 0.0).rgb * w3.x) * w0.y +
        (textureLod(uScene, vec2(t0.x, t12.y), 0.0).rgb * w0.x +
         textureLod(uScene, vec2(t12.x, t12.y), 0.0).rgb * w12.x +
         textureLod(uScene, vec2(t3.x, t12.y), 0.0).rgb * w3.x) * w12.y +
        (textureLod(uScene, vec2(t0.x, t3.y), 0.0).rgb * w0.x +
         textureLod(uScene, vec2(t12.x, t3.y), 0.0).rgb * w12.x +
         textureLod(uScene, vec2(t3.x, t3.y), 0.0).rgb * w3.x) * w3.y;

    vec2 base = center * texel;
    vec3 a = textureLod(uScene, base, 0.0).rgb;
    vec3 b = textureLod(uScene, base + vec2(texel.x, 0.0), 0.0).rgb;
    vec3 c = textureLod(uScene, base + vec2(0.0, texel.y), 0.0).rgb;
    vec3 d = textureLod(uScene, base + texel, 0.0).rgb;
    return clamp(result, min(min(a, b), min(c, d)), max(max(a, b), max(c, d)));
}

// Stephen Hill's fit of the ACES RRT + sRGB ODT.
vec3 acesFitted(vec3 color) {
    const mat3 inputMatrix = mat3(0.59719, 0.07600, 0.02840,
                                  0.35458, 0.90834, 0.13383,
                                  0.04823, 0.01566, 0.83777);
    const mat3 outputMatrix = mat3(1.60475, -0.10208, -0.00327,
                                   -0.53108, 1.10813, -0.07276,
                                   -0.07367, -0.00605, 1.07602);
    color = inputMatrix * color;
    color = (color * (color + 0.0245786) - 0.000090537) /
            (color * (0.983729 * color + 0.4329510) + 0.238081);
    return clamp(outputMatrix * color, 0.0, 1.0);
}

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec3 scene = sampleCatmullRom(vUv);
    vec3 bloom = textureLod(uBloom, vUv, 0.0).rgb;
    vec3 color = mix(scene, bloom, uBloomStrength) * uExposure;
    // ACES desaturates bright blackbody colours; restore a little chroma first.
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = max(mix(vec3(luminance), color, 1.12), vec3(0.0));
    vec3 display = linearToSrgb(acesFitted(color));
    // Triangular dither removes banding in the dark sky gradients.
    vec2 pixel = gl_FragCoord.xy;
    float n0 = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
    float n1 = fract(52.9829189 * fract(dot(pixel + 17.0, vec2(0.06711056, 0.00583715))));
    display += (n0 + n1 - 1.0) / 255.0;
    outColor = vec4(display, 1.0);
}
