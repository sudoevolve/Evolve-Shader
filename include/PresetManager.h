#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <array>
#include "ChannelConfig.h"

namespace fs = std::filesystem;

inline std::vector<std::string> ListPresets(const fs::path& presetsDir = "presets") {
    std::vector<std::string> presets;
    if (fs::exists(presetsDir) && fs::is_directory(presetsDir)) {
        for (const auto& entry : fs::directory_iterator(presetsDir)) {
            if (entry.is_directory()) {
                presets.push_back(entry.path().filename().string());
            }
        }
    }
    std::sort(presets.begin(), presets.end());
    return presets;
}

inline bool IsSafePresetName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || fs::path(name).is_absolute()) {
        return false;
    }
    return name.find("..") == std::string::npos
        && name.find_first_of("\\\\/:") == std::string::npos;
}

inline std::string JsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\\\\\"; break;
        case '"': escaped += "\\\\\""; break;
        case '\n': escaped += "\\\\n"; break;
        case '\r': escaped += "\\\\r"; break;
        case '\t': escaped += "\\\\t"; break;
        default:
            if (ch >= 0x20) escaped += static_cast<char>(ch);
        }
    }
    return escaped;
}

inline void SavePreset(const std::string& name,
    const std::vector<std::string>& fragFiles,
    const std::vector<fs::path>& globalImages,
    const std::vector<std::array<ChannelInput, 4>>& config,
    const fs::path& sourceImageRoot,
    const fs::path& presetsRoot = "presets") {
    if (!IsSafePresetName(name)) {
        std::cerr << "Invalid preset name. Use a single folder name without path characters.\n";
        return;
    }

    fs::path presetDir = presetsRoot / name;
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
                std::error_code relativeError;
                const fs::path relativePath = fs::relative(src, sourceImageRoot, relativeError);
                if (relativeError || relativePath.empty() || relativePath.is_absolute()
                    || relativePath.string().find("..") != std::string::npos) {
                    std::cerr << "Skipping image outside iChannel root: " << src.string() << "\n";
                    continue;
                }
                fs::path dst = presetDir / "iChannel" / relativePath;
                if (!fs::exists(dst)) {
                    fs::create_directories(dst.parent_path());
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
                std::error_code relativeError;
                const fs::path relativePath = fs::relative(globalImages[input.imageIndex], sourceImageRoot, relativeError);
                if (!relativeError && !relativePath.empty() && !relativePath.is_absolute()
                    && relativePath.string().find("..") == std::string::npos) {
                    out << ", \"imagePath\": \"" << JsonEscape(relativePath.generic_string()) << "\"";
                }
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
    
    // imagePath is relative to this preset's iChannel directory. imageName remains
    // supported so existing presets continue to load.
    std::regex objRegex(R"(\{\s*\"type\"\s*:\s*(\d+)\s*,\s*\"bufferIndex\"\s*:\s*(-?\d+)\s*(,\s*\"(imagePath|imageName)\"\s*:\s*\"([^\"]+)\"\s*)?\})");
    
    auto passes_begin = std::sregex_iterator(content.begin(), content.end(), objRegex);
    auto passes_end = std::sregex_iterator();

    std::array<ChannelInput, 4> currentPass;
    // initialize
    for(int k=0; k<4; ++k) currentPass[k].type = ChannelInput::NONE;

    int channelIndex = 0;

    for (std::sregex_iterator i = passes_begin; i != passes_end; ++i) {
        std::smatch match = *i;
        ChannelInput input;
        const int typeValue = std::stoi(match[1].str());
        if (typeValue < ChannelInput::NONE || typeValue > ChannelInput::BUFFER) {
            input.type = ChannelInput::NONE;
        }
        else {
            input.type = static_cast<ChannelInput::Type>(typeValue);
        }
        input.bufferIndex = std::stoi(match[2].str());
        input.imageIndex = -1;

        if (match[5].matched) {
            const std::string imageKey = match[4].str();
            const std::string imageValue = match[5].str();
            for (size_t k = 0; k < presetImages.size(); ++k) {
                std::error_code relativeError;
                const fs::path relativePath = fs::relative(presetImages[k], presetDir / "iChannel", relativeError);
                const bool matches = imageKey == "imagePath"
                    ? (!relativeError && relativePath.generic_string() == imageValue)
                    : (presetImages[k].filename().string() == imageValue);
                if (matches) {
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
