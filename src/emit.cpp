#include "emit.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

std::vector<std::string> FuzzingAST::cacheCorpus;

// Generate unique filename using timestamp + counter
static std::string make_unique_filename(int counter) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return std::to_string(millis) + "_" + std::to_string(counter) + ".json";
}

void FuzzingAST::fuzzerEmitCacheCorpus() {
    fs::create_directories("corpus/tmp");
    fs::create_directories("corpus/queue");

    for (size_t i = 0; i < FuzzingAST::cacheCorpus.size(); ++i) {
        std::string filename = make_unique_filename(i);
        fs::path tmpPath = "corpus/tmp/" + filename;
        fs::path queuePath = "corpus/queue/" + filename;

        // 1. Write to temp
        {
            std::ofstream out(tmpPath);
            out << FuzzingAST::cacheCorpus[i];
        }

        // 2. Atomically move
        fs::rename(tmpPath, queuePath);
    }
}
