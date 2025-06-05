#include "ast.hpp"
#include "log.hpp"

using namespace FuzzingAST;

static const std::string objStr = "object";
static const std::string noneStr = "None";

const std::string &FuzzingAST::getTypeName(TypeID tid, const AST &ast,
                                           const BuiltinContext &ctx) {
    if (tid == 0)
        return objStr;
    if (tid == -1)
        return noneStr;
    int sid = tid / SCOPE_MAX_TYPE;
    if (sid == 0) {
        return ctx.types[tid];
    } else {
        return ast.scopes[sid - 1].types[tid % SCOPE_MAX_TYPE];
    }
}
