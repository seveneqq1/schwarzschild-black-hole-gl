#pragma once

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/glm.hpp>

inline void checkFramebuffer(const char* what) {
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("Incomplete framebuffer: ") + what);
    }
}

inline void setSampling(GLenum target, GLint filter, GLint wrap) {
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter == GL_LINEAR_MIPMAP_LINEAR ? GL_LINEAR : filter);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap);
}

class Texture {
public:
    explicit Texture(GLenum target) : target_(target) { glGenTextures(1, &id_); }
    ~Texture() { glDeleteTextures(1, &id_); }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void bind(int unit) const {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(target_, id_);
    }
    [[nodiscard]] GLuint id() const { return id_; }
    [[nodiscard]] GLenum target() const { return target_; }

private:
    GLenum target_;
    GLuint id_ = 0;
};

// A single RGBA16F colour attachment, resized lazily.
class ColorTarget {
public:
    ColorTarget() : texture_(GL_TEXTURE_2D) {
        glGenFramebuffers(1, &framebuffer_);
        texture_.bind(0);
        setSampling(GL_TEXTURE_2D, GL_LINEAR, GL_CLAMP_TO_EDGE);
    }
    ~ColorTarget() { glDeleteFramebuffers(1, &framebuffer_); }
    ColorTarget(const ColorTarget&) = delete;
    ColorTarget& operator=(const ColorTarget&) = delete;

    void resize(int width, int height) {
        if (width == width_ && height == height_) {
            return;
        }
        width_ = width;
        height_ = height;
        texture_.bind(0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_.id(), 0);
        checkFramebuffer("colour target");
    }

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glViewport(0, 0, width_, height_);
    }
    [[nodiscard]] const Texture& texture() const { return texture_; }
    [[nodiscard]] glm::vec2 size() const { return {static_cast<float>(width_), static_cast<float>(height_)}; }

private:
    GLuint framebuffer_ = 0;
    Texture texture_;
    int width_ = 0;
    int height_ = 0;
};

class BloomChain {
public:
    static constexpr int Levels = 6;

    void resize(int sceneWidth, int sceneHeight) {
        int width = sceneWidth;
        int height = sceneHeight;
        for (ColorTarget& level : levels_) {
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
            level.resize(width, height);
        }
    }
    [[nodiscard]] ColorTarget& level(int index) { return levels_[static_cast<std::size_t>(index)]; }

private:
    std::array<ColorTarget, Levels> levels_;
};

// One cascade of the debris volume: two layered RGBA16F 3D textures holding
// (rho, rho T, rho dust, rho ice) and rho v.
class DebrisVolume {
public:
    DebrisVolume(glm::ivec3 resolution, glm::vec3 halfExtent)
        : resolution_(resolution), halfExtent_(halfExtent),
          composition_(GL_TEXTURE_3D), momentum_(GL_TEXTURE_3D) {
        for (const Texture* texture : {&composition_, &momentum_}) {
            texture->bind(0);
            setSampling(GL_TEXTURE_3D, GL_LINEAR, GL_CLAMP_TO_BORDER);
            const float border[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, border);
            // Texture axes: world x, world z, world y (layers).
            glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, resolution.x, resolution.z, resolution.y, 0,
                         GL_RGBA, GL_HALF_FLOAT, nullptr);
        }
        glGenFramebuffers(1, &framebuffer_);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, composition_.id(), 0);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, momentum_.id(), 0);
        const GLenum buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, buffers);
        checkFramebuffer("debris volume");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    ~DebrisVolume() { glDeleteFramebuffers(1, &framebuffer_); }
    DebrisVolume(const DebrisVolume&) = delete;
    DebrisVolume& operator=(const DebrisVolume&) = delete;

    void bindForSplat() const {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glViewport(0, 0, resolution_.x, resolution_.z);
        const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 0, zero);
        glClearBufferfv(GL_COLOR, 1, zero);
    }

    [[nodiscard]] float voxelSize() const { return 2.0f * halfExtent_.x / static_cast<float>(resolution_.x); }
    [[nodiscard]] glm::vec3 halfExtent() const { return halfExtent_; }
    [[nodiscard]] glm::vec3 textureResolution() const {
        return {static_cast<float>(resolution_.x), static_cast<float>(resolution_.z),
                static_cast<float>(resolution_.y)};
    }
    [[nodiscard]] const Texture& composition() const { return composition_; }
    [[nodiscard]] const Texture& momentum() const { return momentum_; }

private:
    glm::ivec3 resolution_;
    glm::vec3 halfExtent_;
    Texture composition_;
    Texture momentum_;
    GLuint framebuffer_ = 0;
};

// GL_TIME_ELAPSED queries read back a few frames late so they never stall.
class GpuTimer {
public:
    static constexpr int Latency = 4;

    GpuTimer() { glGenQueries(Latency, queries_.data()); }
    ~GpuTimer() { glDeleteQueries(Latency, queries_.data()); }
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    void begin() {
        collect();
        glBeginQuery(GL_TIME_ELAPSED, queries_[static_cast<std::size_t>(cursor_)]);
    }
    void end() {
        glEndQuery(GL_TIME_ELAPSED);
        pending_[static_cast<std::size_t>(cursor_)] = true;
        cursor_ = (cursor_ + 1) % Latency;
    }

    // Mean milliseconds since the last call.
    double takeAverage() {
        const double result = samples_ > 0 ? totalMs_ / samples_ : 0.0;
        totalMs_ = 0.0;
        samples_ = 0;
        return result;
    }

private:
    std::array<GLuint, Latency> queries_{};
    std::array<bool, Latency> pending_{};
    int cursor_ = 0;
    double totalMs_ = 0.0;
    int samples_ = 0;

    void collect() {
        const auto slot = static_cast<std::size_t>(cursor_);
        if (!pending_[slot]) {
            return;
        }
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(queries_[slot], GL_QUERY_RESULT, &nanoseconds);
        pending_[slot] = false;
        totalMs_ += static_cast<double>(nanoseconds) * 1.0e-6;
        ++samples_;
    }
};
