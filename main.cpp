#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "Camera.hpp"
#include "SceneObjects.hpp"
#include "Shader.hpp"

namespace {

struct AppState {
    OrbitCamera camera;
    SceneObjects sceneObjects;
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
    const auto executableDirectory = std::filesystem::absolute(executablePath).parent_path();
    const auto bundledPath = executableDirectory / "shaders" / shaderName;
    if (std::filesystem::exists(bundledPath)) {
        return bundledPath;
    }
    return std::filesystem::path(BLACK_HOLE_SHADER_DIR) / shaderName;
}

} // namespace

int main(int argc, char** argv) {
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

    GLuint fullscreenVertexArray = 0;
    glGenVertexArrays(1, &fullscreenVertexArray);
    glBindVertexArray(fullscreenVertexArray);

    try {
        Shader shader(
            shaderPath(argc > 0 ? argv[0] : "", "blackhole.vert").string(),
            shaderPath(argc > 0 ? argv[0] : "", "blackhole.frag").string());

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        // The window was requested with an sRGB-capable default framebuffer.
        glEnable(GL_FRAMEBUFFER_SRGB);

        double previousTime = glfwGetTime();
        double previousTitleUpdate = previousTime;
        unsigned int framesSinceTitleUpdate = 0;
        std::array<glm::vec4, SceneObjects::MaxObjects> positionRadius{};
        std::array<glm::vec4, SceneObjects::MaxObjects> colorRoughness{};
        std::array<glm::vec4, SceneObjects::MaxObjects> velocities{};
        std::array<glm::vec4, SceneObjects::MaxObjects> objectProperties{};

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();

            const double currentTime = glfwGetTime();
            const float deltaTime = static_cast<float>(currentTime - previousTime);
            previousTime = currentTime;
            state.sceneObjects.update(deltaTime);
            ++framesSinceTitleUpdate;

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

            const float aspect = static_cast<float>(framebufferWidth) /
                                 static_cast<float>(framebufferHeight);
            const glm::mat4 viewProjection =
                state.camera.projectionMatrix(aspect) * state.camera.viewMatrix();

            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            shader.use();
            shader.setVec2("uResolution", glm::vec2(framebufferWidth, framebufferHeight));
            shader.setFloat("uTime", static_cast<float>(glfwGetTime()));
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
                    SceneObjects::tidalStretch(object), object.cohesion);
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
