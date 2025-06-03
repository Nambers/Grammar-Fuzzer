#ifndef DRIVER_HPP
#define DRIVER_HPP

#include "ast.hpp"
#include "FuzzSchedulerState.hpp"
#include <memory>

namespace FuzzingAST {
int runAST(const AST &, bool echo = false);
int initialize(int *, char ***);
int finalize();
void loadBuiltinsFuncs(
	std::unordered_map<std::string, FunctionSignature> &funcSignatures,
	std::vector<std::string> &types);
void reflectObject(const AST &ast, ASTScope &scope);
void dummyAST(const std::shared_ptr<ASTData> &data, const FuzzSchedulerState &scheduler);
} // namespace FuzzingAST

#endif // DRIVER_HPP
