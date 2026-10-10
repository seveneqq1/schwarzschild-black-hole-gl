# Schwarzschild Black Hole — Real-Time OpenGL 4.1

A real-time C++20 / OpenGL 4.1 renderer of a Schwarzschild black hole with a
physically motivated accretion disk and tidal disruption of infalling bodies.

## Features

- **Exact lensing:** null geodesics integrated per pixel (Binet equation, RK4) give the
  shadow, photon ring, secondary image and correctly magnified stars and Milky Way.
- **Accretion disk:** Page–Thorne / Novikov–Thorne temperature profile, Doppler and
  gravitational redshift, Chandrasekhar limb darkening, flow-advected turbulence.
- **Tidal disruption:** Roche ellipsoids (Chandrasekhar), Kelvin f-mode, L1/L2 mass
  shedding, clump formation, collisions, disk drag, radiative cooling, dust sublimation.
- **Debris rendering:** GPU-splatted 3D volumes with emission, absorption and scattering.
- **Post-processing:** blackbody colour LUT (Wyman et al. 2013), bloom, ACES tone mapping.

See [PHYSICS.md](PHYSICS.md) for the model details.

## Requirements

- A GPU/driver supporting **OpenGL 4.1 Core**
- **CMake 3.20+** and a **C++20** compiler
- Libraries: **GLFW 3**, **GLEW**, **GLM**, **zlib**

## macOS

Install the toolchain and libraries (Homebrew):

```bash
xcode-select --install
brew install cmake glfw glew glm
```

Clone, build, run (works from any directory):

```bash
git clone https://github.com/seveneqq1/schwarzschild-black-hole-gl.git
cd schwarzschild-black-hole-gl
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/schwarzschild_black_hole
```

## Windows

Install [Visual Studio 2022](https://visualstudio.microsoft.com/) (with "Desktop
development with C++"), [CMake](https://cmake.org/download/) and
[Git](https://git-scm.com/). Get the libraries with
[vcpkg](https://github.com/microsoft/vcpkg).

In **PowerShell**:

```powershell
git clone https://github.com/microsoft/vcpkg $HOME\vcpkg
& $HOME\vcpkg\bootstrap-vcpkg.bat
& $HOME\vcpkg\vcpkg install glfw3:x64-windows glew:x64-windows glm:x64-windows zlib:x64-windows

git clone https://github.com/seveneqq1/schwarzschild-black-hole-gl.git
cd schwarzschild-black-hole-gl
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$HOME\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
.\build\Release\schwarzschild_black_hole.exe
```

> Windows support is provided on a best-effort basis; the project was developed on macOS.
> If the window fails to open, update your GPU driver (OpenGL 4.1+ is required).

## Command-line options

| Option | Effect |
|---|---|
| `--retina` / `--quality` | Render at full framebuffer resolution |
| `--fast` | Render at half resolution |
| `--benchmark` | Print FPS and GPU timings |
| `--spawn n` | Spawn `n` extra bodies |
| `--camera yaw,pitch,dist` | Set the initial camera |
| `--capture file.png` | Save a screenshot and exit (with `--warmup s` to skip ahead) |
| `--exit-after s` | Quit after `s` seconds |
| `--empty` | Start without bodies |

Example: `./build/schwarzschild_black_hole --retina --camera 125,28,10`

## Controls

| Key | Action |
|---|---|
| `Q` | Cycle render scale (0.5 / 0.75 / 1.0) |
| `B` | Toggle bloom |
| `[` / `]` | Slower / faster time |
| `-` / `=` | Exposure |
| `Space`, `Backspace`, `O`, `P`, `R`, `Esc` | See `main.cpp` |

## Tests

```bash
cmake --build build --target scene_objects_test
ctest --test-dir build        # add -C Release on Windows
```

## License

MIT — see [LICENSE](LICENSE).
