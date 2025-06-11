#include "ast.hpp"
#include "log.hpp"
#include <algorithm>
#include <iterator>
#include <random>

extern std::mt19937 rng;

using namespace FuzzingAST;

void BuiltinContext::update(const std::shared_ptr<ASTData> &ast) {
    size_t n = ast->ast.scopes.size();
    index_.clear();
    index_.resize(n);
    typeList_.clear();
    typeList_.resize(n);
    typeDist_.clear();
    typeDist_.resize(n);
    varDist_.clear();
    varDist_.resize(n);
    funcDist_.clear();
    funcDist_.resize(n);
    funcCnts_.clear();
    funcCnts_.resize(n);

    for (size_t i = 0; i < n; ++i) {
        auto &map = index_[i];
        int parent = ast->ast.scopes[i].parent;
        if (parent != -1) {
            map = index_[parent];
        }
        const ASTScope &scope = ast->ast.scopes[i];
        for (NodeID varID : scope.variables) {
            const ASTNode &decl = ast->ast.declarations[varID];
            const std::string name = std::get<std::string>(decl.fields[0].val);
            map[decl.type].push_back(name);
            map[0].push_back(name);
        }

        auto &types = typeList_[i];
        types.clear();
        for (auto &p : map) {
            if (!p.second.empty())
                types.push_back(p.first);
        }
        if (!types.empty()) {
            typeDist_[i] =
                std::uniform_int_distribution<size_t>(0, types.size() - 1);
        }

        for (TypeID t : types) {
            auto &bucket = map[t];
            if (!bucket.empty()) {
                varDist_[i][t] =
                    std::uniform_int_distribution<size_t>(0, bucket.size() - 1);
            }
        }

        if (i == 0) {
            funcCnts_[0] = builtinsFuncsCnt;
        } else {
            funcCnts_[i] = ast->ast.scopes[i].funcSignatures.size();
            if (parent != -1) {
                funcCnts_[i] += funcCnts_[parent];
            }
        }
        funcDist_[i] =
            std::uniform_int_distribution<size_t>(0, funcCnts_[i] - 1);
    }
}

const std::pair<const std::string, FunctionSignature> &
BuiltinContext::pickRandomFunc(const std::shared_ptr<ASTData> &ast,
                               ScopeID scopeID) {
    size_t idx = funcDist_[scopeID](rng);
    auto &scope = ast->ast.scopes[scopeID];
    if (idx < scope.funcSignatures.size()) {
        auto it = scope.funcSignatures.begin();
        std::advance(it, idx);
        return *it;
    }
    idx -= scope.funcSignatures.size();
    while (scope.parent != -1) {
        scope = ast->ast.scopes[scope.parent];
        if (idx < scope.funcSignatures.size()) {
            auto it = scope.funcSignatures.begin();
            std::advance(it, idx);
            return *it;
        }
        idx -= scope.funcSignatures.size();
    }
    auto it = builtinsFuncs.begin();
    std::advance(it, idx);
    return *it;
}

TypeID BuiltinContext::pickRandomType(ScopeID scopeID) {
    const auto &types = typeList_.at(scopeID);
    if (types.empty())
        return 0;
    return types[typeDist_[scopeID](rng)];
}

std::string BuiltinContext::pickRandomVar(ScopeID scopeID, TypeID type) {
    const auto &map = index_.at(scopeID);
    auto it = map.find(type);
    const auto *slice =
        (it != map.end() && !it->second.empty()) ? &it->second : nullptr;
    if (!slice) {
        auto fb = map.find(0);
        if (fb == map.end() || fb->second.empty())
            return {};
        slice = &fb->second;
    }
    auto distIt = varDist_[scopeID].find(type != 0 ? type : 0);
    size_t idx =
        distIt != varDist_[scopeID].end()
            ? distIt->second(rng)
            : std::uniform_int_distribution<size_t>(0, slice->size() - 1)(rng);
    return (*slice)[idx];
}

std::string BuiltinContext::pickRandomVar(ScopeID scopeID) {
    TypeID t = pickRandomType(scopeID);
    return pickRandomVar(scopeID, t);
}

std::string BuiltinContext::pickRandomVar(ScopeID scopeID,
                                          const std::vector<TypeID> &types) {
    if (types.empty())
        return {};
    std::uniform_int_distribution<size_t> distType(0, types.size() - 1);
    TypeID t = types[rng() % types.size()];
    return pickRandomVar(scopeID, t);
}
