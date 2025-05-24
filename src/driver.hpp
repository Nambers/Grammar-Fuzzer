#ifndef DRIVER_HPP
#define DRIVER_HPP

#include "ast.hpp"

namespace FuzzingAST {
int runAST(const AST &, bool echo = false);
int initialize(int *, char ***);
void reflectObject();
} // namespace FuzzingAST

#endif // DRIVER_HPP
