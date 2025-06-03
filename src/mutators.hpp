#ifndef MUTATORS_HPP
#define MUTATORS_HPP

#include "ast.hpp"
#include <memory>

namespace FuzzingAST {

enum class MutationState { STATE_OK = 0, STATE_REROLL };

int generate_execution_block(const std::shared_ptr<ASTData> &ast,
                             const ScopeID &scope);
int mutate_expression(const std::shared_ptr<ASTData> &ast,
                      const std::vector<NodeID> &nodes);
int generate_execution(const std::shared_ptr<ASTData> &);
int mutate_declaration(const std::shared_ptr<ASTData> &);

} // namespace FuzzingAST
#endif // MUTATORS_HPP