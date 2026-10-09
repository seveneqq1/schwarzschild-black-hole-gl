#include <algorithm>
#include <cstddef>

#include "SceneObjects.hpp"

int main() {
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
