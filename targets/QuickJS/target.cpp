#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "log.hpp"
#include "target.hpp"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <serialization.hpp>
#include <setjmp.h>
#include <signal.h>
#include <sstream>
#include <sys/time.h>
#include <unistd.h>

using namespace FuzzingAST;

constexpr std::array TARGET_LIBS = {"math", "json"};
extern const std::span targetLibs(TARGET_LIBS);
std::uniform_int_distribution<int> distLib(0, TARGET_LIBS.size() - 1);

extern uint32_t newEdgeCnt;
extern uint32_t errCnt;

static sigjmp_buf timeoutJmp;
static int nullFd = open("/dev/null", O_WRONLY);
static int oldStdout = dup(STDOUT_FILENO);
static int oldStderr = dup(STDERR_FILENO);

extern "C" void __sanitizer_cov_trace_pc_guard_init(uint32_t *start,
                                                    uint32_t *stop) {
    if (start == stop || *start)
        return;
    static uint32_t N = 0;
    for (uint32_t *x = start; x < stop; ++x) {
        *x = ++N;
    }
}

extern "C" void __sanitizer_cov_trace_pc_guard(uint32_t *guard) {
    if (!*guard)
        return;
    newEdgeCnt++;
    *guard = 0;
}

class NullStdIORedirect {
  public:
    NullStdIORedirect() { redirect(); }
    ~NullStdIORedirect() { restore(); }
    static void redirect() {
        dup2(nullFd, STDOUT_FILENO);
        dup2(nullFd, STDERR_FILENO);
    }
    static void restore() {
        dup2(oldStdout, STDOUT_FILENO);
        dup2(oldStderr, STDERR_FILENO);
    }
};

static void alarmHandler(int signum) {
    if (signum == SIGALRM) {
        siglongjmp(timeoutJmp, 1);
    }
}

static void set_timeout_ms(int timeout_ms) {
    struct itimerval timer{};
    timer.it_value.tv_sec = timeout_ms / 1000;
    timer.it_value.tv_usec = (timeout_ms % 1000) * 1000;
    timer.it_interval = {};
    setitimer(ITIMER_REAL, &timer, nullptr);
}

static void clear_timeout() {
    struct itimerval zero{};
    setitimer(ITIMER_REAL, &zero, nullptr);
}

static void installSignalHandler() {
    struct sigaction sa{};
    sa.sa_handler = alarmHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, nullptr) == -1) {
        perror("sigaction");
        std::abort();
    }
}

static QuickJSHandle *newQuickJSHandle() {
    auto *h = new QuickJSHandle();
    h->rt = JS_NewRuntime();
    if (!h->rt) {
        delete h;
        return nullptr;
    }
    JS_SetMemoryLimit(h->rt, 256 * 1024 * 1024);
    h->ctx = JS_NewContext(h->rt);
    if (!h->ctx) {
        JS_FreeRuntime(h->rt);
        delete h;
        return nullptr;
    }
    return h;
}

int FuzzingAST::initialize(int * /*argc*/, char *** /*argv*/) {
    installSignalHandler();
    return 0;
}

int FuzzingAST::finalize() { return 0; }

void FuzzingAST::dummyAST(ASTData &data, const BuiltinContext &ctx) {
    TypeID arrType = -1;
    for (size_t i = 0; i < ctx.types.size(); ++i)
        if (ctx.types[i] == "array") {
            arrType = static_cast<TypeID>(i);
            break;
        }

    constexpr int NUM_SEED = 9;
    data.ast.declarations.resize(NUM_SEED);
    data.ast.declarations[0] =
        ASTNode{ASTNodeKind::DeclareVar, {{"str_a"}, {"\"hello\""}}};
    data.ast.declarations[1] =
        ASTNode{ASTNodeKind::DeclareVar, {{"str_b"}, {"\"world\""}}};
    data.ast.declarations[2] =
        ASTNode{ASTNodeKind::DeclareVar, {{"num_a"}, {int64_t(0)}}};
    data.ast.declarations[3] =
        ASTNode{ASTNodeKind::DeclareVar, {{"num_b"}, {int64_t(42)}}};
    data.ast.declarations[4] = ASTNode{ASTNodeKind::DeclareVar, {{"num_c"}, {3.14}}};
    data.ast.declarations[5] =
        ASTNode{ASTNodeKind::DeclareVar, {{"bool_a"}, {true}}};
    data.ast.declarations[6] =
        ASTNode{ASTNodeKind::DeclareVar, {{"bool_b"}, {false}}};
    data.ast.declarations[7] =
        ASTNode{ASTNodeKind::DeclareVar, {{"arr_a"}, {"[3, 1, 4, 1, 5]"}}};
    data.ast.declarations[8] =
        ASTNode{ASTNodeKind::DeclareVar, {{"obj_a"}, {"({a:1, b:2, c:3})"}}};

    data.ast.classProps[NOT_UNDER_CLASS].resize(NUM_SEED);
    data.ast.classProps[NOT_UNDER_CLASS][0] = PropInfo{ctx.strID, 0, "str_a"};
    data.ast.classProps[NOT_UNDER_CLASS][1] = PropInfo{ctx.strID, 0, "str_b"};
    data.ast.classProps[NOT_UNDER_CLASS][2] = PropInfo{ctx.intID, 0, "num_a"};
    data.ast.classProps[NOT_UNDER_CLASS][3] = PropInfo{ctx.intID, 0, "num_b"};
    data.ast.classProps[NOT_UNDER_CLASS][4] = PropInfo{ctx.floatID, 0, "num_c"};
    data.ast.classProps[NOT_UNDER_CLASS][5] = PropInfo{ctx.boolID, 0, "bool_a"};
    data.ast.classProps[NOT_UNDER_CLASS][6] = PropInfo{ctx.boolID, 0, "bool_b"};
    data.ast.classProps[NOT_UNDER_CLASS][7] = PropInfo{arrType, 0, "arr_a"};
    data.ast.classProps[NOT_UNDER_CLASS][8] = PropInfo{0, 0, "obj_a"};

    data.ast.variables.resize(NUM_SEED);
    for (int i = 0; i < NUM_SEED; ++i) {
        data.ast.variables[i] = PropKey{NO_MODULE, static_cast<size_t>(i), -1};
        data.ast.scopes[0].declarations.push_back(i);
        data.ast.scopes[0].variables.push_back(i);
    }
}

static void errorCallback(const std::string &errMsg, AST & /*ast*/,
                          BuiltinContext & /*ctx*/,
                          std::optional<ASTNode> /*node*/ = std::nullopt) {
#ifndef DISABLE_DEBUG_OUTPUT
    ERROR("QuickJS error: {}", errMsg);
#endif
}

static Exe_Result runJSStr(JSContext *ctx, const std::string &code, AST &ast,
                           BuiltinContext &builtinCtx, bool echo,
                           std::optional<ASTNode> node = std::nullopt,
                           uint32_t timeoutMs = RUNLINE_TIMEOUT_MS) {
    if (echo) {
        std::cout << "[Generated JS]:\n" << code << "\n";
    }

    if (sigsetjmp(timeoutJmp, 1) == 0) {
        set_timeout_ms(timeoutMs);
        JSValue result =
            JS_Eval(ctx, code.c_str(), code.size(), "<fuzz>", JS_EVAL_TYPE_GLOBAL);
        clear_timeout();

        if (JS_IsException(result)) {
            ++errCnt;
            std::string errMsg("<quickjs exception>");
            JSValue exc = JS_GetException(ctx);
            const char *msg = JS_ToCString(ctx, exc);
            if (msg) {
                errMsg = msg;
                JS_FreeCString(ctx, msg);
            }
            JS_FreeValue(ctx, exc);
            JS_FreeValue(ctx, result);
            errorCallback(errMsg, ast, builtinCtx, std::move(node));
            return Exe_Result::ERR;
        }
        JS_FreeValue(ctx, result);
        return Exe_Result::OK;
    } else {
        clear_timeout();
        ERROR("QuickJS execution timed out");
        return Exe_Result::TIMEOUT;
    }
}

Exe_Result FuzzingAST::runLine(const ASTNode &node, AST &ast,
                               BuiltinContext &ctx,
                               std::unique_ptr<ExecutionContext> &excCtx,
                               bool echo) {
    std::ostringstream script;
    nodeToJS(script, node, ast, ctx, 0);

    auto *h = reinterpret_cast<QuickJSHandle *>(excCtx->getContext());
    auto ret = runJSStr(h->ctx, script.str(), ast, ctx, echo, std::move(node));
    if (ret == Exe_Result::TIMEOUT)
        excCtx->releasePtr();
    return ret;
}

Exe_Result FuzzingAST::runLines(const std::vector<ASTNode> &nodes, AST &ast,
                                BuiltinContext &ctx,
                                std::unique_ptr<ExecutionContext> &excCtx,
                                bool echo) {
    std::ostringstream script;
    for (auto nodeID : ast.scopes[0].declarations) {
        const auto &node = ast.declarations[nodeID];
        if (node.kind != ASTNodeKind::Function)
            nodeToJS(script, node, ast, ctx, 0);
    }
    for (const auto &node : nodes)
        nodeToJS(script, node, ast, ctx, 0);

    auto *h = reinterpret_cast<QuickJSHandle *>(excCtx->getContext());
    auto ret = runJSStr(h->ctx, script.str(), ast, ctx, echo, std::nullopt,
                        RUNLINES_TIMEOUT_MS);
    if (ret == Exe_Result::TIMEOUT)
        excCtx->releasePtr();
    return ret;
}

Exe_Result FuzzingAST::runAST(AST &ast, BuiltinContext &ctx,
                              std::unique_ptr<ExecutionContext> &excCtx,
                              bool echo) {
    std::ostringstream script;
    scopeToJS(script, 0, ast, ctx, 0);

    auto *h = reinterpret_cast<QuickJSHandle *>(excCtx->getContext());
    auto ret = runJSStr(h->ctx, script.str(), ast, ctx, echo);
    if (ret == Exe_Result::TIMEOUT)
        excCtx->releasePtr();
    return ret;
}

Exe_Result FuzzingAST::reflectObject(AST &ast, ASTScope &scope,
                                     const ScopeID /*sid*/, BuiltinContext &ctx) {
    std::ostringstream script;
    for (NodeID id : scope.declarations) {
        const auto &node = ast.declarations[id];
        if (node.kind != ASTNodeKind::Function)
            nodeToJS(script, node, ast, ctx, 0);
    }
    std::string code = script.str();
    if (code.empty())
        return Exe_Result::OK;

    auto *h = newQuickJSHandle();
    if (!h)
        return Exe_Result::ERR;

    JSValue result =
        JS_Eval(h->ctx, code.c_str(), code.size(), "<reflect>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JS_FreeValue(h->ctx, result);
        if (h->ctx)
            JS_FreeContext(h->ctx);
        if (h->rt)
            JS_FreeRuntime(h->rt);
        delete h;
        return Exe_Result::ERR;
    }

    JS_FreeValue(h->ctx, result);
    if (h->ctx)
        JS_FreeContext(h->ctx);
    if (h->rt)
        JS_FreeRuntime(h->rt);
    delete h;

    ctx.updateVars(ast);
    return Exe_Result::OK;
}

std::unique_ptr<ExecutionContext> FuzzingAST::getInitExecutionContext() {
    QuickJSHandlePtr handle(newQuickJSHandle());
    if (!handle)
        PANIC("Failed to initialize QuickJS runtime/context");
    return std::make_unique<QuickJSExecutionContext>(std::move(handle));
}

void FuzzingAST::updateTypes(ASTData &ast, BuiltinContext &ctx,
                             std::unique_ptr<ExecutionContext> &excCtx) {
    auto *h = reinterpret_cast<QuickJSHandle *>(excCtx->getContext());
    JSContext *jctx = h->ctx;

    JSValue global = JS_GetGlobalObject(jctx);

    for (VarID varID : ast.ast.scopes[0].variables) {
        auto &varInfo = unfoldKey(ast.ast.variables.at(varID), ast.ast, ctx);
        const std::string &name = varInfo.name;

        JSValue val = JS_GetPropertyStr(jctx, global, name.c_str());
        if (JS_IsUndefined(val)) {
            JS_FreeValue(jctx, val);
            continue;
        }

        const char *typeName = nullptr;
        if (JS_IsBool(val))
            typeName = "boolean";
        else if (JS_IsNumber(val))
            typeName = "number";
        else if (JS_IsString(val))
            typeName = "string";
        else if (JS_IsFunction(jctx, val))
            typeName = "function";
        else if (JS_IsObject(val)) {
            if (JS_IsArray(jctx, val))
                typeName = "array";
            else
                typeName = "object";
        }

        if (typeName) {
            TypeID tid = resolveType(typeName, ctx, ast.ast, 0);
            if (tid >= 0)
                varInfo.type = tid;
        }

        JS_FreeValue(jctx, val);
    }

    JS_FreeValue(jctx, global);
}
