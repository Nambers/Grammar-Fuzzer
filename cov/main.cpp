#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>

#include "driver.hpp"
#include "serialization.hpp"

using namespace FuzzingAST;

namespace fs = std::filesystem;
std::atomic<bool> shouldExit = false;

std::string readFile(const fs::path &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot open file: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

void handleSigint(int) {
    std::cerr << "\n[cov] Caught SIGINT, exiting cleanly...\n";
    shouldExit = true;
}

int main() {
    initialize(nullptr, nullptr);
    signal(SIGINT, handleSigint);

    const fs::path queueDir = "corpus/queue";
    const fs::path doneDir = "corpus/done";

    if (!fs::exists(doneDir)) {
        fs::create_directories(doneDir);
    }

    BuiltinContext ctx;
    loadBuiltinsFuncs(ctx);

    std::cout << "[cov] Starting coverage runner...\n";

    while (!shouldExit) {
        std::vector<fs::directory_entry> entries;
        for (const auto &entry : fs::directory_iterator(queueDir)) {
            if (entry.is_regular_file())
                entries.push_back(entry);
        }

        if (entries.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        for (const auto &entry : entries) {
            const fs::path &filePath = entry.path();
            std::string filename = filePath.filename().string();

            try {
                std::string content = readFile(filePath);
                nlohmann::json j = nlohmann::json::parse(content);
                AST ast = j.get<AST>();

                std::cout << "[cov] Running on: " << filename << "\n";
                runAST(ast, ctx);

                // Move to done/
                fs::rename(filePath, doneDir / filename);
            } catch (const std::exception &e) {
                std::cerr << "[cov] Error processing " << filename << ": "
                          << e.what() << "\n";
                // Optionally move to done/ or to a "corpus/bad" folder
            }
        }
    }

    return 0;
}
