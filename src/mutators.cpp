#include "mutators.hpp"
#include "driver.hpp"
#include "log.hpp"

using namespace FuzzingAST;

int FuzzingAST::generate_execution(const std::shared_ptr<ASTData> &ast,
                                   BuiltinContext &ctx) {
    // main scope do stream mode
    for (ScopeID sid = 1; sid < ast->ast.scopes.size(); ++sid) {
        ast->ast.scopes[sid].expressions.clear();
        generate_execution_block(ast, sid, ctx);
    }
    return 0;
}

int FuzzingAST::mutate_declaration(std::shared_ptr<ASTData> &astPtr,
                                   BuiltinContext &ctx) {
    AST ast = astPtr->ast;
    for (ScopeID sid = 0; sid < ast.scopes.size(); ++sid) {
        AST tmpAST;
        do {
            // TODO rn had to copy once, maye it's able to just copy some parts
            tmpAST = ast;
            tmpAST = mutate_expression(tmpAST, sid, ctx);
        } while (reflectObject(tmpAST, tmpAST.scopes[sid], ctx) != 0);
        ast = tmpAST;
    }
    astPtr->ast = ast;
    ctx.update(astPtr);
    return 0;
}
