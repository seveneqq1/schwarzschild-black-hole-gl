#pragma once

#include <GL/glew.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Shader {
public:
    Shader(const std::string& vertexPath, const std::string& fragmentPath)
        : Shader(vertexPath, std::string(), fragmentPath) {}

    Shader(const std::string& vertexPath, const std::string& geometryPath,
           const std::string& fragmentPath) {
        std::vector<GLuint> stages;
        stages.push_back(compile(GL_VERTEX_SHADER, readSource(vertexPath, 0), vertexPath));
        if (!geometryPath.empty()) {
            stages.push_back(compile(GL_GEOMETRY_SHADER, readSource(geometryPath, 0), geometryPath));
        }
        stages.push_back(compile(GL_FRAGMENT_SHADER, readSource(fragmentPath, 0), fragmentPath));

        program_ = glCreateProgram();
        for (const GLuint stage : stages) {
            glAttachShader(program_, stage);
        }
        glLinkProgram(program_);

        GLint linked = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        for (const GLuint stage : stages) {
            glDetachShader(program_, stage);
            glDeleteShader(stage);
        }
        if (linked != GL_TRUE) {
            GLint length = 0;
            glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(program_, length, nullptr, log.data());
            glDeleteProgram(program_);
            program_ = 0;
            throw std::runtime_error("Shader program link failed (" + fragmentPath + "):\n" + log);
        }
    }

    ~Shader() {
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
    }

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept
        : program_(other.program_), locations_(std::move(other.locations_)) {
        other.program_ = 0;
    }

    Shader& operator=(Shader&& other) noexcept {
        if (this != &other) {
            if (program_ != 0) {
                glDeleteProgram(program_);
            }
            program_ = other.program_;
            locations_ = std::move(other.locations_);
            other.program_ = 0;
        }
        return *this;
    }

    void use() const { glUseProgram(program_); }

    void setFloat(const char* name, float value) const {
        glUniform1f(location(name), value);
    }

    void setInt(const char* name, int value) const {
        glUniform1i(location(name), value);
    }

    void setVec2(const char* name, const glm::vec2& value) const {
        glUniform2fv(location(name), 1, glm::value_ptr(value));
    }

    void setVec3(const char* name, const glm::vec3& value) const {
        glUniform3fv(location(name), 1, glm::value_ptr(value));
    }

    void setVec4(const char* name, const glm::vec4& value) const {
        glUniform4fv(location(name), 1, glm::value_ptr(value));
    }

    void setVec4Array(const char* name, std::span<const glm::vec4> values) const {
        if (!values.empty()) {
            glUniform4fv(location(name), static_cast<GLsizei>(values.size()),
                         glm::value_ptr(values.front()));
        }
    }

    void setMat4(const char* name, const glm::mat4& value) const {
        glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
    }

private:
    GLuint program_ = 0;
    mutable std::unordered_map<std::string, GLint> locations_;

    static std::string readFile(const std::string& path) {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to open shader file: " + path);
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    // Expands `#include "file"` lines relative to the including file.
    static std::string readSource(const std::string& path, int depth) {
        if (depth > 8) {
            throw std::runtime_error("Shader include depth exceeded at " + path);
        }
        const std::string directory = std::filesystem::path(path).parent_path().string();
        std::istringstream lines(readFile(path));
        std::ostringstream expanded;
        std::string line;
        while (std::getline(lines, line)) {
            const auto directive = line.find("#include");
            const auto open = line.find('"');
            const auto close = line.rfind('"');
            if (directive != std::string::npos && open != std::string::npos && close > open) {
                const std::string name = line.substr(open + 1, close - open - 1);
                expanded << readSource((std::filesystem::path(directory) / name).string(), depth + 1);
            } else {
                expanded << line << '\n';
            }
        }
        return expanded.str();
    }

    static GLuint compile(GLenum type, const std::string& source, const std::string& path) {
        const GLuint shader = glCreateShader(type);
        const char* sourcePointer = source.c_str();
        glShaderSource(shader, 1, &sourcePointer, nullptr);
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE) {
            GLint length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            glGetShaderInfoLog(shader, length, nullptr, log.data());
            glDeleteShader(shader);
            throw std::runtime_error("Shader compilation failed (" + path + "):\n" + log);
        }
        return shader;
    }

    GLint location(const char* name) const {
        if (const auto found = locations_.find(name); found != locations_.end()) {
            return found->second;
        }
        const GLint result = glGetUniformLocation(program_, name);
        locations_.emplace(name, result);
        return result;
    }
};
