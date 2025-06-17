#include "target.hpp"
#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "log.hpp"
#include <Python.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <serialization.hpp>
#include <setjmp.h>
#include <signal.h>
#include <sstream>
#include <sys/shm.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace FuzzingAST;

extern uint32_t newEdgeCnt;
extern uint32_t errCnt;
static PyObject *driverPyCodeObj;
static sigjmp_buf timeoutJmp;

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

static void alarmHandler(int signum) {
    if (signum == SIGALRM) {
        ERROR("Execution timed out.");
        siglongjmp(timeoutJmp, 1);
    }
}

static void set_timeout_ms(int timeout_ms) {
    struct itimerval timer{};
    timer.it_value.tv_sec = timeout_ms / 1000;
    timer.it_value.tv_usec = (timeout_ms % 1000) * 1000;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0; // one-shot
    setitimer(ITIMER_REAL, &timer, nullptr);
}

static void clear_timeout() {
    struct itimerval zero = {};
    setitimer(ITIMER_REAL, &zero, nullptr);
}

void installSignalHandler() {
    struct sigaction sa{};
    sa.sa_handler = alarmHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGALRM, &sa, nullptr) == -1) {
        perror("sigaction");
        std::abort();
    }
}

int FuzzingAST::initialize(int *argc, char ***argv) {
    std::ifstream driverPyStream("./targets/CPython/driver.py");
    if (!driverPyStream.is_open()) {
        PANIC("Failed to open driver.py, please run build.sh to generate it.");
    }
    std::ostringstream sstr;
    sstr << driverPyStream.rdbuf();
    std::string driverPyConent;
    driverPyConent = sstr.str();
    driverPyStream.close();

    installSignalHandler();

    PyConfig config;
    PyStatus status;
    PyConfig_InitPythonConfig(&config);
    status = PyWideStringList_Append(&config.warnoptions, L"ignore");
    if (PyStatus_Exception(status)) {
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }
    config.install_signal_handlers = 0;
    config.parse_argv = 0;
    config.use_environment = 0;

    status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        Py_ExitStatusException(status);
    }
    driverPyCodeObj =
        Py_CompileString(driverPyConent.c_str(), "<driver.py>", Py_file_input);
    if (PyErr_Occurred()) {
        PyErr_Print();
        PANIC("Failed to compile driver.py");
    }

    int devnull = open("/dev/null", O_WRONLY);
    if (devnull == -1) {
        perror("open(/dev/null)");
        std::abort();
    }
    int py_fd = dup(devnull);
    PyObjectPtr py_err_file(PyFile_FromFd(py_fd, "py_stderr", "w", -1, nullptr,
                                          nullptr, nullptr, 1));
    if (!py_err_file) {
        PyErr_Print();
        std::abort();
    }
    PySys_SetObject("stderr", py_err_file.get());

    close(devnull);
    return 0;
}

int FuzzingAST::finalize() { return Py_FinalizeEx(); }

void FuzzingAST::loadBuiltinsFuncs(BuiltinContext &ctx) {
    std::ifstream in("./targets/CPython/builtins.json");
    if (!in) {
        PANIC("Failed to open builtins.json, run build.sh to generate it.");
    }
    nlohmann::json j;
    in >> j;
    auto tmp =
        j["funcs"].get<std::unordered_map<std::string, FunctionSignature>>();
    ctx.builtinsFuncs.swap(tmp);
    ctx.builtinsFuncsCnt = ctx.builtinsFuncs.size();
    auto tmp2 = j["types"].get<std::vector<std::string>>();
    ctx.types.swap(tmp2);
    ctx.builtinTypesCnt = ctx.types.size();
    auto tmp3 = j["ops"].get<std::vector<std::vector<std::vector<TypeID>>>>();
    ctx.ops.swap(tmp3);
    auto tmp4 = j["uops"].get<std::vector<std::vector<TypeID>>>();
    ctx.unaryOps.swap(tmp4);
}

void FuzzingAST::dummyAST(ASTData &data, const BuiltinContext &ctx) {
    // dummy corpus
    // decl:
    // + str_a = ""
    // + str_b = ""
    // + byte_a = b"""
    // + int_a = 0
    TypeID bytesType = std::find(ctx.types.begin(), ctx.types.end(), "bytes") -
                       ctx.types.begin();
    data.ast.declarations.push_back(
        {ASTNodeKind::DeclareVar, ctx.strID, {{"str_a"}, {"\"\""}}});
    data.ast.declarations.push_back(
        {ASTNodeKind::DeclareVar, ctx.strID, {{"str_b"}, {"\"\""}}});
    data.ast.declarations.push_back(
        {ASTNodeKind::DeclareVar, bytesType, {{"byte_a"}, {"b\"\""}}});
    data.ast.declarations.push_back(
        {ASTNodeKind::DeclareVar, ctx.intID, {{"int_a"}, {0}}});
    data.ast.scopes[0].declarations.resize(4);
    data.ast.scopes[0].variables.resize(4);
    data.ast.scopes[0].declarations[0] = 0;
    data.ast.scopes[0].declarations[1] = 1;
    data.ast.scopes[0].declarations[2] = 2;
    data.ast.scopes[0].declarations[3] = 3;
    data.ast.scopes[0].variables[0] = 0;
    data.ast.scopes[0].variables[1] = 1;
    data.ast.scopes[0].variables[2] = 2;
    data.ast.scopes[0].variables[3] = 3;
}

static TypeID resolveType(const std::string &fullname,
                          const BuiltinContext &ctx, const ASTScope &scope,
                          ScopeID sid) {
    auto matches = [&](const std::string &t) {
        if (fullname == t)
            return true;
        if (t.size() > fullname.size())
            return t.ends_with(fullname);

        return false;
    };

    for (size_t i = 0; i < ctx.types.size(); ++i) {
        if (matches(ctx.types[i])) {
            return i;
        }
    }
    for (size_t j = 0; j < scope.types.size(); ++j) {
        if (matches(scope.types[j])) {
            return j + (sid + 1) * SCOPE_MAX_TYPE;
        }
    }
    return 0; // object
}

static void errorCallback(const PyObjectPtr &exc, const AST &ast,
                          BuiltinContext &ctx) {
    PyObjectPtr errVal(PyObject_Str(exc.get()));
    std::string errMsg(PyUnicode_AsUTF8(errVal.get()));
#ifndef DISABLE_DEBUG_OUTPUT
    // PyErr_Print();
    ERROR("Failed to run code: {}", errMsg);
#endif
    // fix builtin sig dynamically
    if (PyErr_GivenExceptionMatches(exc.get(), PyExc_TypeError)) {
        const static std::string badUnaryOp("bad operand type for unary ");
        if (errMsg.starts_with(badUnaryOp)) {
            // e.g. bad operand type for unary ~: 'str'
            auto colonPos = errMsg.find(":");
            if (colonPos == std::string::npos)
                return;
            std::string opName = errMsg.substr(badUnaryOp.length(),
                                               colonPos - badUnaryOp.length());

            auto firstQuote = errMsg.find("'", colonPos);
            auto lastQuote = errMsg.rfind("'");
            if (firstQuote == std::string::npos ||
                lastQuote == std::string::npos || firstQuote >= lastQuote)
                return;

            std::string targetType =
                errMsg.substr(firstQuote + 1, lastQuote - firstQuote - 1);

            auto opIt = std::find(UNARY_OPS.begin(), UNARY_OPS.end(), opName);
            if (opIt == UNARY_OPS.end())
                return;
            auto &op = ctx.unaryOps[opIt - UNARY_OPS.begin()];

            auto typeIt =
                std::find(ctx.types.begin(), ctx.types.end(), targetType);
            if (typeIt == ctx.types.end())
                return;
            TypeID typeID = typeIt - ctx.types.begin();

            auto found = std::find(op.begin(), op.end(), typeID);
            if (found != op.end()) {
                op.erase(found);
                INFO("Removed typeID {} from unary op '{}'", typeID, opName);
            } else {
                // INFO("TypeID {} not found in op '{}', nothing to remove",
                //      typeID, opName);
            }
        }
        auto badDescriptor = "descriptor ";
        if (errMsg.starts_with(badDescriptor)) {
            // e.g. descriptor '__rand__' requires a 'bool' object but received
            // a 'str'
            // TODO
        }
        // TODO
    }
}

static int runInternal(const AST &ast, BuiltinContext &ctx, PyObjectPtr &code,
                       PyObject *dict, bool readResult = false,
                       std::string *outStr = nullptr,
                       uint32_t timeoutMs = 500) {
    if (sigsetjmp(timeoutJmp, 1) == 0) {
        if (readResult) {
            PyObjectPtr result(PyEval_EvalCode(code.get(), dict, dict));
            if (!result) {
                if (PyErr_Occurred()) {
                    PyObjectPtr exc(PyErr_GetRaisedException());
                    errorCallback(exc, ast, ctx);
                    PyErr_Clear();
                    return -1;
                }
            }
            result.reset(PyEval_EvalCode(driverPyCodeObj, dict, dict));
            if (!result) {
                if (PyErr_Occurred()) {
                    PyObjectPtr exc(PyErr_GetRaisedException());
                    PyObjectPtr errVal(PyObject_Str(exc.get()));
                    std::string errMsg(PyUnicode_AsUTF8(errVal.get()));
                    PANIC("Failed to run driver.py {}", errMsg);
                }
            }
            // a borrowed ptr, no need to DECREF
            PyObject *rawJson = PyDict_GetItemString(dict, "result");
            if (!rawJson) {
                PyErr_Print();
                PANIC("Failed to get 'result' from driver.py");
            }
            if (!PyUnicode_Check(rawJson)) {
                PyErr_Print();
                PANIC("'result' from driver.py is not a string");
            }
            outStr->assign(PyUnicode_AsUTF8(rawJson));
            return 0;

        } else {
            set_timeout_ms(timeoutMs);
            PyObjectPtr result(PyEval_EvalCode(code.get(), dict, dict));
            clear_timeout(); // cancel timeout

            if (!result) {
                if (PyErr_Occurred()) {
                    PyObjectPtr exc(PyErr_GetRaisedException());
                    errorCallback(exc, ast, ctx);
                    PyErr_Clear();
                    ++errCnt;
                    result.release();
                    return -1;
                }
            }
            return 0;
        }
    } else {
        clear_timeout();
        // PyErr_SetString(PyExc_RuntimeError, "Execution timed out");
        code.release();
        ERROR("Execution timed out, restarting Python interpreter");
        // restart Python interpreter
        finalize();

        initialize(nullptr, nullptr);
        return -2;
    }
}

static inline int runASTStr(const std::string &re, const AST &ast,
                            BuiltinContext &ctx, PyObject *dict, bool echo,
                            uint32_t timeoutMs = 500) {
    if (echo) {
        std::cout << "[Generated Python]:\n" << re << "\n";
    }

    PyObjectPtr code(Py_CompileString(re.c_str(), "<ast>", Py_file_input));
    if (PyErr_Occurred()) {
        PyObjectPtr exc(PyErr_GetRaisedException());
        errorCallback(exc, ast, ctx);
        PyErr_Clear();
        return -1;
    }

    return runInternal(ast, ctx, code, dict, false, nullptr, timeoutMs);
}

int FuzzingAST::runLine(const ASTNode &node, const AST &ast,
                        BuiltinContext &ctx,
                        std::unique_ptr<ExecutionContext> &excCtx, bool echo) {
    std::ostringstream script;
    nodeToPython(script, node, ast, ctx, 0);
    const auto ret = runASTStr(
        script.str(), ast, ctx,
        reinterpret_cast<PyObject *>(excCtx.get()->getContext()), echo);
    if (ret == -2)
        excCtx->releasePtr();
    return ret;
}

int FuzzingAST::runLines(const std::vector<ASTNode> &nodes, const AST &ast,
                         BuiltinContext &ctx,
                         std::unique_ptr<ExecutionContext> &excCtx, bool echo) {
    std::ostringstream script;
    for (auto nodeID : ast.scopes[0].declarations) {
        const auto &node = ast.declarations[nodeID];
        if (node.kind != ASTNodeKind::Function) {
            nodeToPython(script, node, ast, ctx, 0);
        }
    }
    for (const auto &node : nodes) {
        nodeToPython(script, node, ast, ctx, 0);
    }
    const auto ret = runASTStr(
        script.str(), ast, ctx,
        reinterpret_cast<PyObject *>(excCtx.get()->getContext()), echo, 2000);
    if (ret == -2)
        excCtx->releasePtr();
    return ret;
}

int FuzzingAST::runAST(const AST &ast, BuiltinContext &ctx,
                       std::unique_ptr<ExecutionContext> &excCtx, bool echo) {
    std::ostringstream script;
    scopeToPython(script, 0, ast, ctx, 0);
    const auto ret = runASTStr(
        script.str(), ast, ctx,
        reinterpret_cast<PyObject *>(excCtx.get()->getContext()), echo);
    if (ret == -2)
        excCtx->releasePtr();
    return ret;
}

int FuzzingAST::reflectObject(const AST &ast, ASTScope &scope,
                              const ScopeID sid, BuiltinContext &ctx) {
    std::ostringstream script;

    bool empty = true;

    for (NodeID id : scope.declarations) {
        const auto &node = ast.declarations[id];
        if (node.kind != ASTNodeKind::Function) {
            nodeToPython(script, ast.declarations[id], ast, ctx, 0);
            empty = false;
        }
    }

    if (empty)
        return 0;

    std::string re = script.str();

    PyObjectPtr code(Py_CompileString(re.c_str(), "<ast>", Py_file_input));
    if (PyErr_Occurred()) {
#ifndef DISABLE_DEBUG_OUTPUT
        PyErr_Print();
#endif
        PyErr_Clear();
        ERROR("Failed to compile decl code block:\n{}", re);
        return -1;
    }

    std::string jsonStr;
    auto excCtx = getInitExecutionContext();
    int ret =
        runInternal(ast, ctx, code,
                    reinterpret_cast<PyObject *>(excCtx.get()->getContext()),
                    true, &jsonStr);
    if (ret != 0) {
        if (ret == -2)
            excCtx->releasePtr();
        return ret;
    }
    nlohmann::json j = nlohmann::json::parse(jsonStr);
    auto &funcs = j["funcs"];

    for (auto &[_name, sig] : funcs.items()) {
        for (auto &typeName : sig["paramTypes"]) {
            typeName =
                resolveType(typeName.get<std::string>(), ctx, scope, sid);
        }

        if (sig.contains("returnType"))
            sig["returnType"] = resolveType(
                sig["returnType"].get<std::string>(), ctx, scope, sid);

        if (sig.contains("selfType") && !sig["selfType"].is_null())
            sig["selfType"] = resolveType(sig["selfType"].get<std::string>(),
                                          ctx, scope, sid);
        else
            sig["selfType"] = -1;
    }

    auto tmp = funcs.get<std::unordered_map<std::string, FunctionSignature>>();

    scope.funcSignatures.swap(tmp);
    auto types = j["types"].get<std::vector<std::string>>();
    for (size_t i = 0; i < types.size(); ++i) {
        if (resolveType(types[i], ctx, scope, sid) == 0 &&
            types[i] != "object") {
            scope.types.push_back(types[i]);
        }
    }
    return 0;
}

std::unique_ptr<ExecutionContext> FuzzingAST::getInitExecutionContext() {
    PyObjectPtr dict(PyDict_New());
    PyObjectPtr name(PyUnicode_FromString("__main__"));
    PyDict_SetItemString(dict.get(), "__name__", name.get());
    PyDict_SetItemString(dict.get(), "__builtins__", PyEval_GetBuiltins());
    return std::make_unique<PythonExecutionContext>(std::move(dict));
}

void FuzzingAST::updateTypes(const std::unordered_set<std::string> &globalVars,
                             ASTData &ast, BuiltinContext &ctx,
                             std::unique_ptr<ExecutionContext> &excCtx) {
    PyObject *dict = reinterpret_cast<PyObject *>(excCtx.get()->getContext());
    // retrieve variable then get type str then match
    // TODO
    PyObjectPtr keys(PyDict_Keys(dict));
    PyObjectPtr iter(PyObject_GetIter(keys.get()));
    // for (const auto &varName : globalVars) {
    for (PyObject *key = PyIter_Next(iter.get()); key;
         key = PyIter_Next(iter.get())) {
        std::string varName(PyUnicode_AsUTF8(key));
        PyObject *var = PyDict_GetItemString(dict, varName.c_str());
        if (!var) {
            ERROR("Variable '{}' not found in execution context", varName);
            continue;
        }
        if (!PyUnicode_Check(var)) {
            ERROR("Variable '{}' is not a string", varName);
            continue;
        }
        std::string typeStr(Py_TYPE(var)->tp_name);
        TypeID typeID = resolveType(typeStr, ctx, ast.ast.scopes[0], 0);
        if (typeID == 0) {
            WARN("Failed to resolve type '{}' for variable '{}'", typeStr,
                 varName);
        }
        // update type
        for (NodeID declID : ast.ast.scopes[0].declarations) {
            const auto &decl = ast.ast.declarations[declID];
            if (decl.kind == ASTNodeKind::DeclareVar &&
                std::get<std::string>(decl.fields[0].val) == varName) {
                ast.ast.declarations[declID].type = typeID;
                continue;
            }
        }
    }
}
