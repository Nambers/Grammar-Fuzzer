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
static std::atomic<bool> shouldExit = false;
static BuiltinContext ctx;
static const fs::path queueDir = "corpus/queue";
static const fs::path doneDir = "corpus/done";

static std::string readFile(const fs::path &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot open file: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

static void handleSigint(int) {
    std::cerr << "\n[cov] Caught SIGINT, exiting cleanly...\n";
    shouldExit = true;
}

static void collect() {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(queueDir)) {
        if (entry.is_regular_file())
            entries.push_back(entry);
    }

    if (entries.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return;
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

int main() {
    initialize(nullptr, nullptr);
    signal(SIGINT, handleSigint);

    if (!fs::exists(doneDir)) {
        fs::create_directories(doneDir);
    }

    loadBuiltinsFuncs(ctx);

    std::cout << "[cov] Starting coverage runner...\n";

    while (!shouldExit) {
        collect();
    }
    collect(); // Final collection to ensure all files are processed

    return 0;
}
