#pragma once
#include <array>
#include <vector>
#include <string>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

struct ChannelInput {
    enum Type { NONE, IMAGE_GLOBAL, BUFFER } type = NONE;
    int bufferIndex = -1;
    int imageIndex = -1;
};

inline std::vector<std::array<ChannelInput, 4>> ConfigureChannelsInteractively(
    const std::vector<std::string>& files,
    const std::vector<fs::path>& globalImages) {

    int N = static_cast<int>(files.size());
    std::vector<std::array<ChannelInput, 4>> configs(N);
    for (auto& chs : configs) for (int i = 0; i < 4; ++i) chs[i].type = ChannelInput::NONE;

    std::cout << "\n=== Interactive iChannel Setup ===\n";
    std::cout << " Press Enter for auto-chain (buffer0 -> buffer1 -> ...),\n";
    std::cout << " or type a buffer index to configure manually: ";

    std::string line;
    std::getline(std::cin, line);

    if (line.empty()) {
        std::cout << " Auto-chain: ";
        for (int i = 1; i < N; ++i) {
            configs[i][0].type = ChannelInput::BUFFER;
            configs[i][0].bufferIndex = i - 1;
            std::cout << "buffer" << i - 1 << " -> ";
        }
        if (N > 0) std::cout << "buffer" << N - 1 << "\n\n";
        return configs;
    }

    while (true) {
        try {
            int idx = std::stoi(line);
            if (idx < 0 || idx >= N) {
                std::cout << " Invalid index. Choose 0-" << N - 1 << ": ";
                std::getline(std::cin, line);
                continue;
            }
            std::string fname = fs::path(files[idx]).filename().string();
            std::cout << "\n Configuring buffer" << idx << " (" << fname << "):\n";
            for (int c = 0; c < 4; ++c) {
                std::cout << "\niChannel" << c << " source:\n";
                std::cout << "  1: none\n";
                std::cout << "  2: self (feedback)\n";
                for (int b = 0; b < N; ++b) {
                    if (b == idx) continue;
                    std::cout << "  " << (b + 3) << ": buffer" << b << "\n";
                }
                int base = N + 3;
                if (!globalImages.empty()) {
                    std::cout << "  " << base << "+: image (see list below)\n";
                }
                std::cout << "> ";
                std::getline(std::cin, line);
                int choice;
                try { choice = std::stoi(line); }
                catch (...) { choice = -1; }
                ChannelInput input;
                if (choice == 1) {
                    input.type = ChannelInput::NONE;
                }
                else if (choice == 2) {
                    input.type = ChannelInput::BUFFER;
                    input.bufferIndex = idx;
                }
                else if (choice >= 3 && choice < base) {
                    int src = choice - 3;
                    if (src >= 0 && src < N && src != idx) {
                        input.type = ChannelInput::BUFFER;
                        input.bufferIndex = src;
                    }
                    else {
                        std::cout << " Invalid buffer index. Skipping.\n"; continue;
                    }
                }
                else if (!globalImages.empty() && choice >= base) {
                    int imgChoice = choice - base;
                    if (imgChoice >= 0 && imgChoice < (int)globalImages.size()) {
                        input.type = ChannelInput::IMAGE_GLOBAL;
                        input.imageIndex = imgChoice;
                    }
                    else {
                        std::cout << " Invalid image index. Skipping.\n"; continue;
                    }
                }
                else {
                    std::cout << " Invalid choice. Skipping.\n"; continue;
                }
                configs[idx][c] = input;
                std::cout << " Set iChannel" << c << " = ";
                if (input.type == ChannelInput::NONE) std::cout << "none\n";
                else if (input.type == ChannelInput::BUFFER && input.bufferIndex == idx) std::cout << "self\n";
                else if (input.type == ChannelInput::BUFFER) std::cout << "buffer" << input.bufferIndex << "\n";
                else if (input.type == ChannelInput::IMAGE_GLOBAL) std::cout << "image: " << globalImages[input.imageIndex].filename().string() << "\n";
                if (c < 3) {
                    std::cout << "1. Continue\n2. Skip\n> ";
                    std::getline(std::cin, line);
                    if (line != "1") break;
                }
            }
            std::cout << "\nEnter another index, or press Enter to finish: ";
        }
        catch (...) { break; }
        std::getline(std::cin, line);
        if (line.empty()) break;
    }

    for (int i = 1; i < N; ++i) {
        bool empty = true;
        for (int c = 0; c < 4; ++c) {
            if (configs[i][c].type != ChannelInput::NONE) {
                empty = false;
                break;
            }
        }
        if (empty && i > 0) {
            configs[i][0].type = ChannelInput::BUFFER;
            configs[i][0].bufferIndex = i - 1;
            std::cout << "[AUTO] Auto-connected buffer" << i << " <- buffer" << i - 1 << "\n";
        }
    }

    return configs;
}