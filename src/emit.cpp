#include "emit.hpp"
#include "serialization.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>

namespace fs = std::filesystem;
using namespace FuzzingAST;

std::vector<std::string> FuzzingAST::cacheCorpus;

// Generate unique filename using timestamp + counter
std::string FuzzingAST::make_unique_filename(int counter) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return std::to_string(millis) + "_" + std::to_string(counter) + ".json";
}

void FuzzingAST::fuzzerLoadCorpus(const std::string &savedPath,
                                  std::deque<ASTData> &corpus) {
    corpus.clear();
    std::set<std::string> pathes;
    for (const auto &entry : fs::directory_iterator(savedPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            pathes.insert(entry.path().string());
    }
    for (const auto &entry : pathes) {
        std::ifstream in(entry);
        if (in) {
            std::string content((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
            auto jsonData = nlohmann::json::parse(content);
            ASTData astData;
            astData.ast = jsonData.get<AST>();

            corpus.push_back(astData);
        }
    }
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
