#include <algorithm>
#include <cmath>
#include <cstddef>

#include "SceneObjects.hpp"

int main() {
    SceneObjects conservationTest;
    conservationTest.clear();
    SceneObject failingBody;
    failingBody.position = {3.2f, 0.0f, 0.0f};
    failingBody.velocity = {0.0f, 0.0f, 0.18f};
    failingBody.radius = 0.4f;
    failingBody.density = 1.1f;
    failingBody.damage = 1.1f;
    conservationTest.addObject(failingBody);
    const float massBeforeBreakup = conservationTest.totalMass();
    const glm::vec3 momentumBeforeBreakup = conservationTest.totalMomentum();
    conservationTest.update(0.0f);
    if (conservationTest.size() != 3 ||
        std::abs(conservationTest.totalMass() - massBeforeBreakup) > 0.00001f ||
        glm::length(conservationTest.totalMomentum() - momentumBeforeBreakup) > 0.00001f) {
        return 3;
    }

    SceneObjects simulation;
    std::size_t largestPopulation = simulation.size();

    // Simulate one minute at 60 Hz. The default sub-circular bodies must enter
    // the tidal region and fragment while never exceeding the uniform capacity.
    for (int frame = 0; frame < 60 * 60; ++frame) {
        simulation.update(1.0f / 60.0f);
        largestPopulation = std::max(largestPopulation, simulation.size());
        if (simulation.size() > SceneObjects::MaxObjects) {
            return 1;
        }
    }

    return largestPopulation > 2 ? 0 : 2;
}
