#include "dumper.hpp"

using namespace FuzzingAST;

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
        scopeToPython(out, node.scope, ast, ctx, indentLevel + 1);
        break;
    }
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

void FuzzingAST::scopeToPython(std::ostringstream &out, ScopeID sid,
                               const AST &ast, const BuiltinContext &ctx,
                               int indentLevel) {
    if (sid == -1)
        return;
    out << std::string(indentLevel * 4, ' ') << "# scope " << sid << '\n';
    const ASTScope &scope = ast.scopes[sid];
    bool empty = true;

    for (NodeID id : scope.declarations) {
        const auto &decl = ast.declarations.at(id);
        // all function are under class, which will be rendered in class handler
        if (decl.kind != ASTNodeKind::Function) {
            nodeToPython(out, ast.declarations.at(id), ast, ctx, indentLevel);
            empty = false;
        }
    }
    for (NodeID id : scope.expressions) {
        nodeToPython(out, ast.expressions.at(id), ast, ctx, indentLevel);
        empty = false;
    }

    if (empty)
        out << std::string(indentLevel * 4, ' ') << "pass\n";
}