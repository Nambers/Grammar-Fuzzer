#ifndef MUTATORS_HPP
#define MUTATORS_HPP

#include "ast.hpp"
#include <memory>

namespace FuzzingAST {

int generate_execution(const std::shared_ptr<ASTData> &);
int mutate_declaration(const std::shared_ptr<ASTData> &);

} // namespace FuzzingAST
#endif // MUTATORS_HPP