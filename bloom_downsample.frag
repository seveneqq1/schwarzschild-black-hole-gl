#version 410 core

// 13-tap downsample from Jimenez, "Next Generation Post Processing in Call of
// Duty: Advanced Warfare" (SIGGRAPH 2014). The first level uses Karis-weighted
// partial averages so single hot pixels cannot flicker into large blooms.

uniform sampler2D uSource;
uniform vec2 uSourceTexel;
uniform int uKarisAverage;

in vec2 vUv;
out vec4 outColor;

vec3 fetch(vec2 offset) {
    return textureLod(uSource, vUv + offset * uSourceTexel, 0.0).rgb;
}

float karisWeight(vec3 c) {
    return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722)));
}

vec3 average(vec3 a, vec3 b, vec3 c, vec3 d) {
    if (uKarisAverage == 0) {
        return 0.25 * (a + b + c + d);
    }
    float wa = karisWeight(a), wb = karisWeight(b), wc = karisWeight(c), wd = karisWeight(d);
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

void main() {
    vec3 a = fetch(vec2(-2.0, 2.0)), b = fetch(vec2(0.0, 2.0)), c = fetch(vec2(2.0, 2.0));
    vec3 d = fetch(vec2(-2.0, 0.0)), e = fetch(vec2(0.0, 0.0)), f = fetch(vec2(2.0, 0.0));
    vec3 g = fetch(vec2(-2.0, -2.0)), h = fetch(vec2(0.0, -2.0)), i = fetch(vec2(2.0, -2.0));
    vec3 j = fetch(vec2(-1.0, 1.0)), k = fetch(vec2(1.0, 1.0));
    vec3 l = fetch(vec2(-1.0, -1.0)), m = fetch(vec2(1.0, -1.0));

    vec3 result = average(j, k, l, m) * 0.5 +
                  average(a, b, d, e) * 0.125 + average(b, c, e, f) * 0.125 +
                  average(d, e, g, h) * 0.125 + average(e, f, h, i) * 0.125;
    outColor = vec4(max(result, vec3(0.0)), 1.0);
}
