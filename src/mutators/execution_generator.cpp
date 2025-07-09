#include "log.hpp"
#include "mutators.hpp"
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;

static std::uniform_int_distribution<int> pickBinaryOp(0,
                                                       BINARY_OPS.size() - 1);
static std::uniform_int_distribution<int> pickUnaryOp(0, UNARY_OPS.size() - 1);

constexpr static std::array PICK_EXEC_WEIGHT = {
    2, // GetProp
    2, // SetProp
    3, // Call
    1, // Return
    1, // BinaryOp
    1  // UnaryOp
};

static_assert(PICK_EXEC_WEIGHT.size() ==
                  (static_cast<int>(EXEC_NODE_END) -
                   static_cast<int>(EXEC_NODE_START) + 1),
              "PICK_EXEC_WEIGHT size mismatch with EXEC_NODE range");

static std::discrete_distribution<int> pickExec(PICK_EXEC_WEIGHT.begin(),
                                                PICK_EXEC_WEIGHT.end());

int FuzzingAST::generate_line(ASTNode &node, const ASTData &ast,
                              BuiltinContext &ctx,
                              std::unordered_set<std::string> &globalVars,
                              ScopeID scopeID, const ASTScope &scope) {
    MutationState state = MutationState::STATE_REROLL;
    auto &curr = node;
    int attempts = 0;

    while (state == MutationState::STATE_REROLL && ++attempts < 500) {
        state = MutationState::STATE_OK;
        ASTNodeKind pick = static_cast<ASTNodeKind>(
            static_cast<int>(EXEC_NODE_START) + pickExec(rng));
        curr.kind = pick;
        curr.fields.clear();

        switch (pick) {

        case ASTNodeKind::GetProp:
            [[fallthrough]];

        case ASTNodeKind::SetProp: {
            TypeID t = ctx.pickRandomType(scopeID);
            const auto v1Key = ctx.pickRandomVar(scopeID, t, false);
            if (v1Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto v2Key = ctx.pickRandomVar(scopeID, t, ctx.pickConst());
            if (v1Key == v2Key || v2Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            if (v1Key.parentType != -1) {
                // need to find an instance
                // TODO
                state = MutationState::STATE_REROLL;
                break;
            }
            if (v2Key.parentType != -1) {
                // need to find an instance
                // TODO
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &v1 = unfoldKey(v1Key, ast.ast, ctx);
            const auto &v2 = unfoldKey(v2Key, ast.ast, ctx);
            if (v1.isConst || v2.isConst) {
                // cannot set property of const variable
                state = MutationState::STATE_REROLL;
                break;
            }

            curr.fields = {{v1.name}, {v2.name}};
            if (!v1.isConst)
                globalVars.insert(v1.name);
            break;
        }

        case ASTNodeKind::Call: {
            // pick function name
            const auto funcKey = ctx.pickRandomFunc(scopeID);
            if (funcKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &func = unfoldKey(funcKey, ast.ast, ctx);
            const auto &sig = func.funcSig;
            const auto &fname = func.name;
            curr.fields.emplace_back(""); // placeholder for return arg
            curr.fields.emplace_back(fname);
            // return value var
            if (sig.returnType != -1) {
                const auto retVarKey =
                    ctx.pickRandomVar(scopeID, sig.returnType, false);
                const auto &retVar =
                    retVarKey.empty() ? ""
                                      : unfoldKey(retVarKey, ast.ast, ctx).name;
                curr.fields[0] = {retVar};
                if (!retVar.empty()) {
                    globalVars.insert(retVar);
                }
            }
            if (funcKey.parentType != -1) {
                if (sig.selfType != -1) {
                    // if it's a method, add self as first parameter
                    const auto selfVarKey =
                        ctx.pickRandomVar(scopeID, sig.selfType, false);
                    if (selfVarKey.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    }
                    const auto &selfVar =
                        unfoldKey(selfVarKey, ast.ast, ctx).name;
                    globalVars.insert(selfVar);
                    // xxx.yyy(...)
                    curr.fields[1] = {selfVar + '.' + fname};
                    // or static usage: yyy(xxx, ...)
                    // curr.fields.push_back({selfVar});
                } else {
                    // static method
                    curr.fields[1] = {
                        getTypeName(funcKey.parentType, ast.ast, ctx) + '.' +
                        fname};
                }
            }

            // parameters
            for (auto i = 0; i < sig.paramTypes.size(); ++i) {
                auto paramType = sig.paramTypes[i];
                const auto paramVarKey =
                    ctx.pickRandomVar(scopeID, paramType, ctx.pickConst());
                if (paramVarKey.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                if (paramVarKey.parentType != -1) {
                    // need to find an instance
                    // TODO
                    --i;
                    continue;
                }
                const auto &paramVar = unfoldKey(paramVarKey, ast.ast, ctx);
                if (!paramVar.isConst)
                    globalVars.insert(paramVar.name);
                curr.fields.emplace_back(paramVar.name);
            }
            break;
        }

        case ASTNodeKind::Return: {
            if (scope.retType != -1) {
                const auto retVarKey =
                    ctx.pickRandomVar(scopeID, scope.retType, ctx.pickConst());
                if (retVarKey.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                const auto &retVar = unfoldKey(retVarKey, ast.ast, ctx).name;
                curr.fields = {{retVar}};
            } else {
                state = MutationState::STATE_REROLL;
            }
            break;
        }

        case ASTNodeKind::BinaryOp: {
            auto op = pickBinaryOp(rng);
            const auto &slice = ctx.ops[op];
            if (slice.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            TypeID t1 = rng() % slice.size();
            if (slice[t1].empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            TypeID t2 = slice[t1][rng() % slice[t1].size()];

            const auto aKey = ctx.pickRandomVar(scopeID, t1, false);
            if (aKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto a2Key = ctx.pickRandomVar(scopeID, t2, ctx.pickConst());
            if (a2Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &a = unfoldKey(aKey, ast.ast, ctx);
            const auto &a2 = unfoldKey(a2Key, ast.ast, ctx).name;
            const auto a3Key = ctx.pickRandomVar(scopeID, t2, ctx.pickConst());
            if (a3Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &a3 = unfoldKey(a3Key, ast.ast, ctx).name;
            if (!a.isConst)
                globalVars.insert(a.name);
            curr.fields = {{a.name}, {a3}, {BINARY_OPS[op]}, {a2}};
            break;
        }

        case ASTNodeKind::UnaryOp: {
            auto op = pickUnaryOp(rng);
            const auto &slice = ctx.unaryOps[op];
            if (slice.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            TypeID t = slice[rng() % slice.size()];
            const auto aKey = ctx.pickRandomVar(scopeID, t, false);
            if (aKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &a = unfoldKey(aKey, ast.ast, ctx);
            const auto a2Key = ctx.pickRandomVar(scopeID, t, ctx.pickConst());
            if (a2Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &a2 = unfoldKey(a2Key, ast.ast, ctx).name;
            curr.fields = {{a.name}, {UNARY_OPS[op]}, {a2}};
            if (!a.isConst)
                globalVars.insert(a.name);
            break;
        }

        default:
            PANIC("Unsupported execution node kind: {}",
                  static_cast<int>(pick));
        }
    }
    if (attempts >= 500) {
        return 1;
    }
    return 0;
}

int FuzzingAST::generate_execution_block(ASTData &ast, const ScopeID &scopeID,
                                         BuiltinContext &ctx) {

    const int NUM_GEN = ast.ast.scopes[scopeID].declarations.size() * 2;
    ASTScope &scope = ast.ast.scopes[scopeID];
    scope.expressions.resize(NUM_GEN, -1);

    std::unordered_set<std::string> globalVars;

    for (int i = 0; i < NUM_GEN; ++i) {
        auto &nodeId = scope.expressions[i];
        nodeId = ast.ast.expressions.size();
        ast.ast.expressions.emplace_back();
        const auto oldExprCount = ast.ast.expressions.size();
        ASTNode node;
        if (generate_line(node, ast, ctx, globalVars, scopeID, scope) != 0) {
            scope.expressions.resize(i);
            ast.ast.expressions.resize(oldExprCount);
            break;
        }
        ast.ast.expressions[nodeId] = std::move(node);
    }
    if (scopeID != 0 && !globalVars.empty()) {
        // filter out if varName is in scope variables
        for (auto it = globalVars.begin(); it != globalVars.end();) {
            bool found = false;
            for (VarID varID : scope.variables) {
                const auto &varInfoKey = ast.ast.variables.at(varID);
                const auto &varInfo = unfoldKey(varInfoKey, ast.ast, ctx);
                if (varInfo.name == *it) {
                    it = globalVars.erase(it);
                    found = true;
                    break;
                }
            }
            if (!found) {
                ++it;
            }
        }
        if (!globalVars.empty()) {
            ASTNode globalVarsNode;
            globalVarsNode.kind = ASTNodeKind::GlobalRef;
            globalVarsNode.fields.reserve(globalVars.size());
            for (const auto &var : globalVars) {
                globalVarsNode.fields.emplace_back(var);
            }
            scope.expressions.insert(scope.expressions.begin(),
                                     ast.ast.expressions.size());
            ast.ast.expressions.push_back(globalVarsNode);
        }
    }
    return 0;
}
