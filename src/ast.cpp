#include "ast.hpp"

using namespace FuzzingAST;

const std::string &FuzzingAST::getTypeName(TypeID tid, const AST &ast,
                                           const BuiltinContext &ctx) {
    int sid = tid / SCOPE_MAX_TYPE;
    if (sid == 0) {
        return ctx.types[tid % SCOPE_MAX_TYPE];
    } else {
        return ast.scopes[sid - 1].types[tid % SCOPE_MAX_TYPE];
    }
}
