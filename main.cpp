// main.cpp - Multi-pass Shader Renderer with Full RAII and Cross-Pass iChannel Support
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION

#pragma warning(disable : 4244)
#pragma warning(disable : 4566)

namespace fs = std::filesystem;

#include "include/GLTypes.h"
#include "include/ShaderProgram.h"
#include "include/ShaderIO.h"
#include "include/Resources.h"
#include "include/ChannelConfig.h"
#include "include/PresetManager.h"

namespace {

constexpr int kChannelCount = 4;
constexpr int kInitialWindowWidth = 960;
constexpr int kInitialWindowHeight = 540;

struct InputState {
    double mouseX = 0.0;
    double mouseY = 0.0;
    double lastClickX = 0.0;
    double lastClickY = 0.0;
    bool mouseLeftDown = false;
};

struct AppContext {
    InputState input;
    int winWidth = kInitialWindowWidth;
    int winHeight = kInitialWindowHeight;
    int frame = 0;
    std::chrono::steady_clock::time_point start;
    std::vector<Framebuffer>* pingFbos = nullptr;
    std::vector<Framebuffer>* pongFbos = nullptr;
};

struct ProgramUniforms {
    GLint iResolution = -1;
    GLint iTime = -1;
    GLint iTimeDelta = -1;
    GLint iFrame = -1;
    GLint iFrameRate = -1;
    GLint iDate = -1;
    GLint iMouse = -1;
    GLint iChannelTime0 = -1;
    GLint iSampleRate = -1;
    std::array<GLint, kChannelCount> iChannel{ -1, -1, -1, -1 };
    std::array<GLint, kChannelCount> iChannelResolution{ -1, -1, -1, -1 };
};

struct ShaderPass {
    GLProgram program;
    ProgramUniforms uniforms;
};

struct PresentPass {
    GLProgram program;
    GLint textureLocation = -1;
};

struct FrameUniformData {
    int width = 0;
    int height = 0;
    int frameIndex = 0;
    float time = 0.0f;
    float deltaTime = 0.0f;
    float frameRate = 0.0f;
    float sampleRate = 44100.0f;
    std::array<float, 4> date{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> mouse{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, kChannelCount> channelTimes{ 0.0f, 0.0f, 0.0f, 0.0f };
};

struct ChannelBinding {
    ChannelInput::Type type = ChannelInput::NONE;
    int bufferIndex = -1;
    const Texture* globalTexture = nullptr;
};

using PassChannelBindings = std::array<ChannelBinding, kChannelCount>;

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

const char* presentFragSrc = R"GLSL(
#version 330 core
in vec2 vTex;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = texture(uTex, vTex);
}
)GLSL";

struct GlfwSession {
    bool initialized = false;

    GlfwSession() : initialized(glfwInit() == GLFW_TRUE) {}
    ~GlfwSession() {
        if (initialized) {
            glfwTerminate();
        }
    }

    explicit operator bool() const {
        return initialized;
    }
};

struct GLFWWindowDeleter {
    void operator()(GLFWwindow* window) const {
        if (window) {
            glfwDestroyWindow(window);
        }
    }
};

using WindowHandle = std::unique_ptr<GLFWwindow, GLFWWindowDeleter>;

AppContext* GetAppContext(GLFWwindow* window) {
    return static_cast<AppContext*>(glfwGetWindowUserPointer(window));
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

void cursorPosCallback(GLFWwindow* window, double x, double y) {
    if (auto* app = GetAppContext(window)) {
        app->input.mouseX = x;
        app->input.mouseY = y;
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;

    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }

    if (auto* app = GetAppContext(window)) {
        if (action == GLFW_PRESS) {
            app->input.mouseLeftDown = true;
            app->input.lastClickX = app->input.mouseX;
            app->input.lastClickY = app->input.mouseY;
        }
        else if (action == GLFW_RELEASE) {
            app->input.mouseLeftDown = false;
        }
    }
}

void resizeAllRenderTargets(std::vector<Framebuffer>& pingFbos,
    std::vector<Framebuffer>& pongFbos,
    int width,
    int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    for (auto& fbo : pingFbos) {
        fbo.create(width, height);
    }
    for (auto& fbo : pongFbos) {
        fbo.create(width, height);
    }
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (auto* app = GetAppContext(window)) {
        app->winWidth = width;
        app->winHeight = height;
        app->frame = 0;

        if (app->pingFbos && app->pongFbos) {
            resizeAllRenderTargets(*app->pingFbos, *app->pongFbos, width, height);
        }
    }

    glViewport(0, 0, width, height);
}

void PrintGlobalImages(const std::vector<fs::path>& globalImages) {
    if (globalImages.empty()) {
        return;
    }

    std::cout << "\nFound " << globalImages.size() << " global image(s):\n";
    for (size_t index = 0; index < globalImages.size(); ++index) {
        std::cout << "  [" << index + 1 << "] "
            << globalImages[index].filename().string()
            << " (" << globalImages[index].parent_path().filename().string() << ")\n";
    }
}

bool ValidateShaderDirectory(const fs::path& dir = "frag") {
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "Error: '" << dir.string() << "' folder not found!\n";
        return false;
    }
    return true;
}

void PrintShaderFiles(const std::vector<std::string>& fragFiles) {
    std::cout << "\nFound " << fragFiles.size() << " shader(s):\n";
    for (size_t index = 0; index < fragFiles.size(); ++index) {
        std::cout << "  [" << index + 1 << "] " << fs::path(fragFiles[index]).filename().string() << "\n";
    }
}

ProgramUniforms CacheProgramUniforms(const GLProgram& program) {
    ProgramUniforms uniforms;
    uniforms.iResolution = program.getUniformLocation("iResolution");
    uniforms.iTime = program.getUniformLocation("iTime");
    uniforms.iTimeDelta = program.getUniformLocation("iTimeDelta");
    uniforms.iFrame = program.getUniformLocation("iFrame");
    uniforms.iFrameRate = program.getUniformLocation("iFrameRate");
    uniforms.iDate = program.getUniformLocation("iDate");
    uniforms.iMouse = program.getUniformLocation("iMouse");
    uniforms.iChannelTime0 = program.getUniformLocation("iChannelTime[0]");
    uniforms.iSampleRate = program.getUniformLocation("iSampleRate");

    for (int channel = 0; channel < kChannelCount; ++channel) {
        uniforms.iChannel[channel] = program.getUniformLocation("iChannel" + std::to_string(channel));
        uniforms.iChannelResolution[channel] = program.getUniformLocation("iChannelResolution[" + std::to_string(channel) + "]");
    }

    return uniforms;
}

std::vector<ShaderPass> BuildShaderPasses(const std::vector<std::string>& fragFiles) {
    std::vector<ShaderPass> passes;
    passes.reserve(fragFiles.size());

    for (const auto& file : fragFiles) {
        std::string code = LoadShaderFile(file);
        if (code.empty()) {
            std::cerr << "Failed to load shader pass: " << file << "\n";
            return {};
        }

        const std::string wrappedCode = WrapShadertoyShader(code);

        ShaderPass pass;
        pass.program = GLProgram(vertShaderSrc, wrappedCode.c_str());
        if (!pass.program.id) {
            std::cerr << "Failed to build shader pass: " << file << "\n";
            return {};
        }

        pass.uniforms = CacheProgramUniforms(pass.program);
        passes.emplace_back(std::move(pass));
    }

    return passes;
}

PresentPass BuildPresentPass() {
    PresentPass pass;
    pass.program = GLProgram(vertShaderSrc, presentFragSrc);
    if (pass.program.id) {
        pass.textureLocation = pass.program.getUniformLocation("uTex");
    }
    return pass;
}

WindowHandle CreateWindow(AppContext& app, bool vsyncEnabled) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    GLFWwindow* rawWindow = glfwCreateWindow(app.winWidth, app.winHeight, "Evolve Shader", nullptr, nullptr);
    if (!rawWindow) {
        return WindowHandle{};
    }

    glfwMakeContextCurrent(rawWindow);
    glfwSwapInterval(vsyncEnabled ? 1 : 0);
    glfwSetWindowUserPointer(rawWindow, &app);
    glfwSetCursorPosCallback(rawWindow, cursorPosCallback);
    glfwSetMouseButtonCallback(rawWindow, mouseButtonCallback);
    glfwSetFramebufferSizeCallback(rawWindow, framebufferSizeCallback);
    return WindowHandle(rawWindow);
}

FrameUniformData BuildFrameUniformData(const AppContext& app, float timeSeconds, float deltaTimeSeconds) {
    FrameUniformData data;
    data.width = app.winWidth;
    data.height = app.winHeight;
    data.frameIndex = app.frame;
    data.time = timeSeconds;
    data.deltaTime = deltaTimeSeconds;
    data.frameRate = (deltaTimeSeconds > 0.0f) ? (1.0f / deltaTimeSeconds) : 0.0f;
    data.channelTimes = { timeSeconds, timeSeconds, timeSeconds, timeSeconds };

    std::time_t nowTime = std::time(nullptr);
    std::tm localTm{};
#ifdef _WIN32
    localtime_s(&localTm, &nowTime);
#else
    localtime_r(&nowTime, &localTm);
#endif
    data.date = {
        static_cast<float>(localTm.tm_year + 1900),
        static_cast<float>(localTm.tm_mon + 1),
        static_cast<float>(localTm.tm_mday),
        static_cast<float>(localTm.tm_hour * 3600 + localTm.tm_min * 60 + localTm.tm_sec)
    };

    const float curX = static_cast<float>(app.input.mouseX);
    const float curY = static_cast<float>(app.winHeight - app.input.mouseY);
    if (app.input.mouseLeftDown) {
        data.mouse = { curX, curY, curX, curY };
    }
    else {
        data.mouse = {
            curX,
            curY,
            -static_cast<float>(app.input.lastClickX),
            -static_cast<float>(app.winHeight - app.input.lastClickY)
        };
    }

    return data;
}

void ApplyCommonUniforms(const ProgramUniforms& uniforms, const FrameUniformData& frameData) {
    if (uniforms.iResolution != -1) {
        glUniform3f(uniforms.iResolution, static_cast<float>(frameData.width), static_cast<float>(frameData.height), 1.0f);
    }
    if (uniforms.iTime != -1) {
        glUniform1f(uniforms.iTime, frameData.time);
    }
    if (uniforms.iTimeDelta != -1) {
        glUniform1f(uniforms.iTimeDelta, frameData.deltaTime);
    }
    if (uniforms.iFrame != -1) {
        glUniform1i(uniforms.iFrame, frameData.frameIndex);
    }
    if (uniforms.iFrameRate != -1) {
        glUniform1f(uniforms.iFrameRate, frameData.frameRate);
    }
    if (uniforms.iDate != -1) {
        glUniform4f(uniforms.iDate, frameData.date[0], frameData.date[1], frameData.date[2], frameData.date[3]);
    }
    if (uniforms.iMouse != -1) {
        glUniform4f(uniforms.iMouse, frameData.mouse[0], frameData.mouse[1], frameData.mouse[2], frameData.mouse[3]);
    }
    if (uniforms.iChannelTime0 != -1) {
        glUniform1fv(uniforms.iChannelTime0, kChannelCount, frameData.channelTimes.data());
    }
    if (uniforms.iSampleRate != -1) {
        glUniform1f(uniforms.iSampleRate, frameData.sampleRate);
    }
}

std::vector<PassChannelBindings> BuildChannelBindings(
    const std::vector<std::array<ChannelInput, kChannelCount>>& channelConfig,
    const std::vector<fs::path>& globalImages) {

    std::vector<PassChannelBindings> bindings(channelConfig.size());
    std::set<std::string> preloadedPaths;

    for (size_t passIndex = 0; passIndex < channelConfig.size(); ++passIndex) {
        for (int channel = 0; channel < kChannelCount; ++channel) {
            const ChannelInput& input = channelConfig[passIndex][channel];
            ChannelBinding binding;
            binding.type = input.type;
            binding.bufferIndex = input.bufferIndex;

            if (input.type == ChannelInput::IMAGE_GLOBAL) {
                if (input.imageIndex >= 0 && input.imageIndex < static_cast<int>(globalImages.size())) {
                    const std::string imagePath = globalImages[input.imageIndex].string();
                    binding.globalTexture = GetTextureForPath(imagePath);
                    preloadedPaths.insert(imagePath);
                }
                else {
                    binding.type = ChannelInput::NONE;
                }
            }

            bindings[passIndex][channel] = binding;
        }
    }

    if (!preloadedPaths.empty()) {
        std::cout << "Preloaded " << preloadedPaths.size() << " configured global image(s).\n";
    }

    return bindings;
}

const Texture* ResolveChannelTexture(const ChannelBinding& binding,
    size_t passIndex,
    int frameIndex,
    const std::vector<Framebuffer>& prevFbos,
    const std::vector<Framebuffer>& currFbos,
    Texture& emptyTex) {

    switch (binding.type) {
    case ChannelInput::NONE:
        return &emptyTex;
    case ChannelInput::IMAGE_GLOBAL:
        return binding.globalTexture ? binding.globalTexture : &emptyTex;
    case ChannelInput::BUFFER:
        if (binding.bufferIndex < 0 || binding.bufferIndex >= static_cast<int>(prevFbos.size())) {
            return &emptyTex;
        }
        if (frameIndex == 0) {
            return &emptyTex;
        }
        if (binding.bufferIndex == static_cast<int>(passIndex)) {
            return &prevFbos[binding.bufferIndex].colorTex;
        }
        if (binding.bufferIndex < static_cast<int>(passIndex)) {
            return &currFbos[binding.bufferIndex].colorTex;
        }
        return &prevFbos[binding.bufferIndex].colorTex;
    }

    return &emptyTex;
}

void UpdateWindowTitle(GLFWwindow* window, double currentTime, double& lastFpsTime, int& frameCount) {
    ++frameCount;
    if (currentTime - lastFpsTime < 1.0) {
        return;
    }

    const std::string title = "Evolve Shader - FPS: " + std::to_string(frameCount);
    glfwSetWindowTitle(window, title.c_str());
    frameCount = 0;
    lastFpsTime = currentTime;
}

} // namespace

int main() {
    fs::path fragDir = "frag";
    fs::path iChannelDir = "iChannel";
    std::string loadedPresetName = "";
    bool vsyncEnabled = false;

    std::vector<std::string> presets = ListPresets();
    if (!presets.empty()) {
        while (true) {
            std::cout << "\nFound existing presets:\n";
            std::cout << "  [0] New Configuration\n";
            for (size_t i = 0; i < presets.size(); ++i) {
                std::cout << "  [" << (i + 1) << "] " << presets[i] << "\n";
            }
            std::cout << "  [S] Settings\n";
            std::cout << "Select a preset, start new, or open settings: ";
            std::string line;
            std::getline(std::cin, line);
            if (line.empty()) {
                break;
            }
            const std::string lower = ToLower(line);
            if (lower == "s" || lower == "settings") {
                vsyncEnabled = ConfigureVsyncSetting(vsyncEnabled);
                continue;
            }
            try {
                int choice = std::stoi(line);
                if (choice == 0) {
                    break;
                }
                if (choice > 0 && choice <= static_cast<int>(presets.size())) {
                    loadedPresetName = presets[choice - 1];
                    fragDir = fs::path("presets") / loadedPresetName / "frag";
                    iChannelDir = fs::path("presets") / loadedPresetName / "iChannel";
                    std::cout << "Loading preset: " << loadedPresetName << "\n";
                    break;
                }
            }
            catch (...) {
            }
            std::cout << " Invalid selection. Try again.\n";
        }
    }
    else {
        std::cout << "\nNo presets found.\n";
        std::cout << "Press Enter to continue or type 's' for settings: ";
        std::string line;
        std::getline(std::cin, line);
        const std::string lower = ToLower(line);
        if (lower == "s" || lower == "settings") {
            vsyncEnabled = ConfigureVsyncSetting(vsyncEnabled);
        }
    }

    const std::vector<fs::path> globalImages = ScanGlobalImages(iChannelDir);
    PrintGlobalImages(globalImages);
    if (!ValidateShaderDirectory(fragDir)) {
        return -1;
    }

    const std::vector<std::string> fragFiles = ScanShaderFiles(fragDir);
    if (fragFiles.empty()) {
        std::cerr << "No .frag files found!\n";
        return -1;
    }
    PrintShaderFiles(fragFiles);

    std::vector<std::array<ChannelInput, 4>> channelConfig;
    if (!loadedPresetName.empty()) {
        channelConfig = LoadPresetConfig(fs::path("presets") / loadedPresetName, globalImages);
        if (channelConfig.empty()) {
            std::cout << "Failed to load config or empty config. Falling back to interactive setup.\n";
            channelConfig = ConfigureChannelsInteractively(fragFiles, globalImages);
        }
    }
    else {
        channelConfig = ConfigureChannelsInteractively(fragFiles, globalImages);

        std::cout << "\nDo you want to save this configuration as a preset? (y/n): ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "y" || line == "Y") {
            std::cout << "Enter preset name: ";
            std::getline(std::cin, line);
            if (!line.empty()) {
                SavePreset(line, fragFiles, globalImages, channelConfig);
            }
        }
    }

    GlfwSession glfwSession;
    if (!glfwSession) {
        return -1;
    }

    AppContext app;
    WindowHandle window = CreateWindow(app, vsyncEnabled);
    if (!window) {
        return -1;
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        return -1;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window.get(), &framebufferWidth, &framebufferHeight);
    if (framebufferWidth > 0 && framebufferHeight > 0) {
        app.winWidth = framebufferWidth;
        app.winHeight = framebufferHeight;
    }
    glViewport(0, 0, app.winWidth, app.winHeight);

    std::cout << "OpenGL: " << glGetString(GL_VERSION) << "\n";

    const bool sRGBCapable = glfwGetWindowAttrib(window.get(), GLFW_SRGB_CAPABLE) == GLFW_TRUE;

    VertexArray vao;
    constexpr std::array<float, 24> quadVertices = {
        -1.f, -1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 0.f,
         1.f,  1.f, 1.f, 1.f,
        -1.f, -1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 1.f
    };
    VertexBuffer vbo(quadVertices.data(), sizeof(quadVertices));
    vao.bind();
    vbo.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    VertexArray::unbind();

    std::vector<ShaderPass> passes = BuildShaderPasses(fragFiles);
    if (passes.empty()) {
        return -1;
    }

    PresentPass presentPass = BuildPresentPass();
    if (!presentPass.program.id) {
        return -1;
    }

    std::vector<Framebuffer> pingFbos(passes.size());
    std::vector<Framebuffer> pongFbos(passes.size());
    app.pingFbos = &pingFbos;
    app.pongFbos = &pongFbos;
    resizeAllRenderTargets(pingFbos, pongFbos, app.winWidth, app.winHeight);

    const std::vector<PassChannelBindings> channelBindings = BuildChannelBindings(channelConfig, globalImages);

    Texture emptyTex;
    emptyTex.createEmpty();

    app.start = std::chrono::steady_clock::now();
    float lastTimeSeconds = 0.0f;
    double lastFpsTime = glfwGetTime();
    int frameCount = 0;
    bool usePingAsPrev = true;

    while (!glfwWindowShouldClose(window.get())) {
        const double currentTime = glfwGetTime();
        UpdateWindowTitle(window.get(), currentTime, lastFpsTime, frameCount);

        const auto now = std::chrono::steady_clock::now();
        const float timeSeconds = std::chrono::duration<float>(now - app.start).count();
        const float deltaTimeSeconds = timeSeconds - lastTimeSeconds;
        lastTimeSeconds = timeSeconds;

        if (app.winWidth <= 0 || app.winHeight <= 0) {
            glfwPollEvents();
            continue;
        }

        const FrameUniformData frameData = BuildFrameUniformData(app, timeSeconds, deltaTimeSeconds);

        auto& prevFbos = usePingAsPrev ? pingFbos : pongFbos;
        auto& currFbos = usePingAsPrev ? pongFbos : pingFbos;

        for (size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
            ShaderPass& pass = passes[passIndex];
            pass.program.use();
            ApplyCommonUniforms(pass.uniforms, frameData);

            for (int channel = 0; channel < kChannelCount; ++channel) {
                if (pass.uniforms.iChannel[channel] == -1) {
                    continue;
                }

                const Texture* boundTexture = ResolveChannelTexture(
                    channelBindings[passIndex][channel],
                    passIndex,
                    frameData.frameIndex,
                    prevFbos,
                    currFbos,
                    emptyTex);

                boundTexture->bind(channel);
                glUniform1i(pass.uniforms.iChannel[channel], channel);

                if (pass.uniforms.iChannelResolution[channel] != -1) {
                    glUniform3f(pass.uniforms.iChannelResolution[channel],
                        static_cast<float>(boundTexture->width),
                        static_cast<float>(boundTexture->height),
                        1.0f);
                }
            }

            currFbos[passIndex].bind();
            glViewport(0, 0, frameData.width, frameData.height);
            if (sRGBCapable) {
                glDisable(GL_FRAMEBUFFER_SRGB);
            }
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, 6);
            VertexArray::unbind();
        }

        Framebuffer::unbind();
        glViewport(0, 0, app.winWidth, app.winHeight);
        if (sRGBCapable) {
            glEnable(GL_FRAMEBUFFER_SRGB);
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        presentPass.program.use();
        currFbos.back().colorTex.bind(0);
        if (presentPass.textureLocation != -1) {
            glUniform1i(presentPass.textureLocation, 0);
        }
        vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        VertexArray::unbind();
        if (sRGBCapable) {
            glDisable(GL_FRAMEBUFFER_SRGB);
        }

        glfwSwapBuffers(window.get());
        glfwPollEvents();
        usePingAsPrev = !usePingAsPrev;
        ++app.frame;
    }

    return 0;
}
