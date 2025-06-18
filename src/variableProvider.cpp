#include "ast.hpp"
#include "log.hpp"
#include <algorithm>
#include <iterator>
#include <random>

extern std::mt19937 rng;

using namespace FuzzingAST;

void BuiltinContext::updateVars(const ASTData &ast) {
    size_t n = ast.ast.scopes.size();
    index_.assign(n, {});
    typeList_.assign(n, {});
    typeDist_.assign(n, {});
    varDist_.assign(n, {});

    for (size_t i = 0; i < n; ++i) {
        // 1) 继承父 scope 的映射
        if (int p = ast.ast.scopes[i].parent; p != -1) {
            index_[i] = index_[p];
        }

        // 2) 把 ASTData::ast.variables 中 scope==i 的属性（变量/函数）插入
        for (size_t vid = 0; vid < ast.ast.variables.size(); ++vid) {
            const auto &pi = ast.ast.variables[vid];
            if (pi.scope != (ScopeID)i)
                continue;
            index_[i][pi.type].push_back(pi.name);
            index_[i][0].push_back(pi.name); // type=0 as fallback
        }

        // 3) 把 builtinsProps 全部也加入（所有 scope 都可见）
        for (auto &kv : builtinsProps) {
            TypeID t = kv.first;
            for (auto &pi : kv.second) {
                index_[i][t].push_back(pi.name);
                index_[i][0].push_back(pi.name);
            }
        }

        // 4) 构建 typeList_, typeDist_，和 varDist_
        auto &types = typeList_[i];
        for (auto &kv : index_[i]) {
            if (!kv.second.empty())
                types.push_back(kv.first);
        }
        if (!types.empty()) {
            typeDist_[i] =
                std::uniform_int_distribution<size_t>(0, types.size() - 1);
            for (auto t : types) {
                auto &bucket = index_[i][t];
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

    for (size_t i = 0; i < n; ++i) {
        std::vector<PropKey> cands;
        cands.reserve(128);

        // 1) AST::classProps 里所有作用域可见的方法
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

        // 2) 全部内建方法
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

/*------------------ pickRandomVar 相关 ------------------*/
TypeID BuiltinContext::pickRandomType(ScopeID scopeID) {
    const auto &types = typeList_[scopeID];
    if (types.empty())
        return 0;
    return types[typeDist_[scopeID](rng)];
}

std::string BuiltinContext::pickRandomVar(ScopeID scopeID, TypeID type) {
    const auto &mp = index_[scopeID];
    if (type == 0) {
        if (mp.empty())
            return {};
        std::uniform_int_distribution<size_t> d(0, mp.size() - 1);
        auto it = mp.begin();
        std::advance(it, d(rng));
        type = it->first;
    }
    auto mit = mp.find(type);
    if (mit == mp.end() || mit->second.empty())
        return {};
    const auto &bucket = mit->second;
    return bucket[varDist_.at(scopeID).at(type)(rng)];
}

std::string BuiltinContext::pickRandomVar(ScopeID scopeID) {
    return pickRandomVar(scopeID, pickRandomType(scopeID));
}

std::string BuiltinContext::pickRandomVar(ScopeID scopeID,
                                          const std::vector<TypeID> &types) {
    if (types.empty())
        return {};
    TypeID t = types[rng() % types.size()];
    return pickRandomVar(scopeID, t);
}

/*------------------ pickRandomMethod ------------------*/
std::optional<PropInfo> BuiltinContext::pickRandomFunc(const AST &ast,
                                                       ScopeID scopeID) {
    // 越界或空直接空
    if (scopeID >= funcList_.size())
        return std::nullopt;
    const auto &lst = funcList_[scopeID];
    if (lst.empty())
        return std::nullopt;

    size_t pick = funcDist_[scopeID](rng);
    const auto &key = lst[pick];

    if (key.isBuiltin) {
        const auto &vec = builtinsProps.at(key.type);
        return vec[key.idx];
    } else {
        const auto &vec = ast.classProps.at(key.type);
        return vec[key.idx];
    }
}

std::optional<PropInfo> BuiltinContext::pickRandomMethod(const AST &ast,
                                                         TypeID tid) {
    auto itD = methodDist_.find(tid);
    if (itD == methodDist_.end())
        return std::nullopt;

    size_t idx = itD->second(rng);
    // 先尝试 builtinsProps
    if (auto itB = builtinsProps.find(tid); itB != builtinsProps.end()) {
        if (idx < itB->second.size())
            return itB->second[idx];
        idx -= itB->second.size();
    }
    // 再尝试 AST classProps
    if (auto itC = ast.classProps.find(tid); itC != ast.classProps.end()) {
        const auto &vec = itC->second;
        if (idx < vec.size())
            return vec[idx];
    }
    return std::nullopt;
}