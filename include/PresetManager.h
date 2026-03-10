#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <array>
#include "ChannelConfig.h"

namespace fs = std::filesystem;

inline std::vector<std::string> ListPresets() {
    std::vector<std::string> presets;
    if (fs::exists("presets") && fs::is_directory("presets")) {
        for (const auto& entry : fs::directory_iterator("presets")) {
            if (entry.is_directory()) {
                presets.push_back(entry.path().filename().string());
            }
        }
    }
    return presets;
}

inline void SavePreset(const std::string& name,
    const std::vector<std::string>& fragFiles,
    const std::vector<fs::path>& globalImages,
    const std::vector<std::array<ChannelInput, 4>>& config) {
    
    fs::path presetDir = fs::path("presets") / name;
    if (fs::exists(presetDir)) {
        std::cout << "Overwrite existing preset '" << name << "'? (y/n): ";
        char c; 
        std::cin >> c;
        // consume newline
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        
        if (c != 'y' && c != 'Y') {
            std::cout << "Save cancelled.\n";
            return;
        }
        fs::remove_all(presetDir);
    }

    fs::create_directories(presetDir);
    fs::create_directories(presetDir / "frag");
    fs::create_directories(presetDir / "iChannel");

    // Copy shaders
    for (const auto& file : fragFiles) {
        fs::path src(file);
        fs::path dst = presetDir / "frag" / src.filename();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    }

    // Copy used images
    for (const auto& pass : config) {
        for (const auto& ch : pass) {
            if (ch.type == ChannelInput::IMAGE_GLOBAL && ch.imageIndex >= 0 && ch.imageIndex < static_cast<int>(globalImages.size())) {
                fs::path src = globalImages[ch.imageIndex];
                fs::path dst = presetDir / "iChannel" / src.filename();
                if (!fs::exists(dst)) {
                    fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
                }
            }
        }
    }

    // Write JSON config
    std::ofstream out(presetDir / "config.json");
    out << "{\n  \"passes\": [\n";
    for (size_t i = 0; i < config.size(); ++i) {
        out << "    [\n";
        for (int c = 0; c < 4; ++c) {
            const auto& input = config[i][c];
            out << "      { \"type\": " << input.type 
                << ", \"bufferIndex\": " << input.bufferIndex;
            if (input.type == ChannelInput::IMAGE_GLOBAL && input.imageIndex >= 0 && input.imageIndex < static_cast<int>(globalImages.size())) {
                out << ", \"imageName\": \"" << globalImages[input.imageIndex].filename().string() << "\"";
            }
            out << " }";
            if (c < 3) out << ",";
            out << "\n";
        }
        out << "    ]";
        if (i < config.size() - 1) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    std::cout << "Preset saved to " << presetDir.string() << "\n";
}

inline std::vector<std::array<ChannelInput, 4>> LoadPresetConfig(
    const fs::path& presetDir,
    const std::vector<fs::path>& presetImages) {
    
    std::vector<std::array<ChannelInput, 4>> config;
    fs::path jsonPath = presetDir / "config.json";
    std::ifstream in(jsonPath);
    if (!in.is_open()) return config;

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    
    // Regex to match: { "type": <d>, "bufferIndex": <d> [, "imageName": "<str>"] }
    std::regex objRegex(R"(\{\s*\"type\"\s*:\s*(\d+)\s*,\s*\"bufferIndex\"\s*:\s*(-?\d+)\s*(,\s*\"imageName\"\s*:\s*\"([^\"]+)\"\s*)?\})");
    
    auto passes_begin = std::sregex_iterator(content.begin(), content.end(), objRegex);
    auto passes_end = std::sregex_iterator();

    std::array<ChannelInput, 4> currentPass;
    // initialize
    for(int k=0; k<4; ++k) currentPass[k].type = ChannelInput::NONE;

    int channelIndex = 0;

    for (std::sregex_iterator i = passes_begin; i != passes_end; ++i) {
        std::smatch match = *i;
        ChannelInput input;
        input.type = static_cast<ChannelInput::Type>(std::stoi(match[1].str()));
        input.bufferIndex = std::stoi(match[2].str());
        input.imageIndex = -1;

        if (match[4].matched) {
            std::string imgName = match[4].str();
            for (size_t k = 0; k < presetImages.size(); ++k) {
                if (presetImages[k].filename().string() == imgName) {
                    input.imageIndex = static_cast<int>(k);
                    break;
                }
            }
        }

        currentPass[channelIndex++] = input;
        if (channelIndex == 4) {
            config.push_back(currentPass);
            channelIndex = 0;
            // Reset for next pass just in case
            for(int k=0; k<4; ++k) currentPass[k].type = ChannelInput::NONE;
        }
    }

    return config;
}
