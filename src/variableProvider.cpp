#include "ast.hpp"
#include "log.hpp"
#include <algorithm>
#include <iterator>
#include <random>

extern std::mt19937 rng;

using namespace FuzzingAST;

void BuiltinContext::update(ASTData &ast) {
    size_t n = ast.ast.scopes.size();
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
        int parent = ast.ast.scopes[i].parent;
        if (parent != -1) {
            map = index_[parent];
        }
        const ASTScope &scope = ast.ast.scopes[i];
        for (NodeID varID : scope.variables) {
            const ASTNode &decl = ast.ast.declarations[varID];
            const std::string &name = std::get<std::string>(decl.fields[0].val);
            map[decl.type].push_back(name);
            map[0].push_back(name);
        }

        auto &types = typeList_[i];
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
            funcCnts_[i] = ast.ast.scopes[i].funcSignatures.size();
            if (parent != -1) {
                funcCnts_[i] += funcCnts_[parent];
            }
        }
        funcDist_[i] =
            std::uniform_int_distribution<size_t>(0, funcCnts_[i] - 1);
    }
}

const std::pair<const std::string, FunctionSignature> &
BuiltinContext::pickRandomFunc(const ASTData &ast, const ScopeID scopeID) {
    size_t idx = funcDist_[scopeID](rng);
    ScopeID parentScopeID;
    {
        const auto &scope = ast.ast.scopes[scopeID];
        if (idx < scope.funcSignatures.size()) {
            auto it = scope.funcSignatures.begin();
            std::advance(it, idx);
            return *it;
        }
        idx -= scope.funcSignatures.size();
        parentScopeID = ast.ast.scopes[scopeID].parent;
    }
    while (parentScopeID != -1) {
        const auto &parentScope = ast.ast.scopes[parentScopeID];
        parentScopeID = parentScope.parent;
        if (idx < parentScope.funcSignatures.size()) {
            auto it = parentScope.funcSignatures.begin();
            std::advance(it, idx);
            return *it;
        }
        idx -= parentScope.funcSignatures.size();
    }
    auto it = builtinsFuncs.begin();
    std::advance(it, idx);
    return *it;
}

TypeID BuiltinContext::pickRandomType(ScopeID scopeID) {
    const auto &types = typeList_[scopeID];
    if (types.empty())
        return 0;
    return types[typeDist_[scopeID](rng)];
}

std::string BuiltinContext::pickRandomVar(ScopeID scopeID, TypeID type) {
    const auto &map = index_[scopeID];
    auto it = map.end();
    if (type == 0) {
        // pick random type from current scope
        if (map.empty())
            return {};
        std::uniform_int_distribution<size_t> distType(0, map.size() - 1);
        it = map.begin();
        std::advance(it, distType(rng));
        type = it->first; // pick a random type from the map
    } else
        it = map.find(type);
    if (it == map.end() || it->second.empty())
        return {};
    return it->second[varDist_[scopeID].find(type)->second(rng)];
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
