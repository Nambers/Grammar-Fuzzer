#include "fuzzer.hpp"
#include "FuzzSchedulerState.hpp"
#include "ast.hpp"
#include "driver.hpp"
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

static nlohmann::json data_backup;
static size_t totalRounds = 0;
uint32_t newEdgeCnt = 0;

std::mt19937 rng(std::random_device{}());

int testOneInput(const std::shared_ptr<ASTData> &data, const BuiltinContext &ctx) {
    data_backup = data->ast;
    return runAST(data->ast, ctx);
}

void crash_handler() {
    ERROR("crash! last saved states");
    std::string dump = data_backup.dump();
    INFO("AST={}", dump);
    _exit(1);
}

void sigint_handler(int signo) {
    crash_handler();
    std::_Exit(130);
}

AST FuzzingAST::FuzzerInitialize(int *argc, char ***argv) {
    AST ret = {};
    if (argc != NULL && argv != NULL) {
        std::ifstream last_case;
        for (int i = 0; i < *argc; i++) {
            if (std::strncmp((*argv)[i],
                             "-last-case=", strlen("-last-case=")) == 0) {
                std::string_view filename = (*argv)[i] + strlen("-last-case=");
                INFO("Using last-case file: {}", filename);
                last_case.open(filename.data(), std::ios::in);
                if (!last_case.is_open()) {
                    PANIC("Failed to open last-case file: {}", filename);
                }
                ret = nlohmann::json::parse(last_case).get<AST>();
                last_case.close();
                break;
            }
        }
    }
    initialize(argc, argv);
    // override potential SIGINT handler in language interpreter
    signal(SIGINT, sigint_handler);
    __sanitizer_set_death_callback(crash_handler);
    return std::move(ret);
}

void initPrimitiveTypes(BuiltinContext &ctx) {
    ctx.strID = std::find(ctx.types.begin(), ctx.types.end(), "str") - ctx.types.begin();
    ctx.intID = std::find(ctx.types.begin(), ctx.types.end(), "int") - ctx.types.begin();
    ctx.floatID = std::find(ctx.types.begin(), ctx.types.end(), "float") - ctx.types.begin();
    ctx.boolID = std::find(ctx.types.begin(), ctx.types.end(), "bool") - ctx.types.begin();
}

void FuzzingAST::fuzzerDriver(AST initAST) {
    FuzzSchedulerState scheduler;
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
        if (scheduler.corpus.empty()) {
            scheduler.corpus.emplace_back(std::make_shared<ASTData>());
        }
        std::shared_ptr<ASTData> newData;
        switch (scheduler.phase) {
        case MutationPhase::ExecutionGeneration: {
            // continue generation on current
            newData =
                std::make_shared<ASTData>(*scheduler.corpus[scheduler.idx]);
            generate_execution(newData);
            // if fail to run, drop the result and do it again.
            auto cacheNewEdgeCnt = newEdgeCnt;
            if (testOneInput(newData, scheduler.ctx) == 0) {
                scheduler.update(newEdgeCnt > cacheNewEdgeCnt,
                                 newData->ast.declarations.size());
            }
            break;
        }
        case MutationPhase::FallbackOldCorpus: {
            if (scheduler.corpus.size() > 1) {
                // randomly fallback to one of first half of the corpus
                scheduler.corpus.erase(scheduler.corpus.begin() +
                                       scheduler.idx);
                scheduler.idx =
                    std::uniform_int_distribution<>(0, scheduler.idx / 2)(rng);
                scheduler.update(0, newData->ast.declarations.size());
                break;
            }
        }
            [[fallthrough]];
        case MutationPhase::DeclarationMutation: {
            newEdgeCnt = 0; // reset edge count for declaration change
            // continue mutating on current
            newData =
                std::make_shared<ASTData>(*scheduler.corpus[scheduler.idx]);
            do {
                mutate_declaration(newData, scheduler.ctx);
            } while (testOneInput(newData, scheduler.ctx) != 0);
            scheduler.update(0, newData->ast.declarations.size());
            scheduler.corpus.emplace_back(newData);
            scheduler.idx = scheduler.corpus.size() - 1;
            break;
        }
        }
    }
}
