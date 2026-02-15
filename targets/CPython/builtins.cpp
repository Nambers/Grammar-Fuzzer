#include "ast.hpp"
#include "driver.hpp"
#include "serialization.hpp"
#include <fstream>
#include <iostream>
#include <signal.h>

using namespace FuzzingAST;

void FuzzingAST::loadBuiltinsFuncs(BuiltinContext &ctx) {
    std::ifstream in("./targets/CPython/builtins.json");
    if (!in) {
        std::cerr
            << "Failed to open builtins.json, run build.sh to generate it."
            << std::endl;
        raise(SIGINT);
    }
    nlohmann::json j;
    in >> j;

    std::unordered_map<TypeID, std::vector<PropInfo>> tmp;
    for (auto &bucket : j["funcs"].items()) {
        const std::string &tidStr = bucket.key();
        TypeID tid = tidStr == "-1" ? -1 : std::stoi(tidStr);
        tmp.emplace(tid, bucket.value().get<std::vector<PropInfo>>());
    }
    for (auto &mod : j["modules"].items()) {
        ModuleID mid = std::stoi(mod.key());
        std::unordered_map<TypeID, std::vector<PropInfo>> modProps;
        for (auto &bucket : mod.value().items()) {
            const std::string &tidStr = bucket.key();
            TypeID tid = tidStr == "-1" ? -1 : std::stoi(tidStr);
            modProps.emplace(tid, bucket.value().get<std::vector<PropInfo>>());
        }
        ctx.modulesProps.emplace(mid, std::move(modProps));
    }
    ctx.builtinsProps.swap(tmp);
    auto tmp2 = j["types"].get<std::vector<std::string>>();
    ctx.types.swap(tmp2);
    ctx.builtinTypesCnt = ctx.types.size();
    auto tmp3 = j["ops"].get<std::vector<std::vector<std::vector<TypeID>>>>();
    ctx.ops.swap(tmp3);
    auto tmp4 = j["uops"].get<std::vector<std::vector<TypeID>>>();
    ctx.unaryOps.swap(tmp4);
}