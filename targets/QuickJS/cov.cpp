#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern "C" {
#include <quickjs.h>
}

using namespace FuzzingAST;
namespace fs = std::filesystem;

static const fs::path queueDir = "corpus/queue";
static const fs::path doneDir = "corpus/done";

static std::string readFile(const fs::path &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot open file: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

static void runJSStr(JSContext *jctx, const std::string &code) {
    JSValue ret =
        JS_Eval(jctx, code.c_str(), code.size(), "<cov>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(jctx);
        JS_FreeValue(jctx, exc);
    }
    JS_FreeValue(jctx, ret);
}

static long long msFromFilename(const std::string &name) {
    auto pos = name.find('_');
    if (pos == std::string::npos)
        return 0;
    try {
        return std::stoll(name.substr(0, pos));
    } catch (...) {
        return 0;
    }
}

static void dumpProfile(const std::string &profrawName) {
    pid_t child = fork();
    if (child == 0) {
        exit(0); // triggers the binary's profile atexit → writes default_<pid>.profraw
    }
    if (child < 0) {
        std::cerr << "[cov] fork failed: " << strerror(errno) << "\n";
        return;
    }
    waitpid(child, nullptr, 0);

    std::string src = "default_" + std::to_string(getpid()) + ".profraw";
    std::error_code ec;
    fs::rename(src, profrawName, ec);
    if (ec)
        std::cerr << "[cov] rename " << src << " -> " << profrawName
                  << ": " << ec.message() << "\n";
}

static void collect() {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(queueDir)) {
        if (entry.is_regular_file())
            entries.push_back(entry);
    }

    JSRuntime *rt = JS_NewRuntime();
    JSContext *jctx = JS_NewContext(rt);

    for (const auto &entry : entries) {
        const fs::path &filePath = entry.path();
        std::string filename = filePath.filename().string();
        try {
            std::string content = readFile(filePath);
            nlohmann::json jsonData = nlohmann::json::parse(content);
            AST ast = jsonData.get<AST>();

            std::ostringstream script;
            scopeToJS(script, 0, ast, 0);

            runJSStr(jctx, script.str());
            fs::rename(filePath, doneDir / filename);
        } catch (const std::exception &e) {
            std::cerr << "[cov] Error processing " << filename << ": "
                      << e.what() << "\n";
        }
    }

    JS_FreeContext(jctx);
    JS_FreeRuntime(rt);
}

// Sorts queue entries by ms prefix ascending, then processes them in groups
// of equal ms.  After each group, dumps one cumulative profraw snapshot named
// time_<ms>.profraw.  Counters are never reset, so each snapshot represents
// coverage accumulated from all entries up to and including that timestamp.
static void collectSorted() {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(queueDir)) {
        if (entry.is_regular_file())
            entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return msFromFilename(a.path().filename().string()) <
               msFromFilename(b.path().filename().string());
    });

    JSRuntime *rt = JS_NewRuntime();
    JSContext *jctx = JS_NewContext(rt);

    size_t i = 0;
    while (i < entries.size()) {
        long long ms = msFromFilename(entries[i].path().filename().string());

        size_t j = i;
        while (j < entries.size() &&
               msFromFilename(entries[j].path().filename().string()) == ms) {
            const fs::path &filePath = entries[j].path();
            std::string filename = filePath.filename().string();
            try {
                std::string content = readFile(filePath);
                nlohmann::json jsonData = nlohmann::json::parse(content);
                AST ast = jsonData.get<AST>();

                std::ostringstream script;
                scopeToJS(script, 0, ast, 0);

                runJSStr(jctx, script.str());
                fs::rename(filePath, doneDir / filename);
            } catch (const std::exception &e) {
                std::cerr << "[cov] Error processing " << filename << ": "
                          << e.what() << "\n";
            }
            ++j;
        }

        const std::string profrawName = "time_" + std::to_string(ms) + ".profraw";
        dumpProfile(profrawName);
        i = j;
    }

    JS_FreeContext(jctx);
    JS_FreeRuntime(rt);
}

int main(int argc, char *argv[]) {
    if (!fs::exists(doneDir))
        fs::create_directories(doneDir);

    std::cout << "[cov] Starting QuickJS coverage runner...\n";
    if (argc > 1 && std::string_view(argv[1]) == "--time-collect")
        collectSorted();
    else
        collect();
    return 0;
}
