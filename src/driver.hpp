#ifndef DRIVER_HPP
#define DRIVER_HPP

#include "FuzzSchedulerState.hpp"
#include "ast.hpp"
#include <memory>

namespace FuzzingAST {
int runAST(const AST &, BuiltinContext &, bool echo = false);
int initialize(int *, char ***);
int finalize();
void loadBuiltinsFuncs(BuiltinContext &ctx);
int reflectObject(const AST &ast, ASTScope &scope, BuiltinContext &ctx);
void dummyAST(const std::shared_ptr<ASTData> &data,
              const BuiltinContext &scheduler);
} // namespace FuzzingAST

#endif // DRIVER_HPP
