#version 410 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uScene;

void main() {
    fragColor = vec4(texture(uScene, vUV).rgb, 1.0);
}
