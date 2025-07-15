#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <Python.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>

using namespace FuzzingAST;

namespace fs = std::filesystem;
static std::atomic<bool> shouldExit = false;
static BuiltinContext ctx;
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
    Py_DECREF(result);
    Py_DECREF(code);
    Py_DECREF(dict);
    Py_DECREF(name);
    // don't care about error
    PyErr_Clear();
}

static void collect() {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(queueDir)) {
        if (entry.is_regular_file())
            entries.push_back(entry);
    }

    for (const auto &entry : entries) {
        const fs::path &filePath = entry.path();
        std::string filename = filePath.filename().string();

        try {
            std::string content = readFile(filePath);
            nlohmann::json jsonData = nlohmann::json::parse(content);
            AST ast = jsonData.get<AST>();
            std::ostringstream astStream;
            scopeToPython(astStream, 0, ast, ctx, 0);
            // std::cout << "[cov] Running on: " << filename << "\n";

            runASTStr(astStream.str());

            // Move to done/
            fs::rename(filePath, doneDir / filename);
        } catch (const std::exception &e) {
            std::cerr << "[cov] Error processing " << filename << ": "
                      << e.what() << "\n";
        }
    }
}

int main() {
    Py_Initialize();

    if (!fs::exists(doneDir)) {
        fs::create_directories(doneDir);
    }

    loadBuiltinsFuncs(ctx);
    initPrimitiveTypes(ctx);

    std::cout << "[cov] Starting coverage runner...\n";
    collect();
    Py_Finalize();
    return 0;
}
