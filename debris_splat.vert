#version 410 core

layout(location = 0) in vec4 aPositionSize; // world position, smoothing radius
layout(location = 1) in vec4 aVelocityMass; // velocity, mass relative to a reference fragment
layout(location = 2) in vec4 aThermal;      // temperature / 1e4, dust, ice, unused

out VertexData {
    vec4 positionSize;
    vec4 velocityMass;
    vec4 thermal;
} vOut;

void main() {
    vOut.positionSize = aPositionSize;
    vOut.velocityMass = aVelocityMass;
    vOut.thermal = aThermal;
    gl_Position = vec4(0.0);
}
