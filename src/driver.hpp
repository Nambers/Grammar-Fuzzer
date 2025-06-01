#ifndef DRIVER_HPP
#define DRIVER_HPP

#include "ast.hpp"
#include <memory>

namespace FuzzingAST {
int runAST(const AST &, bool echo = false);
int initialize(int *, char ***);
int finalize();
void loadBuiltinsFuncs(
	std::unordered_map<std::string, FunctionSignature> &funcSignatures,
	std::vector<std::string> &types);
void reflectObject(const AST &ast, ASTScope &scope);
} // namespace FuzzingAST

#endif // DRIVER_HPP
