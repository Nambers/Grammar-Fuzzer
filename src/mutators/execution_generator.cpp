#include "log.hpp"
#include "mutators.hpp"
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;

static std::uniform_int_distribution<int> pickBinaryOp(0,
                                                       BINARY_OPS.size() - 1);
static std::uniform_int_distribution<int> pickUnaryOp(0, UNARY_OPS.size() - 1);

constexpr static std::array PICK_EXEC_WEIGHT = {
    10, // GetProp
    7,  // SetProp
    20, // Call
    2,  // Return
    3,  // BinaryOp
    2,  // UnaryOp
    7,  // NewInstance
};

static_assert(PICK_EXEC_WEIGHT.size() ==
                  (static_cast<int>(EXEC_NODE_END) -
                   static_cast<int>(EXEC_NODE_START) + 1),
              "PICK_EXEC_WEIGHT size mismatch with EXEC_NODE range");

static std::discrete_distribution<int> pickExec(PICK_EXEC_WEIGHT.begin(),
                                                PICK_EXEC_WEIGHT.end());

int FuzzingAST::generate_line(ASTNode &node, ASTData &ast, BuiltinContext &ctx,
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
            const auto &v1 = unfoldKey(v1Key, ast.ast, ctx);
            auto v1Name = v1.name;
            auto v2Name = unfoldKey(v2Key, ast.ast, ctx).name;
            if (v1Key.parentType != -1) {
                const auto v1pKey =
                    ctx.pickRandomVar(scopeID, v1Key.parentType, false);
                if (v1pKey.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                const auto &v1p = unfoldKey(v1pKey, ast.ast, ctx);
                v1Name = v1p.name + '.' + v1Name;
                // globalVars.insert(v1pName);
                insertGlobalVar(v1p, globalVars);
            } else
                // globalVars.insert(v1);
                insertGlobalVar(v1, globalVars);
            if (v2Key.parentType != -1) {
                const auto v2pKey = ctx.pickRandomVar(scopeID, v2Key.parentType,
                                                      ctx.pickConst());
                if (v2pKey.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                v2Name = unfoldKey(v2pKey, ast.ast, ctx).name + '.' + v2Name;
            }

            curr.fields = {{v1Name}, {v2Name}};

            break;
        }

        case ASTNodeKind::NewInstance: {
            TypeID tid = ctx.pickRandomType(scopeID);
            if (tid == 0) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &typeName = getTypeName(tid, ast.ast, ctx);
            if (typeName.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            // add variables
            auto sig = lookupMethodSig(tid, "__init__", ast.ast, ctx, scopeID);
            if (!sig) {
                sig = lookupMethodSig(tid, "__new__", ast.ast, ctx, scopeID);
            }
            curr.kind = ASTNodeKind::Call;
            // get ret variable name
            curr.fields.emplace_back(ast.ast.nameCnt);
            curr.fields.emplace_back(typeName);
            if (sig) {
                for (auto i = 0; i < sig->paramTypes.size(); ++i) {
                    const auto varKey =
                        ctx.pickRandomVar(scopeID, sig->paramTypes[i], false);
                    if (varKey.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    }
                    const auto &var = unfoldKey(varKey, ast.ast, ctx);
                    if (varKey.parentType != -1) {
                        // if it's a property, get parent variable
                        const auto parentVarKey = ctx.pickRandomVar(
                            scopeID, varKey.parentType, false);
                        if (parentVarKey.empty()) {
                            state = MutationState::STATE_REROLL;
                            break;
                        }
                        const auto &parentVar =
                            unfoldKey(parentVarKey, ast.ast, ctx);
                        curr.fields.emplace_back(parentVar.name + '.' +
                                                 var.name);
                        // globalVars.insert(parentVar.name);
                        insertGlobalVar(parentVar, globalVars);
                    } else {
                        curr.fields.emplace_back(var.name);
                        // globalVars.insert(var.name);
                        insertGlobalVar(var, globalVars);
                    }
                }
            }
            globalVars.insert(ast.ast.nameCnt);
            bumpIdentifier(ast.ast.nameCnt);
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
                const auto &retVarKey =
                    ctx.pickRandomVar(scopeID, sig.returnType, false);
                if (!retVarKey.empty()) {
                    const auto &retVar = unfoldKey(retVarKey, ast.ast, ctx);
                    curr.fields[0] = {retVar.name};
                    // globalVars.insert(retVar);
                    insertGlobalVar(retVar, globalVars);
                } else
                    curr.fields[0] = {""};
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
                    const auto &selfVar = unfoldKey(selfVarKey, ast.ast, ctx);
                    // globalVars.insert(selfVar);
                    insertGlobalVar(selfVar, globalVars);
                    // xxx.yyy(...)
                    curr.fields[1] = {selfVar.name + '.' + fname};
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
                const auto &paramVar = unfoldKey(paramVarKey, ast.ast, ctx);
                auto pName = paramVar.name;
                if (paramVarKey.parentType != -1) {
                    // TODO false workaround
                    const auto parentVarKey = ctx.pickRandomVar(
                        scopeID, paramVarKey.parentType, false);
                    if (parentVarKey.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    }
                    const auto &p = unfoldKey(parentVarKey, ast.ast, ctx);
                    pName = p.name + '.' + pName;
                    insertGlobalVar(p, globalVars);
                } else
                    insertGlobalVar(paramVar, globalVars);
                curr.fields.emplace_back(pName);
            }
            break;
        }

        case ASTNodeKind::Return: {
            if (scope.retType != -1 && scope.retNodeID != -1) {
                const auto retVarKey =
                    ctx.pickRandomVar(scopeID, scope.retType, ctx.pickConst());
                if (retVarKey.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                const auto &retVar = unfoldKey(retVarKey, ast.ast, ctx).name;
                curr.fields = {{retVar}};
            } else
                state = MutationState::STATE_REROLL;
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
            // if (!a.isConst)
            //     globalVars.insert(a.name);
            insertGlobalVar(a, globalVars);
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
            // if (!a.isConst)
            //     globalVars.insert(a.name);
            insertGlobalVar(a, globalVars);
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

    // const int NUM_GEN = ast.ast.scopes[scopeID].declarations.size() * 2;
    constexpr int NUM_GEN = 70; // TODO
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
        if (node.kind == ASTNodeKind::Return) {
            scope.retNodeID = nodeId;
            --i;
        }
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
