#ifndef MUTATORS_HPP
#define MUTATORS_HPP

#include "FuzzSchedulerState.hpp"
#include "ast.hpp"
#include <unordered_map>
#include <unordered_set>

namespace FuzzingAST {

enum class MutationState { STATE_OK = 0, STATE_REROLL };

int generate_execution_block(ASTData &ast, const ScopeID &scope,
                             BuiltinContext &ctx);
AST mutate_expression(AST ast, const ScopeID scopeID, BuiltinContext &ctx);
int generate_execution(ASTData &, BuiltinContext &ctx);
int mutate_declaration(ASTData &, BuiltinContext &ctx);
int generate_line(ASTNode &node, const ASTData &ast, BuiltinContext &ctx,
                  std::unordered_set<std::string> &globalVars, ScopeID scopeID,
                  const ASTScope &scope);

} // namespace FuzzingAST
#endif // MUTATORS_HPP