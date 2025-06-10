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

        auto overFlow = [&](char reset) { ch = reset; };

        if (ch >= '0' && ch <= '8') {
            ch++;
            carry = false;
        } else if (ch == '9') {
            overFlow(i == 0 ? 'a' : '0');
        } else if (ch >= 'A' && ch <= 'Y') {
            ch++;
            carry = false;
        } else if (ch == 'Z') {
            overFlow(i == 0 ? 'a' : '0');
        } else if (ch >= 'a' && ch <= 'y') {
            ch++;
            carry = false;
        } else if (ch == 'z') {
            overFlow(i == 0 ? 'a' : '0');
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

static std::uniform_int_distribution<int>
    dist(0, static_cast<int>(MutationPick::AddImport));

static std::uniform_int_distribution<int> distLib(0, TARGET_LIBS.size() - 1);

int FuzzingAST::mutate_expression(const std::shared_ptr<ASTData> &ast,
                                  const ScopeID sid, ASTScope &scope,
                                  const BuiltinContext &ctx) {
    for (NodeID i : scope.declarations) {
        auto &node = ast->ast.declarations[i];
        if (node.kind == ASTNodeKind::DeclareVar) {
            if (node.type == ctx.strID) {
                havoc(std::get<std::string>(node.fields[1].val), 50);
            } else if (node.type == ctx.intID) {
                static std::uniform_int_distribution<int64_t> pickNum(
                    0, INT64_MAX);
                node.fields[1].val = pickNum(rng);
            } else if (node.type == ctx.floatID) {
                static std::uniform_real_distribution<double> pickFloat(-1e6,
                                                                        1e6);
                node.fields[1].val = pickFloat(rng);
            } else if (node.type == ctx.boolID) {
                node.fields[1].val = (rng() % 2) == 0;
            }
        }
    }
    MutationState state = MutationState::STATE_REROLL;
    size_t typesCnt = scope.types.size() + ctx.types.size();
    const ASTScope &parentScope =
        scope.parent != -1 ? ast->ast.scopes[scope.parent] : scope;
    if (scope.parent != -1) {
        typesCnt += ast->ast.scopes[scope.parent].types.size();
    }
    std::uniform_int_distribution<size_t> pickType(0, typesCnt - 1);
    while (state == MutationState::STATE_REROLL) {
        state = MutationState::STATE_OK;
        // do other mutations
        MutationPick pick = static_cast<MutationPick>(dist(rng));
        switch (pick) {
            /* ----------  AddFunction  ---------- */
        case MutationPick::AddFunction: {
            std::vector<NodeID> classes;
            for (size_t i = 0; i < ast->ast.declarations.size(); ++i)
                if (ast->ast.declarations[i].kind == ASTNodeKind::Class)
                    classes.push_back(static_cast<NodeID>(i));

            if (classes.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }

            NodeID clsID = classes[rng() % classes.size()];
            ASTNode &clsNode = ast->ast.declarations[clsID];

            // get inheritance class name
            if (std::holds_alternative<int64_t>(clsNode.fields[1].val)) {
                // class don't have inheritance
                state = MutationState::STATE_REROLL;
                break;
            }
            const std::string prefix =
                std::get<std::string>(clsNode.fields[1].val) + ".";

            std::vector<const std::pair<const std::string, FunctionSignature> *>
                cands;
            for (const auto &kv : ctx.builtinsFuncs)
                if (kv.first.starts_with(prefix)) // starts_with prefix
                    cands.push_back(&kv);

            if (cands.empty()) {
                state = MutationState::STATE_REROLL;
                break;
            }

            const auto *picked = cands[rng() % cands.size()];
            const std::string &fullName = picked->first;
            const FunctionSignature &sig = picked->second;
            const std::string fnName =
                fullName.substr(prefix.size()); // remove "<cls>."

            ASTNode fun;
            fun.kind = ASTNodeKind::Function;
            fun.scope = ast->ast.scopes.size();
            fun.type = sig.returnType;
            ast->ast.scopes.push_back({sid, sig.returnType});

            fun.fields.push_back({fnName});         // <name>
            fun.fields.push_back({sig.returnType}); // <ret‑type>
            std::string arg = "a";
            for (TypeID pt : sig.paramTypes) { // [param‑types ...]
                fun.fields.push_back({arg});
                bumpIdentifier(arg);
                fun.fields.push_back({pt});
            }

            clsNode.fields.emplace_back(
                static_cast<NodeID>(ast->ast.declarations.size()));

            // will be added in reflectObject
            // scope.funcSignatures[fnName] = sig;
            ast->ast.declarations.push_back(std::move(fun));

            break;
        }

        /* ----------  AddClass  ---------- */
        case MutationPick::AddClass: {
            ASTNode cls;
            cls.kind = ASTNodeKind::Class;

            cls.fields.push_back({ast->ast.nameCnt});
            bumpIdentifier(ast->ast.nameCnt);

            std::string inheritName;
            TypeID inheritType = -1;
            // pick a random type to inherit from
            if (typesCnt > 0) {
                TypeID tid = pickType(rng);
                if (tid < scope.types.size()) {
                    inheritType = tid + SCOPE_MAX_TYPE * (sid + 1);
                    inheritName = scope.types[tid];
                } else {
                    tid -= scope.types.size();
                    if (scope.parent != -1 && tid < parentScope.types.size()) {
                        inheritType = (scope.parent + 1) * SCOPE_MAX_TYPE + tid;
                        inheritName = parentScope.types[tid];
                    } else {
                        tid -=
                            (scope.parent != -1 ? parentScope.types.size() : 0);
                        if (tid < ctx.types.size()) {
                            inheritType = tid;
                            inheritName = ctx.types[tid];
                        }
                    }
                }
                if (!inheritName.empty())
                    cls.fields.push_back({inheritName});
            }

            scope.types.push_back(std::get<std::string>(cls.fields[0].val));
            scope.inheritedTypes.push_back(inheritType);
            // sentinel
            cls.fields.push_back({-1});

            // check if has init class, if so, add it as function and do super
            // call
            std::string initName = inheritName + ".__init__";
            if (!inheritName.empty() && ctx.builtinsFuncs.contains(initName)) {
                NodeID funID = ast->ast.declarations.size();
                ASTNode fun;
                fun.kind = ASTNodeKind::Function;
                fun.scope = ast->ast.scopes.size();
                TypeID retType =
                    scope.types.size() - 1 + SCOPE_MAX_TYPE * (sid + 1);
                ast->ast.scopes.push_back({sid, retType});
                fun.fields.push_back({"__init__"}); // <name>
                fun.fields.push_back({-1});         // <ret‑type>
                const auto &sig =
                    ctx.builtinsFuncs.at(initName); // get signature
                std::string arg = "a";
                for (const TypeID &pt : sig.paramTypes) {
                    fun.fields.push_back({arg});
                    bumpIdentifier(arg);
                    fun.fields.push_back({pt});
                }
                cls.fields.push_back({funID}); // push back as member function
                // will be added in reflectObject
                // scope.funcSignatures[std::get<std::string>(cls.fields[0].val)
                // +
                //                      ".__init__"] = sig;
                ast->ast.declarations.push_back(std::move(fun));
            }

            scope.declarations.push_back(ast->ast.declarations.size());

            ast->ast.declarations.push_back(std::move(cls));
            break;
        }
        case MutationPick::AddVariable: {
            NodeID varID = ast->ast.declarations.size();
            ASTNode var;
            var.kind = ASTNodeKind::DeclareVar;
            var.fields = {{ast->ast.nameCnt}, {}};
            bumpIdentifier(ast->ast.nameCnt);
            TypeID tid = pickType(rng);
            if (tid < scope.types.size()) {
                var.type = scope.inheritedTypes[tid];
                var.fields[1].val = scope.types[tid] + "()";
            } else {
                tid -= scope.types.size();
                if (scope.parent != -1) {
                    if (tid < parentScope.types.size()) {
                        var.type = parentScope.inheritedTypes[tid];
                        var.fields[1].val = parentScope.types[tid] + "()";
                    } else {
                        tid -= parentScope.types.size();
                        var.type = tid;
                        var.fields[1].val = ctx.types[tid] + "()";
                    }
                } else {
                    var.type = tid;
                    var.fields[1].val = ctx.types[tid] + "()";
                }
            }
            scope.variables.push_back(varID);
            scope.declarations.push_back(varID);
            ast->ast.declarations.push_back(std::move(var));
            break;
        }
        case MutationPick::AddImport: {
            // NodeID impID = ast->ast.declarations.size();
            // ASTNode imp;
            // imp.kind = ASTNodeKind::Import;
            // imp.fields = {ASTNodeValue{TARGET_LIBS[distLib(rng)]}};
            // ast->ast.declarations.push_back(imp);
            state = MutationState::STATE_REROLL; // NOT PLANNED YET
            break;
        }
        } // switch
    }
    return 0;
}