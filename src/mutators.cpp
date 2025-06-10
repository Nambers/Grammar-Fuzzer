#include "mutators.hpp"
#include "driver.hpp"
#include "log.hpp"

using namespace FuzzingAST;

int FuzzingAST::generate_execution(const std::shared_ptr<ASTData> &ast,
                                BuiltinContext &ctx) {
    for (ScopeID sid = 0; sid < ast->ast.scopes.size(); ++sid) {
        ast->ast.scopes[sid].expressions.clear();
        generate_execution_block(ast, sid, ctx);
    }
    return 0;
}

int FuzzingAST::mutate_declaration(const std::shared_ptr<ASTData> &ast,
                                BuiltinContext &ctx) {
    for (ScopeID sid = 0; sid < ast->ast.scopes.size(); ++sid) {
        ASTScope scope;
        do {
            scope = ast->ast.scopes[sid];
            mutate_expression(ast, sid, scope, ctx);
        } while (reflectObject(ast->ast, scope, ctx) != 0);
        ast->ast.scopes[sid] = scope;
    }
    ctx.picker.update(ast);
    return 0;
}
