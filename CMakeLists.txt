cmake_minimum_required(VERSION 3.20)

project(SchwarzschildBlackHole VERSION 1.0.0 LANGUAGES CXX)

if(NOT APPLE)
    message(WARNING "This project targets macOS OpenGL 4.1 Core Profile.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Make Homebrew packages discoverable on both Apple Silicon and Intel Macs.
list(APPEND CMAKE_PREFIX_PATH
    "/opt/homebrew"
    "/opt/homebrew/opt/glfw"
    "/opt/homebrew/opt/glm"
    "/opt/homebrew/opt/glew"
    "/usr/local"
    "/usr/local/opt/glfw"
    "/usr/local/opt/glm"
    "/usr/local/opt/glew")

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(GLEW REQUIRED)

add_executable(schwarzschild_black_hole
    main.cpp
    Camera.hpp
    SceneObjects.hpp
    Shader.hpp)

target_compile_features(schwarzschild_black_hole PRIVATE cxx_std_20)
target_compile_definitions(schwarzschild_black_hole PRIVATE
    GL_SILENCE_DEPRECATION
    BLACK_HOLE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(schwarzschild_black_hole PRIVATE
    OpenGL::GL
    glfw
    glm::glm
    GLEW::GLEW)

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    target_compile_options(schwarzschild_black_hole PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

# Keep runtime shader loading independent of the current working directory.
add_custom_command(TARGET schwarzschild_black_hole POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:schwarzschild_black_hole>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/blackhole.vert"
            "${CMAKE_CURRENT_SOURCE_DIR}/blackhole.frag"
            "$<TARGET_FILE_DIR:schwarzschild_black_hole>/shaders"
    COMMENT "Copying GLSL shaders")

include(CTest)
if(BUILD_TESTING)
    add_executable(scene_objects_test tests/SceneObjectsTest.cpp)
    target_include_directories(scene_objects_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(scene_objects_test PRIVATE glm::glm)
    target_compile_features(scene_objects_test PRIVATE cxx_std_20)
    add_test(NAME scene_objects_physics COMMAND scene_objects_test)
endif()
