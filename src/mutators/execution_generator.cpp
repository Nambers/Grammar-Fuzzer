#include "log.hpp"
#include "mutators.hpp"
#include <random>
#include <unordered_set>

using namespace FuzzingAST;

extern std::mt19937 rng;

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

static std::uniform_int_distribution<int> pickBinaryOp(0,
                                                       BINARY_OPS.size() - 1);
static std::uniform_int_distribution<int> pickUnaryOp(0, UNARY_OPS.size() - 1);
static std::uniform_int_distribution<int>
    pickExec(static_cast<int>(EXEC_NODE_START),
             static_cast<int>(EXEC_NODE_END));

int FuzzingAST::generate_execution_block(const std::shared_ptr<ASTData> &ast,
                                         const ScopeID &scopeID,
                                         BuiltinContext &ctx) {

    const int NUM_GEN = ast->ast.scopes[scopeID].declarations.size() * 2;
    ASTScope &scope = ast->ast.scopes[scopeID];
    scope.expressions.resize(NUM_GEN, -1);
    auto &picker = ctx.picker;

    const ASTScope &parentScope =
        scope.parent != -1 ? ast->ast.scopes[scope.parent] : scope;
    int parentLastFunc = -1,
        totalFuncs = scope.funcSignatures.size() + ctx.builtinsFuncs.size();
    if (scope.parent != -1) {
        parentLastFunc = parentScope.funcSignatures.size() - 1;
        totalFuncs += parentScope.funcSignatures.size();
    }
    std::uniform_int_distribution<int> pickFunc(0, totalFuncs - 1);
    std::unordered_set<std::string> globalVars;

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

        while (state == MutationState::STATE_REROLL && ++attempts < 500) {
            state = MutationState::STATE_OK;
            ASTNodeKind pick = static_cast<ASTNodeKind>(pickExec(rng));
            curr.kind = pick;
            curr.fields.clear();

            switch (pick) {

            case ASTNodeKind::Assign: {
                // pick a random type and two vars
                TypeID t = picker.pickRandomType(scopeID);
                auto v1 = picker.pickRandomVar(scopeID, t);
                auto v2 = picker.pickRandomVar(scopeID, t);
                curr.fields = {{v1}, {v2}};
                globalVars.insert(v1);
                break;
            }

            case ASTNodeKind::Call: {
                // pick function name
                if (totalFuncs == 0) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                std::string fname;
                auto &sig = pickRandomFunc(pickFunc, scope, parentScope,
                                           parentLastFunc, ctx, fname);
                curr.fields.push_back({""}); // placeholder for return arg
                curr.fields.push_back({fname});
                // return value var
                if (sig.returnType != -1) {
                    const auto &retVar =
                        picker.pickRandomVar(scopeID, sig.returnType);
                    curr.fields[0] = {retVar};
                    if (!retVar.empty()) {
                        globalVars.insert(retVar);
                    }
                }
                // parameters
                for (auto paramType : sig.paramTypes) {
                    const auto &paramVar =
                        picker.pickRandomVar(scopeID, paramType);
                    if (paramVar.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    }
                    globalVars.insert(paramVar);
                    curr.fields.push_back({paramVar});
                }
                break;
            }

            case ASTNodeKind::Return: {
                if (scope.retType != -1) {
                    const auto &retVar =
                        picker.pickRandomVar(scopeID, scope.retType);
                    if (retVar.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    }
                    curr.fields = {{retVar}};
                } else {
                    state = MutationState::STATE_REROLL;
                }
                break;
            }

            case ASTNodeKind::BinaryOp: {
                TypeID t = picker.pickRandomType(scopeID);
                auto a = picker.pickRandomVar(scopeID, t);
                auto op = pickBinaryOp(rng);
                // pick a var for RHS that matches op type
                // here we simply reuse t for simplicity
                const auto &slice = ctx.ops[op];
                auto b = std::string{};
                if (slice.size() <= t) {
                    b = picker.pickRandomVar(scopeID, slice[t][0]);
                } else {
                    b = picker.pickRandomVar(scopeID, t);
                }
                globalVars.insert(a);
                curr.fields = {{a},
                               {b},
                               {BINARY_OPS[op]},
                               {picker.pickRandomVar(scopeID, t)}};
                break;
            }

            case ASTNodeKind::UnaryOp: {
                TypeID t = picker.pickRandomType(scopeID);
                auto a = picker.pickRandomVar(scopeID, t);
                auto op = UNARY_OPS[pickUnaryOp(rng)];
                curr.fields = {{a}, {op}, {picker.pickRandomVar(scopeID, t)}};
                globalVars.insert(a);
                break;
            }

            default:
                PANIC("Unsupported execution node kind: {}",
                      static_cast<int>(pick));
            }
        }
        if (attempts >= 500) {
            scope.expressions.resize(i);
            break;
        }
    }
    if (scopeID != 0) {
        ast->ast.scopes[scopeID].expressions.insert(
            ast->ast.scopes[scopeID].expressions.begin(),
            ast->ast.expressions.size());
        ASTNode globalVarsNode;
        globalVarsNode.kind = ASTNodeKind::Custom;
        globalVarsNode.fields.reserve(globalVars.size());
        for (const auto &var : globalVars) {
            globalVarsNode.fields.push_back({var});
        }
        ast->ast.expressions.push_back(globalVarsNode);
    }
    return 0;
}
