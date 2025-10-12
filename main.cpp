// main.cpp - Multi-pass Shader Renderer with Full RAII and Cross-Pass iChannel Support
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <chrono>
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
    // Shadertoy iMouse 约定：
    // - iMouse.xy 为当前鼠标位置（从左下角开始，像素坐标）
    // - iMouse.zw 为按下时的当前位置；未按下时为上次点击位置的负值
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_mouseLeftDown = true;
            // 记录点击时的鼠标位置
            g_lastClickX = g_mouseX;
            g_lastClickY = g_mouseY;
        }
        else if (action == GLFW_RELEASE) {
            g_mouseLeftDown = false;
        }
    }
}

struct RenderResources {
    std::vector<Framebuffer>* fbos;
    std::vector<Texture>* prevFrameTextures;
};

void framebufferSizeCallback(GLFWwindow* window, int w, int h);

void resizeAllRenderTargets(std::vector<Framebuffer>& fbos,
    std::vector<Texture>& prevTextures,
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

void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
    g_winWidth = w;
    g_winHeight = h;
    glViewport(0, 0, w, h);
    // 缩放后重置帧计数，确保自反馈缓冲重新初始化，避免黑屏
    g_frame = 0;

    auto* pResources = static_cast<RenderResources*>(glfwGetWindowUserPointer(window));
    if (pResources) {
        resizeAllRenderTargets(*pResources->fbos, *pResources->prevFrameTextures, w, h);
    }
}

// ConfigureChannelsInteractively implementation moved to include/ChannelConfig.h

void resizeAllRenderTargets(std::vector<Framebuffer>& fbos,
    std::vector<Texture>& prevTextures,
    int w, int h) {
    for (auto& fbo : fbos) {
        fbo.create(w, h);
    }
    for (auto& tex : prevTextures) {
        glBindTexture(GL_TEXTURE_2D, tex.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        // 统一缓冲采样为线性，与图片采样保持一致
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        tex.width = w;
        tex.height = h;
    }
}

int main() {
    auto g_globalImages = ScanGlobalImages();
    if (!g_globalImages.empty()) {
        std::cout << "\nFound " << g_globalImages.size() << " global image(s):\n";
        for (size_t i = 0; i < g_globalImages.size(); ++i) {
            std::cout << "  [" << i << "] " << g_globalImages[i].filename().string()
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
        std::cout << "  [" << i << "] " << fs::path(fragFiles[i]).filename().string() << "\n";
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
    // 不在此处全局启用 sRGB，改为在渲染循环中针对最终屏幕输出按需启用

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

    std::vector<Framebuffer> fbos(programs.size());
    std::vector<Texture> prevFrameTextures(programs.size());

    for (auto& tex : prevFrameTextures) {
        glGenTextures(1, &tex.id);
    }

    resizeAllRenderTargets(fbos, prevFrameTextures, g_winWidth, g_winHeight);

    RenderResources resources{ &fbos, &prevFrameTextures };
    glfwSetWindowUserPointer(window, &resources);

    // 用于高效将 FBO 结果复制到纹理的绘制帧缓冲
    GLuint blitFBO = 0;
    glGenFramebuffers(1, &blitFBO);

    Texture emptyTex;
    emptyTex.createEmpty();

    g_start = std::chrono::steady_clock::now();
    float lastTimeVal = 0.0f;
    double lastFPSTime = glfwGetTime();
    int frameCount = 0;

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

        for (size_t i = 0; i < programs.size(); ++i) {
            programs[i].use();
            glUniform3f(programs[i].getUniformLocation("iResolution"), (float)width, (float)height, 1.0f);
            glUniform1f(programs[i].getUniformLocation("iTime"), t);
            glUniform1f(programs[i].getUniformLocation("iTimeDelta"), dt);
            glUniform1i(programs[i].getUniformLocation("iFrame"), g_frame++);

            GLint loc = programs[i].getUniformLocation("iMouse");
            if (loc != -1) {
                // 与 Shadertoy 保持一致的 iMouse 语义
                float curX = (float)g_mouseX;
                float curY = (float)(height - g_mouseY); // 翻转 Y 至左下为原点
                float clickX, clickY;
                if (g_mouseLeftDown) {
                    // 鼠标按下：zw 为当前鼠标位置
                    clickX = curX;
                    clickY = curY;
                } else {
                    // 鼠标未按下：zw 为上次点击位置的负值
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
                    texToBind = &prevFrameTextures[input.bufferIndex];
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

            if (i == programs.size() - 1) {
                // 最后一帧渲染到默认帧缓冲：只有此时启用 sRGB 写入转换
                Framebuffer::unbind();
                glViewport(0, 0, width, height);
                if (sRGBCapable) glEnable(GL_FRAMEBUFFER_SRGB);
            }
            else {
                // 中间 Pass 渲染到线性浮点 FBO：明确禁用 sRGB，以避免不必要的状态影响
                fbos[i].bind();
                glViewport(0, 0, width, height);
                if (sRGBCapable) glDisable(GL_FRAMEBUFFER_SRGB);
            }

            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, 6);
            VertexArray::unbind();
        }

        // 使用帧缓冲 Blit 替换逐纹理 Copy，提高复制性能
        for (size_t i = 0; i < fbos.size(); ++i) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[i].fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blitFBO);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, prevFrameTextures[i].id, 0);
            glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        // 清理绑定状态
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 释放临时帧缓冲
    if (blitFBO) glDeleteFramebuffers(1, &blitFBO);
    return 0;
}