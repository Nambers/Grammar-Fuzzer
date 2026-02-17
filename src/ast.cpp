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

TypeID FuzzingAST::resolveType(const std::string &fullname,
                               const BuiltinContext &ctx, const AST &ast,
                               ScopeID sid) {
    if (fullname.empty()) {
        return -1;
    }
    auto matches = [&](const std::string &t) {
        if (fullname == t)
            return true;
        // TODO may not be neccessary
        if (t.size() > fullname.size())
            return t.ends_with(fullname);

        return false;
    };

    for (size_t i = 0; i < ctx.types.size(); ++i) {
        if (matches(ctx.types[i])) {
            return i;
        }
    }

    while (sid != -1) {
        const ASTScope &scope = ast.scopes[sid];
        for (size_t j = 0; j < scope.types.size(); ++j) {
            if (matches(scope.types[j])) {
                return j + (sid + 1) * SCOPE_MAX_TYPE;
            }
        }
        sid = scope.parent;
    }
    return 0; // object
}

std::optional<PropInfo>
FuzzingAST::getPropByName(const std::string &name,
                          const std::vector<PropInfo> &slice, bool isCallable,
                          ScopeID sid) {
    auto it = std::find_if(slice.begin(), slice.end(),
                           [&name, isCallable, sid](const PropInfo &prop) {
                               return prop.name == name && prop.scope <= sid &&
                                      (isCallable == prop.isCallable);
                           });
    if (it != slice.end()) {
        return *it;
    }
    return std::nullopt;
}

