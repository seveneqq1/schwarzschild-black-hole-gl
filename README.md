# Schwarzschild Black Hole — OpenGL 4.1

A standalone C++20/macOS visualization of a Schwarzschild black hole. Light rays
are integrated with RK4 in a GLSL 4.10 fragment shader, producing an event-horizon
shadow, a photon ring, gravitationally lensed procedural stars and nebulae, and a
volumetric turbulent accretion flow with wavelength-dependent dust extinction,
blackbody color, Doppler beaming, gravitational redshift, and orbiting particles.
Individually simulated objects undergo stress-based tidal failure, radial stretching,
transverse compression, partial core survival, ballistic debris-stream formation,
collision heating, fallback brightening, gravitational lensing, and horizon capture.

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

The app always covers the native Retina framebuffer, but the expensive ray pass uses
an offscreen target and a fullscreen upscale. The default 960×540 performance mode
is intended for smooth interaction. Use `--quality` for 1280×720 ray tracing or
`--retina` for native-density rendering. Add `--benchmark` to print measured FPS
every two seconds.

## Controls

- Left mouse drag: orbit the camera
- Scroll: zoom
- `Space`: spawn one infalling object (up to 24 bodies/fragments)
- `Backspace`: remove all objects
- `O`: reset the default objects
- `P`: pause/resume object physics
- `Q`: toggle performance/quality render resolution
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
satellite.density = 0.9f;
satellite.cohesion = 0.8f; // Lower values accumulate tidal damage earlier.
state.sceneObjects.addObject(satellite);
```

Position and radius use units where the event-horizon radius is `1.0`. Velocity
uses units where the speed of light is `1.0`.

The disruption approximation and its source literature are described in
[PHYSICS.md](PHYSICS.md).

## Implementation notes

The renderer uses one oversized fullscreen triangle and performs all scene work
inside `blackhole.frag`. Dynamic bodies are passed through ordinary uniform arrays,
keeping the renderer within OpenGL 4.1 Core: there are no compute shaders, shader
storage buffers, or post-4.1 extension dependencies. This remains a physically
inspired real-time visualization rather than a precision general-relativity solver.
The ray marcher uses RK4 only in strong-field space and midpoint integration farther
out, exits after leaving the lensing region, adaptively samples the disk, culls
fragments with a broad phase, and caches uniform locations. Noise octaves are reserved
for scales that remain visible at the output resolution.
