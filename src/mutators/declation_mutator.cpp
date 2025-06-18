#include "log.hpp"
#include "mutators.hpp"
#include <cstdlib>
#include <cstring>
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;
extern void havoc(std::string &data, std::size_t max_sz,
                  std::size_t max_havoc_rounds = 16);
// constexpr std::array TARGET_LIBS = {"math",
//                                     "random",
//                                     "os",
//                                     "sys",
//                                     "time",
//                                     "collections",
//                                     "itertools",
//                                     "functools",
//                                     "re",
//                                     "json",
//                                     "pickle",
//                                     "csv",
//                                     "xml.etree.ElementTree",
//                                     "argparse",
//                                     "logging",
//                                     "threading",
//                                     "multiprocessing"};
// TODO
constexpr std::array TARGET_LIBS = {"math"};
/*
all constants mutating - str/bytes by havoc, int/float/bool by rng
pick:
- add new function/class/variable/import
- remove function/class/variable/import
 */
enum class MutationPick {
    AddFunction = 0,
    AddClass,
    AddVariable,
    AddImport,
};

constexpr static std::array PICK_MUTATION_WEIGHT = {
    2, // AddFunction
    1, // AddClass
    3, // AddVariable
    1, // AddImport
};
static_assert(PICK_MUTATION_WEIGHT.size() ==
                  static_cast<int>(MutationPick::AddImport) + 1,
              "PICK_MUTATION_WEIGHT size mismatch with MutationPick enum");

static std::discrete_distribution<int> dist(PICK_MUTATION_WEIGHT.begin(),
                                            PICK_MUTATION_WEIGHT.end());

static std::uniform_int_distribution<int> distLib(0, TARGET_LIBS.size() - 1);

static bool bumpIdentifier(std::string &id) {
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

static std::optional<FunctionSignature>
lookupMethodSig(TypeID tid, const std::string &name, const AST &ast,
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

AST FuzzingAST::mutate_expression(AST ast, const ScopeID sid,
                                  BuiltinContext &ctx) {
    size_t typesCnt;
    ScopeID parentScopeID;
    {
        auto &scope = ast.scopes[sid];
        int cnt = scope.paramCnt;
        for (NodeID i : scope.declarations) {
            auto &node = ast.declarations[i];
            if (node.kind == ASTNodeKind::DeclareVar) {
                PropInfo &varInfo = ast.variables[cnt++];
                if (varInfo.type == ctx.strID) {
                    havoc(std::get<std::string>(node.fields[1].val), 50);
                } else if (varInfo.type == ctx.intID) {
                    static std::uniform_int_distribution<int64_t> pickNum(
                        0, INT64_MAX);
                    node.fields[1].val = pickNum(rng);
                } else if (varInfo.type == ctx.floatID) {
                    static std::uniform_real_distribution<double> pickFloat(
                        -1e6, 1e6);
                    node.fields[1].val = pickFloat(rng);
                } else if (varInfo.type == ctx.boolID) {
                    node.fields[1].val = (rng() % 2) == 0;
                }
            }
        }
        parentScopeID = scope.parent;
        typesCnt = scope.types.size() + ctx.types.size();
    }

    MutationState state = MutationState::STATE_REROLL;
    while (state == MutationState::STATE_REROLL) {
        state = MutationState::STATE_OK;
        // do other mutations
        MutationPick pick = static_cast<MutationPick>(dist(rng));
        switch (pick) {
            /* ----------  AddFunction  ---------- */
        case MutationPick::AddFunction: {
            NodeID clsID;
            {
                std::vector<NodeID> classes;
                for (size_t i = 0; i < ast.declarations.size(); ++i)
                    if (ast.declarations[i].kind == ASTNodeKind::Class)
                        classes.push_back(i);

                if (classes.empty()) {
                    state = MutationState::STATE_REROLL;
                    break;
                }

                clsID = classes[rng() % classes.size()];
            }
            TypeID tid;
            {
                ASTNode &clsNode = ast.declarations[clsID];

                // get inheritance class name
                // unreachable
                if (std::holds_alternative<int64_t>(clsNode.fields[1].val)) {
                    // class don't have inheritance
                    state = MutationState::STATE_REROLL;
                    break;
                }
                tid = resolveType(std::get<std::string>(clsNode.fields[1].val),
                                  ctx, ast, sid);
            }

            const auto &picked = ctx.pickRandomMethod(ast, tid);
            if (!picked) {
                // no method found
                state = MutationState::STATE_REROLL;
                break;
            }

            NodeID funNodeID = ast.declarations.size();
            ScopeID funSid = ast.scopes.size();

            ast.declarations.reserve(funNodeID + 1 +
                                     picked->funcSig.paramTypes.size());

            ast.scopes.emplace_back(sid, picked->funcSig.returnType);

            ast.declarations.emplace_back(); // index == funNodeID
            {
                ASTNode &fun = ast.declarations[funNodeID];
                fun.kind = ASTNodeKind::Function;
                fun.scope = funSid;
                fun.fields.emplace_back(picked->name);
                fun.fields.emplace_back(picked->funcSig.returnType);
            }

            ast.declarations[clsID].fields.emplace_back(funNodeID);

            {
                ASTScope &funScope = ast.scopes[funSid];
                std::string arg = "arg_a";
                for (TypeID pt : picked->funcSig.paramTypes) {
                    ast.declarations[funNodeID].fields.emplace_back(arg);

                    funScope.variables.push_back(ast.variables.size());
                    ast.variables.push_back({pt, funSid, arg});
                    bumpIdentifier(arg);
                    ast.declarations[funNodeID].fields.emplace_back(pt);
                }
                funScope.paramCnt = funScope.variables.size();
            }
            break;
        }

        /* ----------  AddClass  ---------- */
        case MutationPick::AddClass: {
            ASTNode cls;
            cls.kind = ASTNodeKind::Class;

            cls.fields.emplace_back(ast.nameCnt);
            bumpIdentifier(ast.nameCnt);

            std::string inheritName;
            TypeID inheritType = -1;
            // pick a random type to inherit from
            {
                auto &scope = ast.scopes[sid];
                const auto &parentScope =
                    (parentScopeID != -1 ? ast.scopes[parentScopeID] : scope);
                if (typesCnt > 0) {
                    TypeID tid = ctx.pickRandomType(sid);
                    if (tid < scope.types.size()) {
                        inheritType = tid + SCOPE_MAX_TYPE * (sid + 1);
                        inheritName = scope.types[tid];
                    } else {
                        tid -= scope.types.size();
                        if (scope.parent != -1 &&
                            tid < parentScope.types.size()) {
                            inheritType =
                                (scope.parent + 1) * SCOPE_MAX_TYPE + tid;
                            inheritName = parentScope.types[tid];
                        } else {
                            tid -=
                                (scope.parent != -1 ? parentScope.types.size()
                                                    : 0);
                            if (tid < ctx.types.size()) {
                                inheritType = tid;
                                inheritName = ctx.types[tid];
                            }
                        }
                    }
                    if (!inheritName.empty())
                        cls.fields.push_back({inheritName});
                }

                scope.inheritedTypes.push_back(inheritType);
            }
            // sentinel
            cls.fields.push_back({-1});

            // check if has init class, if so, add it as function and do super
            // call
            // std::string initName = inheritName + ".__init__";
            const auto &func = getPropByName(
                "__init__", ctx.builtinsProps.at(inheritType), true, sid);
            if (func) {
                NodeID funID = ast.declarations.size();
                const ScopeID funSid = ast.scopes.size();
                ast.declarations.emplace_back(); // index == funID
                {
                    ASTNode &fun = ast.declarations[funID];
                    fun.kind = ASTNodeKind::Function;
                    fun.scope = funSid;
                    fun.fields.emplace_back("__init__");
                    fun.fields.emplace_back(-1);
                }

                ast.scopes.emplace_back(sid, 0);

                auto &funScope = ast.scopes[funSid];
                std::string arg = "arg_a";
                for (TypeID pt :
                     func->funcSig.paramTypes) { // [param‑types ...]
                    auto &fun = ast.declarations[funID];
                    fun.fields.emplace_back(arg);
                    funScope.variables.push_back(ast.declarations.size());
                    ast.variables.push_back({pt, funSid, arg});
                    bumpIdentifier(arg);
                    fun.fields.emplace_back(pt);
                }
                funScope.paramCnt = funScope.variables.size();
                cls.fields.emplace_back(funID); // push back as member function
                // will be added in reflectObject
                // scope.funcSignatures[std::get<std::string>(cls.fields[0].val)
                // +
                //                      ".__init__"] = sig;
            }

            ast.scopes[sid].declarations.push_back(ast.declarations.size());

            ast.declarations.push_back(std::move(cls));
            break;
        }
        case MutationPick::AddVariable: {
            ASTNode var;
            var.kind = ASTNodeKind::DeclareVar;
            var.fields = {{ast.nameCnt}, {}};
            bumpIdentifier(ast.nameCnt);
            TypeID tid = ctx.pickRandomType(sid);
            {
                auto &scope = ast.scopes[sid];
                std::string typeName;
                const auto &parentScope =
                    (parentScopeID != -1 ? ast.scopes[parentScopeID] : scope);

                if (tid < scope.types.size()) {
                    typeName = scope.types[tid];
                } else {
                    tid -= scope.types.size();
                    if (scope.parent != -1) {
                        if (tid < parentScope.types.size()) {
                            typeName = parentScope.types[tid];
                        } else {
                            tid -= parentScope.types.size();
                            typeName = ctx.types[tid];
                        }
                    } else {
                        typeName = ctx.types[tid];
                    }
                }
                // check if registered `__new__` or `__init__` function
                // to get args. priority to `__new__`
                // check builtins, curr scope, parent scope and parent's
                auto sig = lookupMethodSig(tid, "__new__", ast, ctx, sid);
                if (!sig)
                    sig = lookupMethodSig(tid, "__init__", ast, ctx, sid);

                if (!sig) {
                    var.fields[1].val = typeName + "()"; // default value
                } else {
                    // get all args
                    std::unordered_set<std::string> globalVars;
                    globalVars.reserve(sig->paramTypes.size());
                    auto callExpr = std::string(typeName) + "(";
                    for (size_t i = 0; i < sig->paramTypes.size(); ++i) {
                        if (i > 0)
                            callExpr += ", ";
                        auto varName =
                            ctx.pickRandomVar(sid, sig->paramTypes[i]);
                        if (varName.empty()) {
                            state = MutationState::STATE_REROLL;
                            break;
                        }
                        callExpr += varName;
                        globalVars.insert(varName);
                    }
                    if (state == MutationState::STATE_REROLL)
                        break; // reroll if failed to pick vars
                    var.fields[1].val = callExpr + ")";
                    if (!globalVars.empty() && sid != 0) {
                        // filter out global variables
                        for (auto it = globalVars.begin();
                             it != globalVars.end();) {
                            bool found = false;
                            for (const auto &declID : scope.declarations) {
                                const auto &decl = ast.declarations[declID];
                                if (std::get<std::string>(decl.fields[0].val) ==
                                    *it) {
                                    found = true;
                                    break;
                                }
                            }
                            if (found)
                                it = globalVars.erase(it);
                            else
                                ++it;
                        }
                        if (!globalVars.empty()) {
                            // add global reference
                            ast.declarations.emplace_back(
                                ASTNodeKind::GlobalRef);
                            scope.declarations.push_back(
                                ast.declarations.size() - 1);
                            auto &node = ast.declarations.back();
                            for (const auto &varName : globalVars) {
                                node.fields.emplace_back(varName);
                            }
                        }
                    }
                }

                NodeID varID = ast.declarations.size();
                scope.variables.push_back(ast.variables.size());
                ast.variables.push_back(
                    {tid, sid, std::get<std::string>(var.fields[0].val)});
                scope.declarations.push_back(varID);
            }
            ast.declarations.push_back(std::move(var));
            break;
        }
        case MutationPick::AddImport: {
            // NodeID impID = ast.declarations.size();
            // ASTNode imp;
            // imp.kind = ASTNodeKind::Import;
            // imp.fields = {ASTNodeValue{TARGET_LIBS[distLib(rng)]}};
            // ast.declarations.push_back(imp);
            state = MutationState::STATE_REROLL; // NOT PLANNED YET
            break;
        }
        } // switch
    }
    return ast;
}