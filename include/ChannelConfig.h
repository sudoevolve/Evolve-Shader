#pragma once
#include <array>
#include <vector>
#include <string>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <map>

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
    std::cout << " Press Enter for auto-chain (buffer1 -> buffer2 -> ...),\n";
    std::cout << " or type a buffer index (1 to " << N << ") to configure manually: ";

    std::string line;
    std::getline(std::cin, line);

    if (line.empty()) {
        std::cout << " Auto-chain: ";
        for (int i = 1; i < N; ++i) {
            configs[i][0].type = ChannelInput::BUFFER;
            configs[i][0].bufferIndex = i - 1;
            std::cout << "buffer" << i << " -> ";
        }
        if (N > 0) std::cout << "buffer" << N << "\n\n";
        return configs;
    }

    while (true) {
        try {
            int idx_one_based = std::stoi(line);
            if (idx_one_based < 1 || idx_one_based > N) {
                std::cout << " Invalid index. Choose 1-" << N << ": ";
                std::getline(std::cin, line);
                continue;
            }
            int idx = idx_one_based - 1; // Convert to 0-based index

            std::string fname = fs::path(files[idx]).filename().string();
            std::cout << "\n Configuring buffer" << idx_one_based << " (" << fname << "):\n";

            std::cout << "Enter up to 4 choices for iChannel0-3, separated by spaces.\n";
            std::cout << "  1: none\n";
            std::cout << "  2: self (feedback)\n";

            std::map<int, int> choiceToBuffer;
            int currentChoice = 3;
            for (int b = 0; b < N; ++b) {
                if (b == idx) continue;
                choiceToBuffer[currentChoice] = b;
                std::cout << "  " << currentChoice << ": buffer" << (b + 1) << "\n";
                currentChoice++;
            }

            int imageBaseChoice = currentChoice;
            if (!globalImages.empty()) {
                for (size_t i = 0; i < globalImages.size(); ++i) {
                    std::cout << "  " << imageBaseChoice + i << ": " << globalImages[i].filename().string() << "\n";
                }
            }
            std::cout << "> ";
            std::getline(std::cin, line);
            std::stringstream ss(line);
            int choice;
            int c = 0;
            while (ss >> choice && c < 4) {
                ChannelInput input;
                if (choice == 1) {
                    input.type = ChannelInput::NONE;
                }
                else if (choice == 2) {
                    input.type = ChannelInput::BUFFER;
                    input.bufferIndex = idx;
                }
                else if (choiceToBuffer.count(choice)) {
                    input.type = ChannelInput::BUFFER;
                    input.bufferIndex = choiceToBuffer[choice];
                }
                else if (!globalImages.empty() && choice >= imageBaseChoice) {
                    int imgChoice = choice - imageBaseChoice;
                    if (imgChoice >= 0 && imgChoice < (int)globalImages.size()) {
                        input.type = ChannelInput::IMAGE_GLOBAL;
                        input.imageIndex = imgChoice;
                    }
                    else {
                        std::cout << " Invalid image index: " << imgChoice + 1 << ". Skipping.\n";
                        c++;
                        continue;
                    }
                }
                else {
                    std::cout << " Invalid choice: " << choice << ". Skipping.\n";
                    c++;
                    continue;
                }
                configs[idx][c] = input;
                std::cout << " Set iChannel" << c << " = ";
                if (input.type == ChannelInput::NONE) std::cout << "none\n";
                else if (input.type == ChannelInput::BUFFER && input.bufferIndex == idx) std::cout << "self\n";
                else if (input.type == ChannelInput::BUFFER) std::cout << "buffer" << (input.bufferIndex + 1) << "\n";
                else if (input.type == ChannelInput::IMAGE_GLOBAL) std::cout << "image: " << globalImages[input.imageIndex].filename().string() << "\n";
                c++;
            }

            std::cout << "\nEnter another index (1 to " << N << "), or press Enter to finish: ";
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
            std::cout << "[AUTO] Auto-connected buffer" << (i + 1) << " <- buffer" << i << "\n";
        }
    }

    return configs;
}
