#include "driver.hpp"
#include "Python.h"
#include "ast.hpp"
#include "log.hpp"
#include "pythonrun.h"
#include <fstream>
#include <iostream>
#include <serialization.hpp>
#include <sstream>

using namespace FuzzingAST;

void valueToPython(std::ostringstream &out, const ASTNodeValue &val,
                   const AST &ast, int indentLevel);

void nodeToPython(std::ostringstream &out, const ASTNode &node, const AST &ast,
                  int indentLevel);

void scopeToPython(std::ostringstream &out, ScopeID sid, const AST &ast,
                   int indentLevel);

void valueToPython(std::ostringstream &out, const ASTNodeValue &val,
                   const AST &ast, int indentLevel) {
    if (std::holds_alternative<std::string>(val.val)) {
        const std::string &s = std::get<std::string>(val.val);
        out << s;
    } else if (std::holds_alternative<int64_t>(val.val)) {
        out << std::get<int64_t>(val.val);
    } else if (std::holds_alternative<bool>(val.val)) {
        out << (std::get<bool>(val.val) ? "True" : "False");
    } else if (std::holds_alternative<double>(val.val)) {
        out << std::get<double>(val.val);
    } else if (std::holds_alternative<NodeID>(val.val)) {
        const ASTNode &child = ast.expressions.at(std::get<NodeID>(val.val));
        nodeToPython(out, child, ast, indentLevel);
    } else {
        out << "None";
    }
}

void nodeToPython(std::ostringstream &out, const ASTNode &node, const AST &ast,
                  int indentLevel) {
    std::string ind(indentLevel * 4, ' ');
    out << ind;

    switch (node.kind) {
    case ASTNodeKind::DeclareVar:
    case ASTNodeKind::Assign:
        valueToPython(out, node.fields[0], ast, indentLevel);
        out << " = ";
        valueToPython(out, node.fields[1], ast, indentLevel);
        break;

    case ASTNodeKind::Return:
        out << "return ";
        valueToPython(out, node.fields[0], ast, indentLevel);
        break;

    case ASTNodeKind::Call:
        valueToPython(out, node.fields[0], ast, indentLevel);
        out << "(";
        for (size_t i = 1; i < node.fields.size(); ++i) {
            if (i > 1)
                out << ", ";
            valueToPython(out, node.fields[i], ast, indentLevel);
        }
        out << ")";
        break;

    case ASTNodeKind::BinaryOp:
        valueToPython(out, node.fields[0], ast, indentLevel);
        out << " = ";
        valueToPython(out, node.fields[1], ast, indentLevel);
        out << " " << std::get<std::string>(node.fields[2].val) << " ";
        valueToPython(out, node.fields[3], ast, indentLevel);
        break;

    case ASTNodeKind::UnaryOp:
        valueToPython(out, node.fields[0], ast, indentLevel);
        out << " = " << std::get<std::string>(node.fields[1].val) << " ";
        valueToPython(out, node.fields[2], ast, indentLevel);
        break;

    default:
        out << "# unsupported kind " << static_cast<int>(node.kind);
        break;
    }
    out << "\n";
}

void scopeToPython(std::ostringstream &out, ScopeID sid, const AST &ast,
                   int indentLevel) {
    if (sid == -1)
        return;

    const ASTScope &scope = ast.scopes[sid];
    bool empty = true;

    for (NodeID id : scope.declarations) {
        nodeToPython(out, ast.declarations.at(id), ast, indentLevel);
        empty = false;
    }
    for (NodeID id : scope.expressions) {
        nodeToPython(out, ast.expressions.at(id), ast, indentLevel);
        empty = false;
    }

    if (empty) {
        out << std::string(indentLevel * 4, ' ') << "pass\n";
    }
}

int FuzzingAST::runAST(const AST &ast, bool echo) {
    std::ostringstream script;
    scopeToPython(script, 0, ast, 0);
    std::string re = script.str();

    if (echo) {
        std::cout << "[Generated Python]:\n" << re << "\n";
    }

    PyObject *code = Py_CompileString(re.c_str(), "<ast>", Py_file_input);
    if (!code || PyErr_Occurred()) {
#ifndef QUIET
        PyErr_Print();
#endif
        return -1;
    }

    PyObject *dict = PyDict_New();
    PyObject *name = PyUnicode_FromString("__main__");
    PyDict_SetItemString(dict, "__name__", name);
    PyDict_SetItemString(dict, "__builtins__", PyEval_GetBuiltins());
    PyEval_EvalCode(code, dict, dict);
    Py_DECREF(dict);
    Py_DECREF(name);
    if (PyErr_Occurred()) {
#ifndef QUIET
        PyErr_Print();
#endif
        return -1;
    }
    return 0;
}

static std::string driverPyConent;

int FuzzingAST::initialize(int *argc, char ***argv) {
    std::ifstream driverPyStream("./src/driver/python/driver.py");
    if (!driverPyStream.is_open()) {
        PANIC("Failed to open driver.py, please run build.sh to generate it.");
    }
    std::ostringstream sstr;
    sstr << driverPyStream.rdbuf();
    driverPyConent = sstr.str();
    driverPyStream.close();

    Py_Initialize();
    return 0;
}

int FuzzingAST::finalize() {
    Py_Finalize();
    return 0;
}

void FuzzingAST::loadBuiltinsFuncs(
    std::unordered_map<std::string, FunctionSignature> &funcSignatures,
    std::vector<std::string> &types) {
    FILE *file = fopen("./src/driver/python/builtins.json", "r");
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

void FuzzingAST::dummyAST(const std::shared_ptr<ASTData> &data,
                          const FuzzSchedulerState &scheduler) {
    // dummy corpus
    // decl:
    // + str_a = ""
    // + str_b = ""
    // + byte_a = b"""
    // + int_a = 0
    int strType =
        std::find(scheduler.types.begin(), scheduler.types.end(), "str") -
        scheduler.types.begin();
    int bytesType =
        std::find(scheduler.types.begin(), scheduler.types.end(), "bytes") -
        scheduler.types.begin();
    int intType =
        std::find(scheduler.types.begin(), scheduler.types.end(), "int") -
        scheduler.types.begin();

    data->ast.declarations.push_back(
        ASTNode{ASTNodeKind::DeclareVar,
                strType,
                {ASTNodeValue{"str_a"}, ASTNodeValue{std::string("\"\"")}}});
    data->ast.declarations.push_back(
        ASTNode{ASTNodeKind::DeclareVar,
                strType,
                {ASTNodeValue{"str_b"}, ASTNodeValue{std::string("\"\"")}}});
    data->ast.declarations.push_back(
        ASTNode{ASTNodeKind::DeclareVar,
                bytesType,
                {ASTNodeValue{"byte_a"}, ASTNodeValue{std::string("b\"\"")}}});
    data->ast.declarations.push_back(
        ASTNode{ASTNodeKind::DeclareVar,
                intType,
                {ASTNodeValue{"int_a"}, ASTNodeValue{int64_t(0)}}});
    data->ast.scopes[0].declarations.resize(4);
    data->ast.scopes[0].variables.resize(4);
    data->ast.scopes[0].declarations[0] = 0;
    data->ast.scopes[0].declarations[1] = 1;
    data->ast.scopes[0].declarations[2] = 2;
    data->ast.scopes[0].declarations[3] = 3;
    data->ast.scopes[0].variables[0] = 0;
    data->ast.scopes[0].variables[1] = 1;
    data->ast.scopes[0].variables[2] = 2;
    data->ast.scopes[0].variables[3] = 3;
}

void FuzzingAST::reflectObject(const AST &ast, ASTScope &scope) {
    std::ostringstream script;

    bool empty = true;

    for (NodeID id : scope.declarations) {
        nodeToPython(script, ast.declarations.at(id), ast, 0);
        empty = false;
    }

    if (empty)
        return;

    std::string re = script.str();

    PyObject *dict = PyDict_New();
    PyObject *name = PyUnicode_FromString("__main__");
    PyDict_SetItemString(dict, "__name__", name);
    PyDict_SetItemString(dict, "__builtins__", PyEval_GetBuiltins());
    PyRun_String(re.c_str(), Py_file_input, dict, dict);
    if (PyErr_Occurred()) {
        PyErr_Print();
        PANIC("Failed to run decl code block:\n{}", re);
    }

    PyRun_String(driverPyConent.c_str(), Py_file_input, dict, dict);
    if (PyErr_Occurred()) {
        PyErr_Print();
        PANIC("Failed to run driver.py");
    }
    PyObject *rawJson = PyDict_GetItemString(dict, "result");
    auto *aaa = PyDict_Keys(dict);
    // print all
    if (aaa) {
        for (Py_ssize_t i = 0; i < PyList_Size(aaa); ++i) {
            PyObject *key = PyList_GetItem(aaa, i);
            if (PyUnicode_Check(key)) {
                std::string keyStr = PyUnicode_AsUTF8(key);
                std::cout << "Key: " << keyStr << std::endl;
            } else {
                std::cout << "Key is not a string" << std::endl;
            }
        }
        Py_DECREF(aaa);
    }
    if (!rawJson) {
        PyErr_Print();
        PANIC("Failed to get 'result' from driver.py");
    }
    if (!PyUnicode_Check(rawJson)) {
        PyErr_Print();
        PANIC("'result' from driver.py is not a string");
    }
    std::string jsonStr(PyUnicode_AsUTF8(rawJson));
    Py_DECREF(rawJson);
    Py_DECREF(dict);
    Py_DECREF(name);
    if (jsonStr.empty()) {
        PANIC("Empty result from driver.py");
    }
    nlohmann::json j = nlohmann::json::parse(jsonStr);
    auto tmp =
        j["funcs"].get<std::unordered_map<std::string, FunctionSignature>>();
    scope.funcSignatures.swap(tmp);
    auto tmp2 = j["types"].get<std::vector<std::string>>();
    scope.types.swap(tmp2);
}
