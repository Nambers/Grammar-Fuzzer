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

void nodeToPython(std::ostringstream &out, const ASTNode &node, const AST &ast,
                  const BuiltinContext &ctx, int indentLevel);
void scopeToPython(std::ostringstream &out, ScopeID sid, const AST &ast,
                   const BuiltinContext &ctx, int indentLevel);

/* ───────── helpers ───────── */
void valueToPython(std::ostringstream &out, const ASTNodeValue &val,
                   const AST &ast, const BuiltinContext &ctx, int indentLevel) {
    if (std::holds_alternative<std::string>(val.val)) {
        out << std::get<std::string>(val.val);
    } else if (std::holds_alternative<int64_t>(val.val)) {
        out << std::get<int64_t>(val.val);
    } else if (std::holds_alternative<bool>(val.val)) {
        out << (std::get<bool>(val.val) ? "True" : "False");
    } else if (std::holds_alternative<double>(val.val)) {
        out << std::get<double>(val.val);
    } else if (std::holds_alternative<NodeID>(val.val)) {
        const ASTNode &child = ast.expressions.at(std::get<NodeID>(val.val));
        nodeToPython(out, child, ast, ctx, indentLevel);
    } else {
        out << "None";
    }
}

void nodeToPython(std::ostringstream &out, const ASTNode &node, const AST &ast,
                  const BuiltinContext &ctx, int indentLevel) {
    const std::string ind(indentLevel * 4, ' ');
    out << ind;

    switch (node.kind) {
    case ASTNodeKind::DeclareVar: {
        // name [: type] = value
        const std::string &name = std::get<std::string>(node.fields[0].val);
        out << name;
        if (node.type != -1)
            out << ": " << getTypeName(node.type, ast, ctx);
        out << " = ";
        valueToPython(out, node.fields[1], ast, ctx, indentLevel);
        break;
    }
    case ASTNodeKind::Assign:
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        out << " = ";
        valueToPython(out, node.fields[1], ast, ctx, indentLevel);
        break;

    case ASTNodeKind::Return:
        out << "return ";
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        break;

    case ASTNodeKind::Call:
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        out << "(";
        for (size_t i = 1; i < node.fields.size(); ++i) {
            if (i > 1)
                out << ", ";
            valueToPython(out, node.fields[i], ast, ctx, indentLevel);
        }
        out << ")";
        break;

    case ASTNodeKind::BinaryOp:
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        out << " = ";
        valueToPython(out, node.fields[1], ast, ctx, indentLevel);
        out << ' ' << std::get<std::string>(node.fields[2].val) << ' ';
        valueToPython(out, node.fields[3], ast, ctx, indentLevel);
        break;

    case ASTNodeKind::UnaryOp:
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        out << " = " << std::get<std::string>(node.fields[1].val) << ' ';
        valueToPython(out, node.fields[2], ast, ctx, indentLevel);
        break;

    case ASTNodeKind::Function: {
        const std::string &name = std::get<std::string>(node.fields[0].val);

        // def fields[0](fields[2]...) -> fields[1]
        size_t paramCnt = node.fields.size() > 2 ? node.fields.size() - 2 : 0;
        out << "def " << name << '(';
        for (size_t i = 0; i < paramCnt; ++i) {
            if (i)
                out << ", ";
            TypeID pt =
                static_cast<TypeID>(std::get<int64_t>(node.fields[2 + i].val));
            out << "arg" << i << ": " << getTypeName(pt, ast, ctx);
        }
        out << ")";

        TypeID retType = std::get<TypeID>(node.fields[1].val);
        if (retType != -1) {
            out << " -> " << getTypeName(retType, ast, ctx);
        }

        out << ":\n";
        out << std::string((indentLevel + 1) * 4, ' ') << "pass";
        break;
    }

    /* ─── 类定义 ─── */
    case ASTNodeKind::Class: {
        const std::string &name = std::get<std::string>(node.fields[0].val);

        std::vector<std::string> bases;
        size_t idx = 1; // collect bases
        for (; idx < node.fields.size(); ++idx) {
            if (std::holds_alternative<int64_t>(node.fields[idx].val) &&
                std::get<int64_t>(node.fields[idx].val) == -1) {
                ++idx; // skip sentinel
                break;
            }
            bases.push_back(std::get<std::string>(node.fields[idx].val));
        }

        out << "class " << name;
        if (!bases.empty()) {
            out << '(';
            for (size_t i = 0; i < bases.size(); ++i) {
                if (i)
                    out << ", ";
                out << bases[i];
            }
            out << ')';
        }
        out << ":\n";

        bool bodyEmpty = true;
        for (; idx < node.fields.size(); ++idx) {
            NodeID fnID =
                static_cast<NodeID>(std::get<int64_t>(node.fields[idx].val));
            nodeToPython(out, ast.declarations.at(fnID), ast, ctx,
                         indentLevel + 1);
            bodyEmpty = false;
        }
        if (bodyEmpty)
            out << std::string((indentLevel + 1) * 4, ' ') << "pass";
        break;
    }

    default:
        out << "# unsupported kind " << static_cast<int>(node.kind);
        break;
    }
    out << '\n';
}

void scopeToPython(std::ostringstream &out, ScopeID sid, const AST &ast,
                   const BuiltinContext &ctx, int indentLevel) {
    if (sid == -1)
        return;

    const ASTScope &scope = ast.scopes[sid];
    bool empty = true;

    for (NodeID id : scope.declarations) {
        nodeToPython(out, ast.declarations.at(id), ast, ctx, indentLevel);
        empty = false;
    }
    for (NodeID id : scope.expressions) {
        nodeToPython(out, ast.expressions.at(id), ast, ctx, indentLevel);
        empty = false;
    }

    if (empty)
        out << std::string(indentLevel * 4, ' ') << "pass\n";
}

int FuzzingAST::runAST(const AST &ast, const BuiltinContext &ctx, bool echo) {
    std::ostringstream script;
    scopeToPython(script, 0, ast, ctx, 0);
    std::string re = script.str();

    if (echo) {
        std::cout << "[Generated Python]:\n" << re << "\n";
    }

    PyObject *code = Py_CompileString(re.c_str(), "<ast>", Py_file_input);
    if (!code || PyErr_Occurred()) {
#ifndef QUIET
        PyErr_Print();
#endif
        PyErr_Clear();
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
        PyErr_Clear();
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

void FuzzingAST::loadBuiltinsFuncs(BuiltinContext &ctx) {
    auto &funcSignatures = ctx.builtinsFuncs;
    auto &types = ctx.types;
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
                          const BuiltinContext &ctx) {
    // dummy corpus
    // decl:
    // + str_a = ""
    // + str_b = ""
    // + byte_a = b"""
    // + int_a = 0
    TypeID bytesType = std::find(ctx.types.begin(), ctx.types.end(), "bytes") -
                       ctx.types.begin();
    data->ast.declarations.push_back(
        ASTNode{ASTNodeKind::DeclareVar,
                ctx.strID,
                {ASTNodeValue{"str_a"}, ASTNodeValue{std::string("\"\"")}}});
    data->ast.declarations.push_back(
        ASTNode{ASTNodeKind::DeclareVar,
                ctx.strID,
                {ASTNodeValue{"str_b"}, ASTNodeValue{std::string("\"\"")}}});
    data->ast.declarations.push_back(
        ASTNode{ASTNodeKind::DeclareVar,
                bytesType,
                {ASTNodeValue{"byte_a"}, ASTNodeValue{std::string("b\"\"")}}});
    data->ast.declarations.push_back(
        ASTNode{ASTNodeKind::DeclareVar,
                ctx.intID,
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

void FuzzingAST::reflectObject(const AST &ast, ASTScope &scope,
                               const BuiltinContext &ctx) {
    std::ostringstream script;

    bool empty = true;

    for (NodeID id : scope.declarations) {
        nodeToPython(script, ast.declarations.at(id), ast, ctx, 0);
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
