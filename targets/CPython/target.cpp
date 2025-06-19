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

static void takePipe(const char *pipe) {
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull == -1) {
        perror("open(/dev/null)");
        std::abort();
    }
    int py_fd = dup(devnull);
    PyObjectPtr py_err_file(PyFile_FromFd(py_fd, "devlnull", "w", -1, nullptr,
                                          nullptr, nullptr, 1));
    if (!py_err_file) {
        PyErr_Print();
        std::abort();
    }
    PySys_SetObject(pipe, py_err_file.get());
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

    // NullStdIORedirect guard;

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

    takePipe("stderr");
    takePipe("stdout");
    return 0;
}

int FuzzingAST::finalize() { return Py_FinalizeEx(); }

void FuzzingAST::dummyAST(ASTData &data, const BuiltinContext &ctx) {
    // dummy corpus
    // decl:
    // + str_a = ""
    // + str_b = ""
    // + byte_a = b"""
    // + int_a = 0
    TypeID bytesType = std::find(ctx.types.begin(), ctx.types.end(), "bytes") -
                       ctx.types.begin();
    data.ast.declarations.resize(4);
    data.ast.declarations[0] =
        ASTNode{ASTNodeKind::DeclareVar, {{"str_a"}, {"\"\""}}};
    data.ast.declarations[1] =
        ASTNode{ASTNodeKind::DeclareVar, {{"str_b"}, {"\"\""}}};
    data.ast.declarations[2] =
        ASTNode{ASTNodeKind::DeclareVar, {{"byte_a"}, {"b\"\""}}};
    data.ast.declarations[3] =
        ASTNode{ASTNodeKind::DeclareVar, {{"int_a"}, {0}}};
    data.ast.classProps[-1].resize(4);
    data.ast.classProps[-1][0] = PropInfo{ctx.strID, 0, "str_a", false};
    data.ast.classProps[-1][1] = PropInfo{ctx.strID, 0, "str_b", false};
    data.ast.classProps[-1][2] = PropInfo{bytesType, 0, "byte_a", false};
    data.ast.classProps[-1][3] = PropInfo{ctx.intID, 0, "int_a", false};
    data.ast.variables.resize(4);
    data.ast.variables[0] = PropKey{false, 0, -1};
    data.ast.variables[1] = PropKey{false, 1, -1};
    data.ast.variables[2] = PropKey{false, 2, -1};
    data.ast.variables[3] = PropKey{false, 3, -1};
    data.ast.scopes.resize(1);
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

static std::string getQuoteText(const std::string &str, size_t &pos) {
    size_t start = pos;
    size_t end = str.find_first_of("\"'", start);
    if (end == std::string::npos) {
        return "";
    }
    char quoteChar = str[end];
    end = str.find(quoteChar, end + 1);
    if (end == std::string::npos) {
        return "";
    }
    pos = end + 1; // move past the closing quote
    return str.substr(start, end - start + 1);
}

static void errorCallback(AST &ast, BuiltinContext &ctx,
                          std::optional<ASTNode> node = std::nullopt) {
    PyObjectPtr exc(PyErr_GetRaisedException());
    PyObjectPtr errVal(PyObject_Str(exc.get()));
    std::string errMsg(PyUnicode_AsUTF8(errVal.get()));
#ifndef DISABLE_DEBUG_OUTPUT
    // PyErr_Print();
    ERROR("Failed to run code: {}", errMsg);
#endif
    // fix builtin sig dynamically
    if (PyErr_GivenExceptionMatches(exc.get(), PyExc_TypeError)) {
        const static std::string badUnaryOp("bad operand type for unary ");
        const static std::string noAttr(" has no attribute ");
        const static std::string badDescriptor("descriptor ");
        if (errMsg.starts_with(badUnaryOp)) {
            // e.g. bad operand type for unary ~: 'str'
            auto colonPos = errMsg.find(':');
            if (colonPos == std::string::npos)
                return;
            std::string opName = errMsg.substr(badUnaryOp.length(),
                                               colonPos - badUnaryOp.length());

            std::string targetType = getQuoteText(errMsg, ++colonPos);

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
        } else if (errMsg.contains(noAttr)) {
            // e.g. 'dict' object has no attribute 'find'
            size_t pos = 0;
            std::string typeName = getQuoteText(errMsg, pos);
            TypeID tid = resolveType(typeName, ctx, ast, 0);
            if (tid <= 0) {
                // extracted type in errMsg can be inaccurate (like base class
                // name)
                std::string obj = "";
                if (node->kind == ASTNodeKind::Call) {
                    obj = std::get<std::string>(node->fields[0].val);
                    obj = obj.substr(0, obj.find('.'));
                } else if (node->kind == ASTNodeKind::GetProp) {
                    obj = std::get<std::string>(node->fields[1].val);
                    obj = obj.substr(0, obj.find('.'));
                } else if (node->kind == ASTNodeKind::SetProp) {
                    obj = std::get<std::string>(node->fields[0].val);
                    obj = obj.substr(0, obj.find('.'));
                }
                // TODO
                // if (!obj.empty()) {
                //     // find correct type in variables
                //     auto it =
                //         std::find_if(ast.variables.begin(),
                //         ast.variables.end(),
                //                      [&obj](const PropInfo &var) {
                //                          return var.name == obj;
                //                      });
                //     if (it != ast.variables.end()) {
                //         tid = it->type;
                //     }
                // }
            }
            if (tid > 0) {
                std::string attrName = getQuoteText(errMsg, ++pos);
                auto &props = tid < ctx.builtinTypesCnt ? ctx.builtinsProps[tid]
                                                        : ast.classProps[tid];

                auto it = std::find_if(props.begin(), props.end(),
                                       [&attrName](const PropInfo &prop) {
                                           return prop.name == attrName;
                                       });
                if (it != props.end()) {
                    // remove the property
                    props.erase(it);
                    INFO("Removed property '{}' from typeID {}", attrName, tid);
                }
            }
        } else if (errMsg.starts_with(badDescriptor)) {
            // e.g. descriptor '__rand__' requires a 'bool' object but received
            // a 'str'
            // TODO
        }
        // TODO
    }
    PyErr_Clear();
}

static int runInternal(const AST &ast, BuiltinContext &ctx, PyObjectPtr &code,
                       PyObject *dict, bool readResult = false,
                       std::string *outStr = nullptr,
                       uint32_t timeoutMs = 600) {
    if (sigsetjmp(timeoutJmp, 1) == 0) {
        // NullStdIORedirect guard;
        if (readResult) {
            PyObjectPtr result(PyEval_EvalCode(code.get(), dict, dict));
            if (!result) {
                if (PyErr_Occurred()) {
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
                    ++errCnt;
                    return -1;
                }
            }
            return 0;
        }
    } else {
        clear_timeout();
        // NullStdIORedirect::restore();
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
                            uint32_t timeoutMs = 600) {
    if (echo) {
        std::cout << "[Generated Python]:\n" << re << "\n";
    }

    PyObjectPtr code(Py_CompileString(re.c_str(), "<ast>", Py_file_input));
    if (PyErr_Occurred()) {
        return -1;
    }

    return runInternal(ast, ctx, code, dict, false, nullptr, timeoutMs);
}

int FuzzingAST::runLine(const ASTNode &node, AST &ast, BuiltinContext &ctx,
                        std::unique_ptr<ExecutionContext> &excCtx, bool echo) {
    std::ostringstream script;
    nodeToPython(script, node, ast, ctx, 0);
    const auto ret = runASTStr(
        script.str(), ast, ctx,
        reinterpret_cast<PyObject *>(excCtx.get()->getContext()), echo);
    if (ret == -1)
        errorCallback(ast, ctx, std::move(node));
    else if (ret == -2)
        excCtx->releasePtr();
    return ret;
}

int FuzzingAST::runLines(const std::vector<ASTNode> &nodes, AST &ast,
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
    if (ret == -1)
        errorCallback(ast, ctx);
    else if (ret == -2)
        excCtx->releasePtr();
    return ret;
}

int FuzzingAST::runAST(AST &ast, BuiltinContext &ctx,
                       std::unique_ptr<ExecutionContext> &excCtx, bool echo) {
    std::ostringstream script;
    scopeToPython(script, 0, ast, ctx, 0);
    const auto ret = runASTStr(
        script.str(), ast, ctx,
        reinterpret_cast<PyObject *>(excCtx.get()->getContext()), echo);
    if (ret == -1)
        errorCallback(ast, ctx);
    else if (ret == -2)
        excCtx->releasePtr();
    return ret;
}

int FuzzingAST::reflectObject(AST &ast, ASTScope &scope, const ScopeID sid,
                              BuiltinContext &ctx) {
    std::ostringstream script;

    for (NodeID id : scope.declarations) {
        const auto &node = ast.declarations[id];
        if (node.kind != ASTNodeKind::Function)
            nodeToPython(script, ast.declarations[id], ast, ctx, 0);
    }

    std::string re = script.str();

    if (re.empty())
        return 0;

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
    std::unordered_map<TypeID, std::vector<PropInfo>> tmpMethods;
    auto &funcs = j["funcs"];

    for (auto &[_tname, arr] : funcs.items()) {
        TypeID typeID;
        if (_tname == "-1")
            typeID = -1;
        else
            typeID = resolveType(_tname, ctx, ast, sid);
        if (!tmpMethods.contains(typeID))
            tmpMethods.emplace(typeID, std::vector<PropInfo>());
        else
            tmpMethods[typeID].clear();
        auto &vec = tmpMethods[typeID];
        for (auto &item : arr) {
            if (item.contains("type"))
                item["type"] =
                    resolveType(item["type"].get<std::string>(), ctx, ast, sid);
            if (item.value<bool>("isCallable", false)) {
                auto &sig = item["funcSig"];
                for (auto &typeName : sig["paramTypes"]) {
                    typeName =
                        resolveType(typeName.get<std::string>(), ctx, ast, sid);
                }
                sig["returnType"] = resolveType(
                    sig["returnType"].get<std::string>(), ctx, ast, sid);
                sig["selfType"] = resolveType(
                    sig["selfType"].get<std::string>(), ctx, ast, sid);
            }
            tmpMethods[typeID].push_back(item.get<PropInfo>());
        }
    }
    ast.classProps.swap(tmpMethods);

    auto types = j["types"].get<std::vector<std::string>>();
    for (size_t i = 0; i < types.size(); ++i) {
        if (resolveType(types[i], ctx, ast, sid) == 0 && types[i] != "object") {
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
        TypeID typeID = resolveType(typeStr, ctx, ast.ast, 0);
        if (typeID == 0) {
            WARN("Failed to resolve type '{}' for variable '{}'", typeStr,
                 varName);
        }
        // update type
        for (VarID varID : ast.ast.scopes[0].variables) {
            const auto &varInfoKey = ast.ast.variables[varID];
            auto &varInfo = unfoldKey(varInfoKey, ast.ast, ctx);
            if (varInfo.name == varName) {
                varInfo.type = typeID;
                continue;
            }
        }
    }
}
