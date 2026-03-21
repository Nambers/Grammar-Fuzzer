#include "dumper.hpp"
#include <iostream>

using namespace FuzzingAST;

#ifndef FUZZ_ENABLE_DUMPER_VALIDATION
#define FUZZ_ENABLE_DUMPER_VALIDATION 1
#endif

namespace {
template <typename T>
inline bool isValidID(int id, const std::vector<T> &v) {
#if FUZZ_ENABLE_DUMPER_VALIDATION
    return id >= 0 && static_cast<size_t>(id) < v.size();
#else
    (void)id;
    (void)v;
    return true;
#endif
}
} // namespace

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
    } else {
        out << "None";
    }
}

void FuzzingAST::nodeToPython(std::ostringstream &out, const ASTNode &node,
                              const AST &ast, const BuiltinContext &ctx,
                              int indentLevel) {
    const std::string ind(indentLevel * 4, ' ');
    out << ind;

    auto requireFields = [&](size_t n) -> bool {
#if FUZZ_ENABLE_DUMPER_VALIDATION
        if (node.fields.size() < n) {
            out << "# malformed node: kind " << static_cast<int>(node.kind)
                << " expects >= " << n << " fields, got "
                << node.fields.size();
            return false;
        }
#else
        (void)n;
#endif
        return true;
    };

    switch (node.kind) {
    case ASTNodeKind::DeclareVar: {
        if (!requireFields(2))
            break;
        // name [: type] = value
        const std::string &name = std::get<std::string>(node.fields[0].val);
        out << name;
        // no annotation bc will conflict with global
        // if (node.type != -1)
        //     out << ": " << getTypeName(node.type, ast, ctx);
        out << " = ";
        valueToPython(out, node.fields[1], ast, ctx, indentLevel);
        break;
    }

    case ASTNodeKind::Return:
        if (!requireFields(1))
            break;
        out << "return ";
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        break;
    case ASTNodeKind::GetProp:
        [[fallthrough]];
    case ASTNodeKind::SetProp:
        if (!requireFields(2))
            break;
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        out << " = ";
        valueToPython(out, node.fields[1], ast, ctx, indentLevel);
        break;
    case ASTNodeKind::Call:
        if (!requireFields(2))
            break;
        if (!std::get<std::string>(node.fields[0].val).empty()) {
            valueToPython(out, node.fields[0], ast, ctx, indentLevel);
            out << " = ";
        }
        valueToPython(out, node.fields[1], ast, ctx, indentLevel);
        out << "(";
        for (size_t i = 2; i < node.fields.size(); ++i) {
            if (i > 2)
                out << ", ";
            valueToPython(out, node.fields[i], ast, ctx, indentLevel);
        }
        out << ")";
        break;

    case ASTNodeKind::BinaryOp:
        if (!requireFields(4))
            break;
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        out << " = ";
        valueToPython(out, node.fields[1], ast, ctx, indentLevel);
        out << ' ' << std::get<std::string>(node.fields[2].val) << ' ';
        valueToPython(out, node.fields[3], ast, ctx, indentLevel);
        break;

    case ASTNodeKind::UnaryOp:
        if (!requireFields(3))
            break;
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        out << " = " << std::get<std::string>(node.fields[1].val) << ' ';
        valueToPython(out, node.fields[2], ast, ctx, indentLevel);
        break;

    case ASTNodeKind::SetItem:
        if (!requireFields(3))
            break;
        // container[index] = value
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        out << "[";
        valueToPython(out, node.fields[1], ast, ctx, indentLevel);
        out << "] = ";
        valueToPython(out, node.fields[2], ast, ctx, indentLevel);
        break;

    case ASTNodeKind::GetItem:
        if (!requireFields(3))
            break;
        // result = container[index]
        valueToPython(out, node.fields[0], ast, ctx, indentLevel);
        out << " = ";
        valueToPython(out, node.fields[1], ast, ctx, indentLevel);
        out << "[";
        valueToPython(out, node.fields[2], ast, ctx, indentLevel);
        out << "]";
        break;

    case ASTNodeKind::Function: {
        if (!requireFields(2))
            break;
        const std::string &name = std::get<std::string>(node.fields[0].val);

        // def fields[0](fields[2]...) -> fields[1]
        size_t paramCnt = node.fields.size() > 2 ? node.fields.size() - 2 : 0;
        out << "def " << name << '(';
        for (size_t i = 0; i < paramCnt; i += 2) {
            if (i)
                out << ", ";
            std::string argName = std::get<std::string>(node.fields[2 + i].val);
            TypeID pt = static_cast<TypeID>(
                std::get<int64_t>(node.fields[2 + i + 1].val));
            out << argName << ": " << getTypeName(pt, ast, ctx);
        }
        out << ")";

        TypeID retType = std::get<int64_t>(node.fields[1].val);
        out << " -> " << getTypeName(retType, ast, ctx) << ":\n";
        if (node.scope != EMPTY_SCOPE && !isValidID(node.scope, ast.scopes)) {
            out << std::string((indentLevel + 1) * 4, ' ')
                << "# invalid function scope " << node.scope << '\n';
        } else {
            scopeToPython(out, node.scope, ast, ctx, indentLevel + 1);
        }
        break;
    }
    case ASTNodeKind::Class: {
        if (!requireFields(1))
            break;
        const std::string &name = std::get<std::string>(node.fields[0].val);

        std::vector<std::string> bases;
        size_t idx = 1; // collect bases
        for (; idx < node.fields.size(); ++idx) {
            if (std::holds_alternative<int64_t>(node.fields[idx].val) &&
                node.fields[idx] == SENTINEL_NODE) {
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
            if (!isValidID(fnID, ast.declarations)) {
                out << std::string((indentLevel + 1) * 4, ' ')
                    << "# invalid class member function id " << fnID << '\n';
                continue;
            }
            nodeToPython(out, ast.declarations[fnID], ast, ctx, indentLevel + 1);
            bodyEmpty = false;
        }
        if (bodyEmpty)
            out << std::string((indentLevel + 1) * 4, ' ') << "pass";
        break;
    }
    case ASTNodeKind::GlobalRef: {
        if (node.fields.empty()) {
            out << "# malformed global ref: empty";
            break;
        }
        // every field is a string, join with space
        out << "global " << std::get<std::string>(node.fields[0].val);
        for (size_t i = 1; i < node.fields.size(); ++i) {
            out << ", " << std::get<std::string>(node.fields[i].val);
        }
        break;
    }
    case ASTNodeKind::Import: {
        if (!requireFields(1))
            break;
        out << "exec('from " << std::get<std::string>(node.fields[0].val)
            << " import *', globals())";
        break;
    }

    default:
        out << "# unsupported kind " << static_cast<int>(node.kind);
        break;
    }
    out << '\n';
}

void FuzzingAST::scopeToPython(std::ostringstream &out, ScopeID sid,
                               const AST &ast, const BuiltinContext &ctx,
                               int indentLevel) {
    if (sid == EMPTY_SCOPE)
        return;
    if (!isValidID(sid, ast.scopes)) {
        out << std::string(indentLevel * 4, ' ') << "# invalid scope id " << sid
            << "\n";
        return;
    }
    out << std::string(indentLevel * 4, ' ') << "# scope " << sid << '\n';
    const ASTScope &scope = ast.scopes[sid];
    bool empty = true;

    if (scope.globalRefID != -1 && isValidID(scope.globalRefID, ast.declarations)) {
        nodeToPython(out, ast.declarations[scope.globalRefID], ast, ctx,
                     indentLevel);
        empty = false;
    } else if (scope.globalRefID != -1) {
        out << std::string(indentLevel * 4, ' ') << "# invalid globalRefID "
            << scope.globalRefID << '\n';
    }
    for (NodeID id : scope.declarations) {
        if (!isValidID(id, ast.declarations)) {
            out << std::string(indentLevel * 4, ' ')
                << "# invalid declaration id " << id << '\n';
            continue;
        }
        const auto &decl = ast.declarations[id];
        // all function are under class, which will be rendered in class handler
        if (decl.kind != ASTNodeKind::Function) {
            nodeToPython(out, ast.declarations[id], ast, ctx, indentLevel);
            empty = false;
        }
    }
    for (NodeID id : scope.expressions) {
        if (!isValidID(id, ast.expressions)) {
            out << std::string(indentLevel * 4, ' ')
                << "# invalid expression id " << id << '\n';
            continue;
        }
        nodeToPython(out, ast.expressions[id], ast, ctx, indentLevel);
        empty = false;
    }
    // return at the end
    if (scope.retNodeID != -1 && isValidID(scope.retNodeID, ast.expressions)) {
        const auto &retNode = ast.expressions[scope.retNodeID];
        nodeToPython(out, retNode, ast, ctx, indentLevel);
        empty = false;
    } else if (scope.retNodeID != -1) {
        out << std::string(indentLevel * 4, ' ') << "# invalid retNodeID "
            << scope.retNodeID << '\n';
    }

    if (empty)
        out << std::string(indentLevel * 4, ' ') << "pass\n";
}