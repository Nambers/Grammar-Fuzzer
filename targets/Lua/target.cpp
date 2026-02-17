#include "target.hpp"
#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "log.hpp"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <regex>
#include <serialization.hpp>
#include <setjmp.h>
#include <signal.h>
#include <sstream>
#include <sys/time.h>
#include <unistd.h>
#include <unordered_set>

using namespace FuzzingAST;

extern uint32_t newEdgeCnt;
extern uint32_t errCnt;

static sigjmp_buf timeoutJmp;
static int nullFd = open("/dev/null", O_WRONLY);
static int oldStdout = dup(STDOUT_FILENO);
static int oldStderr = dup(STDERR_FILENO);

// -- SanitizerCoverage hooks -------------------------------------------------
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

// -- Redirect stdout/stderr to /dev/null -------------------------------------
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

// -- Timeout handling (SIGALRM + siglongjmp) ---------------------------------
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

// -- Lua helpers -------------------------------------------------------------
static lua_State *newLuaState() {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    return L;
}

// -- Target interface --------------------------------------------------------
int FuzzingAST::initialize(int * /*argc*/, char *** /*argv*/) {
    installSignalHandler();
    return 0;
}

int FuzzingAST::finalize() { return 0; }

// -- Seed corpus -------------------------------------------------------------
void FuzzingAST::dummyAST(ASTData &data, const BuiltinContext &ctx) {
    // Seed with every primitive type so the variable pool is rich from the
    // start.  A populated table lets table.sort / table.concat exercise
    // their comparison / concatenation paths.  A float number exercises
    // the float branch of the number type.
    TypeID tableType = -1;
    for (size_t i = 0; i < ctx.types.size(); ++i)
        if (ctx.types[i] == "table") {
            tableType = static_cast<TypeID>(i);
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
    data.ast.declarations[4] =
        ASTNode{ASTNodeKind::DeclareVar, {{"num_c"}, {3.14}}};
    data.ast.declarations[5] =
        ASTNode{ASTNodeKind::DeclareVar, {{"bool_a"}, {true}}};
    data.ast.declarations[6] =
        ASTNode{ASTNodeKind::DeclareVar, {{"bool_b"}, {false}}};
    data.ast.declarations[7] =
        ASTNode{ASTNodeKind::DeclareVar, {{"tbl_a"}, {"{3, 1, 4, 1, 5, 9}"}}};
    data.ast.declarations[8] =
        ASTNode{ASTNodeKind::DeclareVar, {{"tbl_b"}, {"{}"}}};

    data.ast.classProps[-1].resize(NUM_SEED);
    data.ast.classProps[-1][0] = PropInfo{ctx.strID,  0, "str_a",  false};
    data.ast.classProps[-1][1] = PropInfo{ctx.strID,  0, "str_b",  false};
    data.ast.classProps[-1][2] = PropInfo{ctx.intID,  0, "num_a",  false};
    data.ast.classProps[-1][3] = PropInfo{ctx.intID,  0, "num_b",  false};
    data.ast.classProps[-1][4] = PropInfo{ctx.floatID, 0, "num_c", false};
    data.ast.classProps[-1][5] = PropInfo{ctx.boolID, 0, "bool_a", false};
    data.ast.classProps[-1][6] = PropInfo{ctx.boolID, 0, "bool_b", false};
    data.ast.classProps[-1][7] = PropInfo{tableType, 0, "tbl_a",  false};
    data.ast.classProps[-1][8] = PropInfo{tableType, 0, "tbl_b",  false};

    data.ast.variables.resize(NUM_SEED);
    for (int i = 0; i < NUM_SEED; ++i) {
        data.ast.variables[i] = PropKey{NO_MODULE, static_cast<size_t>(i), -1};
        data.ast.scopes[0].declarations.push_back(i);
        data.ast.scopes[0].variables.push_back(i);
    }
}

// -- Error callback — dynamically fix builtins from Lua errors ---------------
static void errorCallback(const std::string &errMsg, AST &ast,
                          BuiltinContext &ctx,
                          std::optional<ASTNode> node = std::nullopt) {
#ifndef DISABLE_DEBUG_OUTPUT
    ERROR("Lua error: {}", errMsg);
#endif

    /* --- "bad argument #N to 'func' (type expected, got type)" ------ */
    {
        static const std::regex reBadArg(
            R"(bad argument #(\d+) to '([^']+)' \((\w+) expected, got (\w+)\))");
        std::smatch m;
        if (std::regex_search(errMsg, m, reBadArg)) {
            size_t argNum = std::stoul(m[1]);
            std::string funcName = m[2];
            std::string expType = m[3];

            TypeID expTid = resolveType(expType, ctx, ast, 0);

            // try dotted name first  (e.g. "upper" from string.upper)
            const auto dot = funcName.find('.');
            if (dot != std::string::npos) {
                std::string typeName = funcName.substr(0, dot);
                std::string methodName = funcName.substr(dot + 1);
                TypeID tid = resolveType(typeName, ctx, ast, 0);
                if (tid > 0) {
                    auto &methods = tid < static_cast<TypeID>(ctx.builtinTypesCnt)
                                        ? ctx.builtinsProps[tid]
                                        : ast.classProps[tid];
                    for (auto &pi : methods) {
                        if (pi.name == methodName && pi.isCallable &&
                            argNum - 1 < pi.funcSig.paramTypes.size()) {
                            pi.funcSig.paramTypes[argNum - 1] = expTid;
                            return;
                        }
                    }
                }
            }
            // global function
            auto &globals = ctx.builtinsProps[-1];
            for (auto &pi : globals) {
                if (pi.name == funcName && pi.isCallable &&
                    argNum - 1 < pi.funcSig.paramTypes.size()) {
                    pi.funcSig.paramTypes[argNum - 1] = expTid;
                    return;
                }
            }
        }
    }

    /* --- "attempt to perform arithmetic on a <type> value" -------- */
    {
        static const std::regex reArith(
            R"(attempt to perform arithmetic on a (\w+) value)");
        std::smatch m;
        if (std::regex_search(errMsg, m, reArith)) {
            TypeID badTid = resolveType(m[1], ctx, ast, 0);
            if (badTid > 0) {
                // remove this type from arithmetic ops (ops 0-6: + - * / % ** //)
                for (int opIdx = 0; opIdx < 7 && opIdx < (int)ctx.ops.size(); ++opIdx) {
                    auto &row = ctx.ops[opIdx];
                    if (badTid < (TypeID)row.size()) {
                        row[badTid].clear();
                    }
                    // also remove as RHS partner
                    for (auto &compat : row) {
                        compat.erase(
                            std::remove(compat.begin(), compat.end(), badTid),
                            compat.end());
                    }
                }
            }
            return;
        }
    }

    /* --- "attempt to compare two <type> values" ------------------- */
    {
        static const std::regex reCmp(
            R"(attempt to compare two (\w+) values)");
        std::smatch m;
        if (std::regex_search(errMsg, m, reCmp)) {
            TypeID badTid = resolveType(m[1], ctx, ast, 0);
            if (badTid > 0) {
                // remove from comparison ops (ops 9-12: < > <= >=)
                for (int opIdx = 9; opIdx <= 12 && opIdx < (int)ctx.ops.size(); ++opIdx) {
                    auto &row = ctx.ops[opIdx];
                    if (badTid < (TypeID)row.size()) {
                        row[badTid].clear();
                    }
                    for (auto &compat : row) {
                        compat.erase(
                            std::remove(compat.begin(), compat.end(), badTid),
                            compat.end());
                    }
                }
            }
            return;
        }
    }

    /* --- "attempt to call a <type> value" ------------------------- */
    {
        static const std::regex reCall(
            R"(attempt to call a (\w+) value)");
        std::smatch m;
        if (std::regex_search(errMsg, m, reCall)) {
            // Extract the called name from the node if available
            if (node && node->kind == ASTNodeKind::Call &&
                node->fields.size() >= 2) {
                std::string callName =
                    std::get<std::string>(node->fields[1].val);
                // Try to find and mark as non-callable
                auto dot = callName.rfind('.');
                if (dot != std::string::npos) {
                    std::string typeName = callName.substr(0, dot);
                    std::string methodName = callName.substr(dot + 1);
                    TypeID tid = resolveType(typeName, ctx, ast, 0);
                    auto removeCallable = [&](std::vector<PropInfo> &props) {
                        for (auto it = props.begin(); it != props.end(); ++it) {
                            if (it->name == methodName && it->isCallable) {
                                props.erase(it);
                                return true;
                            }
                        }
                        return false;
                    };
                    if (ctx.builtinsProps.contains(tid))
                        removeCallable(ctx.builtinsProps[tid]);
                    else if (ast.classProps.contains(tid))
                        removeCallable(ast.classProps[tid]);
                }
            }
            return;
        }
    }

    /* --- "attempt to index a <type> value" ------------------------ */
    {
        static const std::regex reIndex(
            R"(attempt to index a (\w+) value)");
        std::smatch m;
        if (std::regex_search(errMsg, m, reIndex)) {
            // The type shouldn't have properties — remove all props
            TypeID badTid = resolveType(m[1], ctx, ast, 0);
            if (badTid > 0 && badTid != resolveType("table", ctx, ast, 0)) {
                if (ctx.builtinsProps.contains(badTid))
                    ctx.builtinsProps.erase(badTid);
                if (ast.classProps.contains(badTid))
                    ast.classProps.erase(badTid);
            }
            return;
        }
    }

    /* --- "expected at most/least N arguments" ---------------------- */
    {
        static const std::regex reArgCnt(
            R"((\w+) expected at most (\d+) arguments?, got (\d+))");
        std::smatch m;
        if (std::regex_search(errMsg, m, reArgCnt)) {
            std::string funcName = m[1];
            size_t expectedMax = std::stoul(m[2]);
            // shrink parameter list to expectedMax
            auto shrink = [&](std::vector<PropInfo> &props) {
                for (auto &pi : props) {
                    if (pi.name == funcName && pi.isCallable &&
                        pi.funcSig.paramTypes.size() > expectedMax) {
                        pi.funcSig.paramTypes.resize(expectedMax);
                        return true;
                    }
                }
                return false;
            };
            if (!shrink(ctx.builtinsProps[-1])) {
                for (auto &[tid, props] : ctx.builtinsProps)
                    if (shrink(props)) break;
            }
            return;
        }
    }
}

// -- Execute a Lua string in state L -----------------------------------------
static int runLuaStr(lua_State *L, const std::string &code, AST &ast,
                     BuiltinContext &ctx, bool echo,
                     std::optional<ASTNode> node = std::nullopt,
                     uint32_t timeoutMs = 600) {
    if (echo) {
        std::cout << "[Generated Lua]:\n" << code << "\n";
    }

    if (sigsetjmp(timeoutJmp, 1) == 0) {
        set_timeout_ms(timeoutMs);
        int ret = luaL_dostring(L, code.c_str());
        clear_timeout();

        if (ret != LUA_OK) {
            ++errCnt;
            std::string errMsg;
            if (lua_isstring(L, -1))
                errMsg = lua_tostring(L, -1);
            lua_pop(L, 1);
            errorCallback(errMsg, ast, ctx, std::move(node));
            return -1;
        }
        return 0;
    } else {
        clear_timeout();
        ERROR("Lua execution timed out");
        return -2;
    }
}

// -- driver.hpp implementation -----------------------------------------------
int FuzzingAST::runLine(const ASTNode &node, AST &ast, BuiltinContext &ctx,
                        std::unique_ptr<ExecutionContext> &excCtx, bool echo) {
    std::ostringstream script;
    nodeToLua(script, node, ast, ctx, 0);
    auto *L = reinterpret_cast<lua_State *>(excCtx->getContext());
    auto ret = runLuaStr(L, script.str(), ast, ctx, echo, std::move(node));
    if (ret == -2)
        excCtx->releasePtr();
    return ret;
}

int FuzzingAST::runLines(const std::vector<ASTNode> &nodes, AST &ast,
                         BuiltinContext &ctx,
                         std::unique_ptr<ExecutionContext> &excCtx, bool echo) {
    std::ostringstream script;
    for (auto nodeID : ast.scopes[0].declarations) {
        const auto &node = ast.declarations[nodeID];
        if (node.kind != ASTNodeKind::Function)
            nodeToLua(script, node, ast, ctx, 0);
    }
    for (const auto &node : nodes)
        nodeToLua(script, node, ast, ctx, 0);

    auto *L = reinterpret_cast<lua_State *>(excCtx->getContext());
    auto ret = runLuaStr(L, script.str(), ast, ctx, echo, std::nullopt, 2000);
    if (ret == -2)
        excCtx->releasePtr();
    return ret;
}

int FuzzingAST::runAST(AST &ast, BuiltinContext &ctx,
                       std::unique_ptr<ExecutionContext> &excCtx, bool echo) {
    std::ostringstream script;
    scopeToLua(script, 0, ast, ctx, 0);
    auto *L = reinterpret_cast<lua_State *>(excCtx->getContext());
    auto ret = runLuaStr(L, script.str(), ast, ctx, echo);
    if (ret == -2)
        excCtx->releasePtr();
    return ret;
}

// -- reflectObject: run declarations, discover new types via Lua C API -------
int FuzzingAST::reflectObject(AST &ast, ASTScope &scope, const ScopeID sid,
                              BuiltinContext &ctx) {
    std::ostringstream script;
    for (NodeID id : scope.declarations) {
        const auto &node = ast.declarations[id];
        if (node.kind != ASTNodeKind::Function)
            nodeToLua(script, node, ast, ctx, 0);
    }
    std::string code = script.str();
    if (code.empty())
        return 0;

    lua_State *L = newLuaState();
    int ret = luaL_dostring(L, code.c_str());
    if (ret != LUA_OK) {
#ifndef DISABLE_DEBUG_OUTPUT
        if (lua_isstring(L, -1))
            ERROR("reflectObject: {}", lua_tostring(L, -1));
#endif
        lua_pop(L, 1);
        lua_close(L);
        return -1;
    }

    // iterate globals, discover tables with metatables → new class-like types
    lua_pushglobaltable(L);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_isstring(L, -2) && lua_istable(L, -1)) {
            const char *name = lua_tostring(L, -2);
            // check if it has a metatable (our class marker)
            if (lua_getmetatable(L, -1)) {
                lua_pop(L, 1); // pop metatable
                std::string typeName(name);
                if (resolveType(typeName, ctx, ast, sid) == 0 &&
                    typeName != "object") {
                    scope.types.push_back(typeName);
                }
            }
        }
        lua_pop(L, 1); // pop value
    }
    lua_pop(L, 1); // pop global table

    lua_close(L);
    ctx.update(ast);
    return 0;
}

// -- Create a fresh Lua state as execution context ---------------------------
std::unique_ptr<ExecutionContext> FuzzingAST::getInitExecutionContext() {
    LuaStatePtr state(newLuaState());
    return std::make_unique<LuaExecutionContext>(std::move(state));
}

// -- Update variable types after execution -----------------------------------
void FuzzingAST::updateTypes(const std::unordered_set<std::string> &globalVars,
                             ASTData &ast, BuiltinContext &ctx,
                             std::unique_ptr<ExecutionContext> &excCtx) {
    lua_State *L = reinterpret_cast<lua_State *>(excCtx->getContext());

    // Only query globals for variables we actually track — avoid iterating
    // the entire Lua global table (string, math, io, os, etc.).
    for (VarID varID : ast.ast.scopes[0].variables) {
        auto &varInfo =
            unfoldKey(ast.ast.variables.at(varID), ast.ast, ctx);
        const std::string &name = varInfo.name;

        lua_getglobal(L, name.c_str());
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        const char *typeName = nullptr;
        int lt = lua_type(L, -1);
        switch (lt) {
        case LUA_TSTRING:  typeName = "string";  break;
        case LUA_TNUMBER:  typeName = "number";  break;
        case LUA_TBOOLEAN: typeName = "boolean"; break;
        case LUA_TTABLE:   typeName = "table";   break;
        default: break;
        }

        if (typeName) {
            TypeID tid = resolveType(typeName, ctx, ast.ast, 0);
            varInfo.type = tid;
        }
        lua_pop(L, 1);
    }
}
