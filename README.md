# Schwarzschild Black Hole — OpenGL 4.1

A standalone C++20/macOS visualization of a Schwarzschild black hole. Light rays
are integrated in a GLSL 4.10 fragment shader, producing an event-horizon shadow,
a photon ring, gravitationally lensed procedural stars, and a turbulent accretion
disk with approximate Doppler beaming and gravitational redshift.

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

Drag with the left mouse button to orbit, use the scroll wheel to zoom, press
`R` to reset the camera, and press `Esc` to quit.

## Implementation notes

The renderer uses one oversized fullscreen triangle and performs all scene work
inside `blackhole.frag`. It stays within OpenGL 4.1 Core: there are no compute
shaders, shader storage buffers, or post-4.1 extension dependencies. The shader
uses midpoint integration of the supplied weak-field Schwarzschild null-geodesic
equation. This is a physically inspired real-time visualization rather than a
precision general-relativity solver.
