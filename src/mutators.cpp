#include "mutators.hpp"
#include "driver.hpp"
#include "log.hpp"
#include "serialization.hpp"

using namespace FuzzingAST;

extern std::string data_backup;
extern std::string data_backup2;

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
            // TODO rn had to copy once, maybe it's able to just copy some parts
            tmpAST = ast;
            tmpAST = mutate_expression(tmpAST, sid, ctx);
            data_backup = nlohmann::json(tmpAST).dump();
            data_backup2.clear();
        } while (reflectObject(tmpAST, tmpAST.scopes[sid], sid, ctx) != 0);
        ast = std::move(tmpAST);
    }
    astPtr.ast = std::move(ast);
    ctx.update(astPtr.ast);
    return 0;
}

std::optional<FunctionSignature>
FuzzingAST::lookupMethodSig(TypeID tid, const std::string &name, const AST &ast,
                            const BuiltinContext &ctx, ScopeID startScopeID) {
    const auto &slice = ctx.builtinsProps.find(tid);
    if (slice != ctx.builtinsProps.end()) {
        auto ret = getPropByName(name, slice->second, true, startScopeID);
        if (ret)
            return ret->funcSig;
    }

    const auto &slice2 = ast.classProps.find(tid);
    if (slice2 != ast.classProps.end()) {
        auto it = getPropByName(name, slice2->second, true, startScopeID);
        if (it)
            return it->funcSig;
    }

    return std::nullopt;
}

bool FuzzingAST::bumpIdentifier(std::string &id) {
    if (id.empty()) {
        id = "a";
        return true;
    }

    // [A‑Za‑z][A‑Za‑z0‑9]*  ；
    // assert(std::isalpha(static_cast<unsigned char>(id.front())));

    bool carry = true;
    for (int i = static_cast<int>(id.size()) - 1; i >= 0 && carry; --i) {
        char &ch = id[i];

        if (ch >= '0' && ch <= '8') {
            ch++;
            carry = false;
        } else if (ch == '9') {
            ch = (i == 0 ? 'a' : '0');
        } else if (ch >= 'A' && ch <= 'Y') {
            ch++;
            carry = false;
        } else if (ch == 'Z') {
            ch = (i == 0 ? 'a' : '0');
        } else if (ch >= 'a' && ch <= 'y') {
            ch++;
            carry = false;
        } else if (ch == 'z') {
            ch = (i == 0 ? 'a' : '0');
        } else {
            PANIC("illegal character in identifier");
        }
    }

    if (carry) {
        id.insert(id.begin(), 'a');
        return true;
    }
    return false;
}
