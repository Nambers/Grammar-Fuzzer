#include "dumper.hpp"

using namespace FuzzingAST;

// -- operator mapping (Python AST strings -> Lua source) ----------------------
static const char *mapBinaryOp(const std::string &op) {
    if (op == "**")
        return "^";
    if (op == "!=")
        return "~=";
    if (op == "^") // Python XOR → Lua XOR
        return "~";
    // +  -  *  /  %  //  ==  <  >  <=  >=  &  |  <<  >>
    return op.c_str();
}

static const char *mapUnaryOp(const std::string &op) {
    // "-", "not", "~"  – all identical
    return op.c_str();
}

// -- value helper -------------------------------------------------------------
static void valueToLua(std::ostringstream &out, const ASTNodeValue &val,
                       const AST & /*ast*/, const BuiltinContext & /*ctx*/,
                       int /*indentLevel*/) {
    if (std::holds_alternative<std::string>(val.val)) {
        const auto &s = std::get<std::string>(val.val);
        // Intercept invalid Lua constructor calls emitted by AddVariable
        // for primitive types (e.g. "None()", "nil()", "number()", "string()",
        // "boolean()", "table()", "object()").
        if (s == "None()" || s == "nil()")
            out << "nil";
        else if (s == "number()")
            out << "0";
        else if (s == "string()")
            out << "\"\"";
        else if (s == "boolean()")
            out << "false";
        else if (s == "table()")
            out << "{}";
        else if (s == "object()")
            out << "nil";
        else
            out << s;
    } else if (std::holds_alternative<int64_t>(val.val)) {
        out << std::get<int64_t>(val.val);
    } else if (std::holds_alternative<bool>(val.val)) {
        out << (std::get<bool>(val.val) ? "true" : "false");
    } else if (std::holds_alternative<double>(val.val)) {
        out << std::get<double>(val.val);
    } else {
        out << "nil";
    }
}

// -- node → Lua source -------------------------------------------------------
void FuzzingAST::nodeToLua(std::ostringstream &out, const ASTNode &node,
                           const AST &ast, const BuiltinContext &ctx,
                           int indentLevel) {
    const std::string ind(indentLevel * 4, ' ');
    out << ind;

    switch (node.kind) {

    /* -- DeclareVar ------------------------------------------------ */
    case ASTNodeKind::DeclareVar: {
        const std::string &name = std::get<std::string>(node.fields[0].val);
        out << name << " = ";
        valueToLua(out, node.fields[1], ast, ctx, indentLevel);
        break;
    }

    /* -- Return ---------------------------------------------------- */
    case ASTNodeKind::Return:
        out << "return ";
        valueToLua(out, node.fields[0], ast, ctx, indentLevel);
        break;

    /* -- GetProp / SetProp  (simple assignment) -------------------- */
    case ASTNodeKind::GetProp:
        [[fallthrough]];
    case ASTNodeKind::SetProp:
        valueToLua(out, node.fields[0], ast, ctx, indentLevel);
        out << " = ";
        valueToLua(out, node.fields[1], ast, ctx, indentLevel);
        break;

    /* -- Call ------------------------------------------------------- */
    case ASTNodeKind::Call: {
        if (!std::get<std::string>(node.fields[0].val).empty()) {
            valueToLua(out, node.fields[0], ast, ctx, indentLevel);
            out << " = ";
        }
        valueToLua(out, node.fields[1], ast, ctx, indentLevel);
        out << "(";
        for (size_t i = 2; i < node.fields.size(); ++i) {
            if (i > 2)
                out << ", ";
            valueToLua(out, node.fields[i], ast, ctx, indentLevel);
        }
        out << ")";
        break;
    }

    /* -- BinaryOp -------------------------------------------------- */
    case ASTNodeKind::BinaryOp: {
        valueToLua(out, node.fields[0], ast, ctx, indentLevel);
        out << " = ";
        valueToLua(out, node.fields[1], ast, ctx, indentLevel);
        const char *op =
            mapBinaryOp(std::get<std::string>(node.fields[2].val));
        out << ' ' << op << ' ';
        valueToLua(out, node.fields[3], ast, ctx, indentLevel);
        break;
    }

    /* -- UnaryOp --------------------------------------------------- */
    case ASTNodeKind::UnaryOp: {
        valueToLua(out, node.fields[0], ast, ctx, indentLevel);
        out << " = ";
        const char *op =
            mapUnaryOp(std::get<std::string>(node.fields[1].val));
        out << op << ' ';
        valueToLua(out, node.fields[2], ast, ctx, indentLevel);
        break;
    }

    /* -- Function -------------------------------------------------- */
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
        out << ")\n";

        scopeToLua(out, node.scope, ast, ctx, indentLevel + 1);

        out << ind << "end";
        break;
    }

    /* -- Class  (metatable-based OOP) ------------------------------ */
    case ASTNodeKind::Class: {
        const std::string &name = std::get<std::string>(node.fields[0].val);

        // collect bases up to the -1 sentinel
        std::vector<std::string> bases;
        size_t idx = 1;
        for (; idx < node.fields.size(); ++idx) {
            if (std::holds_alternative<int64_t>(node.fields[idx].val) &&
                std::get<int64_t>(node.fields[idx].val) == -1) {
                ++idx;
                break;
            }
            const auto &baseName =
                std::get<std::string>(node.fields[idx].val);
            // Skip primitive type names that aren't valid Lua tables.
            // "number", "boolean", "nil", "string", "object", "math" cannot
            // be used as __index bases — only user-defined class names and
            // "table" are valid.
            if (baseName != "number" && baseName != "boolean" &&
                baseName != "nil" && baseName != "string" &&
                baseName != "object" && baseName != "math" &&
                baseName != "None") {
                bases.push_back(baseName);
            }
        }

        // emit metatable-based class
        out << name << " = setmetatable({}, {";
        if (!bases.empty()) {
            out << "__index = " << bases[0];
        }
        out << ",\n";
        out << ind << "    __call = function(cls, ...)\n";
        out << ind << "        local self = setmetatable({}, {__index = cls})\n";
        out << ind
            << "        if cls.__init__ then cls.__init__(self, ...) end\n";
        out << ind << "        return self\n";
        out << ind << "    end\n";
        out << ind << "})\n";
        out << ind << name << ".__index = " << name;

        // emit member functions
        for (; idx < node.fields.size(); ++idx) {
            NodeID fnID =
                static_cast<NodeID>(std::get<int64_t>(node.fields[idx].val));
            out << '\n';
            const auto &fn = ast.declarations[fnID];
            const std::string &fnName =
                std::get<std::string>(fn.fields[0].val);

            // emit as ClassName.method(self, ...)
            size_t pCnt =
                fn.fields.size() > 2 ? (fn.fields.size() - 2) / 2 : 0;
            out << ind << "function " << name << ":" << fnName << "(";
            for (size_t p = 0; p < pCnt; ++p) {
                if (p)
                    out << ", ";
                out << std::get<std::string>(fn.fields[2 + p * 2].val);
            }
            out << ")\n";
            scopeToLua(out, fn.scope, ast, ctx, indentLevel + 1);
            out << ind << "end";
        }
        break;
    }

    /* -- GlobalRef  (no-op in Lua — vars are global by default) -- */
    case ASTNodeKind::GlobalRef:
        out << "-- global refs (implicit in Lua)";
        break;

    /* -- Import ---------------------------------------------------- */
    case ASTNodeKind::Import: {
        const std::string &mod = std::get<std::string>(node.fields[0].val);
        // make the module table available as a global with its name
        out << mod << " = require(\"" << mod << "\")";
        break;
    }

    /* -- NewInstance (should be lowered to Call before reaching here) */
    case ASTNodeKind::NewInstance:
        out << "-- unsupported raw NewInstance";
        break;

    default:
        out << "-- unsupported kind " << static_cast<int>(node.kind);
        break;
    }
    out << '\n';
}

// -- scope → Lua source ------------------------------------------------------
void FuzzingAST::scopeToLua(std::ostringstream &out, ScopeID sid,
                            const AST &ast, const BuiltinContext &ctx,
                            int indentLevel) {
    if (sid == -1)
        return;
    out << std::string(indentLevel * 4, ' ') << "-- scope " << sid << '\n';
    const ASTScope &scope = ast.scopes[sid];
    bool empty = true;

    // GlobalRef (no-op for Lua but we emit the comment for consistency)
    if (scope.globalRefID != -1) {
        nodeToLua(out, ast.declarations[scope.globalRefID], ast, ctx,
                  indentLevel);
        empty = false;
    }

    // declarations (functions rendered inside Class)
    for (NodeID id : scope.declarations) {
        const auto &decl = ast.declarations[id];
        if (decl.kind != ASTNodeKind::Function) {
            nodeToLua(out, decl, ast, ctx, indentLevel);
            empty = false;
        }
    }

    // expressions
    for (NodeID id : scope.expressions) {
        nodeToLua(out, ast.expressions[id], ast, ctx, indentLevel);
        empty = false;
    }

    // return
    if (scope.retNodeID != -1) {
        const auto &retNode = ast.expressions[scope.retNodeID];
        nodeToLua(out, retNode, ast, ctx, indentLevel);
        empty = false;
    }

    if (empty)
        out << std::string(indentLevel * 4, ' ') << "-- (empty scope)\n";
}
