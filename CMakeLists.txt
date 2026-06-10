cmake_minimum_required(VERSION 3.21)

project(EvolveShader
    VERSION 0.1.0
    DESCRIPTION "Minimal multi-pass Shadertoy-style shader viewer"
    LANGUAGES C CXX
)

find_package(Git REQUIRED)

set(GLFW_GIT_REF 3.4)
set(GLEW_GIT_REF glew-cmake-2.2.0)
set(STB_GIT_REF 31c1ad37456438565541f4919958214b6e762fb4)

function(fetch_git_dependency name repository ref)
    set(source_dir "${CMAKE_CURRENT_SOURCE_DIR}/3rd/${name}-src")

    if(NOT EXISTS "${source_dir}/.git")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rd")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --quiet --no-checkout --depth 1 "${repository}" "${source_dir}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" fetch --quiet --depth 1 origin "${ref}"
        WORKING_DIRECTORY "${source_dir}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c advice.detachedHead=false checkout --quiet --detach FETCH_HEAD
        WORKING_DIRECTORY "${source_dir}"
        COMMAND_ERROR_IS_FATAL ANY
    )

    set(${name}_SOURCE_DIR "${source_dir}" PARENT_SCOPE)
endfunction()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

fetch_git_dependency(glfw https://github.com/glfw/glfw.git "${GLFW_GIT_REF}")
fetch_git_dependency(glew https://github.com/Perlmint/glew-cmake.git "${GLEW_GIT_REF}")
fetch_git_dependency(stb https://github.com/nothings/stb.git "${STB_GIT_REF}")

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
