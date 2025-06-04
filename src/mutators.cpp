#include "mutators.hpp"
#include "driver.hpp"
#include <random>

using namespace FuzzingAST;

int FuzzingAST::generate_execution(const std::shared_ptr<ASTData> &ast) {
    for (ScopeID sid = 0; sid < ast->ast.scopes.size(); ++sid) {
        if (ast->ast.scopes[sid].expressions.empty()) {
            generate_execution_block(ast, sid);
        }
    }
    return 0;
}

int FuzzingAST::mutate_declaration(const std::shared_ptr<ASTData> &ast, const BuiltinContext &ctx) {
    for (ScopeID sid = 0; sid < ast->ast.scopes.size(); ++sid) {
        ASTScope &scope = ast->ast.scopes[sid];
        mutate_expression(ast, sid, ctx);
        reflectObject(ast->ast, scope, ctx);
    }
    return 0;
}
