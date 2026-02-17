#ifndef LUA_DUMPER_HPP
#define LUA_DUMPER_HPP

#include "ast.hpp"
#include <sstream>

namespace FuzzingAST {
void nodeToLua(std::ostringstream &out, const ASTNode &node, const AST &ast,
               const BuiltinContext &ctx, int indentLevel);
void scopeToLua(std::ostringstream &out, ScopeID sid, const AST &ast,
                const BuiltinContext &ctx, int indentLevel);
} // namespace FuzzingAST

#endif // LUA_DUMPER_HPP
