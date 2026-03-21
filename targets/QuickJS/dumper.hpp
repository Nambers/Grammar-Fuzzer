#ifndef QUICKJS_DUMPER_HPP
#define QUICKJS_DUMPER_HPP

#include "ast.hpp"
#include <sstream>

namespace FuzzingAST {
void nodeToJS(std::ostringstream &out, const ASTNode &node, const AST &ast,
              const BuiltinContext &ctx, int indentLevel);
void scopeToJS(std::ostringstream &out, ScopeID sid, const AST &ast,
               const BuiltinContext &ctx, int indentLevel);
} // namespace FuzzingAST

#endif // QUICKJS_DUMPER_HPP
