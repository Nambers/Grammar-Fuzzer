#include "ast.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <Python.h>
#include <fstream>
#include <iostream>
#include <sys/time.h>

using json = nlohmann::json;
using namespace FuzzingAST;

inline constexpr const char *RED = "\033[0;31m";
inline constexpr const char *RESET = "\033[0m";

template <typename... Args>
[[noreturn]] void __attribute__((noreturn))
PANIC(std::format_string<Args...> fmt, Args &&...args) {
    std::cerr << RED << std::format(fmt, std::forward<Args>(args)...) << RESET
              << std::endl;
    abort();
}

static void runASTStr(const std::string &re) {
    PyObject *code = Py_CompileString(re.c_str(), "<ast>", Py_file_input);
    if (PyErr_Occurred()) {
        PyErr_Print();
        PyErr_Clear();
        PANIC("Failed to compile AST code");
    }
    PyObject *dict = PyDict_New();
    PyObject *name = PyUnicode_FromString("__main__");
    PyDict_SetItemString(dict, "__name__", name);
    PyDict_SetItemString(dict, "__builtins__", PyEval_GetBuiltins());
    struct timeval start{};
    gettimeofday(&start, nullptr);
    PyObject *result = PyEval_EvalCode(code, dict, dict);
    struct timeval now{};
    gettimeofday(&now, nullptr);

    int elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                     (now.tv_usec - start.tv_usec) / 1000;
    std::cout << "Execution time: " << elapsed_ms << " ms\n";
    if (!result) {
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();
            PANIC("Failed to run AST code");
        }
    }
    Py_DECREF(result);
    Py_DECREF(code);
    Py_DECREF(dict);
    Py_DECREF(name);
}

void loadBuiltinsFuncs(BuiltinContext &ctx) {
    auto &funcSignatures = ctx.builtinsFuncs;
    auto &types = ctx.types;
    FILE *file = fopen("./targets/CPython/builtins.json", "r");
    if (!file) {
        PANIC("Failed to open builtins.json, run build.sh to generate it.");
    }
    nlohmann::json j = nlohmann::json::parse(file);
    auto tmp =
        j["funcs"].get<std::unordered_map<std::string, FunctionSignature>>();
    funcSignatures.swap(tmp);
    auto tmp2 = j["types"].get<std::vector<std::string>>();
    types.swap(tmp2);
}

int main(int argc, char *argv[]) {
    Py_Initialize();
    std::ifstream in("ast_test.json");
    if (!in) {
        std::cerr << "Failed to open input\n";
        return 1;
    }
    json j;
    in >> j;

    AST ast = j.get<AST>();
    BuiltinContext ctx;
    loadBuiltinsFuncs(ctx);
    initPrimitiveTypes(ctx);
    std::string result;
    if (argc == 2 && std::string(argv[1]) == "-d") {
        // only run declarations
        std::cout << "Generated Python declaration:\n";
        for (const auto &decl : ast.scopes[0].declarations) {
            const auto &node = ast.declarations[decl];
            if (node.kind != ASTNodeKind::Function) {
                std::ostringstream script;
                nodeToPython(script, node, ast, ctx, 0);
                std::cout << script.str();
                result += script.str();
            }
        }
    } else {
        std::ostringstream script;
        scopeToPython(script, 0, ast, ctx, 0);
        std::cout << "Generated Python script:\n" << script.str() << "\n";
        result = script.str();
    }
    runASTStr(result);
    if (PyErr_Occurred()) {
        PyErr_Print();
    }
    Py_Finalize();
    return 0;
}
