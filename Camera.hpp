#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class OrbitCamera {
public:
    OrbitCamera() { reset(); }

    void reset() {
        target_ = glm::vec3(0.0f);
        yaw_ = glm::radians(35.0f);
        pitch_ = glm::radians(18.0f);
        distance_ = 18.0f;
    }

    void rotate(float deltaX, float deltaY) {
        constexpr float sensitivity = 0.005f;
        yaw_ -= deltaX * sensitivity;
        pitch_ += deltaY * sensitivity;
        pitch_ = std::clamp(pitch_, glm::radians(-85.0f), glm::radians(85.0f));
    }

    void zoom(float scrollDelta) {
        distance_ *= std::exp(-scrollDelta * 0.10f);
        distance_ = std::clamp(distance_, 7.0f, 35.0f);
    }

    [[nodiscard]] glm::vec3 position() const {
        const float cosPitch = std::cos(pitch_);
        return target_ + distance_ * glm::vec3(
            cosPitch * std::cos(yaw_),
            std::sin(pitch_),
            cosPitch * std::sin(yaw_));
    }

    [[nodiscard]] glm::mat4 viewMatrix() const {
        return glm::lookAt(position(), target_, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    [[nodiscard]] glm::mat4 projectionMatrix(float aspectRatio) const {
        return glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
    }

private:
    glm::vec3 target_{0.0f};
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float distance_ = 18.0f;
};
