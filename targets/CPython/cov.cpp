#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <Python.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace FuzzingAST;

namespace fs = std::filesystem;
static std::atomic<bool> shouldExit = false;
static const fs::path queueDir = "corpus/queue";
static const fs::path doneDir = "corpus/done";

inline constexpr const char *RED = "\033[0;31m";
inline constexpr const char *RESET = "\033[0m";

template <typename... Args>
[[noreturn]] void __attribute__((noreturn))
PANIC(std::format_string<Args...> fmt, Args &&...args) {
    std::cerr << RED << std::format(fmt, std::forward<Args>(args)...) << RESET
              << std::endl;
    abort();
}

static std::string readFile(const fs::path &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot open file: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

static void runASTStr(const std::string &re) {
    PyObject *code = Py_CompileString(re.c_str(), "<ast>", Py_file_input);
    PyObject *dict = PyDict_New();
    PyObject *name = PyUnicode_FromString("__main__");
    PyDict_SetItemString(dict, "__name__", name);
    PyDict_SetItemString(dict, "__builtins__", PyEval_GetBuiltins());
    PyObject *result = PyEval_EvalCode(code, dict, dict);
    Py_XDECREF(result);
    Py_XDECREF(code);
    Py_XDECREF(dict);
    Py_XDECREF(name);

    // don't care about error
    PyErr_Clear();
}

static void runFile(const fs::path &filePath) {
    std::string filename = filePath.filename().string();

    try {
        std::string content = readFile(filePath);
        if (content.empty()) {
            std::cerr << "[cov] file " << filePath << " is empty, skipped."
                      << std::endl;
            return;
        }
        nlohmann::json jsonData = nlohmann::json::parse(content);
        AST ast = jsonData.get<AST>();
        std::ostringstream astStream;
        scopeToPython(astStream, 0, ast, 0);
        // std::cout << "[cov] Running on: " << filename << "\n";

        runASTStr(astStream.str());

        // Move to done/
        fs::rename(filePath, doneDir / filename);
    } catch (const std::exception &e) {
        std::cerr << "[cov] Error processing " << filename << ": " << e.what()
                  << "\n";
    }
}

// libpython's LLVM profile runtime is statically linked with hidden visibility,
// so __llvm_profile_write_file cannot be called from outside the library.
// The atexit handler registered inside libpython CAN write the profraw though.
//
// Trick: fork() a child that inherits the parent's in-memory profile counters,
// then exits cleanly.  The child's exit triggers libpython's atexit handler,
// which writes default_<child_pid>.profraw.  The parent renames that file to
// the desired name and continues accumulating counters for the next snapshot.
//
// LLVM_PROFILE_FILE must contain %p (e.g. "default_%p.profraw") so each child
// writes to a uniquely-named file rather than overwriting a shared one.
static void dumpProfile(const std::string &profrawName) {
    pid_t child = fork();
    if (child == 0) {
        exit(0); // triggers libpython's profile atexit → writes default_<pid>.profraw
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

static void collect() {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(queueDir)) {
        if (entry.is_regular_file())
            entries.push_back(entry);
    }

    for (const auto &entry : entries) {
        const fs::path &filePath = entry.path();
        runFile(filePath);
    }
}

// Sorts queue entries by ms prefix ascending, then processes them in groups
// of equal ms.  After each group, dumps one cumulative profraw snapshot named
// <ms>.profraw.  Counters are never reset, so each snapshot represents
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

    size_t i = 0;
    while (i < entries.size()) {
        long long ms = msFromFilename(entries[i].path().filename().string());

        // Run all entries sharing this ms before snapshotting
        size_t j = i;
        while (j < entries.size() &&
               msFromFilename(entries[j].path().filename().string()) == ms) {
            runFile(entries[j].path());
            ++j;
        }

        // One snapshot per ms group — cumulative counters, no reset
        const std::string profrawName = "time_" + std::to_string(ms) + ".profraw";
        dumpProfile(profrawName.c_str());

        i = j;
    }
}

int main(int argc, char *argv[]) {
    Py_Initialize();

    if (!fs::exists(doneDir)) {
        fs::create_directories(doneDir);
    }

    std::cout << "[cov] Starting coverage runner...\n";
    if (argc > 1 && std::string_view(argv[1]) == "--time-collect")
        collectSorted();
    else
        collect();
    Py_Finalize();
    return 0;
}
