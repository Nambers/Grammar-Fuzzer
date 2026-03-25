#include "ast.hpp"
#include <algorithm>
#include <iostream>
#include <random>

extern std::mt19937 rng;

using namespace FuzzingAST;

static inline auto makeIndexDist(size_t size) {
    return std::uniform_int_distribution<size_t>(0, size > 0 ? size - 1 : 0);
}

void BuiltinContext::updateVars(const AST &ast) {
    size_t n = ast.scopes.size();
    scopeProviders.assign(n, {});
    typeList_.assign(n, {});
    typeDist_.assign(n, {});

    if (n > 0)
        scopeProviders[0] = builtinsScopeCache;

    // any function is callable in scope, if they had variable for class
    // any variable in class should be usable in parent and child scope, if they
    // had variable for class but right now we lucky don't need to consider
    // child scope, since we don't allow nested class or function definition

    for (const auto &[tid, pis] : ast.classProps) {
        for (size_t j = 0; j < pis.size(); ++j) {
            const auto &pi = pis[j];

            if (pi.scope < 0 || static_cast<size_t>(pi.scope) >= n)
                continue;

            auto &index = scopeProviders[pi.scope].selectIndex(pi);
            index[pi.type].emplace_back(NO_MODULE, j, tid);
            index[0].emplace_back(NO_MODULE, j,
                                  tid); // fallback to add to object
        }
    }

    for (size_t i = 0; i < n; ++i) {
        for (auto mid : ast.scopes[i].importedModules) {
            for (const auto &kv : modulesProps[mid]) {
                TypeID parentType = kv.first;
                const auto &pis = kv.second;
                for (size_t j = 0; j < pis.size(); ++j) {
                    const auto &pi = pis[j];
                    auto &index = scopeProviders[i].selectIndex(pi);
                    index[pi.type].emplace_back(mid, j, parentType);
                    index[0].emplace_back(mid, j,
                                          parentType); // fallback
                }
            }
        }
    }

    // update distribution
    for (size_t i = 0; i < n; ++i) {
        // current scope provider
        auto &pd = scopeProviders[i];

        auto &types = typeList_[i];
        if (i == 0)
            types.clear();
        else
            // types(typeList_[ast.scopes[i].parent].begin(),
            //              typeList_[ast.scopes[i].parent].end());
            types = typeList_[ast.scopes[i].parent];

        for (const auto &kv : pd.constIndex) {
            // only interested with those types can be interacted with
            if (kv.second.empty() || (std::find(types.begin(), types.end(),
                                                kv.first) != types.end()))
                continue;

            types.push_back(kv.first);
        }

        for (const auto &kv : pd.mutableIndex) {
            // only interested with those types can be interacted with
            if (kv.second.empty() || (std::find(types.begin(), types.end(),
                                                kv.first) != types.end()))
                continue;

            types.push_back(kv.first);
        }

        for (const auto &kv : pd.funcIndex) {

            // only interested with those types can be interacted with
            if (kv.second.empty() || (std::find(types.begin(), types.end(),
                                                kv.first) != types.end()))
                continue;

            types.push_back(kv.first);
        }

        typeDist_[i] = makeIndexDist(types.size());
        for (auto t : types) {
            pd.constDist[t] = makeIndexDist(pd.constIndex[t].size());
            pd.mutableDist[t] = makeIndexDist(pd.mutableIndex[t].size());
            pd.funcDist[t] = makeIndexDist(pd.funcIndex[t].size());
        }
    }
}

/*------------------ pickRandomVar ------------------*/
TypeID BuiltinContext::pickRandomType(ScopeID scopeID) {
    const auto &types = typeList_[scopeID];
    if (types.empty())
        return 0;
    return types[typeDist_[scopeID](rng)];
}

PropKey BuiltinContext::pickRandomVar(ScopeID scopeID, TypeID type,
                                      ObjectKind valueKind,
                                      const std::vector<ASTScope> &scopes) {
    auto &pd = scopeProviders[scopeID];
    auto &md = pd.selectDist(valueKind);
    if (!md.contains(type))
        type = 0;

    auto itDist = md.find(type);
    if (itDist == md.end()) {
        if (scopeID != 0) {
            const auto parent = scopes[scopeID].parent;
            if (parent >= 0)
                return pickRandomVar(parent, type, valueKind, scopes);
        }
        return PropKey::emptyKey();
    }

    auto &index = pd.selectIndex(valueKind);
    auto itVec = index.find(type);
    const bool hasCand = (itVec != index.end() && !itVec->second.empty() &&
                          itDist->second.max() < itVec->second.size());

    if (scopeID != 0 &&
        (pd.useParent.at(static_cast<int>(valueKind))(rng) || !hasCand)) {
        const auto parent = scopes[scopeID].parent;
        if (parent >= 0)
            return pickRandomVar(parent, type, valueKind, scopes);
    }

    if (!hasCand)
        return PropKey::emptyKey();
    return itVec->second[itDist->second(rng)];
}

PropKey BuiltinContext::pickRandomVar(ScopeID scopeID, ObjectKind valueKind,
                                      const std::vector<ASTScope> &scopes) {
    return pickRandomVar(scopeID, pickRandomType(scopeID), valueKind, scopes);
}

PropKey BuiltinContext::pickRandomVar(ScopeID scopeID,
                                      const std::vector<TypeID> &types,
                                      ObjectKind valueKind,
                                      const std::vector<ASTScope> &scopes) {
    if (types.empty())
        return PropKey::emptyKey();
    TypeID t = types[rng() % types.size()];
    return pickRandomVar(scopeID, t, valueKind, scopes);
}

ObjectKind FuzzingAST::BuiltinContext::pickValueKind() {
    return static_cast<ObjectKind>(pickValueKindDist(rng));
}

bool FuzzingAST::BuiltinContext::respectType() { return respectType_dist(rng); }
