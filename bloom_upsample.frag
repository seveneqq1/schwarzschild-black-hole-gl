#version 410 core

// 3x3 tent upsample, additively blended into the next larger mip.

uniform sampler2D uSource;
uniform vec2 uSourceTexel;
uniform float uRadius;

in vec2 vUv;
out vec4 outColor;

void main() {
    vec2 r = uSourceTexel * uRadius;
    vec3 sum = textureLod(uSource, vUv, 0.0).rgb * 4.0;
    sum += (textureLod(uSource, vUv + vec2(-r.x, 0.0), 0.0).rgb +
            textureLod(uSource, vUv + vec2(r.x, 0.0), 0.0).rgb +
            textureLod(uSource, vUv + vec2(0.0, -r.y), 0.0).rgb +
            textureLod(uSource, vUv + vec2(0.0, r.y), 0.0).rgb) * 2.0;
    sum += textureLod(uSource, vUv + vec2(-r.x, -r.y), 0.0).rgb +
           textureLod(uSource, vUv + vec2(r.x, -r.y), 0.0).rgb +
           textureLod(uSource, vUv + vec2(-r.x, r.y), 0.0).rgb +
           textureLod(uSource, vUv + vec2(r.x, r.y), 0.0).rgb;
    outColor = vec4(sum / 16.0, 1.0);
}
