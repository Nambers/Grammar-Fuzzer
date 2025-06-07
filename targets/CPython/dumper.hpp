#ifndef DUMPER_HPP
#define DUMPER_HPP

#include "ast.hpp"
#include <sstream>

namespace FuzzingAST {
void nodeToPython(std::ostringstream &out, const ASTNode &node, const AST &ast,
                  const BuiltinContext &ctx, int indentLevel);
void scopeToPython(std::ostringstream &out, ScopeID sid, const AST &ast,
                   const BuiltinContext &ctx, int indentLevel);
} // namespace FuzzingAST

#endif