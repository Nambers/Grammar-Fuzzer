#ifndef DRIVER_HPP
#define DRIVER_HPP

#include "FuzzSchedulerState.hpp"
#include "ast.hpp"
#include <memory>
#include <unordered_set>

namespace FuzzingAST {
int runAST(const AST &, BuiltinContext &,
           std::unique_ptr<ExecutionContext> &excCtx, bool echo = false);
int runLines(const std::vector<ASTNode> &nodes, const AST &,
             BuiltinContext &ctx, std::unique_ptr<ExecutionContext> &excCtx,
             bool echo = false);
int runLine(const ASTNode &node, const AST &, BuiltinContext &ctx,
            std::unique_ptr<ExecutionContext> &excCtx, bool echo = false);
int initialize(int *, char ***);
int finalize();
void loadBuiltinsFuncs(BuiltinContext &ctx);
int reflectObject(AST &ast, ASTScope &scope, const ScopeID sid, BuiltinContext &ctx);
void dummyAST(ASTData &data, const BuiltinContext &scheduler);
std::unique_ptr<ExecutionContext> getInitExecutionContext();
void updateTypes(const std::unordered_set<std::string> &globalVars, ASTData &ast,
                 BuiltinContext &ctx, std::unique_ptr<ExecutionContext> &excCtx);
} // namespace FuzzingAST

#endif // DRIVER_HPP
