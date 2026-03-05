#pragma once
#include <string>
#include <vector>
#include <regex>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

inline std::string WrapShadertoyShader(const std::string& code) {
    std::string prelude = R"GLSL(
#version 330 core
out vec4 fragColor;
in vec2 vTex;
uniform vec3 iResolution;
uniform float iTime;
uniform float iTimeDelta;
uniform int iFrame;
uniform float iFrameRate;
uniform vec4 iDate;
uniform vec4 iMouse;
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;
uniform float iChannelTime[4];
uniform vec3 iChannelResolution[4];
uniform float iSampleRate;
)GLSL";
    std::string postlude = R"GLSL(
void main() {
    vec2 fragCoord = vTex * iResolution.xy;
    mainImage(fragColor, fragCoord);
}
)GLSL";
    return prelude + "\n#line 1\n" + code + postlude;
}

inline std::string LoadShaderFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

inline std::vector<std::string> ScanShaderFiles() {
    std::vector<std::pair<int, fs::path>> entries;
    for (const auto& entry : fs::directory_iterator("frag")) {
        if (entry.is_regular_file() && entry.path().extension() == ".frag") {
            std::string stem = entry.path().stem().string();
            std::regex numPattern(R"((\d+))");
            std::smatch match;
            if (std::regex_search(stem, match, numPattern)) {
                entries.emplace_back(std::stoi(match[1]), entry.path());
            }
        }
    }
    std::sort(entries.begin(), entries.end());
    std::vector<std::string> result;
    for (auto& [idx, path] : entries) result.push_back(path.string());
    return result;
}
