#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "Blackbody.hpp"
#include "Camera.hpp"
#include "NoiseVolume.hpp"
#include "PngWriter.hpp"
#include "RenderResources.hpp"
#include "SceneObjects.hpp"
#include "Shader.hpp"

namespace {

struct LaunchOptions {
    bool benchmark = false;
    bool emptyScene = false;
    float renderScale = 0.75f;
    std::string capturePath;
    float warmupSeconds = 0.0f;
    float exitAfterSeconds = 0.0f;
    bool customCamera = false;
    glm::vec3 camera{35.0f, 18.0f, 18.0f};
    int spawnCount = 0;
};

LaunchOptions parseOptions(int argc, char** argv) {
    LaunchOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        const bool hasValue = index + 1 < argc;
        if (argument == "--retina" || argument == "--quality") {
            options.renderScale = 1.0f;
        } else if (argument == "--fast") {
            options.renderScale = 0.5f;
        } else if (argument == "--benchmark") {
            options.benchmark = true;
        } else if (argument == "--empty") {
            options.emptyScene = true;
        } else if (argument == "--capture" && hasValue) {
            options.capturePath = argv[++index];
        } else if (argument == "--warmup" && hasValue) {
            options.warmupSeconds = std::stof(argv[++index]);
        } else if (argument == "--exit-after" && hasValue) {
            options.exitAfterSeconds = std::stof(argv[++index]);
        } else if (argument == "--spawn" && hasValue) {
            options.spawnCount = std::stoi(argv[++index]);
        } else if (argument == "--camera" && hasValue) {
            // yaw,pitch,distance in degrees and r_s
            std::istringstream values(argv[++index]);
            char separator = 0;
            values >> options.camera.x >> separator >> options.camera.y >> separator >> options.camera.z;
            options.customCamera = true;
        } else {
            std::cerr << "Ignoring unknown option " << argument << '\n';
        }
    }
    return options;
}

struct AppState {
    OrbitCamera camera;
    SceneObjects sceneObjects;
    float renderScale = 0.75f;
    float exposure = 1.6f;
    bool bloom = true;
    bool dragging = false;
    double previousX = 0.0;
    double previousY = 0.0;
};

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    case GLFW_KEY_SPACE:
        if (action == GLFW_PRESS && !state->sceneObjects.spawnInfallingObject()) {
            std::cout << "Object limit reached (" << SceneObjects::MaxObjects << ").\n";
        }
        break;
    case GLFW_KEY_BACKSPACE:
        state->sceneObjects.clear();
        break;
    case GLFW_KEY_O:
        state->sceneObjects.reset();
        break;
    case GLFW_KEY_P:
        state->sceneObjects.togglePaused();
        break;
    case GLFW_KEY_R:
        state->camera.reset();
        break;
    case GLFW_KEY_Q:
        // Internal resolution relative to the native framebuffer: 50% -> 75% -> 100%.
        state->renderScale = state->renderScale < 0.6f ? 0.75f : state->renderScale < 0.9f ? 1.0f : 0.5f;
        break;
    case GLFW_KEY_B:
        state->bloom = !state->bloom;
        break;
    case GLFW_KEY_LEFT_BRACKET:
        state->sceneObjects.scaleTime(1.0f / 1.25f);
        break;
    case GLFW_KEY_RIGHT_BRACKET:
        state->sceneObjects.scaleTime(1.25f);
        break;
    case GLFW_KEY_MINUS:
        state->exposure = std::max(0.1f, state->exposure / 1.15f);
        break;
    case GLFW_KEY_EQUAL:
        state->exposure = std::min(10.0f, state->exposure * 1.15f);
        break;
    default:
        break;
    }
}

void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << '\n';
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    state->dragging = action == GLFW_PRESS;
    if (state->dragging) {
        glfwGetCursorPos(window, &state->previousX, &state->previousY);
    }
}

void cursorPositionCallback(GLFWwindow* window, double x, double y) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state->dragging) {
        return;
    }
    const float deltaX = static_cast<float>(x - state->previousX);
    const float deltaY = static_cast<float>(y - state->previousY);
    state->previousX = x;
    state->previousY = y;
    state->camera.rotate(deltaX, deltaY);
}

void scrollCallback(GLFWwindow* window, double, double yOffset) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    state->camera.zoom(static_cast<float>(yOffset));
}

std::filesystem::path shaderPath(const char* executablePath, const char* shaderName) {
    // Prefer live source shaders during development so editing GLSL never runs a
    // stale copied file when the C++ target itself did not need relinking.
    const auto sourcePath = std::filesystem::path(BLACK_HOLE_SHADER_DIR) / shaderName;
    if (std::filesystem::exists(sourcePath)) {
        return sourcePath;
    }
    const auto executableDirectory = std::filesystem::absolute(executablePath).parent_path();
    const auto bundledPath = executableDirectory / "shaders" / shaderName;
    if (std::filesystem::exists(bundledPath)) {
        return bundledPath;
    }
    return sourcePath;
}

// Physical and rendering constants shared by the passes.
constexpr float DiskTemperature = physics::DiskPeakTemperature;
constexpr float LuminanceExponent = 0.55f;
constexpr float ReferenceParticleMass = 5.0e-7f;
constexpr float DiskMarchRadius = 12.2f;          // outer edge of the disk taper in common.glsl
constexpr glm::ivec3 NearResolution{192, 64, 192};
constexpr glm::vec3 NearHalfExtent{7.5f, 2.5f, 7.5f};
constexpr glm::ivec3 FarResolution{160, 48, 160};
constexpr glm::vec3 FarHalfExtent{20.0f, 6.0f, 20.0f};
constexpr glm::ivec3 OccupancyResolution{48, 12, 48};
constexpr glm::ivec2 DiskLightResolution{128, 96};

struct SplatVertex {
    glm::vec4 positionSize;
    glm::vec4 velocityMass;
    glm::vec4 thermal;
};

// Builds the per-frame particle stream and a coarse occupancy grid used by the
// ray marcher to leap over empty space.
class DebrisUpload {
public:
    DebrisUpload() : occupancy_(GL_TEXTURE_3D) {
        glGenVertexArrays(1, &vertexArray_);
        glGenBuffers(1, &buffer_);
        glBindVertexArray(vertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(SplatVertex) * DebrisSystem::MaxParticles),
                     nullptr, GL_STREAM_DRAW);
        for (GLuint attribute = 0; attribute < 3; ++attribute) {
            glEnableVertexAttribArray(attribute);
            glVertexAttribPointer(attribute, 4, GL_FLOAT, GL_FALSE, sizeof(SplatVertex),
                                  reinterpret_cast<const void*>(sizeof(glm::vec4) * attribute));
        }
        glBindVertexArray(0);

        occupancy_.bind(0);
        setSampling(GL_TEXTURE_3D, GL_NEAREST, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, OccupancyResolution.x, OccupancyResolution.z,
                     OccupancyResolution.y, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        vertices_.reserve(DebrisSystem::MaxParticles);
        cells_.resize(static_cast<std::size_t>(OccupancyResolution.x * OccupancyResolution.y *
                                               OccupancyResolution.z));
        dilated_.resize(cells_.size());
    }
    ~DebrisUpload() {
        glDeleteBuffers(1, &buffer_);
        glDeleteVertexArrays(1, &vertexArray_);
    }
    DebrisUpload(const DebrisUpload&) = delete;
    DebrisUpload& operator=(const DebrisUpload&) = delete;

    void upload(const DebrisSystem& debris) {
        vertices_.clear();
        std::fill(cells_.begin(), cells_.end(), std::uint8_t{0});
        const glm::vec3 cellSize = 2.0f * FarHalfExtent / glm::vec3(OccupancyResolution);
        for (const DebrisParticle& particle : debris.particles()) {
            SplatVertex vertex;
            vertex.positionSize = glm::vec4(particle.position, particle.size * 2.2f);
            vertex.velocityMass = glm::vec4(particle.velocity, particle.mass / ReferenceParticleMass);
            vertex.thermal = glm::vec4(particle.temperature * 1.0e-4f, particle.dust, particle.ice, 0.0f);
            vertices_.push_back(vertex);

            const glm::ivec3 cell(glm::floor((particle.position + FarHalfExtent) / cellSize));
            if (glm::all(glm::greaterThanEqual(cell, glm::ivec3(0))) &&
                glm::all(glm::lessThan(cell, OccupancyResolution))) {
                cells_[index(cell)] = 1;
            }
        }
        count_ = vertices_.size();

        // Dilate by one cell so kernels straddling a face are never skipped.
        std::fill(dilated_.begin(), dilated_.end(), std::uint8_t{0});
        for (int z = 0; z < OccupancyResolution.z; ++z) {
            for (int y = 0; y < OccupancyResolution.y; ++y) {
                for (int x = 0; x < OccupancyResolution.x; ++x) {
                    if (cells_[index({x, y, z})] == 0) {
                        continue;
                    }
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                const glm::ivec3 n(x + dx, y + dy, z + dz);
                                if (glm::all(glm::greaterThanEqual(n, glm::ivec3(0))) &&
                                    glm::all(glm::lessThan(n, OccupancyResolution))) {
                                    dilated_[index(n)] = 255;
                                }
                            }
                        }
                    }
                }
            }
        }

        glBindBuffer(GL_ARRAY_BUFFER, buffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(SplatVertex) * DebrisSystem::MaxParticles),
                     nullptr, GL_STREAM_DRAW);
        if (count_ > 0) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(SplatVertex) * count_),
                            vertices_.data());
        }
        occupancy_.bind(0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, OccupancyResolution.x, OccupancyResolution.z,
                        OccupancyResolution.y, GL_RED, GL_UNSIGNED_BYTE, dilated_.data());
    }

    void draw() const {
        glBindVertexArray(vertexArray_);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(count_));
    }

    [[nodiscard]] std::size_t count() const { return count_; }
    [[nodiscard]] const Texture& occupancy() const { return occupancy_; }

private:
    GLuint vertexArray_ = 0;
    GLuint buffer_ = 0;
    Texture occupancy_;
    std::vector<SplatVertex> vertices_;
    std::vector<std::uint8_t> cells_;
    std::vector<std::uint8_t> dilated_;
    std::size_t count_ = 0;

    // Storage order matches the texture: x fastest, then world z, then world y.
    static std::size_t index(const glm::ivec3& cell) {
        return static_cast<std::size_t>((cell.y * OccupancyResolution.z + cell.z) * OccupancyResolution.x + cell.x);
    }
};

struct BodyUniforms {
    std::array<glm::vec4, SceneObjects::MaxObjects> center{};
    std::array<glm::vec4, SceneObjects::MaxObjects> axisX{};
    std::array<glm::vec4, SceneObjects::MaxObjects> axisY{};
    std::array<glm::vec4, SceneObjects::MaxObjects> axisZ{};
    std::array<glm::vec4, SceneObjects::MaxObjects> surface{};
    std::array<glm::vec4, SceneObjects::MaxObjects> state{};
    int count = 0;
    float extent = 0.0f;
};

// Orients each Roche ellipsoid with its long axis on the tidal (radial)
// direction and its shortest axis along the orbital angular momentum.
BodyUniforms packBodies(const std::vector<SceneObject>& objects) {
    BodyUniforms uniforms;
    for (const SceneObject& object : objects) {
        if (uniforms.count >= static_cast<int>(SceneObjects::MaxObjects)) {
            break;
        }
        const auto slot = static_cast<std::size_t>(uniforms.count++);
        const glm::vec3 radial = glm::normalize(object.position);
        glm::vec3 track = object.velocity - radial * glm::dot(object.velocity, radial);
        if (glm::dot(track, track) < 1.0e-10f) {
            track = glm::cross(radial, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        track = glm::normalize(track);
        const glm::vec3 normal = glm::cross(radial, track);
        const float a1 = object.radius * object.elongation;
        const float a2 = object.radius * std::pow(object.elongation, -0.47f);
        const float a3 = object.radius * std::pow(object.elongation, -0.53f);

        uniforms.center[slot] = glm::vec4(object.position, a1 * 1.01f);
        uniforms.axisX[slot] = glm::vec4(radial / a1, 0.0f);
        uniforms.axisY[slot] = glm::vec4(track / a2, 0.0f);
        uniforms.axisZ[slot] = glm::vec4(normal / a3, 0.0f);
        uniforms.surface[slot] = glm::vec4(object.albedo, object.roughness);
        uniforms.state[slot] = glm::vec4(static_cast<float>(object.material), object.seed,
                                         std::clamp(object.shedRate / 0.03f, 0.0f, 1.0f),
                                         object.temperature * 1.0e-4f);
        uniforms.extent = std::max(uniforms.extent, glm::length(object.position) + a1);
    }
    return uniforms;
}

void drawFullscreen(GLuint vertexArray) {
    glBindVertexArray(vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

} // namespace

int main(int argc, char** argv) {
    const LaunchOptions launchOptions = parseOptions(argc, argv);
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "Failed to initialize GLFW.\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Schwarzschild Black Hole", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create an OpenGL 4.1 Core window.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    const bool uncapped = launchOptions.benchmark || !launchOptions.capturePath.empty();
    glfwSwapInterval(uncapped ? 0 : 1);

    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    // GLEW may provoke GL_INVALID_ENUM while probing a forward-compatible context.
    glGetError();
    if (glewStatus != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW: "
                  << reinterpret_cast<const char*>(glewGetErrorString(glewStatus)) << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "OpenGL " << glGetString(GL_VERSION) << " — " << glGetString(GL_RENDERER) << '\n';

    AppState state;
    state.renderScale = launchOptions.renderScale;
    if (launchOptions.customCamera) {
        state.camera.set(launchOptions.camera.x, launchOptions.camera.y, launchOptions.camera.z);
    }
    if (launchOptions.emptyScene) {
        state.sceneObjects.clear();
    }
    for (int spawn = 0; spawn < launchOptions.spawnCount; ++spawn) {
        state.sceneObjects.spawnInfallingObject();
    }
    // Deterministic warm-up so captures show a chosen moment of the disruption.
    for (float elapsed = 0.0f; elapsed < launchOptions.warmupSeconds; elapsed += 1.0f / 60.0f) {
        state.sceneObjects.update(1.0f / 60.0f);
    }

    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPositionCallback);
    glfwSetScrollCallback(window, scrollCallback);

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    std::cout << "Output surface: " << framebufferWidth << 'x' << framebufferHeight << '\n';

    GLuint fullscreenVertexArray = 0;
    glGenVertexArrays(1, &fullscreenVertexArray);

    int exitCode = EXIT_SUCCESS;
    try {
        const std::string executable = argc > 0 ? argv[0] : "";
        const auto path = [&](const char* name) { return shaderPath(executable.c_str(), name).string(); };
        Shader sceneShader(path("blackhole.vert"), path("blackhole.frag"));
        Shader presentShader(path("blackhole.vert"), path("present.frag"));
        Shader downsampleShader(path("blackhole.vert"), path("bloom_downsample.frag"));
        Shader upsampleShader(path("blackhole.vert"), path("bloom_upsample.frag"));
        Shader diskLightShader(path("blackhole.vert"), path("disk_light.frag"));
        Shader splatShader(path("debris_splat.vert"), path("debris_splat.geom"), path("debris_splat.frag"));

        // Static lookup data.
        Texture blackbodyLut(GL_TEXTURE_2D);
        {
            const std::vector<float> lut = blackbody::buildLut();
            blackbodyLut.bind(0);
            setSampling(GL_TEXTURE_2D, GL_LINEAR, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, blackbody::LutSize, 1, 0, GL_RGBA, GL_FLOAT, lut.data());
        }
        Texture noiseVolume(GL_TEXTURE_3D);
        {
            const std::vector<std::uint8_t> texels = noise::buildVolume();
            noiseVolume.bind(0);
            setSampling(GL_TEXTURE_3D, GL_LINEAR_MIPMAP_LINEAR, GL_REPEAT);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, noise::VolumeSize, noise::VolumeSize, noise::VolumeSize,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
            glGenerateMipmap(GL_TEXTURE_3D);
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FRAMEBUFFER_SRGB);

        ColorTarget diskLight;
        diskLight.resize(DiskLightResolution.x, DiskLightResolution.y);
        diskLight.bind();
        diskLightShader.use();
        diskLightShader.setVec2("uResolution", glm::vec2(DiskLightResolution));
        diskLightShader.setFloat("uDiskTemperature", DiskTemperature);
        diskLightShader.setFloat("uLuminanceExponent", LuminanceExponent);
        diskLightShader.setInt("uBlackbody", 0);
        blackbodyLut.bind(0);
        drawFullscreen(fullscreenVertexArray);

        DebrisVolume nearVolume(NearResolution, NearHalfExtent);
        DebrisVolume farVolume(FarResolution, FarHalfExtent);
        DebrisUpload debrisUpload;
        ColorTarget sceneTarget;
        BloomChain bloom;
        GpuTimer splatTimer;
        GpuTimer sceneTimer;
        GpuTimer postTimer;

        sceneShader.use();
        sceneShader.setInt("uBlackbody", 0);
        sceneShader.setInt("uNoise", 1);
        sceneShader.setInt("uDiskLight", 2);
        sceneShader.setInt("uNearComposition", 3);
        sceneShader.setInt("uNearMomentum", 4);
        sceneShader.setInt("uFarComposition", 5);
        sceneShader.setInt("uFarMomentum", 6);
        sceneShader.setInt("uOccupancy", 7);
        sceneShader.setFloat("uLuminanceExponent", LuminanceExponent);
        sceneShader.setFloat("uDiskTemperature", DiskTemperature);
        sceneShader.setFloat("uDiskOpacity", 1.2f);
        sceneShader.setFloat("uDebrisOpacity", 3.5f);
        sceneShader.setVec3("uNearHalfExtent", NearHalfExtent);
        sceneShader.setVec3("uFarHalfExtent", FarHalfExtent);
        sceneShader.setVec3("uOccupancyResolution", glm::vec3(OccupancyResolution));
        sceneShader.setFloat("uStarBrightness", 1.0f);
        sceneShader.setFloat("uGalaxyBrightness", 0.03f);
        sceneShader.setInt("uDebugDebris", std::getenv("BH_DEBUG_DEBRIS") != nullptr ? 1 : 0);

        double previousTime = glfwGetTime();
        const double startTime = previousTime;
        double previousTitleUpdate = previousTime;
        double previousBenchmarkLog = previousTime;
        unsigned int framesSinceTitleUpdate = 0;
        unsigned int framesSinceBenchmarkLog = 0;
        int capturedFrames = 0;

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();

            const double currentTime = glfwGetTime();
            const float deltaTime = static_cast<float>(currentTime - previousTime);
            previousTime = currentTime;
            if (launchOptions.capturePath.empty()) {
                state.sceneObjects.update(deltaTime);
            }
            ++framesSinceTitleUpdate;
            ++framesSinceBenchmarkLog;

            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0) {
                glfwWaitEvents();
                continue;
            }
            const int renderWidth = std::max(1, static_cast<int>(static_cast<float>(framebufferWidth) * state.renderScale + 0.5f));
            const int renderHeight = std::max(1, static_cast<int>(static_cast<float>(framebufferHeight) * state.renderScale + 0.5f));
            sceneTarget.resize(renderWidth, renderHeight);
            bloom.resize(renderWidth, renderHeight);

            // --- Debris volumes -------------------------------------------------
            const DebrisSystem& debris = state.sceneObjects.debris();
            debrisUpload.upload(debris);
            splatTimer.begin();
            if (debrisUpload.count() > 0) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                splatShader.use();
                for (const DebrisVolume* volume : {&nearVolume, &farVolume}) {
                    volume->bindForSplat();
                    splatShader.setVec3("uVolumeMin", -volume->halfExtent());
                    splatShader.setFloat("uVoxelSize", volume->voxelSize());
                    splatShader.setVec3("uVolumeResolution", volume->textureResolution());
                    splatShader.setFloat("uMinimumKernel", 1.6f);
                    debrisUpload.draw();
                }
                glDisable(GL_BLEND);
                if (std::getenv("BH_DEBUG_VOLUME") != nullptr && capturedFrames == 0) {
                    const glm::vec3 res = nearVolume.textureResolution();
                    std::vector<float> texels(static_cast<std::size_t>(res.x * res.y * res.z) * 4u);
                    nearVolume.composition().bind(0);
                    glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, texels.data());
                    double mass = 0.0, heat = 0.0;
                    float peak = 0.0f;
                    std::size_t filled = 0;
                    for (std::size_t i = 0; i < texels.size(); i += 4) {
                        mass += texels[i];
                        heat += texels[i + 1];
                        peak = std::max(peak, texels[i]);
                        filled += texels[i] > 1.0e-3f ? 1u : 0u;
                    }
                    double expected = 0.0;
                    for (const DebrisParticle& p : debris.particles()) {
                        expected += 1.0e-3 * p.mass / ReferenceParticleMass;
                    }
                    const double voxel = std::pow(nearVolume.voxelSize(), 3.0);
                    std::cout << "near volume: mass " << mass * voxel << " expected " << expected
                              << ", peak " << peak << ", filled " << filled
                              << ", mean T " << (mass > 0 ? heat / mass * 1e4 : 0) << " K\n";
                }
            }
            splatTimer.end();

            // --- Ray-traced scene -----------------------------------------------
            const float aspect = static_cast<float>(renderWidth) / static_cast<float>(renderHeight);
            const glm::mat4 viewProjection = state.camera.projectionMatrix(aspect) * state.camera.viewMatrix();
            const BodyUniforms bodies = packBodies(state.sceneObjects.objects());
            const float debrisExtent = debrisUpload.count() > 0
                ? std::min(debris.maximumRadius() + 0.8f, glm::length(FarHalfExtent))
                : 0.0f;
            const float marchRadius = std::max({DiskMarchRadius, debrisExtent, bodies.extent + 0.2f});

            sceneTimer.begin();
            sceneTarget.bind();
            sceneShader.use();
            sceneShader.setVec2("uResolution", glm::vec2(renderWidth, renderHeight));
            sceneShader.setVec3("uCameraPosition", state.camera.position());
            sceneShader.setMat4("uInverseViewProjection", glm::inverse(viewProjection));
            sceneShader.setFloat("uSimulationTime", state.sceneObjects.simulatedTime());
            sceneShader.setFloat("uMarchRadius", marchRadius);
            sceneShader.setFloat("uPixelAngle",
                                 2.0f * std::tan(0.5f * OrbitCamera::verticalFieldOfView()) /
                                     static_cast<float>(renderHeight));
            sceneShader.setFloat("uFlare", state.sceneObjects.accretionLuminosity());
            sceneShader.setInt("uDebrisActive", debrisUpload.count() > 0 ? 1 : 0);
            sceneShader.setInt("uBodyCount", bodies.count);
            const auto count = static_cast<std::size_t>(bodies.count);
            sceneShader.setVec4Array("uBodyCenter[0]", std::span<const glm::vec4>(bodies.center.data(), count));
            sceneShader.setVec4Array("uBodyAxisX[0]", std::span<const glm::vec4>(bodies.axisX.data(), count));
            sceneShader.setVec4Array("uBodyAxisY[0]", std::span<const glm::vec4>(bodies.axisY.data(), count));
            sceneShader.setVec4Array("uBodyAxisZ[0]", std::span<const glm::vec4>(bodies.axisZ.data(), count));
            sceneShader.setVec4Array("uBodySurface[0]", std::span<const glm::vec4>(bodies.surface.data(), count));
            sceneShader.setVec4Array("uBodyState[0]", std::span<const glm::vec4>(bodies.state.data(), count));
            blackbodyLut.bind(0);
            noiseVolume.bind(1);
            diskLight.texture().bind(2);
            nearVolume.composition().bind(3);
            nearVolume.momentum().bind(4);
            farVolume.composition().bind(5);
            farVolume.momentum().bind(6);
            debrisUpload.occupancy().bind(7);
            drawFullscreen(fullscreenVertexArray);
            sceneTimer.end();

            // --- Bloom and presentation ----------------------------------------
            postTimer.begin();
            downsampleShader.use();
            downsampleShader.setInt("uSource", 0);
            for (int level = 0; level < BloomChain::Levels; ++level) {
                const ColorTarget& source = level == 0 ? sceneTarget : bloom.level(level - 1);
                bloom.level(level).bind();
                source.texture().bind(0);
                downsampleShader.setVec2("uSourceTexel", 1.0f / source.size());
                downsampleShader.setInt("uKarisAverage", level == 0 ? 1 : 0);
                drawFullscreen(fullscreenVertexArray);
            }
            upsampleShader.use();
            upsampleShader.setInt("uSource", 0);
            upsampleShader.setFloat("uRadius", 1.0f);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            for (int level = BloomChain::Levels - 1; level > 0; --level) {
                bloom.level(level - 1).bind();
                bloom.level(level).texture().bind(0);
                upsampleShader.setVec2("uSourceTexel", 1.0f / bloom.level(level).size());
                drawFullscreen(fullscreenVertexArray);
            }
            glDisable(GL_BLEND);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            presentShader.use();
            presentShader.setInt("uScene", 0);
            presentShader.setInt("uBloom", 1);
            presentShader.setVec2("uSceneSize", sceneTarget.size());
            presentShader.setVec2("uOutputSize", glm::vec2(framebufferWidth, framebufferHeight));
            presentShader.setFloat("uExposure", state.exposure);
            // The chain sums six levels, so normalise before blending.
            presentShader.setFloat("uBloomStrength", state.bloom ? 0.05f : 0.0f);
            sceneTarget.texture().bind(0);
            bloom.level(0).texture().bind(1);
            drawFullscreen(fullscreenVertexArray);
            postTimer.end();

            if (!launchOptions.capturePath.empty() && ++capturedFrames == 3) {
                std::vector<std::uint8_t> pixels(static_cast<std::size_t>(framebufferWidth) *
                                                 static_cast<std::size_t>(framebufferHeight) * 3u);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadBuffer(GL_BACK);
                glReadPixels(0, 0, framebufferWidth, framebufferHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                writePng(launchOptions.capturePath, framebufferWidth, framebufferHeight, pixels);
                std::cout << "Captured " << launchOptions.capturePath << " (" << framebufferWidth << 'x'
                          << framebufferHeight << ", " << debrisUpload.count() << " debris particles)\n";
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }

            glfwSwapBuffers(window);

            if (launchOptions.benchmark && currentTime - previousBenchmarkLog >= 2.0) {
                const double interval = currentTime - previousBenchmarkLog;
                std::cout << std::fixed << std::setprecision(2)
                          << "Benchmark: " << static_cast<double>(framesSinceBenchmarkLog) / interval
                          << " FPS, internal " << renderWidth << 'x' << renderHeight
                          << ", GPU splat " << splatTimer.takeAverage()
                          << " ms, scene " << sceneTimer.takeAverage()
                          << " ms, post " << postTimer.takeAverage()
                          << " ms, " << state.sceneObjects.size() << " bodies, "
                          << debrisUpload.count() << " debris" << std::endl;
                previousBenchmarkLog = currentTime;
                framesSinceBenchmarkLog = 0;
            }
            if (launchOptions.exitAfterSeconds > 0.0f &&
                currentTime - startTime > static_cast<double>(launchOptions.exitAfterSeconds)) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }

            if (currentTime - previousTitleUpdate > 0.5) {
                const double titleInterval = currentTime - previousTitleUpdate;
                const int framesPerSecond = static_cast<int>(
                    static_cast<double>(framesSinceTitleUpdate) / titleInterval + 0.5);
                std::ostringstream title;
                title << "Schwarzschild Black Hole — " << state.sceneObjects.size() << " bodies, "
                      << debrisUpload.count() << " debris — " << framesPerSecond << " FPS — time x"
                      << std::setprecision(2) << state.sceneObjects.timeScale();
                if (state.sceneObjects.paused()) {
                    title << " (paused)";
                }
                glfwSetWindowTitle(window, title.str().c_str());
                previousTitleUpdate = currentTime;
                framesSinceTitleUpdate = 0;
            }
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        exitCode = EXIT_FAILURE;
    }

    glDeleteVertexArrays(1, &fullscreenVertexArray);
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
