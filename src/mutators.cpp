#include "ast.hpp"
#include "driver.hpp"
#include "log.hpp"
#include "mutators.hpp"
#include "serialization.hpp"

using namespace FuzzingAST;

extern std::string ast_backup_str;
extern std::string history_backup_str;

constexpr size_t NUM_MUTATE = 4;

int FuzzingAST::generate_execution(ASTData &ast, BuiltinContext &ctx) {
    // main scope do stream mode
    for (ScopeID sid = 1; sid < ast.ast.scopes.size(); ++sid) {
        // ast.ast.scopes[sid].expressions.clear();
        generate_execution_block(ast, sid, ctx);
    }
    return 0;
}

int FuzzingAST::mutate_declaration(ASTData &astPtr, BuiltinContext &ctx) {
    ctx.updateVars(astPtr.ast);

    AST ast = astPtr.ast;
    history_backup_str.clear();
    // avoid to mutate new generated scopes
    const auto s = ast.scopes.size();
    for (ScopeID sid = 0; sid < s; ++sid) {
        // for each scope, mutate certain times
        for (auto i = 0; i < NUM_MUTATE; i++) {
            AST tmpAST;
            do {
                // Pass ast directly by value into mutate_expression — one copy
                // (the by-value parameter) instead of two.
                tmpAST = mutate_expression(ast, sid, ctx);
                ast_backup_str = nlohmann::json(tmpAST).dump();
            } while (reflectObject(tmpAST, tmpAST.scopes[sid], sid, ctx) !=
                     Exe_Result::OK);
            ast = std::move(tmpAST);
        }
    }
    astPtr.ast = std::move(ast);
    ctx.updateVars(astPtr.ast);
    return 0;
}

std::optional<FunctionSignature>
FuzzingAST::lookupMethodSig(TypeID tid, const std::string &name, const AST &ast,
                            const BuiltinContext &ctx, ScopeID startScopeID) {
    const auto &slice = ctx.builtinsProps.find(tid);
    if (slice != ctx.builtinsProps.end()) {
        auto ret = getPropByName(name, slice->second, true, startScopeID);
        if (ret)
            return ret->extra.get<FunctionSignature>();
    }

    const auto &slice2 = ast.classProps.find(tid);
    if (slice2 != ast.classProps.end()) {
        auto it = getPropByName(name, slice2->second, true, startScopeID);
        if (it)
            return it->extra.get<FunctionSignature>();
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

std::string FuzzingAST::buildFunctionCallG(
    const std::vector<FuzzingAST::TypeID> &paramTypes, ScopeID sid,
    const AST &ast, BuiltinContext &ctx,
    std::unordered_set<std::string> &globalVars) {
    if (paramTypes.empty())
        return "()";

    auto callExpr = std::string("(");
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        if (i > 0)
            callExpr += ", ";
        const auto valueType = ctx.respectType() ? paramTypes[i] : 0;
        const auto varNameKey =
            ctx.pickRandomVar(sid, valueType, ctx.pickValueKind(), ast.scopes);
        if (varNameKey.empty()) {
            return {};
        }
        const auto &varProp = unfoldKey(varNameKey, ast, ctx);
        if (varProp.isCallable &&
            varProp.extra.get<FunctionSignature>().returnType ==
                paramTypes[i]) {
            auto ret = buildFunctionCallG(
                varProp.extra.get<FunctionSignature>().paramTypes, sid, ast,
                ctx, globalVars);
            if (ret.empty())
                return {};
            callExpr += varProp.name + ret;
        } else {
            callExpr += varProp.name;
            insertGlobalVar(varProp, globalVars);
        }
    }
    return callExpr + ")";
}

void FuzzingAST::addFunction(const PropInfo &picked, AST &ast, ScopeID funSid,
                             NodeID funNodeID) {
    const auto &funcSig = picked.extra.get<FunctionSignature>();
    ASTNode &fun = ast.declarations[funNodeID];
    ASTScope &funScope = ast.scopes[funSid];

    fun.kind = ASTNodeKind::Function;
    fun.scope = funSid;
    fun.fields.emplace_back(picked.name);
    fun.fields.emplace_back(funcSig.returnType);

    std::string arg = "arg_a";
    // the first arg equiv to self
    if (funcSig.selfType != -1) {
        ast.declarations[funNodeID].fields.emplace_back(arg);

        funScope.variables.push_back(ast.variables.size());
        ast.variables.emplace_back(
            NO_MODULE, ast.classProps[NOT_UNDER_CLASS].size(), NOT_UNDER_CLASS);
        ast.classProps[NOT_UNDER_CLASS].emplace_back(funcSig.selfType, funSid,
                                                     arg, false, false, true);
        bumpIdentifier(arg);
        ast.declarations[funNodeID].fields.emplace_back(funcSig.selfType);
    }
    for (TypeID pt : funcSig.paramTypes) {
        ast.declarations[funNodeID].fields.emplace_back(arg);

        funScope.variables.push_back(ast.variables.size());
        ast.variables.emplace_back(
            NO_MODULE, ast.classProps[NOT_UNDER_CLASS].size(), NOT_UNDER_CLASS);
        ast.classProps[NOT_UNDER_CLASS].emplace_back(pt, funSid, arg, false,
                                                     false, true);
        bumpIdentifier(arg);
        ast.declarations[funNodeID].fields.emplace_back(pt);
    }
    funScope.paramCnt = funScope.variables.size();
}