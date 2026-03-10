#pragma once
#include <map>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include "GLTypes.h"

namespace fs = std::filesystem;

inline std::map<std::string, Texture> g_globalTextureCache;

inline Texture* GetTextureForPath(const std::string& path) {
    auto it = g_globalTextureCache.find(path);
    if (it != g_globalTextureCache.end()) return &it->second;
    Texture tex;
    if (tex.loadFromFile(path)) {
        std::cout << "Loaded texture: " << path << " (" << tex.width << "x" << tex.height << ")\n";
        auto result = g_globalTextureCache.emplace(path, std::move(tex));
        return &result.first->second;
    }
    else {
        static Texture empty;
        static bool init = false;
        if (!init) { empty.createEmpty(); init = true; }
        return &empty;
    }
}

inline std::vector<fs::path> ScanGlobalImages(const fs::path& dir = "iChannel") {
    std::vector<fs::path> images;
    std::vector<std::string> extensions = { ".png", ".jpg", ".jpeg" };
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        // std::cerr << "Warning: '" << dir.string() << "' folder not found.\n";
        return images;
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
            images.push_back(entry.path());
        }
    }
    std::sort(images.begin(), images.end());
    return images;
}
