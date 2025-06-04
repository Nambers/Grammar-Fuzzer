#include "log.hpp"
#include "mutators.hpp"
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;

void collectVar(const std::shared_ptr<ASTData> &ast, const ASTScope &scope,
                std::vector<std::string> &vars, TypeID type) {
    for (const NodeID &varID : scope.variables) {
        const ASTNode &decl = ast->ast.declarations[varID];
        // type 0 is object (any)
        if (decl.type == type || type == 0)
            vars.push_back(std::get<std::string>(decl.fields[0].val));
    }
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
    ASTScope *parentScope;
    int parentLastVar = -1, parentLastFunc = -1,
        totalVars = scope.variables.size(),
        totalFuncs = scope.funcSignatures.size() + ctx.builtinsFuncs.size();
    if (scope.parent != -1) {
        parentScope = &ast->ast.scopes[scope.parent];
        totalVars += parentScope->variables.size();
        parentLastVar = parentScope->variables.size() - 1;
        parentLastFunc = parentScope->funcSignatures.size() - 1;
        totalVars += parentScope->variables.size();
        totalFuncs += parentScope->funcSignatures.size();
    }
    std::uniform_int_distribution<int> pickVar(0, totalVars - 1);
    std::uniform_int_distribution<int> pickFunc(0, totalFuncs - 1);
    for (int i = 0; i < NUM_GEN; ++i) {
        MutationState state = MutationState::STATE_REROLL;
        while (state != MutationState::STATE_OK) {
            state = MutationState::STATE_OK;
            auto nodeId = scope.expressions[i];
            if (nodeId == -1) {
                nodeId = static_cast<NodeID>(ast->ast.expressions.size());
                ast->ast.expressions.emplace_back();
                scope.expressions[i] = nodeId;
            }
            auto &curr = ast->ast.expressions[nodeId];
            ASTNodeKind pick = static_cast<ASTNodeKind>(pickExec(rng));

            curr.kind = pick;

            auto pickRandomVar = [&]() -> std::string {
                auto pickID = pickVar(rng);
                NodeID varID;
                if (parentScope && pickID <= parentLastVar) {
                    varID = parentScope->variables[pickID];
                } else {
                    pickID -= (parentLastVar + 1);
                    varID = scope.variables[pickID];
                }
                return std::get<std::string>(
                    ast->ast.declarations[varID].fields[0].val);
            };

            auto pickRandomFunc =
                [&](std::string &name) -> const FunctionSignature & {
                auto pickID = pickFunc(rng);
                if(pickID < ctx.builtinsFuncs.size()) {
                    auto it = std::next(ctx.builtinsFuncs.begin(), pickID);
                    name = it->first;
                    return it->second;
                }
                pickID -= ctx.builtinsFuncs.size();
                if (parentScope && pickID <= parentLastFunc) {
                    auto target =
                        std::next(parentScope->funcSignatures.begin(), pickID);
                    name = target->first;
                    return target->second;
                }
                auto target = std::next(scope.funcSignatures.begin(),
                                        pickID - (parentLastFunc + 1));
                name = target->first;
                return target->second;
            };

            switch (pick) {
            case ASTNodeKind::Assign: {
                if (totalVars == 0) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                curr.fields = {ASTNodeValue{pickRandomVar()},
                               ASTNodeValue{pickRandomVar()}};
                break;
            }
            case ASTNodeKind::Call: {
                if (totalFuncs == 0 || totalVars == 0) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                curr.fields.resize(1);
                std::string funcName;
                auto &func = pickRandomFunc(funcName);
                curr.fields[0] = ASTNodeValue{std::move(funcName)};
                // check if params can be satisfied
                std::vector<std::string> params;
                params.clear();
                for (const TypeID paramType : func.paramTypes) {
                    if (parentScope) {
                        // check parent scope first
                        collectVar(ast, *parentScope, params, paramType);
                    }
                    collectVar(ast, scope, params, paramType);
                    if (params.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    } else {
                        curr.fields.push_back(ASTNodeValue{
                            std::move(params[std::uniform_int_distribution<int>(
                                0, params.size() - 1)(rng)])});
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
                std::vector<std::string> params;
                params.clear();
                collectVar(ast, scope, params, scope.retType);
                if (params.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                } else {
                    curr.fields = {ASTNodeValue{
                        std::move(params[std::uniform_int_distribution<int>(
                            0, params.size() - 1)(rng)])}};
                }
                break;
            }
            case ASTNodeKind::BinaryOp: {
                if (totalVars == 0) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                // a = b op c
                curr.fields = {ASTNodeValue{pickRandomVar()},
                               ASTNodeValue{pickRandomVar()},
                               ASTNodeValue{BINARY_OPS[pickBinaryOp(rng)]},
                               ASTNodeValue{int64_t(1)}};
                break;
            }
            case ASTNodeKind::UnaryOp: {
                if (totalVars == 0) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                // a = op b
                curr.fields = {ASTNodeValue{pickRandomVar()},
                               ASTNodeValue{UNARY_OPS[pickUnaryOp(rng)]},
                               ASTNodeValue{pickRandomVar()}};
                break;
            }
            default:
                PANIC("Unsupported execution node kind: {}",
                      static_cast<int>(pick));
                break;
            }
        }
    }
    return 0;
}