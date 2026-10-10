#version 410 core

// Rasterises each debris particle's compact kernel into every layer of the 3D
// volume it overlaps. Volume axes: x -> texel x, world z -> texel y, world y -> layer.
layout(points) in;
layout(triangle_strip, max_vertices = 24) out;

in VertexData {
    vec4 positionSize;
    vec4 velocityMass;
    vec4 thermal;
} vIn[];

uniform vec3 uVolumeMin;
uniform float uVoxelSize;
uniform vec3 uVolumeResolution;
uniform float uMinimumKernel; // in voxels

flat out vec3 gCenter;   // voxel-space centre (x, y) and offset of this layer
flat out float gInverseRadiusSquared;
flat out float gWeight;
flat out vec3 gVelocity;
flat out vec3 gThermal;

void main() {
    vec3 world = (vIn[0].positionSize.xyz - uVolumeMin) / uVoxelSize;
    vec3 voxel = world.xzy;
    float radius = clamp(vIn[0].positionSize.w / uVoxelSize, uMinimumKernel, 2.5);
    if (any(lessThan(voxel + radius, vec3(0.0))) ||
        any(greaterThan(voxel - radius, uVolumeResolution))) {
        return;
    }

    // (1 - q^2)^2 integrates to 0.9574 h^3; weights are density in world units.
    float worldRadius = radius * uVoxelSize;
    float weight = 1.0e-3 * vIn[0].velocityMass.w / (0.9574 * worldRadius * worldRadius * worldRadius);

    int firstLayer = max(int(ceil(voxel.z - radius - 0.5)), 0);
    int lastLayer = min(int(floor(voxel.z + radius - 0.5)), int(uVolumeResolution.z) - 1);
    lastLayer = min(lastLayer, firstLayer + 5);

    for (int layer = firstLayer; layer <= lastLayer; ++layer) {
        float dz = float(layer) + 0.5 - voxel.z;
        float sliceSquared = radius * radius - dz * dz;
        if (sliceSquared <= 0.0) {
            continue;
        }
        float slice = sqrt(sliceSquared) + 0.5;
        vec2 low = (voxel.xy - slice) / uVolumeResolution.xy * 2.0 - 1.0;
        vec2 high = (voxel.xy + slice) / uVolumeResolution.xy * 2.0 - 1.0;
        vec2 corners[4] = vec2[4](vec2(low.x, low.y), vec2(high.x, low.y),
                                  vec2(low.x, high.y), vec2(high.x, high.y));
        for (int corner = 0; corner < 4; ++corner) {
            gCenter = vec3(voxel.xy, dz);
            gInverseRadiusSquared = 1.0 / (radius * radius);
            gWeight = weight;
            gVelocity = vIn[0].velocityMass.xyz;
            gThermal = vIn[0].thermal.xyz;
            gl_Layer = layer;
            gl_Position = vec4(corners[corner], 0.0, 1.0);
            EmitVertex();
        }
        EndPrimitive();
    }
}
