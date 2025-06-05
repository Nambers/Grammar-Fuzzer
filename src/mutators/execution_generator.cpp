#include "log.hpp"
#include "mutators.hpp"
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;

static void collectVar(const std::shared_ptr<ASTData> &ast,
                       const ASTScope &scope, std::vector<std::string> &vars,
                       TypeID type) {
    for (const NodeID &varID : scope.variables) {
        const ASTNode &decl = ast->ast.declarations[varID];
        // type 0 is object (any)
        if (decl.type == type || type == 0 || decl.type == 0) {
            vars.push_back(std::get<std::string>(decl.fields[0].val));
        }
    }
}

static const std::string &
pickRandomVar(std::uniform_int_distribution<int> &pickVar,
              const std::shared_ptr<ASTData> &ast, const ASTScope &scope,
              const ASTScope &parentScope, int parentLastVar) {
    auto pickID = pickVar(rng);
    NodeID varID;
    if (scope.parent != -1 && pickID <= parentLastVar) {
        varID = parentScope.variables[pickID];
    } else {
        pickID -= (parentLastVar + 1);
        varID = scope.variables[pickID];
    }
    return std::get<std::string>(ast->ast.declarations[varID].fields[0].val);
}

static inline const FunctionSignature &
pickRandomFunc(std::uniform_int_distribution<int> &pickFunc,
               const ASTScope &scope, const ASTScope &parentScope,
               int parentLastFunc, const BuiltinContext &ctx,
               std::string &name) {
    auto pickID = pickFunc(rng);
    if (pickID < ctx.builtinsFuncs.size()) {
        auto it = std::next(ctx.builtinsFuncs.begin(), pickID);
        name = it->first;
        return it->second;
    }
    pickID -= ctx.builtinsFuncs.size();
    if (scope.parent != -1 && pickID <= parentLastFunc) {
        auto target = std::next(parentScope.funcSignatures.begin(), pickID);
        name = target->first;
        return target->second;
    }
    auto target =
        std::next(scope.funcSignatures.begin(), pickID - (parentLastFunc + 1));
    name = target->first;
    return target->second;
}

int FuzzingAST::generate_execution_block(const std::shared_ptr<ASTData> &ast,
                                         const ScopeID &scopeID,
                                         const BuiltinContext &ctx) {
    constexpr int NUM_GEN = 10; // TODO
    ASTScope &scope = ast->ast.scopes[scopeID];
    scope.expressions.resize(NUM_GEN, -1);
    std::uniform_int_distribution<int> pickExec(
        static_cast<int>(EXEC_NODE_START), static_cast<int>(EXEC_NODE_END));
    std::uniform_int_distribution<int> pickBinaryOp(0, BINARY_OPS.size() - 1);
    std::uniform_int_distribution<int> pickUnaryOp(0, UNARY_OPS.size() - 1);
    // placeholder as scope
    const ASTScope &parentScope =
        scope.parent != -1 ? ast->ast.scopes[scope.parent] : scope;
    int parentLastVar = -1, parentLastFunc = -1,
        totalVars = scope.variables.size(),
        totalFuncs = scope.funcSignatures.size() + ctx.builtinsFuncs.size();
    if (scope.parent != -1) {
        parentLastVar = parentScope.variables.size() - 1;
        parentLastFunc = parentScope.funcSignatures.size() - 1;
        totalVars += parentScope.variables.size();
        totalFuncs += parentScope.funcSignatures.size();
    }
    std::uniform_int_distribution<int> pickVar(0, totalVars - 1);
    std::uniform_int_distribution<int> pickFunc(0, totalFuncs - 1);
    for (int i = 0; i < NUM_GEN; ++i) {
        MutationState state = MutationState::STATE_REROLL;
        auto &nodeId = scope.expressions[i];
        if (nodeId == -1) {
            nodeId = static_cast<NodeID>(ast->ast.expressions.size());
            ast->ast.expressions.emplace_back();
            scope.expressions[i] = nodeId;
        }
        auto &curr = ast->ast.expressions[nodeId];
        int attempts = 0;
        while (state == MutationState::STATE_REROLL && ++attempts < 5000) {
            state = MutationState::STATE_OK;
            ASTNodeKind pick = static_cast<ASTNodeKind>(pickExec(rng));
            curr.kind = pick;

            switch (pick) {
            case ASTNodeKind::Assign: {
                if (totalVars == 0) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                curr.fields = {{pickRandomVar(pickVar, ast, scope, parentScope,
                                              parentLastVar)},
                               {pickRandomVar(pickVar, ast, scope, parentScope,
                                              parentLastVar)}};
                break;
            }
            case ASTNodeKind::Call: {
                if (totalFuncs == 0 || totalVars == 0) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                curr.fields.resize(1);
                std::string funcName;
                auto &func = pickRandomFunc(pickFunc, scope, parentScope,
                                            parentLastFunc, ctx, funcName);
                curr.fields[0] = {std::move(funcName)};
                // check if params can be satisfied
                std::vector<std::string> params = {};
                if (func.selfType != -1) {
                    // if it's a method, we need to add self
                    collectVar(ast, scope, params, func.selfType);
                    if (params.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    } else {
                        curr.fields.push_back({params[rng() % params.size()]});
                        params.clear();
                    }
                }
                for (const TypeID paramType : func.paramTypes) {
                    if (scope.parent != -1) {
                        // check parent scope first
                        collectVar(ast, parentScope, params, paramType);
                    }
                    collectVar(ast, scope, params, paramType);
                    if (params.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    } else {
                        curr.fields.push_back({params[rng() % params.size()]});
                        params.clear();
                    }
                }
                break;
            }
            case ASTNodeKind::Return: {
                if (scope.retType == -1) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                std::vector<std::string> params = {};
                collectVar(ast, scope, params, scope.retType);
                if (params.empty()) {
                    state = MutationState::STATE_REROLL;
                } else {
                    curr.fields = {{params[rng() % params.size()]}};
                }
                break;
            }
            case ASTNodeKind::BinaryOp: {
                if (totalVars == 0) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                // a = b op c
                curr.fields = {{pickRandomVar(pickVar, ast, scope, parentScope,
                                              parentLastVar)},
                               {pickRandomVar(pickVar, ast, scope, parentScope,
                                              parentLastVar)},
                               {BINARY_OPS[pickBinaryOp(rng)]},
                               {1}};
                break;
            }
            case ASTNodeKind::UnaryOp: {
                if (totalVars == 0) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                // a = op b
                curr.fields = {{pickRandomVar(pickVar, ast, scope, parentScope,
                                              parentLastVar)},
                               {UNARY_OPS[pickUnaryOp(rng)]},
                               {pickRandomVar(pickVar, ast, scope, parentScope,
                                              parentLastVar)}};
                break;
            }
            default:
                PANIC("Unsupported execution node kind: {}",
                      static_cast<int>(pick));
                break;
            }
        }
        if (attempts >= 5000) {
            scope.expressions.resize(i);
            break;
        }
    }
    return 0;
}