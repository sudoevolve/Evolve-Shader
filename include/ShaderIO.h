#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>

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

inline std::vector<std::string> ScanShaderFiles(const fs::path& dir = "frag") {
    std::vector<std::pair<int, fs::path>> entries;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return {};
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".frag") {
            std::string stem = entry.path().stem().string();
            size_t numberStart = std::string::npos;
            for (size_t index = 0; index < stem.size(); ++index) {
                if (std::isdigit(static_cast<unsigned char>(stem[index])) != 0) {
                    numberStart = index;
                    break;
                }
            }

            if (numberStart != std::string::npos) {
                size_t numberEnd = numberStart;
                while (numberEnd < stem.size() && std::isdigit(static_cast<unsigned char>(stem[numberEnd])) != 0) {
                    ++numberEnd;
                }

                entries.emplace_back(std::stoi(stem.substr(numberStart, numberEnd - numberStart)), entry.path());
            }
        }
    }
    std::sort(entries.begin(), entries.end());
    std::vector<std::string> result;
    for (auto& [idx, path] : entries) result.push_back(path.string());
    return result;
}
