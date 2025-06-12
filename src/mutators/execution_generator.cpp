#include "log.hpp"
#include "mutators.hpp"
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;

static std::uniform_int_distribution<int> pickBinaryOp(0,
                                                       BINARY_OPS.size() - 1);
static std::uniform_int_distribution<int> pickUnaryOp(0, UNARY_OPS.size() - 1);
static std::uniform_int_distribution<int>
    pickExec(static_cast<int>(EXEC_NODE_START),
             static_cast<int>(EXEC_NODE_END));

int FuzzingAST::generate_line(ASTNode &node, const ASTData &ast,
                              BuiltinContext &ctx,
                              std::unordered_set<std::string> &globalVars,
                              ScopeID scopeID, const ASTScope &scope) {
    MutationState state = MutationState::STATE_REROLL;
    auto &curr = node;
    int attempts = 0;

    while (state == MutationState::STATE_REROLL && ++attempts < 500) {
        state = MutationState::STATE_OK;
        ASTNodeKind pick = static_cast<ASTNodeKind>(pickExec(rng));
        curr.kind = pick;
        curr.fields.clear();

        switch (pick) {

        case ASTNodeKind::Assign: {
            // pick a random type and two vars
            TypeID t = ctx.pickRandomType(scopeID);
            auto v1 = ctx.pickRandomVar(scopeID, t);
            if (v1.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            auto v2 = ctx.pickRandomVar(scopeID, t);
            if (v1 == v2) {
                state = MutationState::STATE_REROLL;
                break;
            }
            curr.fields = {{v1}, {v2}};
            globalVars.insert(v1);
            break;
        }

        case ASTNodeKind::Call: {
            // pick function name
            const auto &[fname, sig] = ctx.pickRandomFunc(ast, scopeID);
            curr.fields.push_back({""}); // placeholder for return arg
            curr.fields.push_back({fname});
            // return value var
            if (sig.returnType != -1) {
                const auto &retVar = ctx.pickRandomVar(scopeID, sig.returnType);
                curr.fields[0] = {retVar};
                if (!retVar.empty()) {
                    globalVars.insert(retVar);
                }
            }
            if (sig.selfType != -1) {
                // if it's a method, add self as first parameter
                const auto &selfVar = ctx.pickRandomVar(scopeID, sig.selfType);
                if (selfVar.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                globalVars.insert(selfVar);
                curr.fields.push_back({selfVar});
            }
            // parameters
            for (auto paramType : sig.paramTypes) {
                const auto &paramVar = ctx.pickRandomVar(scopeID, paramType);
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
                const auto &retVar = ctx.pickRandomVar(scopeID, scope.retType);
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

            auto a = ctx.pickRandomVar(scopeID, t1);
            globalVars.insert(a);
            curr.fields = {{a},
                           {ctx.pickRandomVar(scopeID, t1)},
                           {BINARY_OPS[op]},
                           {ctx.pickRandomVar(scopeID, t2)}};
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
            auto a = ctx.pickRandomVar(scopeID, t);
            if (a.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            curr.fields = {
                {a}, {UNARY_OPS[op]}, {ctx.pickRandomVar(scopeID, t)}};
            globalVars.insert(a);
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
        if (nodeId == -1) {
            nodeId = static_cast<NodeID>(ast.ast.expressions.size());
            ast.ast.expressions.emplace_back();
            scope.expressions[i] = nodeId;
        }
        const auto oldExprCount = ast.ast.expressions.size();
        if (generate_line(ast.ast.expressions[nodeId], ast, ctx, globalVars,
                          scopeID, scope) != 0) {
            scope.expressions.resize(i);
            ast.ast.expressions.resize(oldExprCount);
            break;
        }
    }
    if (scopeID != 0 && !globalVars.empty()) {
        // filter out if varName is in scope variables
        for (auto it = globalVars.begin(); it != globalVars.end();) {
            bool found = false;
            for (const auto &varID : scope.variables) {
                const auto &decl = ast.ast.declarations[varID];
                if (std::get<std::string>(decl.fields[0].val) == *it) {
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
            scope.expressions.insert(scope.expressions.begin(),
                                     ast.ast.expressions.size());
            ASTNode globalVarsNode;
            globalVarsNode.kind = ASTNodeKind::GlobalRef;
            globalVarsNode.fields.reserve(globalVars.size());
            for (const auto &var : globalVars) {
                globalVarsNode.fields.emplace_back(var);
            }
            ast.ast.expressions.push_back(globalVarsNode);
        }
    }
    return 0;
}
