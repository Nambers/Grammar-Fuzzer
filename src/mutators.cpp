#include "mutators.hpp"
#include "driver.hpp"
#include <random>
#include "log.hpp"

using namespace FuzzingAST;

int FuzzingAST::generate_execution(const std::shared_ptr<ASTData> &ast,
                                   const BuiltinContext &ctx) {
    for (ScopeID sid = 0; sid < ast->ast.scopes.size(); ++sid) {
        ast->ast.scopes[sid].expressions.clear();
        if (!ast->ast.scopes[sid].declarations.empty()) {
            generate_execution_block(ast, sid, ctx);
        }
    }
    return 0;
}

int FuzzingAST::mutate_declaration(const std::shared_ptr<ASTData> &ast,
                                   const BuiltinContext &ctx) {
    for (ScopeID sid = 0; sid < ast->ast.scopes.size(); ++sid) {
        ASTScope scope;
        do {
            scope = ast->ast.scopes[sid];
            mutate_expression(ast, sid, ctx);
        } while (reflectObject(ast->ast, scope, ctx) != 0);
        ast->ast.scopes[sid] = scope;
    }
    return 0;
}
