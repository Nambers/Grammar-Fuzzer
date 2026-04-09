#include "log.hpp"
#include "mutators.hpp"
#include <cstdlib>
#include <cstring>
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;
extern void havoc(std::string &data, std::size_t max_sz,
                  std::size_t max_havoc_rounds = 16);
// constexpr std::array targetLibs = {"math",
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

extern const std::span<const char *const> targetLibs;
extern std::uniform_int_distribution<int> distLib;

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
    30, // AddFunction
    12, // AddClass
    20, // AddVariable
    6,  // AddImport
};
static_assert(PICK_MUTATION_WEIGHT.size() ==
                  static_cast<int>(MutationPick::AddImport) + 1,
              "PICK_MUTATION_WEIGHT size mismatch with MutationPick enum");

static std::discrete_distribution<int> dist(PICK_MUTATION_WEIGHT.begin(),
                                            PICK_MUTATION_WEIGHT.end());

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
                const auto &varInfoKey = ast.variables[cnt++];
                const auto &varInfo = unfoldKey(varInfoKey, ast, ctx);
                if (varInfo.type == ctx.strID) {
                    havoc(std::get<std::string>(node.fields[1].val), 50);
                } else if (varInfo.type == ctx.intID) {
                    static std::uniform_int_distribution<int64_t> pickLarge(
                        0, INT64_MAX);
                    static std::uniform_int_distribution<int64_t> pickSmall(
                        -100, 100);
                    // 80% small (valid as container indices), 20% large
                    static std::discrete_distribution<int> pickLargeOrSmall(
                        {8, 2});
                    node.fields[1].val =
                        pickLargeOrSmall(rng) ? pickSmall(rng) : pickLarge(rng);
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
        MutationPick pick;
        if (ast.scopes.size() > MAX_SCOPE_CNT)
            pick = static_cast<MutationPick>(rng() % 2 +
                                             2); // only add variable or import
        else
            pick = static_cast<MutationPick>(dist(rng));

        switch (pick) {
        /* ----------  AddClass  ---------- */
        case MutationPick::AddClass: {
            if (ast.scopes[sid].parent != -1) {
                // don't do nested class
                state = MutationState::STATE_REROLL;
                break;
            }
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
                    if (tid == 0) {
                        state = MutationState::STATE_REROLL;
                        break;
                    }
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
                // QuickJS has a builtin type named "undefined" which is not a
                // valid class base in `class A extends ...`.
                if (inheritType == -1 || inheritName == "undefined") {
                    // no need plain class
                    state = MutationState::STATE_REROLL;
                    break;
                }

                scope.inheritedTypes.push_back(inheritType);
            }
            // sentinel
            cls.fields.push_back(SENTINEL_NODE);

            ast.scopes[sid].declarations.push_back(ast.declarations.size());

            // add potential override function to extra, grab from the
            // methodIndex
            {
                auto clz =
                    PropInfo{.type = inheritType,
                             .scope = sid,
                             .name = std::get<std::string>(cls.fields[0].val),
                             .extra = {ctx.methodIndex_[inheritType]}};
                std::shuffle(clz.extra.get<std::vector<PropKey>>().begin(),
                             clz.extra.get<std::vector<PropKey>>().end(), rng);
                ast.classes[ast.declarations.size()] = std::move(clz);
            };

            ast.declarations.push_back(std::move(cls));

            // continue to add a function
            [[fallthrough]];
        }
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

            auto &cands = ast.classes[clsID].extra.get<std::vector<PropKey>>();
            if (cands.empty()) {
                // no method found
                state = MutationState::STATE_REROLL;
                break;
            }
            const PropKey pickedKey = cands.back();
            cands.pop_back();
            const auto &picked = unfoldKey(pickedKey, ast, ctx);

            NodeID funNodeID = ast.declarations.size();
            ScopeID funSid = ast.scopes.size();
            const auto &funcSig = picked.extra.get<FunctionSignature>();

            ast.declarations.reserve(funNodeID + 1 + funcSig.paramTypes.size());

            ast.scopes.emplace_back(sid, funcSig.returnType);

            ast.declarations.emplace_back(); // index == funNodeID

            ast.declarations[clsID].fields.emplace_back(funNodeID);

            addFunction(picked, ast, funSid, funNodeID);

            break;
        }
        case MutationPick::AddVariable: {
            ASTNode var;
            var.kind = ASTNodeKind::DeclareVar;
            var.fields = {{ast.nameCnt}, {}};
            bumpIdentifier(ast.nameCnt);
            TypeID tid = ctx.pickRandomType(sid);
            if (tid == 0) {
                state = MutationState::STATE_REROLL;
                break;
            }
            {
                auto &scope = ast.scopes[sid];
                std::string typeName = getTypeName(tid, ast, ctx);
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
                    auto callExpr = buildFunctionCallG(sig->paramTypes, sid,
                                                       ast, ctx, globalVars);
                    if (callExpr.empty()) {
                        state = MutationState::STATE_REROLL;
                        break; // reroll if failed to pick vars
                    }
                    var.fields[1].val = typeName + callExpr;
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
                            scope.globalRefID = ast.declarations.size();
                            ast.declarations.emplace_back(
                                ASTNodeKind::GlobalRef);
                            auto &node = ast.declarations.back();
                            for (const auto &varName : globalVars) {
                                node.fields.emplace_back(varName);
                            }
                        }
                    }
                }

                NodeID varID = ast.declarations.size();
                scope.variables.push_back(ast.variables.size());
                ast.variables.emplace_back(
                    NO_MODULE, ast.classProps[NOT_UNDER_CLASS].size(),
                    NOT_UNDER_CLASS);
                ast.classProps[NOT_UNDER_CLASS].emplace_back(
                    tid, sid, std::get<std::string>(var.fields[0].val));
                scope.declarations.push_back(varID);
            }
            ast.declarations.push_back(std::move(var));
            break;
        }
        case MutationPick::AddImport: {
            NodeID impID = ast.declarations.size();
            ModuleID mid = distLib(rng);
            if (ast.scopes[sid].importedModules.contains(mid + 1)) {
                state = MutationState::STATE_REROLL;
                break;
            }
            ASTNode imp;
            imp.kind = ASTNodeKind::Import;
            imp.fields = {ASTNodeValue{targetLibs[mid]}};
            ast.scopes[sid].declarations.push_back(ast.declarations.size());
            ast.declarations.push_back(imp);
            ast.scopes[sid].importedModules.insert(mid +
                                                   1); // moduleID starts from 1
            break;
        }
        } // switch
    }
    return ast;
}