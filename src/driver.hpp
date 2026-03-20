#ifndef DRIVER_HPP
#define DRIVER_HPP

#include "FuzzSchedulerState.hpp"
#include "ast.hpp"
#include <memory>
#include <unordered_set>

namespace FuzzingAST {

enum class Exe_Result { OK, ERR, TIMEOUT };

Exe_Result runAST(AST &, BuiltinContext &,
                  std::unique_ptr<ExecutionContext> &excCtx, bool echo = false);
Exe_Result runLines(const std::vector<ASTNode> &nodes, AST &,
                    BuiltinContext &ctx,
                    std::unique_ptr<ExecutionContext> &excCtx,
                    bool echo = false);
Exe_Result runLine(const ASTNode &node, AST &, BuiltinContext &ctx,
                   std::unique_ptr<ExecutionContext> &excCtx,
                   bool echo = false);
int initialize(int *, char ***);
int finalize();
void loadBuiltinsFuncs(BuiltinContext &ctx);
Exe_Result reflectObject(AST &ast, ASTScope &scope, const ScopeID sid,
                         BuiltinContext &ctx);
void dummyAST(ASTData &data, const BuiltinContext &scheduler);
std::unique_ptr<ExecutionContext> getInitExecutionContext();
void updateTypes(const std::unordered_set<std::string> &globalVars,
                 ASTData &ast, BuiltinContext &ctx,
                 std::unique_ptr<ExecutionContext> &excCtx);
} // namespace FuzzingAST

template <>
struct std::formatter<FuzzingAST::Exe_Result, char>
    : std::formatter<std::string_view, char> {
    auto format(const FuzzingAST::Exe_Result &result,
                std::format_context &ctx) const {
        std::string_view text = "(unknown)";
        switch (result) {
        case FuzzingAST::Exe_Result::OK:
            text = "(ok)";
            break;
        case FuzzingAST::Exe_Result::ERR:
            text = "(error)";
            break;
        case FuzzingAST::Exe_Result::TIMEOUT:
            text = "(timeout)";
            break;
        }
        return std::formatter<std::string_view, char>::format(text, ctx);
    }
};

#endif // DRIVER_HPP
