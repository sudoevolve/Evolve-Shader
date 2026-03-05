// main.cpp - Multi-pass Shader Renderer with Full RAII and Cross-Pass iChannel Support
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <ctime>
#include <regex>
#include <algorithm>
#include <set>
#include <filesystem>
#include <map>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION

#pragma warning(disable : 4244)
#pragma warning(disable : 4566)

namespace fs = std::filesystem;

#include "include/GLTypes.h"        // Texture, VertexBuffer, VertexArray, Framebuffer (requires STB_IMAGE_IMPLEMENTATION)
#include "include/ShaderProgram.h"  // CompileShader, CreateProgram, GLProgram
#include "include/ShaderIO.h"       // LoadShaderFile, ScanShaderFiles, WrapShadertoyShader
#include "include/Resources.h"      // g_globalTextureCache, GetTextureForPath, ScanGlobalImages
#include "include/ChannelConfig.h"  // ChannelInput, ConfigureChannelsInteractively


double g_mouseX = 0.0, g_mouseY = 0.0;
double g_lastClickX = 0.0, g_lastClickY = 0.0;
bool g_mouseLeftDown = false;
int g_winWidth = 960, g_winHeight = 540;
int g_frame = 0;

std::chrono::steady_clock::time_point g_start;

void cursorPosCallback(GLFWwindow* window, double x, double y) {
    g_mouseX = x;
    g_mouseY = y;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    // Shadertoy iMouse convention:
    // - iMouse.xy: current mouse position (origin at bottom-left), in pixels
    // - iMouse.zw: current position when pressed; when not pressed, negative last click position
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_mouseLeftDown = true;
            // Record mouse position on click
            g_lastClickX = g_mouseX;
            g_lastClickY = g_mouseY;
        }
        else if (action == GLFW_RELEASE) {
            g_mouseLeftDown = false;
        }
    }
}

struct RenderResources {
    std::vector<Framebuffer>* pingFbos;
    std::vector<Framebuffer>* pongFbos;
};

void framebufferSizeCallback(GLFWwindow* window, int w, int h);

void resizeAllRenderTargets(std::vector<Framebuffer>& pingFbos,
    std::vector<Framebuffer>& pongFbos,
    int w, int h);

const char* vertShaderSrc = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aTex;
out vec2 vTex;
void main() {
    vTex = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// ScanShaderFiles is now provided by include/ShaderIO.h

// ConfigureChannelsInteractively is provided by include/ChannelConfig.h

const char* presentFragSrc = R"GLSL(
#version 330 core
in vec2 vTex;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = texture(uTex, vTex);
}
)GLSL";

void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
    g_winWidth = w;
    g_winHeight = h;
    glViewport(0, 0, w, h);
    // Reset frame counter after resize so feedback buffers re-initialize
    g_frame = 0;

    auto* pResources = static_cast<RenderResources*>(glfwGetWindowUserPointer(window));
    if (pResources) {
        resizeAllRenderTargets(*pResources->pingFbos, *pResources->pongFbos, w, h);
    }
}

// ConfigureChannelsInteractively implementation moved to include/ChannelConfig.h

void resizeAllRenderTargets(std::vector<Framebuffer>& pingFbos,
    std::vector<Framebuffer>& pongFbos,
    int w, int h) {
    for (auto& fbo : pingFbos) {
        fbo.create(w, h);
    }
    for (auto& fbo : pongFbos) {
        fbo.create(w, h);
    }
}

int main() {
    auto g_globalImages = ScanGlobalImages();
    if (!g_globalImages.empty()) {
        std::cout << "\nFound " << g_globalImages.size() << " global image(s):\n";
        for (size_t i = 0; i < g_globalImages.size(); ++i) {
            std::cout << "  [" << i + 1 << "] " << g_globalImages[i].filename().string()
                << " (" << g_globalImages[i].parent_path().filename().string() << ")\n";
        }
    }

    if (!fs::exists("frag") || !fs::is_directory("frag")) {
        std::cerr << "Error: 'frag' folder not found!\n";
        return -1;
    }
    auto fragFiles = ScanShaderFiles();
    if (fragFiles.empty()) {
        std::cerr << "No .frag files found!\n";
        return -1;
    }
    std::cout << "\nFound " << fragFiles.size() << " shader(s):\n";
    for (size_t i = 0; i < fragFiles.size(); ++i) {
        std::cout << "  [" << i + 1 << "] " << fs::path(fragFiles[i]).filename().string() << "\n";
    }

    auto channelConfig = ConfigureChannelsInteractively(fragFiles, g_globalImages);

    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(g_winWidth, g_winHeight, "Evolve Shader", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSwapInterval(0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\n";
    bool sRGBCapable = glfwGetWindowAttrib(window, GLFW_SRGB_CAPABLE);
    // Do not enable sRGB globally; enable it only for final screen output

    VertexArray vao;
    float quad[] = {
        -1.f, -1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 0.f,
         1.f,  1.f, 1.f, 1.f,
        -1.f, -1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 1.f
    };
    VertexBuffer vbo(quad, sizeof(quad));
    vao.bind(); vbo.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    VertexArray::unbind();

    std::vector<GLProgram> programs;
    for (const auto& file : fragFiles) {
        std::string code = LoadShaderFile(file);
        if (code.empty()) continue;
        code = WrapShadertoyShader(code);
        programs.emplace_back(vertShaderSrc, code.c_str());
    }
    if (programs.empty()) return -1;

    GLProgram presentProgram(vertShaderSrc, presentFragSrc);

    std::vector<Framebuffer> pingFbos(programs.size());
    std::vector<Framebuffer> pongFbos(programs.size());

    resizeAllRenderTargets(pingFbos, pongFbos, g_winWidth, g_winHeight);

    RenderResources resources{ &pingFbos, &pongFbos };
    glfwSetWindowUserPointer(window, &resources);

    Texture emptyTex;
    emptyTex.createEmpty();

    g_start = std::chrono::steady_clock::now();
    float lastTimeVal = 0.0f;
    double lastFPSTime = glfwGetTime();
    int frameCount = 0;
    bool usePingAsPrev = true;

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        frameCount++;
        if (currentTime - lastFPSTime >= 1.0) {
            std::string title = "Evolve Shader - FPS: " + std::to_string(frameCount);
            glfwSetWindowTitle(window, title.c_str());
            frameCount = 0;
            lastFPSTime = currentTime;
        }

        auto now = std::chrono::steady_clock::now();
        float t = std::chrono::duration<float>(now - g_start).count();
        float dt = t - lastTimeVal;
        lastTimeVal = t;

        int width = g_winWidth;
        int height = g_winHeight;
        int frameIndex = g_frame;

        std::time_t nowTime = std::time(nullptr);
        std::tm localTm{};
        localtime_s(&localTm, &nowTime);
        float dateYear = (float)(localTm.tm_year + 1900);
        float dateMonth = (float)(localTm.tm_mon + 1);
        float dateDay = (float)localTm.tm_mday;
        float dateSeconds = (float)(localTm.tm_hour * 3600 + localTm.tm_min * 60 + localTm.tm_sec);
        float frameRate = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
        float channelTimes[4] = { t, t, t, t };
        float sampleRate = 44100.0f;

        auto& prevFbos = usePingAsPrev ? pingFbos : pongFbos;
        auto& currFbos = usePingAsPrev ? pongFbos : pingFbos;

        for (size_t i = 0; i < programs.size(); ++i) {
            programs[i].use();
            glUniform3f(programs[i].getUniformLocation("iResolution"), (float)width, (float)height, 1.0f);
            glUniform1f(programs[i].getUniformLocation("iTime"), t);
            glUniform1f(programs[i].getUniformLocation("iTimeDelta"), dt);
            glUniform1i(programs[i].getUniformLocation("iFrame"), frameIndex);

            GLint frameRateLoc = programs[i].getUniformLocation("iFrameRate");
            if (frameRateLoc != -1) glUniform1f(frameRateLoc, frameRate);
            GLint dateLoc = programs[i].getUniformLocation("iDate");
            if (dateLoc != -1) glUniform4f(dateLoc, dateYear, dateMonth, dateDay, dateSeconds);
            GLint channelTimeLoc = programs[i].getUniformLocation("iChannelTime[0]");
            if (channelTimeLoc != -1) glUniform1fv(channelTimeLoc, 4, channelTimes);
            GLint sampleRateLoc = programs[i].getUniformLocation("iSampleRate");
            if (sampleRateLoc != -1) glUniform1f(sampleRateLoc, sampleRate);

            GLint loc = programs[i].getUniformLocation("iMouse");
            if (loc != -1) {
                // Match Shadertoy iMouse semantics
                float curX = (float)g_mouseX;
                float curY = (float)(height - g_mouseY); // Flip Y so origin is bottom-left
                float clickX, clickY;
                if (g_mouseLeftDown) {
                    // Pressed: zw is current mouse position
                    clickX = curX;
                    clickY = curY;
                } else {
                    // Not pressed: zw is negative last click position
                    clickX = -(float)g_lastClickX;
                    clickY = -(float)(height - g_lastClickY);
                }
                glUniform4f(loc, curX, curY, clickX, clickY);
            }

            auto& configForThis = channelConfig[i];
            for (int c = 0; c < 4; ++c) {
                const ChannelInput& input = configForThis[c];
                std::string name = "iChannel" + std::to_string(c);
                GLint loc = programs[i].getUniformLocation(name);
                if (loc == -1) continue;

                Texture* texToBind = &emptyTex;
                switch (input.type) {
                case ChannelInput::NONE:
                    break;
                case ChannelInput::IMAGE_GLOBAL:
                    if (input.imageIndex >= 0 && input.imageIndex < (int)g_globalImages.size()) {
                        std::string imgPath = g_globalImages[input.imageIndex].string();
                        texToBind = GetTextureForPath(imgPath);
                    }
                    break;
                case ChannelInput::BUFFER:
                    texToBind = &prevFbos[input.bufferIndex].colorTex;
                    break;
                }
                texToBind->bind(c);
                glUniform1i(loc, c);

                std::string resName = "iChannelResolution[" + std::to_string(c) + "]";
                GLint resLoc = programs[i].getUniformLocation(resName);
                if (resLoc != -1) {
                    glUniform3f(resLoc, (float)texToBind->width, (float)texToBind->height, 1.0f);
                }
            }

            currFbos[i].bind();
            glViewport(0, 0, width, height);
            if (sRGBCapable) glDisable(GL_FRAMEBUFFER_SRGB);

            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, 6);
            VertexArray::unbind();
        }

        Framebuffer::unbind();
        glViewport(0, 0, width, height);
        if (sRGBCapable) glEnable(GL_FRAMEBUFFER_SRGB);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        presentProgram.use();
        currFbos.back().colorTex.bind(0);
        GLint presentTexLoc = presentProgram.getUniformLocation("uTex");
        if (presentTexLoc != -1) glUniform1i(presentTexLoc, 0);
        vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        VertexArray::unbind();
        if (sRGBCapable) glDisable(GL_FRAMEBUFFER_SRGB);

        glfwSwapBuffers(window);
        glfwPollEvents();
        usePingAsPrev = !usePingAsPrev;
        g_frame++;
    }

    return 0;
}
