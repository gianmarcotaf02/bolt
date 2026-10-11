cmake_minimum_required(VERSION 3.20)

project(FilePilotClone
    VERSION 0.1.0
    DESCRIPTION "Clone of a fast native Windows file manager, reconstructed from static binary analysis"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_executable(FilePilotClone WIN32
    src/main.cpp
    src/ui_renderer.cpp
    src/ui_renderer.h
    src/font_atlas.cpp
    src/font_atlas.h
    src/shell_icons.cpp
    src/shell_icons.h
    src/shell_preview.cpp
    src/shell_preview.h
    src/dir_stats.cpp
    src/dir_stats.h
    src/gl_loader.cpp
    src/gl_loader.h)

target_include_directories(FilePilotClone PRIVATE src)

# The shader directory is passed in as a compile definition so development builds
# read the shaders straight from the source tree and can be iterated on without
# rebuilding.
target_compile_definitions(FilePilotClone PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    REA_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")

# opengl32 provides the OpenGL 1.1 entry points; everything newer is resolved at
# runtime through wglGetProcAddress.
# shell32 + ole32 + uuid provide the shell icon, thumbnail and known-folder APIs:
# the FOLDERID_* identifiers live in uuid.lib.
target_link_libraries(FilePilotClone PRIVATE opengl32 gdi32 user32 shell32 ole32 uuid)

if(MSVC)
    target_compile_options(FilePilotClone PRIVATE /W4 /permissive- /utf-8)
    # Static CRT keeps the executable self-contained, matching the reference
    # application's standalone single-file property.
    set_property(TARGET FilePilotClone PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(FilePilotClone PRIVATE -Wall -Wextra)
endif()

# Also stage the shaders next to the binary so a copied build directory stays
# runnable.
add_custom_command(TARGET FilePilotClone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
        "$<TARGET_FILE_DIR:FilePilotClone>/shaders"
    COMMENT "Staging shaders next to the executable")
