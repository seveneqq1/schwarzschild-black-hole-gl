#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "Camera.hpp"
#include "SceneObjects.hpp"
#include "Shader.hpp"

namespace {

struct LaunchOptions {
    bool retina = false;
    bool benchmark = false;
    bool emptyScene = false;
    float renderScale = 0.75f;
};

LaunchOptions parseOptions(int argc, char** argv) {
    LaunchOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--retina") {
            options.retina = true;
            options.renderScale = 1.0f;
        } else if (argument == "--quality") {
            options.renderScale = 1.0f;
        } else if (argument == "--benchmark") {
            options.benchmark = true;
        } else if (argument == "--empty") {
            options.emptyScene = true;
        }
    }
    return options;
}

struct AppState {
    OrbitCamera camera;
    SceneObjects sceneObjects;
    float renderScale = 0.75f;
    bool dragging = false;
    double previousX = 0.0;
    double previousY = 0.0;
};

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) {
        return;
    }

    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    case GLFW_KEY_SPACE:
        if (!state->sceneObjects.spawnInfallingObject()) {
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
        state->renderScale = state->renderScale < 0.9f ? 1.0f : 0.75f;
        break;
    default:
        break;
    }
}

void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << '\n';
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
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

class RenderTarget {
public:
    RenderTarget() {
        glGenFramebuffers(1, &framebuffer_);
        glGenTextures(1, &colorTexture_);
        glBindTexture(GL_TEXTURE_2D, colorTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }

    ~RenderTarget() {
        glDeleteTextures(1, &colorTexture_);
        glDeleteFramebuffers(1, &framebuffer_);
    }

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    void resize(int width, int height) {
        if (width == width_ && height == height_) {
            return;
        }
        width_ = width;
        height_ = height;

        glBindTexture(GL_TEXTURE_2D, colorTexture_);
        // The ray shader has already tone-mapped and dithered its output, so an
        // 8-bit target avoids needless Retina presentation bandwidth.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTexture_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Failed to create the offscreen render target.");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void bind() const { glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_); }
    [[nodiscard]] GLuint colorTexture() const { return colorTexture_; }

private:
    GLuint framebuffer_ = 0;
    GLuint colorTexture_ = 0;
    int width_ = 0;
    int height_ = 0;
};

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
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    GLFWwindow* window = glfwCreateWindow(
        1280, 720, "Schwarzschild Black Hole", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create an OpenGL 4.1 Core window.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

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

    int contextMajor = 0;
    int contextMinor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &contextMajor);
    glGetIntegerv(GL_MINOR_VERSION, &contextMinor);
    std::cout << "OpenGL " << contextMajor << '.' << contextMinor << " — "
              << glGetString(GL_RENDERER) << '\n';

    AppState state;
    state.renderScale = launchOptions.renderScale;
    if (launchOptions.emptyScene) {
        state.sceneObjects.clear();
    }
    glfwSetWindowUserPointer(window, &state);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPositionCallback);
    glfwSetScrollCallback(window, scrollCallback);

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    std::cout << "Output surface: " << framebufferWidth << 'x' << framebufferHeight << '\n';

    GLuint fullscreenVertexArray = 0;
    glGenVertexArrays(1, &fullscreenVertexArray);
    glBindVertexArray(fullscreenVertexArray);

    try {
        Shader shader(
            shaderPath(argc > 0 ? argv[0] : "", "blackhole.vert").string(),
            shaderPath(argc > 0 ? argv[0] : "", "blackhole.frag").string());
        Shader presentShader(
            shaderPath(argc > 0 ? argv[0] : "", "blackhole.vert").string(),
            shaderPath(argc > 0 ? argv[0] : "", "present.frag").string());
        RenderTarget renderTarget;
        presentShader.use();
        presentShader.setInt("uScene", 0);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        // The window was requested with an sRGB-capable default framebuffer.
        glEnable(GL_FRAMEBUFFER_SRGB);

        double previousTime = glfwGetTime();
        double previousTitleUpdate = previousTime;
        double previousBenchmarkLog = previousTime;
        unsigned int framesSinceTitleUpdate = 0;
        unsigned int framesSinceBenchmarkLog = 0;
        std::array<glm::vec4, SceneObjects::MaxObjects> positionRadius{};
        std::array<glm::vec4, SceneObjects::MaxObjects> colorRoughness{};
        std::array<glm::vec4, SceneObjects::MaxObjects> velocities{};
        std::array<glm::vec4, SceneObjects::MaxObjects> objectProperties{};
        int renderWidth = 1280;
        int renderHeight = 720;

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();

            const double currentTime = glfwGetTime();
            const float deltaTime = static_cast<float>(currentTime - previousTime);
            previousTime = currentTime;
            state.sceneObjects.update(deltaTime);
            ++framesSinceTitleUpdate;
            ++framesSinceBenchmarkLog;

            if (launchOptions.benchmark && currentTime - previousBenchmarkLog >= 2.0) {
                const double interval = currentTime - previousBenchmarkLog;
                const double measuredFps = static_cast<double>(framesSinceBenchmarkLog) / interval;
                std::cout << "Benchmark: " << static_cast<int>(measuredFps + 0.5)
                          << " FPS, internal " << renderWidth << 'x' << renderHeight
                          << ", output " << framebufferWidth << 'x' << framebufferHeight
                          << ", " << state.sceneObjects.size() << " fragments" << std::endl;
                previousBenchmarkLog = currentTime;
                framesSinceBenchmarkLog = 0;
            }

            if (currentTime - previousTitleUpdate > 0.5) {
                const double titleInterval = currentTime - previousTitleUpdate;
                const int framesPerSecond = static_cast<int>(
                    static_cast<double>(framesSinceTitleUpdate) / titleInterval + 0.5);
                std::string title = "Schwarzschild Black Hole — ";
                title += std::to_string(state.sceneObjects.size()) + " fragments — ";
                title += std::to_string(framesPerSecond) + " FPS";
                if (state.sceneObjects.paused()) {
                    title += " (paused)";
                }
                glfwSetWindowTitle(window, title.c_str());
                previousTitleUpdate = currentTime;
                framesSinceTitleUpdate = 0;
            }

            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0) {
                glfwWaitEvents();
                continue;
            }

            int windowWidth = 0;
            int windowHeight = 0;
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            const int baseRenderWidth = launchOptions.retina
                ? framebufferWidth
                : std::min(framebufferWidth, std::max(windowWidth, 1));
            const int baseRenderHeight = launchOptions.retina
                ? framebufferHeight
                : std::min(framebufferHeight, std::max(windowHeight, 1));
            renderWidth = std::max(1, static_cast<int>(
                static_cast<float>(baseRenderWidth) * state.renderScale + 0.5f));
            renderHeight = std::max(1, static_cast<int>(
                static_cast<float>(baseRenderHeight) * state.renderScale + 0.5f));
            renderTarget.resize(renderWidth, renderHeight);

            const float aspect = static_cast<float>(renderWidth) /
                                 static_cast<float>(renderHeight);
            const glm::mat4 viewProjection =
                state.camera.projectionMatrix(aspect) * state.camera.viewMatrix();

            renderTarget.bind();
            glViewport(0, 0, renderWidth, renderHeight);
            glDisable(GL_FRAMEBUFFER_SRGB);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            shader.use();
            shader.setVec2("uResolution", glm::vec2(renderWidth, renderHeight));
            shader.setFloat("uTime", static_cast<float>(glfwGetTime()));
            shader.setFloat("uFallbackLuminosity", state.sceneObjects.fallbackLuminosity());
            shader.setVec3("uCameraPosition", state.camera.position());
            shader.setMat4("uInverseViewProjection", glm::inverse(viewProjection));

            const auto& objects = state.sceneObjects.objects();
            for (std::size_t index = 0; index < objects.size(); ++index) {
                const SceneObject& object = objects[index];
                positionRadius[index] = glm::vec4(object.position, object.radius);
                colorRoughness[index] = glm::vec4(object.albedo, object.roughness);
                velocities[index] = glm::vec4(object.velocity, 0.0f);
                objectProperties[index] = glm::vec4(
                    static_cast<float>(object.fractureLevel), object.seed,
                    object.tidalStrain, object.internalEnergy);
            }
            const std::size_t objectCount = objects.size();
            shader.setInt("uObjectCount", static_cast<int>(objectCount));
            shader.setVec4Array("uObjectPositionRadius[0]",
                                std::span<const glm::vec4>(positionRadius.data(), objectCount));
            shader.setVec4Array("uObjectColorRoughness[0]",
                                std::span<const glm::vec4>(colorRoughness.data(), objectCount));
            shader.setVec4Array("uObjectVelocity[0]",
                                std::span<const glm::vec4>(velocities.data(), objectCount));
            shader.setVec4Array("uObjectProperties[0]",
                                std::span<const glm::vec4>(objectProperties.data(), objectCount));

            glBindVertexArray(fullscreenVertexArray);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glEnable(GL_FRAMEBUFFER_SRGB);
            presentShader.use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, renderTarget.colorTexture());
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glfwSwapBuffers(window);
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        glDeleteVertexArrays(1, &fullscreenVertexArray);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glDeleteVertexArrays(1, &fullscreenVertexArray);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
