#ifndef MUTATORS_HPP
#define MUTATORS_HPP

#include "FuzzSchedulerState.hpp"
#include "ast.hpp"
#include <memory>

namespace FuzzingAST {

enum class MutationState { STATE_OK = 0, STATE_REROLL };

int generate_execution_block(const std::shared_ptr<ASTData> &ast,
                             const ScopeID &scope, const BuiltinContext &ctx);
int mutate_expression(const std::shared_ptr<ASTData> &ast,
                      const ScopeID scopeID, ASTScope &scope,
                      const BuiltinContext &ctx);
int generate_execution(const std::shared_ptr<ASTData> &,
                       const BuiltinContext &ctx);
int mutate_declaration(const std::shared_ptr<ASTData> &,
                       const BuiltinContext &ctx);

} // namespace FuzzingAST
#endif // MUTATORS_HPP