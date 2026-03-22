#include "dumper.hpp"

using namespace FuzzingAST;

static const char *mapBinaryOp(const std::string &op) {
    if (op == "//")
        return "/";
    return op.c_str();
}

static const char *mapUnaryOp(const std::string &op) {
    if (op == "not")
        return "!";
    return op.c_str();
}

static void valueToJS(std::ostringstream &out, const ASTNodeValue &val,
                      const AST & /*ast*/, int /*indentLevel*/) {
    if (std::holds_alternative<std::string>(val.val)) {
        const auto &s = std::get<std::string>(val.val);
        if (s == "None()" || s == "nil()")
            out << "undefined";
        else if (s == "number()")
            out << "0";
        else if (s == "string()")
            out << "\"\"";
        else if (s == "boolean()")
            out << "false";
        else if (s == "table()" || s == "array()")
            out << "[]";
        else if (s == "object()")
            out << "({})";
        else
            out << s;
    } else if (std::holds_alternative<int64_t>(val.val)) {
        out << std::get<int64_t>(val.val);
    } else if (std::holds_alternative<bool>(val.val)) {
        out << (std::get<bool>(val.val) ? "true" : "false");
    } else if (std::holds_alternative<double>(val.val)) {
        out << std::get<double>(val.val);
    } else {
        out << "undefined";
    }
}

void FuzzingAST::nodeToJS(std::ostringstream &out, const ASTNode &node,
                          const AST &ast, int indentLevel) {
    const std::string ind(indentLevel * 4, ' ');
    out << ind;

    switch (node.kind) {
    case ASTNodeKind::DeclareVar: {
        const std::string &name = std::get<std::string>(node.fields[0].val);
        out << "let " << name << " = ";
        valueToJS(out, node.fields[1], ast, indentLevel);
        out << ";";
        break;
    }

    case ASTNodeKind::Return:
        out << "return ";
        valueToJS(out, node.fields[0], ast, indentLevel);
        out << ";";
        break;

    case ASTNodeKind::GetProp:
        [[fallthrough]];
    case ASTNodeKind::SetProp:
        valueToJS(out, node.fields[0], ast, indentLevel);
        out << " = ";
        valueToJS(out, node.fields[1], ast, indentLevel);
        out << ";";
        break;

    case ASTNodeKind::Call: {
        if (!std::get<std::string>(node.fields[0].val).empty()) {
            valueToJS(out, node.fields[0], ast, indentLevel);
            out << " = ";
        }
        valueToJS(out, node.fields[1], ast, indentLevel);
        out << "(";
        for (size_t i = 2; i < node.fields.size(); ++i) {
            if (i > 2)
                out << ", ";
            valueToJS(out, node.fields[i], ast, indentLevel);
        }
        out << ");";
        break;
    }

    case ASTNodeKind::BinaryOp: {
        valueToJS(out, node.fields[0], ast, indentLevel);
        out << " = ";
        valueToJS(out, node.fields[1], ast, indentLevel);
        const char *op = mapBinaryOp(std::get<std::string>(node.fields[2].val));
        out << ' ' << op << ' ';
        valueToJS(out, node.fields[3], ast, indentLevel);
        out << ";";
        break;
    }

    case ASTNodeKind::UnaryOp: {
        valueToJS(out, node.fields[0], ast, indentLevel);
        out << " = ";
        const char *op = mapUnaryOp(std::get<std::string>(node.fields[1].val));
        out << op;
        if (std::string(op) != "~" && std::string(op) != "-")
            out << " ";
        valueToJS(out, node.fields[2], ast, indentLevel);
        out << ";";
        break;
    }

    case ASTNodeKind::Function: {
        const std::string &name = std::get<std::string>(node.fields[0].val);
        size_t paramCnt =
            node.fields.size() > 2 ? (node.fields.size() - 2) / 2 : 0;

        out << "function " << name << "(";
        for (size_t i = 0; i < paramCnt; ++i) {
            if (i)
                out << ", ";
            out << std::get<std::string>(node.fields[2 + i * 2].val);
        }
        out << ") {\n";

        scopeToJS(out, node.scope, ast, indentLevel + 1);

        out << ind << "}";
        break;
    }

    case ASTNodeKind::Class: {
        const std::string &name = std::get<std::string>(node.fields[0].val);

        std::string baseName;
        size_t idx = 1;
        for (; idx < node.fields.size(); ++idx) {
            if (std::holds_alternative<int64_t>(node.fields[idx].val) &&
                node.fields[idx] == SENTINEL_NODE) {
                ++idx;
                break;
            }
            const auto &b = std::get<std::string>(node.fields[idx].val);
            if (b != "number" && b != "boolean" && b != "string" &&
                b != "object" && b != "array" && b != "function" &&
                b != "Math" && b != "JSON") {
                baseName = b;
                break;
            }
        }

        out << "class " << name;
        if (!baseName.empty())
            out << " extends " << baseName;
        out << " {\n";

        for (; idx < node.fields.size(); ++idx) {
            NodeID fnID =
                static_cast<NodeID>(std::get<int64_t>(node.fields[idx].val));
            const auto &fn = ast.declarations[fnID];
            const std::string &fnName = std::get<std::string>(fn.fields[0].val);

            size_t pCnt = fn.fields.size() > 2 ? (fn.fields.size() - 2) / 2 : 0;
            bool hasSelf =
                (pCnt > 0 && std::get<std::string>(fn.fields[2].val) == "self");
            size_t startParam = hasSelf ? 1 : 0;

            out << ind << "    "
                << ((fnName == "__init__") ? "constructor" : fnName) << "(";
            for (size_t p = startParam; p < pCnt; ++p) {
                if (p > startParam)
                    out << ", ";
                out << std::get<std::string>(fn.fields[2 + p * 2].val);
            }
            out << ") {\n";
            scopeToJS(out, fn.scope, ast, indentLevel + 2);
            out << ind << "    }\n";
        }

        out << ind << "}";
        break;
    }

    case ASTNodeKind::GlobalRef:
        out << "// global refs unsupported for JS target";
        break;

    case ASTNodeKind::Import: {
        const std::string &mod = std::get<std::string>(node.fields[0].val);
        if (mod == "math")
            out << "const math = Math;";
        else if (mod == "json")
            out << "const json = JSON;";
        else
            out << "const " << mod << " = globalThis[\"" << mod << "\"];";
        break;
    }

    case ASTNodeKind::SetItem:
        valueToJS(out, node.fields[0], ast, indentLevel);
        out << "[";
        valueToJS(out, node.fields[1], ast, indentLevel);
        out << "] = ";
        valueToJS(out, node.fields[2], ast, indentLevel);
        out << ";";
        break;

    case ASTNodeKind::GetItem:
        valueToJS(out, node.fields[0], ast, indentLevel);
        out << " = ";
        valueToJS(out, node.fields[1], ast, indentLevel);
        out << "[";
        valueToJS(out, node.fields[2], ast, indentLevel);
        out << "];";
        break;

    case ASTNodeKind::NewInstance:
        out << "// unsupported raw NewInstance";
        break;

    default:
        out << "// unsupported kind " << static_cast<int>(node.kind);
        break;
    }

    out << '\n';
}

void FuzzingAST::scopeToJS(std::ostringstream &out, ScopeID sid, const AST &ast,
                           int indentLevel) {
    if (sid == EMPTY_SCOPE)
        return;
    out << std::string(indentLevel * 4, ' ') << "// scope " << sid << '\n';
    const ASTScope &scope = ast.scopes[sid];
    bool empty = true;

    if (scope.globalRefID != -1) {
        nodeToJS(out, ast.declarations[scope.globalRefID], ast, indentLevel);
        empty = false;
    }

    for (NodeID id : scope.declarations) {
        const auto &decl = ast.declarations[id];
        if (decl.kind != ASTNodeKind::Function) {
            nodeToJS(out, decl, ast, indentLevel);
            empty = false;
        }
    }

    for (NodeID id : scope.expressions) {
        nodeToJS(out, ast.expressions[id], ast, indentLevel);
        empty = false;
    }

    if (scope.retNodeID != -1) {
        const auto &retNode = ast.expressions[scope.retNodeID];
        nodeToJS(out, retNode, ast, indentLevel);
        empty = false;
    }

    if (empty)
        out << std::string(indentLevel * 4, ' ') << ";\n";
}
