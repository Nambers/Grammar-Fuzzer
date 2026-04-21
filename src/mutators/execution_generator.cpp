#include "log.hpp"
#include "mutators.hpp"
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;

static std::uniform_int_distribution<int> pickBinaryOp(0,
                                                       BINARY_OPS.size() - 1);
static std::uniform_int_distribution<int> pickUnaryOp(0, UNARY_OPS.size() - 1);

constexpr static std::array PICK_EXEC_WEIGHT = {
    12, // GetProp
    13, // SetProp
    14, // Call
    2,  // Return
    2,  // BinaryOp
    1,  // UnaryOp
    9,  // NewInstance
    15, // SetItem
    15, // GetItem
};

static_assert(PICK_EXEC_WEIGHT.size() ==
                  (static_cast<int>(EXEC_NODE_END) -
                   static_cast<int>(EXEC_NODE_START) + 1),
              "PICK_EXEC_WEIGHT size mismatch with EXEC_NODE range");

static std::discrete_distribution<int> pickExec(PICK_EXEC_WEIGHT.begin(),
                                                PICK_EXEC_WEIGHT.end());

static TypeID pickMaybeObjectType(BuiltinContext &ctx, TypeID expectedType) {
    return ctx.respectType() ? expectedType : 0;
}

static PropKey pickTypedValueKey(BuiltinContext &ctx, ScopeID scopeID,
                                 TypeID expectedType,
                                 const std::vector<ASTScope> &scopes) {
    return ctx.pickRandomVar(scopeID, pickMaybeObjectType(ctx, expectedType),
                             ctx.pickValueKind(), scopes);
}

static std::optional<std::string>
buildValueExprFromKey(const PropKey &valueKey, ScopeID scopeID, const AST &ast,
                      BuiltinContext &ctx,
                      std::unordered_set<std::string> &globalVars,
                      ObjectKind parentKind = ObjectKind::mutable_var) {
    if (valueKey.empty())
        return std::nullopt;

    const auto &value = unfoldKey(valueKey, ast, ctx);
    std::string valueExpr = value.name;

    if (valueKey.parentType != NOT_UNDER_CLASS) {
        const auto parentKey =
            ctx.pickRandomVar(scopeID, valueKey.parentType, parentKind,
                              ast.scopes); // parent can't be callable
        if (parentKey.empty())
            return std::nullopt;
        const auto &parent = unfoldKey(parentKey, ast, ctx);
        valueExpr = parent.name + '.' + valueExpr;
        if (std::find(ctx.types.begin(), ctx.types.end(), parent.name) ==
            ctx.types.end())
            insertGlobalVar(parent, globalVars);
    }

    if (value.isCallable) {
        auto callExpr =
            buildFunctionCallG(value.extra.get<FunctionSignature>().paramTypes,
                               scopeID, ast, ctx, globalVars);
        if (callExpr.empty())
            return std::nullopt;
        valueExpr += callExpr;
    } else if (!value.isConst && valueKey.parentType == NOT_UNDER_CLASS) {
        // when the value is a mutable variable, and isn't under object, add to
        // the globalVars
        insertGlobalVar(value, globalVars);
    }

    return valueExpr;
}

// Pick index. With 25% probability, use a user-defined class
// instance: triggers __index__ for sequences, __hash__/__eq__ for
// dict — both are interesting dispatch paths for bugs.
static TypeID pickIndexType(BuiltinContext &ctx, const AST &ast,
                            TypeID containerType, ScopeID scopeID) {
    TypeID indexType;
    if (!ast.classes.empty() && rng() % 4 == 0) {
        auto it = ast.classes.begin();
        std::advance(it, rng() % ast.classes.size());
        TypeID userTid = resolveType(it->second.name, ctx, ast, scopeID);
        indexType = (userTid != 0) ? userTid : ctx.intID;
    } else if (containerType == ctx.dictID) {
        indexType = (rng() % 2 == 0 ? ctx.strID : ctx.intID);
    } else {
        indexType = ctx.intID;
    }
    return indexType;
}

extern uint32_t badState;

MutationState
FuzzingAST::generate_return(ASTNode &curr, ASTData &ast, BuiltinContext &ctx,
                            std::unordered_set<std::string> &globalVars,
                            ScopeID scopeID, const ASTScope &scope) {
    curr.kind = ASTNodeKind::Return;
    curr.scope = scopeID;
    curr.fields.clear();
    // Try to find a variable of the return type
    const auto retVarKey = ctx.pickRandomVar(
        scopeID, pickMaybeObjectType(ctx, scope.retType), ctx.pickValueKind(),
        ast.ast.scopes); // disrespect original return
                         // type to check if crash
    if (!retVarKey.empty()) {
        const auto retVar =
            buildValueExprFromKey(retVarKey, scopeID, ast.ast, ctx, globalVars);
        if (!retVar) {
            return MutationState::STATE_REROLL;
        }
        curr.fields = {{*retVar}};
    } else {
        // Fall back to literal return for common types
        if (scope.retType == ctx.intID) {
            static std::uniform_int_distribution<int64_t> pickRet(-255, 255);
            curr.fields = {{pickRet(rng)}};
        } else if (scope.retType == ctx.boolID) {
            curr.fields = {{(rng() % 2) == 0}};
        } else if (scope.retType == ctx.strID) {
            curr.fields = {{std::string("\"\"")}};
        } else {
            return MutationState::STATE_REROLL;
        }
    }
    return MutationState::STATE_OK;
}

int FuzzingAST::generate_line(ASTNode &node, ASTData &ast, BuiltinContext &ctx,
                              std::unordered_set<std::string> &globalVars,
                              ScopeID scopeID, const ASTScope &scope) {
    MutationState state = MutationState::STATE_REROLL;
    auto &curr = node;
    int attempts = 0;

    auto &scopeVars = ast.ast.scopes[scopeID].variables;
    const auto snap = ast.ast.snapshot(scope);

    while (state == MutationState::STATE_REROLL &&
           ++attempts < REROLL_ATTEMPTS) {
        if (attempts > 1) {
            // Hard rollback for symbol table side effects introduced during a
            // failed attempt (fresh variable names, var indices, classProps).
            ast.ast.restore(snap, ast.ast.scopes[scopeID]);
            // Keep provider indexes/distributions consistent with rolled-back
            // AST state; otherwise stale PropKey.idx can go out of bounds in
            // unfoldKey().
            ctx.updateVars(ast.ast);
        }

        state = MutationState::STATE_OK;
        ASTNodeKind pick = static_cast<ASTNodeKind>(
            static_cast<int>(EXEC_NODE_START) + pickExec(rng));
        curr.kind = pick;
        curr.scope = scopeID;
        curr.fields.clear();

        switch (pick) {

        case ASTNodeKind::GetProp:
            [[fallthrough]];

        case ASTNodeKind::SetProp: {
            TypeID t = ctx.pickRandomType(scopeID);
            const auto v1Key = ctx.pickRandomVar(
                scopeID, t, ObjectKind::mutable_var, ast.ast.scopes);
            if (v1Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto v2Key =
                pickTypedValueKey(ctx, scopeID, t, ast.ast.scopes);
            if (v1Key == v2Key || v2Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &v1 = unfoldKey(v1Key, ast.ast, ctx);
            auto v1Name = v1.name;
            const auto v2Name =
                buildValueExprFromKey(v2Key, scopeID, ast.ast, ctx, globalVars);
            if (!v2Name) {
                state = MutationState::STATE_REROLL;
                break;
            }
            if (v1Key.parentType != NOT_UNDER_CLASS) {
                // parent can't be function call, and constant
                const auto v1pKey =
                    ctx.pickRandomVar(scopeID, v1Key.parentType,
                                      ObjectKind::mutable_var, ast.ast.scopes);
                if (v1pKey.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                const auto &v1p = unfoldKey(v1pKey, ast.ast, ctx);
                v1Name = v1p.name + '.' + v1Name;
                // if v1p is instance, instead of type (class static method)
                if (std::find(ctx.types.begin(), ctx.types.end(), v1p.name) ==
                    ctx.types.end())
                    insertGlobalVar(v1p, globalVars);
            } else
                insertGlobalVar(v1, globalVars);

            curr.fields = {{v1Name}, {*v2Name}};

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
                    const auto varKey = pickTypedValueKey(
                        ctx, scopeID, sig->paramTypes[i], ast.ast.scopes);
                    if (varKey.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    }
                    const auto valueExpr = buildValueExprFromKey(
                        varKey, scopeID, ast.ast, ctx, globalVars);
                    if (!valueExpr) {
                        state = MutationState::STATE_REROLL;
                        break;
                    }
                    curr.fields.emplace_back(*valueExpr);
                }
            }
            globalVars.insert(ast.ast.nameCnt);
            // Register the new instance variable in the AST type system.
            {
                auto &scope_ = ast.ast.scopes[scopeID];
                scope_.variables.push_back(
                    static_cast<VarID>(ast.ast.variables.size()));
                ast.ast.variables.emplace_back(
                    NO_MODULE, ast.ast.classProps[NOT_UNDER_CLASS].size(),
                    NOT_UNDER_CLASS);
                ast.ast.classProps[NOT_UNDER_CLASS].emplace_back(
                    tid, scopeID, ast.ast.nameCnt, false);
            }
            bumpIdentifier(ast.ast.nameCnt);
            break;
        }

        case ASTNodeKind::Call: {
            // pick function name
            const auto funcKey = ctx.pickRandomVar(
                scopeID, ObjectKind::function, ast.ast.scopes);
            if (funcKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &func = unfoldKey(funcKey, ast.ast, ctx);
            const auto &sig = func.extra.get<FunctionSignature>();
            const auto fname = func.name;
            curr.fields.emplace_back(""); // placeholder for return arg
            curr.fields.emplace_back(fname);
            // return value — create a fresh variable so the pool grows
            if (sig.returnType != NO_RETURN) {
                const auto newVarName = ast.ast.nameCnt;
                bumpIdentifier(ast.ast.nameCnt);
                curr.fields[0] = {newVarName};
                // Register the new variable in the AST type system so
                // subsequent generate_line calls can find it by type.
                auto &scope_ = ast.ast.scopes[scopeID];
                scope_.variables.push_back(
                    static_cast<VarID>(ast.ast.variables.size()));
                ast.ast.variables.emplace_back(
                    NO_MODULE, ast.ast.classProps[NOT_UNDER_CLASS].size(),
                    NOT_UNDER_CLASS);
                ast.ast.classProps[NOT_UNDER_CLASS].emplace_back(
                    sig.returnType, scopeID, newVarName, false);
                globalVars.insert(newVarName);
            }

            // if it's method, we should skip first self variable caused it's
            // imply by `x.x`
            size_t i = 0;

            if (funcKey.parentType != NOT_UNDER_CLASS) {
                if (sig.selfType != -1) {
                    // if it's a method, add self as first parameter
                    const auto selfVarKey = ctx.pickRandomVar(
                        scopeID, sig.selfType, ObjectKind::mutable_var,
                        ast.ast.scopes);
                    if (selfVarKey.empty()) {
                        state = MutationState::STATE_REROLL;
                        break;
                    }
                    const auto &selfVar = unfoldKey(selfVarKey, ast.ast, ctx);
                    if (std::find(ctx.types.begin(), ctx.types.end(),
                                  selfVar.name) == ctx.types.end())
                        insertGlobalVar(selfVar, globalVars);
                    // xxx.yyy(...)
                    curr.fields[1] = {selfVar.name + '.' + fname};
                    // or static usage: yyy(xxx, ...)
                    // curr.fields.push_back({selfVar});
                    i = 1;
                } else {
                    // static method
                    curr.fields[1] = {
                        getTypeName(funcKey.parentType, ast.ast, ctx) + '.' +
                        fname};
                }
            }

            // parameters
            for (; i < sig.paramTypes.size(); ++i) {
                auto paramType = sig.paramTypes[i];
                const auto paramVarKey =
                    pickTypedValueKey(ctx, scopeID, paramType, ast.ast.scopes);
                if (paramVarKey.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                const auto pName = buildValueExprFromKey(
                    paramVarKey, scopeID, ast.ast, ctx, globalVars);
                if (!pName) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                curr.fields.emplace_back(*pName);
            }
            break;
        }

        case ASTNodeKind::Return: {
            if (scope.retType == NO_RETURN) {
                state = MutationState::STATE_REROLL;
                break;
            }
            // Try to find a variable of the return type
            const auto retVarKey = ctx.pickRandomVar(
                scopeID, pickMaybeObjectType(ctx, scope.retType),
                ctx.pickValueKind(),
                ast.ast.scopes); // disrespect original return
                                 // type to check if crash
            if (!retVarKey.empty()) {
                const auto retVar = buildValueExprFromKey(
                    retVarKey, scopeID, ast.ast, ctx, globalVars);
                if (!retVar) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                curr.fields = {{*retVar}};
            } else {
                // Fall back to literal return for common types
                if (scope.retType == ctx.intID) {
                    static std::uniform_int_distribution<int64_t> pickRet(-255,
                                                                          255);
                    curr.fields = {{pickRet(rng)}};
                } else if (scope.retType == ctx.boolID) {
                    curr.fields = {{(rng() % 2) == 0}};
                } else if (scope.retType == ctx.strID) {
                    curr.fields = {{std::string("\"\"")}};
                } else {
                    state = MutationState::STATE_REROLL;
                    break;
                }
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

            const auto aKey = ctx.pickRandomVar(
                scopeID, t1, ObjectKind::mutable_var, ast.ast.scopes);
            if (aKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto a2Key =
                pickTypedValueKey(ctx, scopeID, t2, ast.ast.scopes);
            if (a2Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &a = unfoldKey(aKey, ast.ast, ctx);
            const auto a2 =
                buildValueExprFromKey(a2Key, scopeID, ast.ast, ctx, globalVars);
            if (!a2) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto a3Key =
                pickTypedValueKey(ctx, scopeID, t2, ast.ast.scopes);
            if (a3Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto a3 =
                buildValueExprFromKey(a3Key, scopeID, ast.ast, ctx, globalVars);
            if (!a3) {
                state = MutationState::STATE_REROLL;
                break;
            }
            insertGlobalVar(a, globalVars);
            curr.fields = {{a.name}, {*a3}, {BINARY_OPS[op]}, {*a2}};
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
            const auto aKey = ctx.pickRandomVar(
                scopeID, t, ObjectKind::mutable_var, ast.ast.scopes);
            if (aKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &a = unfoldKey(aKey, ast.ast, ctx);
            const auto a2Key =
                pickTypedValueKey(ctx, scopeID, t, ast.ast.scopes);
            if (a2Key.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto a2 =
                buildValueExprFromKey(a2Key, scopeID, ast.ast, ctx, globalVars);
            if (!a2) {
                state = MutationState::STATE_REROLL;
                break;
            }
            curr.fields = {{a.name}, {UNARY_OPS[op]}, {*a2}};
            insertGlobalVar(a, globalVars);
            break;
        }

        case ASTNodeKind::SetItem: {
            // container[index] = value
            std::vector<TypeID> containerTypes;
            if (ctx.listID > 0)
                containerTypes.push_back(ctx.listID);
            if (ctx.bytearrayID > 0)
                containerTypes.push_back(ctx.bytearrayID);
            if (ctx.dictID > 0)
                containerTypes.push_back(ctx.dictID);
            if (containerTypes.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            TypeID containerType =
                containerTypes[rng() % containerTypes.size()];

            const auto containerKey =
                ctx.pickRandomVar(scopeID, containerType,
                                  ObjectKind::mutable_var, ast.ast.scopes);
            if (containerKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto &container = unfoldKey(containerKey, ast.ast, ctx);
            auto containerName = container.name;
            if (containerKey.parentType != NOT_UNDER_CLASS) {
                const auto parentKey =
                    ctx.pickRandomVar(scopeID, containerKey.parentType,
                                      ObjectKind::mutable_var, ast.ast.scopes);
                if (parentKey.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }
                containerName = unfoldKey(parentKey, ast.ast, ctx).name + '.' +
                                containerName;
            }
            insertGlobalVar(container, globalVars);

            const auto indexKey = pickTypedValueKey(
                ctx, scopeID,
                pickIndexType(ctx, ast.ast, containerType, scopeID),
                ast.ast.scopes);
            if (indexKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto indexName = buildValueExprFromKey(
                indexKey, scopeID, ast.ast, ctx, globalVars);
            if (!indexName) {
                state = MutationState::STATE_REROLL;
                break;
            }

            // Pick value: any type (triggers __index__ on bytearray assignment)
            const auto valueKey =
                ctx.pickRandomVar(scopeID, ctx.pickValueKind(), ast.ast.scopes);
            if (valueKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto valueName = buildValueExprFromKey(
                valueKey, scopeID, ast.ast, ctx, globalVars);
            if (!valueName) {
                state = MutationState::STATE_REROLL;
                break;
            }

            curr.fields = {{containerName}, {*indexName}, {*valueName}};
            break;
        }

        case ASTNodeKind::GetItem: {
            // result = container[index]
            std::vector<TypeID> containerTypes;
            if (ctx.listID > 0)
                containerTypes.push_back(ctx.listID);
            if (ctx.bytearrayID > 0)
                containerTypes.push_back(ctx.bytearrayID);
            if (ctx.dictID > 0)
                containerTypes.push_back(ctx.dictID);
            if (ctx.strID > 0)
                containerTypes.push_back(ctx.strID);
            if (containerTypes.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            TypeID containerType =
                containerTypes[rng() % containerTypes.size()];

            const auto containerKey =
                pickTypedValueKey(ctx, scopeID, containerType, ast.ast.scopes);
            if (containerKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto containerName = buildValueExprFromKey(
                containerKey, scopeID, ast.ast, ctx, globalVars);
            if (!containerName) {
                state = MutationState::STATE_REROLL;
                break;
            }

            const auto indexKey = pickTypedValueKey(
                ctx, scopeID,
                pickIndexType(ctx, ast.ast, containerType, scopeID),
                ast.ast.scopes);
            if (indexKey.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }
            const auto indexName = buildValueExprFromKey(
                indexKey, scopeID, ast.ast, ctx, globalVars);
            if (!indexName) {
                state = MutationState::STATE_REROLL;
                break;
            }

            // Create result variable (type=object, will be updated by runtime)
            curr.fields = {{ast.ast.nameCnt}, {*containerName}, {*indexName}};
            {
                auto &scope_ = ast.ast.scopes[scopeID];
                scope_.variables.push_back(
                    static_cast<VarID>(ast.ast.variables.size()));
                ast.ast.variables.emplace_back(
                    NO_MODULE, ast.ast.classProps[NOT_UNDER_CLASS].size(),
                    NOT_UNDER_CLASS);
                ast.ast.classProps[NOT_UNDER_CLASS].emplace_back(
                    0, scopeID, ast.ast.nameCnt, false);
            }
            globalVars.insert(ast.ast.nameCnt);
            bumpIdentifier(ast.ast.nameCnt);
            break;
        }

        default:
            PANIC("Unsupported execution node kind: {}",
                  static_cast<int>(pick));
        }
    }
    if (state != MutationState::STATE_OK) {
        // Final failed attempt can still leave side effects in symbol tables.
        // Restore the pre-call snapshot and resync provider caches.
        ast.ast.restore(snap, ast.ast.scopes[scopeID]);
        ctx.updateVars(ast.ast);
        ++badState;
        return 1;
    }
    return 0;
}

int FuzzingAST::generate_execution_block(ASTData &ast, const ScopeID &scopeID,
                                         BuiltinContext &ctx) {

    // const int NUM_GEN = ast.ast.scopes[scopeID].declarations.size() * 2;
    constexpr int NUM_GEN = 70; // TODO
    ASTScope &scope = ast.ast.scopes[scopeID];
    // Fresh generation pass: avoid carrying stale return node from previous
    // rounds, which may later point to invalid/placeholder expressions.
    scope.retNodeID = -1;
    scope.expressions.resize(NUM_GEN, -1);

    std::unordered_set<std::string> globalVars;

    for (int i = 0; i < NUM_GEN; ++i) {
        auto &nodeId = scope.expressions[i];
        nodeId = ast.ast.expressions.size();
        ast.ast.expressions.emplace_back();
        ASTNode node;
        if (generate_line(node, ast, ctx, globalVars, scopeID, scope) != 0) {
            scope.expressions.resize(i);
            // remove the placeholder inserted for this failed generation step
            ast.ast.expressions.resize(nodeId);
            break;
        }
        ast.ast.expressions[nodeId] = std::move(node);
        if (node.kind == ASTNodeKind::Return) {
            scope.retNodeID = nodeId;
            --i;
        }
    }
    // ensure return expression exists for every function
    if (scope.retType != -1 && scope.retNodeID == -1) {
        const auto nodeId = ast.ast.expressions.size();
        ast.ast.expressions.emplace_back();
        ASTNode node;
        if (generate_return(node, ast, ctx, globalVars, scopeID, scope) !=
            MutationState::STATE_OK) {
            ast.ast.expressions.pop_back();
        } else {
            scope.retNodeID = nodeId;
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
