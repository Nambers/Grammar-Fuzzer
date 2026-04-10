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

std::vector<std::tuple<int64_t, std::string>> FuzzingAST::cacheCorpus;

// Generate unique filename using timestamp + counter
static std::string make_unique_filename(int64_t elapsed_seconds,
                                        bool lastEqual = false) {
    static unsigned int counter = 0;
    if (lastEqual == false) {
        counter = 0;
    }
    return std::to_string(elapsed_seconds) + "_" + std::to_string(counter++) +
           ".json";
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
        if (in && in.peek() != std::ifstream::traits_type::eof()) {
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
    int64_t lastSeconds = -1;

    for (size_t i = 0; i < FuzzingAST::cacheCorpus.size(); ++i) {
        int64_t elapsed_seconds = std::get<0>(FuzzingAST::cacheCorpus[i]);
        std::string filename = make_unique_filename(
            elapsed_seconds, lastSeconds == elapsed_seconds);
        lastSeconds = elapsed_seconds;
        fs::path tmpPath = "corpus/tmp/" + filename;
        fs::path queuePath = "corpus/queue/" + filename;

        // 1. Write to temp
        {
            std::ofstream out(tmpPath);
            out << std::get<1>(FuzzingAST::cacheCorpus[i]);
        }

        // 2. Atomically move
        fs::rename(tmpPath, queuePath);
    }
}
