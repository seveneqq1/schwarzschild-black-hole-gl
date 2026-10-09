# Schwarzschild Black Hole — OpenGL 4.1

A standalone C++20/macOS visualization of a Schwarzschild black hole. Light rays
are integrated with RK4 in a GLSL 4.10 fragment shader, producing an event-horizon
shadow, a photon ring, gravitationally lensed procedural stars and nebulae, and a
volumetric turbulent accretion flow with wavelength-dependent dust extinction,
blackbody color, Doppler beaming, gravitational redshift, and orbiting particles.
Individually simulated objects orbit, lose angular momentum, stretch under tidal
forces, fracture into hot debris, lens correctly, and disappear after crossing the
event horizon.

## Requirements

- macOS on Apple Silicon or Intel
- Xcode Command Line Tools
- CMake 3.20+
- GLFW, GLM, and GLEW

Install the dependencies with Homebrew:

```bash
brew install cmake glfw glm glew
```

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/schwarzschild_black_hole
```

## Controls

- Left mouse drag: orbit the camera
- Scroll: zoom
- `Space`: spawn one infalling object (up to 24 bodies/fragments)
- `Backspace`: remove all objects
- `O`: reset the default objects
- `P`: pause/resume object physics
- `R`: reset the camera
- `Esc`: quit

## Adding custom objects

`SceneObjects.hpp` exposes a small API. Add a body anywhere you have access to the
`SceneObjects` instance:

```cpp
SceneObject satellite;
satellite.position = {8.0f, 1.0f, 0.0f};
satellite.velocity = {-0.04f, 0.0f, 0.18f};
satellite.radius = 0.3f;
satellite.albedo = {0.2f, 0.55f, 0.9f};
satellite.roughness = 0.35f;
satellite.cohesion = 0.8f; // Lower values break apart slightly earlier.
state.sceneObjects.addObject(satellite);
```

Position and radius use units where the event-horizon radius is `1.0`. Velocity
uses units where the speed of light is `1.0`.

## Implementation notes

The renderer uses one oversized fullscreen triangle and performs all scene work
inside `blackhole.frag`. Dynamic bodies are passed through ordinary uniform arrays,
keeping the renderer within OpenGL 4.1 Core: there are no compute shaders, shader
storage buffers, or post-4.1 extension dependencies. This remains a physically
inspired real-time visualization rather than a precision general-relativity solver.
The ray marcher uses RK4 near the black hole and midpoint integration in weak-field
space, adaptive disk sampling, broad-phase fragment culling, and cached uniform
locations to reduce GPU and CPU work without reducing near-hole detail.
