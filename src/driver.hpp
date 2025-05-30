#ifndef DRIVER_HPP
#define DRIVER_HPP

#include "ast.hpp"

namespace FuzzingAST {
int runAST(const AST &, bool echo = false);
int initialize(int *, char ***);
int finalize();
void reflectObject(ASTData &data, ScopeID sid);
void loadBuiltinsFuncs(std::unordered_map<std::string, FunctionSignature> &funcSignatures);
} // namespace FuzzingAST

#endif // DRIVER_HPP
