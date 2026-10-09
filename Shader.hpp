#pragma once

#include <GL/glew.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Shader {
public:
    Shader(const std::string& vertexPath, const std::string& fragmentPath) {
        const std::string vertexSource = readFile(vertexPath);
        const std::string fragmentSource = readFile(fragmentPath);

        const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource, vertexPath);
        const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, fragmentPath);

        program_ = glCreateProgram();
        glAttachShader(program_, vertex);
        glAttachShader(program_, fragment);
        glLinkProgram(program_);

        GLint linked = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint length = 0;
            glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(program_, length, nullptr, log.data());
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            glDeleteProgram(program_);
            program_ = 0;
            throw std::runtime_error("Shader program link failed:\n" + log);
        }

        glDetachShader(program_, vertex);
        glDetachShader(program_, fragment);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }

    ~Shader() {
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
    }

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept : program_(other.program_) { other.program_ = 0; }

    Shader& operator=(Shader&& other) noexcept {
        if (this != &other) {
            if (program_ != 0) {
                glDeleteProgram(program_);
            }
            program_ = other.program_;
            other.program_ = 0;
        }
        return *this;
    }

    void use() const { glUseProgram(program_); }

    void setFloat(const char* name, float value) const {
        glUniform1f(location(name), value);
    }

    void setVec2(const char* name, const glm::vec2& value) const {
        glUniform2fv(location(name), 1, glm::value_ptr(value));
    }

    void setVec3(const char* name, const glm::vec3& value) const {
        glUniform3fv(location(name), 1, glm::value_ptr(value));
    }

    void setMat4(const char* name, const glm::mat4& value) const {
        glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
    }

private:
    GLuint program_ = 0;

    static std::string readFile(const std::string& path) {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to open shader file: " + path);
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
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
        return glGetUniformLocation(program_, name);
    }
};
