#include "mutators.hpp"
#include "driver.hpp"
#include "log.hpp"

using namespace FuzzingAST;

int FuzzingAST::generate_execution(ASTData &ast, BuiltinContext &ctx) {
    // main scope do stream mode
    for (ScopeID sid = 1; sid < ast.ast.scopes.size(); ++sid) {
        // ast.ast.scopes[sid].expressions.clear();
        generate_execution_block(ast, sid, ctx);
    }
    return 0;
}

int FuzzingAST::mutate_declaration(ASTData &astPtr, BuiltinContext &ctx) {
    AST ast = astPtr.ast;
    // avoid to mutate new generated scopes
    const auto s = ast.scopes.size();
    for (ScopeID sid = 0; sid < s; ++sid) {
        AST tmpAST;
        do {
            // TODO rn had to copy once, maye it's able to just copy some parts
            tmpAST = ast;
            tmpAST = mutate_expression(tmpAST, sid, ctx);
        } while (reflectObject(tmpAST, tmpAST.scopes[sid], sid, ctx) != 0);
        ast = std::move(tmpAST);
    }
    astPtr.ast = std::move(ast);
    ctx.update(astPtr);
    return 0;
}
