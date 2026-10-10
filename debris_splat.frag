#version 410 core

flat in vec3 gCenter;
flat in float gInverseRadiusSquared;
flat in float gWeight;
flat in vec3 gVelocity;
flat in vec3 gThermal;

layout(location = 0) out vec4 outComposition; // rho, rho T/1e4, rho dust, rho ice
layout(location = 1) out vec4 outMomentum;    // rho v

void main() {
    vec2 delta = gl_FragCoord.xy - gCenter.xy;
    float q2 = (dot(delta, delta) + gCenter.z * gCenter.z) * gInverseRadiusSquared;
    if (q2 >= 1.0) {
        discard;
    }
    float kernel = (1.0 - q2) * (1.0 - q2);
    float density = gWeight * kernel;
    outComposition = density * vec4(1.0, gThermal);
    outMomentum = vec4(density * gVelocity, 0.0);
}
