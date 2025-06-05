#include "fuzzer.hpp"
#include "FuzzSchedulerState.hpp"
#include "ast.hpp"
#include "driver.hpp"
#include "emit.hpp"
#include "log.hpp"
#include "mutators.hpp"
#include "serialization.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <signal.h>

using namespace FuzzingAST;
extern "C" void __sanitizer_set_death_callback(void (*)(void));
constexpr size_t SAVE_POINT = 1000; // save every 1000 rounds

static std::string data_backup;
static size_t totalRounds = 0;
static FuzzSchedulerState scheduler;
uint32_t newEdgeCnt = 0;

std::mt19937 rng(std::random_device{}());

int testOneInput(const std::shared_ptr<ASTData> &data,
                 const BuiltinContext &ctx) {
    data_backup = nlohmann::json(data->ast).dump();
    return runAST(data->ast, ctx);
}

void crash_handler() {
    ERROR("crash! last saved states");
    INFO("AST={}", data_backup);
    fuzzerEmitCacheCorpus();
    int cnt = 0;
    for (const auto &data : scheduler.corpus) {
        std::ofstream out("corpus/saved/" + std::to_string(cnt++) + ".json");
        out << nlohmann::json(data->ast).dump();
    }
    _exit(1);
}

void sigint_handler(int signo) {
    crash_handler();
    std::_Exit(130);
}

AST FuzzingAST::FuzzerInitialize(int *argc, char ***argv) {
    AST ret = {};
    if (argc != NULL && argv != NULL) {
        if(*argc >= 1 && std::strcmp((*argv)[0], "-load-saved") == 0){
            std::string savedPath = "corpus/saved/";
            if(*argc == 2 && (*argv)[1] != nullptr) {
                savedPath = (*argv)[1];
            }
            INFO("Loading saved corpus from: {}", savedPath);
            fuzzerLoadCorpus(savedPath, scheduler.corpus);
            scheduler.idx = scheduler.corpus.size() - 1;
        }
    }
    initialize(argc, argv);
    // override potential SIGINT handler in language interpreter
    signal(SIGINT, sigint_handler);
    __sanitizer_set_death_callback(crash_handler);
    return std::move(ret);
}

void FuzzingAST::fuzzerDriver(AST initAST) {
    cacheCorpus.reserve(MAX_CACHE_SIZE);
    loadBuiltinsFuncs(scheduler.ctx);
    initPrimitiveTypes(scheduler.ctx);
    std::shared_ptr<ASTData> data = std::make_shared<ASTData>();
    data->ast = std::move(initAST);
    if (data->ast.declarations.empty()) {
        dummyAST(data, scheduler.ctx);
    }
    scheduler.corpus.emplace_back(data);
    // dummy check
    if (testOneInput(data, scheduler.ctx) != 0) {
        PANIC("Initial AST is not valid.");
    }
    newEdgeCnt = 0; // reset edge count
    while (true) {
        // if (scheduler.corpus.empty()) {
        //     scheduler.corpus.emplace_back(std::make_shared<ASTData>());
        // }
        switch (scheduler.phase) {
        case MutationPhase::ExecutionGeneration: {
            // continue generation on current
            std::shared_ptr<ASTData> newData =
                std::make_shared<ASTData>(*scheduler.corpus[scheduler.idx]);
            newData->ast.expressions.clear();
            generate_execution(newData, scheduler.ctx);
            // if fail to run, drop the result and do it again.
            const auto cacheNewEdgeCnt = newEdgeCnt;
            if (testOneInput(newData, scheduler.ctx) == 0) {
                if (newEdgeCnt > cacheNewEdgeCnt) {
                    scheduler.update(1, newData->ast.declarations.size());

                    cacheCorpus.emplace_back(
                        nlohmann::json(newData->ast).dump());
                    if (cacheCorpus.size() > MAX_CACHE_SIZE) {
                        fuzzerEmitCacheCorpus();
                        cacheCorpus.clear();
                    }

                } else {
                    scheduler.update(0, newData->ast.declarations.size());
                }
            }
            break;
        }
        case MutationPhase::FallbackOldCorpus: {
            if (scheduler.corpus.size() >= 2) {
                // randomly fallback to one of first half of the corpus
                scheduler.corpus.erase(scheduler.corpus.begin() +
                                       scheduler.idx);
                scheduler.idx = rng() % (scheduler.corpus.size() / 2);
                scheduler.update(
                    0,
                    scheduler.corpus[scheduler.idx]->ast.declarations.size());
                break;
            }
        }
            [[fallthrough]];
        case MutationPhase::DeclarationMutation: {
            newEdgeCnt = 0; // reset edge count for declaration change
            // continue mutating on current
            std::shared_ptr<ASTData> newData =
                std::make_shared<ASTData>(*scheduler.corpus[scheduler.idx]);
            mutate_declaration(newData, scheduler.ctx);
            scheduler.update(0, newData->ast.declarations.size());
            scheduler.corpus.emplace_back(newData);
            scheduler.idx = scheduler.corpus.size() - 1;
            break;
        }
        }
    }
}
