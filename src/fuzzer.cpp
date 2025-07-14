#include "fuzzer.hpp"
#include "FuzzSchedulerState.hpp"
#include "UI.hpp"
#include "ast.hpp"
#include "driver.hpp"
#include "emit.hpp"
#include "log.hpp"
#include "mutators.hpp"
#include "serialization.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <fstream>
#include <iostream>
#include <signal.h>

#define WRITE_STDERR(msg)                                                      \
    do {                                                                       \
        (void)write(STDERR_FILENO, msg, strlen(msg));                          \
    } while (0)
#define WRITE_STDOUT(msg)                                                      \
    do {                                                                       \
        (void)write(STDOUT_FILENO, msg, strlen(msg));                          \
    } while (0)

using namespace FuzzingAST;
extern "C" void __sanitizer_set_death_callback(void (*)(void));
extern std::vector<std::string> FuzzingAST::cacheCorpus;

std::string data_backup;
std::string data_backup2;
static size_t totalRounds = 0;
static FuzzSchedulerState scheduler;
uint32_t newEdgeCnt = 0;
uint32_t errCnt = 0;
uint32_t corpusSize = 0;

std::mt19937 rng(std::random_device{}());

static int testOneInput(ASTData &data, BuiltinContext &ctx) {
    data_backup = nlohmann::json(data.ast).dump();
    auto tmp = getInitExecutionContext();
    return runAST(data.ast, ctx, tmp);
}

extern "C" void __sanitizer_print_stack_trace();

static void print_backtrace() {
    WRITE_STDERR("\n=== Backtrace ===\n");
    __sanitizer_print_stack_trace();
    WRITE_STDERR("==================\n");
}

static void crash_handler() {
    WRITE_STDERR("crash! last saved states:");
    WRITE_STDOUT("\n===AST===\n");
    WRITE_STDOUT(data_backup.c_str());
    WRITE_STDOUT(data_backup2.c_str());
    fuzzerEmitCacheCorpus();
    int cnt = 0;
    for (const auto &data : scheduler.corpus) {
        std::ofstream out("corpus/saved/" + std::to_string(cnt++) + ".json");
        out << nlohmann::json(data.ast).dump();
    }
    // backtrace already printed by sanitizer
    _exit(1);
}

static void sigint_handler(int signo) {
    WRITE_STDERR("crash! sig=");
    WRITE_STDERR(strsignal(signo));
    WRITE_STDERR("\n");
    print_backtrace();
    crash_handler();
    std::_Exit(130);
}

void myTerminateHandler() {
    std::exception_ptr eptr = std::current_exception();
    if (eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception &e) {
            WRITE_STDERR("Unhandled exception:");
            WRITE_STDERR(e.what());
            WRITE_STDERR("\n");
        } catch (...) {
            WRITE_STDERR("Unhandled non-std exception\n");
        }
    } else {
        WRITE_STDERR("Terminate called without an active exception\n");
    }
    print_backtrace();
    crash_handler();
    std::abort();
}

void FuzzingAST::FuzzerInitialize(int *argc, char ***argv) {
    if (argc != NULL && argv != NULL) {
        if (*argc >= 2 && std::strcmp((*argv)[1], "-load-saved") == 0) {
            std::string savedPath = "./corpus/saved";
            if (*argc == 3 && (*argv)[2] != nullptr) {
                savedPath = std::string((*argv)[2]);
            }
            INFO("Loading saved corpus from: {}", savedPath);
            fuzzerLoadCorpus(savedPath, scheduler.corpus);
            scheduler.idx = scheduler.corpus.size() - 1;
        }
    }
    initialize(argc, argv);
    // override potential SIGINT handler in language interpreter
    signal(SIGINT, sigint_handler);
    // signal(SIGSEGV, sigint_handler);
    // signal(SIGABRT, sigint_handler);
    // signal(SIGTERM, sigint_handler);
    std::set_terminate(myTerminateHandler);
    __sanitizer_set_death_callback(crash_handler);
}

static std::vector<ASTNode> testInputStream(ASTData &ast,
                                            FuzzSchedulerState &scheduler) {
    // const auto genNum = ast.ast.scopes[0].declarations.size() * 2;
    auto &ctx = scheduler.ctx;
    // generate execution line-based, then collect affected variable's type and
    // update also fix the incorrect funcSignatures
    ASTScope &scope = ast.ast.scopes[0];
    std::vector<ASTNode> history;

    std::unordered_set<std::string> globalVars;
    auto execCtx = getInitExecutionContext();
    data_backup2.clear();
    data_backup = nlohmann::json(ast.ast).dump() + "\n---DECL_END---\n";
    const auto declRet = runLines(history, ast.ast, ctx, execCtx);
    // get declarations
    if (declRet != 0) {
        PANIC("Failed to run declarations.code={}", declRet);
    };
    history.reserve(200);
    const auto scopeCnt = ast.ast.scopes.size();
    while (scheduler.noEdgeCount <= scheduler.execFailureThreshold() &&
           history.size() < 200) {
        TUI::update(scheduler, scopeCnt);
        ASTNode data;
        if (generate_line(data, ast, ctx, globalVars, 0, scope) != 0) {
            // can't generate a valid line, go mutate declaration
            data_backup2.clear();
            return std::move(history);
        }
        const auto cacheNewEdgeCnt = newEdgeCnt;
        data_backup2 = nlohmann::json(data).dump() + ",";
        // exec
        auto ret = runLine(data, ast.ast, ctx, execCtx);
        if (cacheNewEdgeCnt < newEdgeCnt) {
            // got new edge
            scheduler.noEdgeCount = 0;
        } else {
            // no new edge
            ++scheduler.noEdgeCount;
        }
        if (ret == 0) {
            data_backup += data_backup2;
            data_backup2.clear();
            updateTypes(globalVars, ast, ctx, execCtx);
            scheduler.ctx.updateVars(ast.ast);
            history.push_back(data);
        } else if (ret == -1) {
            // update index to match with fixed result
            scheduler.ctx.update(ast.ast);
        } else if (ret == -2) {
            // timeout
            execCtx = getInitExecutionContext();
            // re-gain the context
            ret = runLines(history, ast.ast, ctx, execCtx);
            if (ret != 0) {
                if (ret == -1)
                    PANIC("Failed to replay lines after timeout");
                else if (ret == -2)
                    PANIC("Timeout while replaying lines after timeout");
            };
        } else {
            PANIC("Unexpected return code from runLine: {}", ret);
        }
        globalVars.clear();
    }
    return std::move(history);
}

void FuzzingAST::fuzzerDriver() {
    cacheCorpus.reserve(MAX_CACHE_SIZE);
    loadBuiltinsFuncs(scheduler.ctx);
    initPrimitiveTypes(scheduler.ctx);
    {
        ASTData data;
        if (scheduler.corpus.empty()) {
            dummyAST(data, scheduler.ctx);
            scheduler.corpus.push_back(data);
            scheduler.idx = 0;
        } else {
            data = scheduler.corpus[scheduler.idx];
        }
        // dummy check
        if (testOneInput(data, scheduler.ctx) != 0) {
            PANIC("Initial AST is not valid.");
        }
    }
    corpusSize = scheduler.corpus.size();
    scheduler.ctx.update(scheduler.corpus[scheduler.idx].ast);
    newEdgeCnt = 0; // reset edge count
    cacheCorpus.reserve(MAX_CACHE_SIZE);
    TUI::initTUI();
    while (true) {
        // if (scheduler.corpus.empty()) {
        //     scheduler.corpus.emplace_back(std::make_shared<ASTData>());
        // }
        switch (scheduler.phase) {
        case MutationPhase::ExecutionGeneration: {
            // continue generation on current
            const auto cacheNewEdgeCnt = newEdgeCnt;
            ASTData newData = scheduler.corpus.at(scheduler.idx);
            scheduler.ctx.update(newData.ast);
            auto lines = testInputStream(newData, scheduler);
            if (cacheNewEdgeCnt < newEdgeCnt) {
                // got new edge
                scheduler.update(1, newData.ast.scopes.size());
                const size_t base = newData.ast.expressions.size();
                newData.ast.expressions.insert(newData.ast.expressions.end(),
                                               lines.begin(), lines.end());
                auto &exprs = newData.ast.scopes[0].expressions;
                exprs.resize(lines.size());

                for (size_t j = 0; j < lines.size(); ++j) {
                    exprs[j] = base + j;
                }
                cacheCorpus.emplace_back(nlohmann::json(newData.ast).dump());
                if (cacheCorpus.size() > MAX_CACHE_SIZE) {
                    fuzzerEmitCacheCorpus();
                    cacheCorpus.clear();
                }
            } else {
                // no new edge
                scheduler.update(0, newData.ast.scopes.size());
            }
            break;
        }
        case MutationPhase::FallbackOldCorpus: {
            scheduler.corpus.erase(scheduler.corpus.begin() + scheduler.idx);
            --corpusSize;
            if (corpusSize > 0) {
                // randomly fallback to one of all
                // maybe don't remove current one?
                scheduler.idx = rng() % corpusSize;
            } else {
                scheduler.idx = 0;
                scheduler.corpus.push_back({});
                ++corpusSize;
            }
            scheduler.update(
                0, scheduler.corpus.at(scheduler.idx).ast.scopes.size());
            newEdgeCnt = 0;
            break;
        }
        case MutationPhase::DeclarationMutation: {
            // continue mutating on current
            ASTData newData = scheduler.corpus.at(scheduler.idx);
            mutate_declaration(newData, scheduler.ctx);
            newData.ast.expressions.clear();
            generate_execution(newData, scheduler.ctx);
            scheduler.update(0, newData.ast.scopes.size());
            // if current newEdgeCnt is 0, newData replaced the current one
            if (newEdgeCnt > 0) {
                scheduler.corpus.push_back(newData);
                ++corpusSize;
            } else
                scheduler.corpus[scheduler.idx] = newData;
            scheduler.idx = corpusSize - 1;
            newEdgeCnt = 0; // reset edge count for declaration change
            break;
        }
        }
    }
}
