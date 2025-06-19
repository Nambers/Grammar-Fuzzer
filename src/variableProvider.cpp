#include "ast.hpp"
#include "log.hpp"
#include <algorithm>
#include <iterator>
#include <random>

extern std::mt19937 rng;

using namespace FuzzingAST;

void BuiltinContext::updateVars(const ASTData &ast) {
    size_t n = ast.ast.scopes.size();
    mutableIndex_.assign(n, {});
    constIndex_.assign(n, {});
    typeList_.assign(n, {});
    typeDist_.assign(n, {});
    varDist_.assign(n, {});

    for (size_t i = 0; i < n; ++i) {
        if (int p = ast.ast.scopes[i].parent; p != -1) {
            mutableIndex_[i] = mutableIndex_[p];
            constIndex_[i] = constIndex_[p];
        }

        for (const auto &[tid, pis] : ast.ast.classProps) {
            for (size_t j = 0; j < pis.size(); ++j) {
                const auto &pi = pis[j];
                if (pi.scope > (ScopeID)i)
                    continue;

                auto &index = pi.isConst ? constIndex_ : mutableIndex_;
                index[i][pi.type].emplace_back(false, j, tid);
                index[i][0].emplace_back(false, j, tid); // fallback
            }
        }

        for (auto &kv : builtinsProps) {
            TypeID parentType = kv.first;
            const auto &pis = kv.second;
            for (size_t j = 0; j < pis.size(); ++j) {
                const auto &pi = pis[j];
                auto &index = pi.isConst ? constIndex_ : mutableIndex_;
                index[i][pi.type].emplace_back(true, j, parentType);
                index[i][0].emplace_back(true, j, parentType); // fallback
            }
        }

        auto &types = typeList_[i];
        for (auto &kv : mutableIndex_[i]) {
            // only interested with those types can be interacted with
            if (!builtinsProps.contains(kv.first) &&
                !ast.ast.classProps.contains(kv.first))
                continue;
            if (!kv.second.empty())
                types.push_back(kv.first);
        }
        if (!types.empty()) {
            typeDist_[i] =
                std::uniform_int_distribution<size_t>(0, types.size() - 1);
            for (auto t : types) {
                auto &bucket = mutableIndex_[i][t];
                varDist_[i][t] =
                    std::uniform_int_distribution<size_t>(0, bucket.size() - 1);
            }
        }
    }
}

/*------------------ updateFuncs ------------------*/
void BuiltinContext::updateFuncs(const ASTData &ast) {
    size_t n = ast.ast.scopes.size();
    funcList_.assign(n, {});
    funcDist_.assign(n, {});
    funcCnts_.assign(n, 0);
    methodDist_.clear();

    for (auto &[tid, props] : builtinsProps) {
        if (methodDist_.find(tid) == methodDist_.end()) {
            size_t s = props.size();
            if (ast.ast.classProps.contains(tid))
                s += ast.ast.classProps.at(tid).size();
            if (s)
                methodDist_.emplace(
                    tid, std::uniform_int_distribution<size_t>(0, s - 1));
        }
    }

    for (auto &[tid, props] : ast.ast.classProps) {
        if (methodDist_.contains(tid))
            continue; // already added in builtinsProps
        size_t s = props.size();
        if (s)
            methodDist_.emplace(
                tid, std::uniform_int_distribution<size_t>(0, s - 1));
    }

    for (size_t i = 0; i < n; ++i) {
        std::vector<PropKey> cands;
        cands.reserve(128);

        for (auto &kv : ast.ast.classProps) {
            TypeID tid = kv.first;
            auto &vec = kv.second;
            for (size_t j = 0; j < vec.size(); ++j) {
                const auto &pi = vec[j];
                if (pi.isCallable && pi.scope <= (ScopeID)i) {
                    cands.emplace_back(false, j, tid);
                }
            }
        }

        for (auto &kv : builtinsProps) {
            TypeID tid = kv.first;
            auto &vec = kv.second;
            for (size_t j = 0; j < vec.size(); ++j) {
                if (vec[j].isCallable) {
                    cands.emplace_back(true, j, tid);
                }
            }
        }

        funcList_[i] = std::move(cands);
        funcCnts_[i] = funcList_[i].size();
        if (funcCnts_[i] > 0) {
            funcDist_[i] =
                std::uniform_int_distribution<size_t>(0, funcCnts_[i] - 1);
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
                                      bool isConst) {
    const auto &mp = (isConst ? constIndex_ : mutableIndex_)[scopeID];
    if (type == 0) {
        if (mp.empty())
            return PropKey::emptyKey();
        type = pickRandomType(scopeID);
        if (type == 0)
            return PropKey::emptyKey();
    }

    auto mit = mp.find(type);
    if (mit == mp.end() || mit->second.empty())
        return PropKey::emptyKey();

    const auto &bucket = mit->second;
    return bucket[varDist_[scopeID][type](rng)];
}

PropKey BuiltinContext::pickRandomVar(ScopeID scopeID, bool isConst) {
    return pickRandomVar(scopeID, pickRandomType(scopeID), isConst);
}

PropKey BuiltinContext::pickRandomVar(ScopeID scopeID,
                                      const std::vector<TypeID> &types,
                                      bool isConst) {
    if (types.empty())
        return PropKey::emptyKey();
    TypeID t = types[rng() % types.size()];
    return pickRandomVar(scopeID, t, isConst);
}

/*------------------ pickRandomMethod ------------------*/
PropKey BuiltinContext::pickRandomFunc(ScopeID scopeID) {

    if (scopeID >= funcList_.size())
        return PropKey::emptyKey();
    const auto &lst = funcList_[scopeID];
    if (lst.empty())
        return PropKey::emptyKey();

    size_t pick = funcDist_[scopeID](rng);
    return lst[pick];
}

PropKey BuiltinContext::pickRandomMethod(TypeID tid) {
    auto itD = methodDist_.find(tid);
    if (itD == methodDist_.end())
        return PropKey::emptyKey();

    size_t idx = itD->second(rng);

    if (auto itB = builtinsProps.find(tid); itB != builtinsProps.end()) {
        if (idx < itB->second.size())
            return PropKey{true, idx, tid};
        idx -= itB->second.size();
    }
    return {false, idx, tid};
}

bool FuzzingAST::BuiltinContext::pickConst() { return pickConstDist(rng); }
