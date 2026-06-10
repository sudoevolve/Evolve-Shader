cmake_minimum_required(VERSION 3.21)

project(EvolveShader
    VERSION 0.1.0
    DESCRIPTION "Minimal multi-pass Shadertoy-style shader viewer"
    LANGUAGES C CXX
)

find_package(Git REQUIRED)

function(fetch_git_dependency name repository tag)
    set(source_dir "${CMAKE_CURRENT_SOURCE_DIR}/3rd/${name}-src")

    if(EXISTS "${source_dir}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" fetch --depth 1 origin "${tag}"
            WORKING_DIRECTORY "${source_dir}"
            COMMAND_ERROR_IS_FATAL ANY
        )
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" checkout --detach FETCH_HEAD
            WORKING_DIRECTORY "${source_dir}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    else()
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rd")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --depth 1 --branch "${tag}" "${repository}" "${source_dir}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()

    set(${name}_SOURCE_DIR "${source_dir}" PARENT_SCOPE)
endfunction()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ONLY_LIBS ON CACHE BOOL "" FORCE)

fetch_git_dependency(glfw https://github.com/glfw/glfw.git 3.4)
fetch_git_dependency(glew https://github.com/Perlmint/glew-cmake.git glew-cmake-2.2.0)
fetch_git_dependency(stb https://github.com/nothings/stb.git master)

add_subdirectory("${glfw_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/3rd/glfw-build")

add_library(libglew_static STATIC
    "${glew_SOURCE_DIR}/src/glew.c"
)

target_include_directories(libglew_static
    PUBLIC
        "${glew_SOURCE_DIR}/include"
)

target_compile_definitions(libglew_static
    PUBLIC
        GLEW_STATIC
    PRIVATE
        GLEW_NO_GLU
)

add_executable(EvolveShader
    main.cpp
    include/ChannelConfig.h
    include/GLTypes.h
    include/PresetManager.h
    include/Resources.h
    include/ShaderIO.h
    include/ShaderProgram.h
)

target_compile_features(EvolveShader PRIVATE cxx_std_17)

target_include_directories(EvolveShader
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${stb_SOURCE_DIR}"
)

find_package(OpenGL REQUIRED)

target_link_libraries(EvolveShader
    PRIVATE
        OpenGL::GL
        glfw
        libglew_static
)

if(MSVC)
    target_compile_options(EvolveShader PRIVATE /W3 /permissive-)
else()
    target_compile_options(EvolveShader PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
    target_link_libraries(EvolveShader PRIVATE "-framework Cocoa" "-framework IOKit")
endif()

set_target_properties(EvolveShader PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:EvolveShader>"
)

set(EVOLVE_SHADER_RUNTIME_DIRS frag iChannel presets)
foreach(runtime_dir IN LISTS EVOLVE_SHADER_RUNTIME_DIRS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${runtime_dir}")
        add_custom_command(TARGET EvolveShader POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/${runtime_dir}"
                    "$<TARGET_FILE_DIR:EvolveShader>/${runtime_dir}"
            COMMENT "Copying ${runtime_dir} runtime assets"
        )
    endif()
endforeach()
