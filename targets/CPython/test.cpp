#include "ast.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <Python.h>
#include <fstream>
#include <iostream>
#include "log.hpp"

using json = nlohmann::json;
using namespace FuzzingAST;

static void runASTStr(const std::string &re) {
    PyObject *code = Py_CompileString(re.c_str(), "<ast>", Py_file_input);
    PyObject *dict = PyDict_New();
    PyObject *name = PyUnicode_FromString("__main__");
    PyDict_SetItemString(dict, "__name__", name);
    PyDict_SetItemString(dict, "__builtins__", PyEval_GetBuiltins());
    PyObject *result = PyEval_EvalCode(code, dict, dict);
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

int main() {
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
    std::ostringstream script;
    scopeToPython(script, 0, ast, ctx, 0);
    std::cout << "Generated Python script:\n" << script.str() << "\n";
    runASTStr(script.str());
    if (PyErr_Occurred()) {
        PyErr_Print();
    }
    Py_Finalize();
    return 0;
}
