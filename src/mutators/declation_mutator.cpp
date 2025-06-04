#include "log.hpp"
#include "mutators.hpp"
#include <cstdlib>
#include <cstring>
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;

extern "C" size_t LLVMFuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize);

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

static inline bool bumpIdentifier(std::string &id) {
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
                                  const ScopeID sid,
                                  const BuiltinContext &ctx) {
    const ASTScope &scope = ast->ast.scopes[sid];
    const std::vector<NodeID> &nodes = scope.declarations;
    for (NodeID i : nodes) {
        auto &node = ast->ast.declarations[i];
        static char mutateBuf[50];
        if (node.kind == ASTNodeKind::DeclareVar) {
            if (node.type == ctx.strID) {
                std::string &strVal = std::get<std::string>(node.fields[1].val);
                strncpy(mutateBuf, strVal.c_str() + 1, strVal.size() - 2);
                mutateBuf[strVal.size() - 2] = '\0'; // remove quotes
                // LLVMFuzzerMutate(reinterpret_cast<uint8_t *>(mutateBuf),
                //                  strlen(mutateBuf), sizeof(mutateBuf));
                // TODO
                node.fields[1].val = "\"" + std::string(mutateBuf) + "\"";
            } else if (node.type == ctx.intID) {
                static std::uniform_int_distribution<uint64_t> pickNum(
                    0, UINT64_MAX);
                node.fields[1].val = std::to_string(pickNum(rng));
            } else if (node.type == ctx.floatID) {
                static std::uniform_real_distribution<double> pickFloat(-1e6,
                                                                        1e6);
                node.fields[1].val = std::to_string(pickFloat(rng));
            } else if (node.type == ctx.boolID) {
                node.fields[1].val = (rng() % 2) == 0;
            }
        }
    }
    MutationState state = MutationState::STATE_REROLL;
    size_t typesCnt = scope.types.size() + ctx.types.size();
    ASTScope *parentScope = nullptr;
    if (scope.parent != -1) {
        parentScope = &ast->ast.scopes[scope.parent];
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

            const std::string &className =
                std::get<std::string>(clsNode.fields[0].val);
            const std::string prefix = className + ".";

            std::vector<const std::pair<const std::string, FunctionSignature> *>
                cands;
            for (const auto &kv : ctx.builtinsFuncs)
                if (kv.first.rfind(prefix, 0) == 0) // starts_with prefix
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

            NodeID funID = ast->ast.declarations.size();
            ASTNode fun;
            fun.kind = ASTNodeKind::Function;
            fun.scope = ast->ast.scopes.size();
            fun.type = sig.returnType;
            ast->ast.scopes.push_back({sid, sig.returnType});
            
            fun.fields.push_back({fnName});         // <name>
            fun.fields.push_back({sig.returnType}); // <ret‑type>
            for (TypeID pt : sig.paramTypes)        // [param‑types ...]
                fun.fields.push_back({pt});

            ast->ast.declarations.push_back(std::move(fun));

            clsNode.fields.push_back({funID}); // push back as member function
            break;
        }

        /* ----------  AddClass  ---------- */
        case MutationPick::AddClass: {
            NodeID clsID = static_cast<NodeID>(ast->ast.declarations.size());
            ASTNode cls;
            cls.kind = ASTNodeKind::Class;
            cls.scope = sid;

            cls.fields.push_back({ast->ast.nameCnt});
            bumpIdentifier(ast->ast.nameCnt);

            std::string inheritName;
            // pick a random type to inherit from
            if (typesCnt > 0) {
                TypeID tid = pickType(rng);
                if (tid < scope.types.size()) {
                    inheritName = scope.types[tid];
                } else {
                    tid -= scope.types.size();
                    if (parentScope && tid < parentScope->types.size()) {
                        inheritName = parentScope->types[tid];
                    } else {
                        tid -= (parentScope ? parentScope->types.size() : 0);
                        if (tid < ctx.types.size())
                            inheritName = ctx.types[tid];
                    }
                }
                if (!inheritName.empty())
                    cls.fields.push_back({inheritName});
            }

            // sentinel
            cls.fields.push_back({-1});

            ast->ast.declarations.push_back(std::move(cls));
            break;
        }
        case MutationPick::AddVariable: {
            NodeID varID = ast->ast.declarations.size();
            ASTNode var;
            var.kind = ASTNodeKind::DeclareVar;
            var.fields = {{ast->ast.nameCnt}, {}};
            TypeID tid = pickType(rng);
            if (tid < scope.types.size()) {
                var.type = tid + SCOPE_MAX_TYPE;
                var.fields[1].val = scope.types[tid] + "()";
            } else {
                tid -= scope.types.size();
                if (scope.parent != -1) {
                    if (tid < parentScope->types.size()) {
                        var.type = (scope.parent + 1) * SCOPE_MAX_TYPE + tid;
                        var.fields[1].val = parentScope->types[tid] + "()";
                    } else {
                        var.type = tid;
                        var.fields[1].val = ctx.types[tid] + "()";
                    }
                } else {
                    var.type = SCOPE_MAX_TYPE + tid;
                    var.fields[1].val = ctx.types[tid] + "()";
                }
            }
            bumpIdentifier(ast->ast.nameCnt);
            ast->ast.declarations.push_back(var);
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