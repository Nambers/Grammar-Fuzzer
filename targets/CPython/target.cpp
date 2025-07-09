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
#include <unordered_set>

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
        bool handled = false;
        const static std::string badUnaryOp("bad operand type for unary ");
        const static std::string noAttr(" has no attribute ");
        const static std::string badDescriptor("descriptor ");
        if (errMsg.starts_with(badUnaryOp) && !handled) {
            // e.g. bad operand type for unary ~: 'str'
            auto colonPos = errMsg.find(':');
            if (colonPos != std::string::npos) {
                std::string opName = errMsg.substr(
                    badUnaryOp.length(), colonPos - badUnaryOp.length());

                std::string targetType = getQuoteText(errMsg, ++colonPos);

                auto opIt =
                    std::find(UNARY_OPS.begin(), UNARY_OPS.end(), opName);
                if (opIt != UNARY_OPS.end()) {
                    auto &op = ctx.unaryOps[opIt - UNARY_OPS.begin()];

                    auto typeIt = std::find(ctx.types.begin(), ctx.types.end(),
                                            targetType);
                    if (typeIt != ctx.types.end()) {
                        TypeID typeID = typeIt - ctx.types.begin();

                        auto found = std::find(op.begin(), op.end(), typeID);
                        if (found != op.end()) {
                            op.erase(found);
                            INFO("Removed typeID {} from unary op '{}'", typeID,
                                 opName);
                        } else {
                            // INFO("TypeID {} not found in op '{}', nothing to
                            // remove",
                            //      typeID, opName);
                        }
                        handled = true;
                    }
                }
            }
        }
        if (errMsg.contains(noAttr) && !handled) {
            // e.g. 'dict' object has no attribute 'find'
            size_t pos = 0;
            std::string typeName = getQuoteText(errMsg, pos);
            TypeID tid = resolveType(typeName, ctx, ast, 0);
            if (tid <= 0) {
                // extracted type in errMsg can be inaccurate (like base class
                // name)
                handled = true;
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
                } else
                    handled = false;
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
            } else {
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
                    handled = true;
                }
            }
        }
        if (errMsg.starts_with(badDescriptor) && !handled) {
            // e.g. descriptor '__rand__' requires a 'bool' object but received
            // a 'str'
            // TODO
        }
        if (errMsg.contains(" argument ") && errMsg.contains(" must be ") &&
            !handled) {
            // e.g. replace() argument 1 must be str, not None
            size_t pos = errMsg.find(" argument ") + strlen(" argument ");
            size_t argNum = strtol(errMsg.c_str() + pos, nullptr, 10);
            pos = errMsg.find(" must be ", pos) + strlen(" must be ");
            size_t end = errMsg.find(',', pos);
            std::string expType = errMsg.substr(pos, end - pos);
            if (!expType.contains(' ')) {
                // 0 is ret val name, 1 is func name
                const std::string &funcName =
                    std::get<std::string>(node->fields[1].val);
                const auto p = funcName.find('.');
                if (p != std::string::npos) {
                    const std::string &typeName = funcName.substr(0, p);
                    const std::string &methodName = funcName.substr(p + 1);
                    TypeID tid = resolveType(typeName, ctx, ast, 0);
                    if (tid > 0) {
                        auto &methods = tid < ctx.builtinTypesCnt
                                            ? ctx.builtinsProps[tid]
                                            : ast.classProps[tid];
                        auto it =
                            std::find_if(methods.begin(), methods.end(),
                                         [&methodName](const PropInfo &prop) {
                                             return prop.name == methodName;
                                         });
                        if (it != methods.end()) {
                            it->funcSig.paramTypes[argNum - 1] =
                                resolveType(expType, ctx, ast, 0);
                            INFO("Updated method '{}' for expected "
                                 "type '{}'({}) for argument {}",
                                 methodName, expType, tid, argNum);
                        }
                    }
                } else {
                    // no designed parent type, we only search in builtins
                    // caused custom func should already has type.
                    auto it = std::find_if(ctx.builtinsProps.at(-1).begin(),
                                           ctx.builtinsProps.at(-1).end(),
                                           [&funcName](const auto &info) {
                                               return info.name == funcName;
                                           });
                    if (it != ctx.builtinsProps.at(-1).end()) {
                        const auto tid = resolveType(expType, ctx, ast, 0);
                        it->funcSig.paramTypes[argNum - 1] = tid;
                        INFO("Updated function '{}' with expected type "
                             "'{}'({}) for "
                             "argument {}",
                             funcName, expType, tid, argNum);
                    } else {
                        WARN("Failed to find function '{}' in builtins",
                             funcName);
                    }
                }
                handled = true;
            }
            // otherwise is not this template error
        }
        if (errMsg.contains(" expected at ") &&
            errMsg.contains(" arguments, got ") && !handled) {
            if (node->kind == ASTNodeKind::Call) {
                // e.g. rfind expected at most 3 arguments, got 4
                size_t pos =
                    errMsg.find(" expected at ") + strlen(" expected at ");
                if (errMsg.contains(" expected at most "))
                    pos += strlen("most ");
                else
                    pos += strlen("least ");
                if (pos != std::string::npos) {
                    int expectedArgs =
                        strtol(errMsg.c_str() + pos - 1, nullptr, 10);
                    const std::string &funcName =
                        std::get<std::string>(node->fields[1].val);
                    const auto p = funcName.find('.');
                    if (p != std::string::npos) {
                        const std::string &typeName = funcName.substr(0, p);
                        const std::string &methodName = funcName.substr(p + 1);
                        TypeID tid = resolveType(typeName, ctx, ast, 0);
                        if (tid > 0) {
                            auto &methods = tid < ctx.builtinTypesCnt
                                                ? ctx.builtinsProps[tid]
                                                : ast.classProps[tid];
                            auto it = std::find_if(
                                methods.begin(), methods.end(),
                                [&methodName](const PropInfo &prop) {
                                    return prop.name == methodName;
                                });
                            if (it != methods.end()) {
                                // remove the last param type
                                it->funcSig.paramTypes.resize(expectedArgs);
                                INFO("Updated method '{}' to have {} "
                                     "arguments",
                                     methodName, expectedArgs);
                                handled = true;
                            }
                        }
                    }
                }
            }
        }
        if (errMsg.contains(" object attribute ") &&
            errMsg.contains(" is read-only") && !handled) {
            size_t pos = 0;
            std::string typeName = getQuoteText(errMsg, pos);
            pos += strlen(" object attribute ");
            std::string attrName = getQuoteText(errMsg, pos);
            TypeID tid = resolveType(typeName, ctx, ast, 0);
            if (tid > 0) {
                auto &props = tid < ctx.builtinTypesCnt
                                  ? ctx.builtinsProps.at(tid)
                                  : ast.classProps.at(tid);

                auto it = std::find_if(props.begin(), props.end(),
                                       [&attrName](const PropInfo &prop) {
                                           return prop.name == attrName;
                                       });
                if (it != props.end()) {
                    // remove the property
                    it->isConst = true;
                    INFO("Marked property '{}' as read-only for typeID {}",
                         attrName, tid);
                    handled = true;
                }
            }
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
    // TODO somewhere forgot to clear pyErr.
    PyErr_Clear();

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

    // TODO current just insert anything news bc the classProps is both managed
    // by manually and auto.
    for (auto &[tid, vec] : tmpMethods) {
        auto vec1 = ast.classProps[tid];
        if (vec1.empty()) {
            vec1.swap(vec);
            continue;
        }
        std::unordered_set<PropInfo, PropInfo::Hash> set1(vec1.begin(),
                                                          vec1.end());
        for (const auto &item : vec) {
            if (set1.contains(item)) {
                vec1.push_back(item);
            }
        }
    }

    auto types = j["types"].get<std::vector<std::string>>();
    for (size_t i = 0; i < types.size(); ++i) {
        // discovered new type
        if (resolveType(types[i], ctx, ast, sid) == 0 && types[i] != "object") {
            scope.types.push_back(types[i]);
        }
    }
    ctx.update(ast);
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
        if (!PyUnicode_Check(key)) {
            ERROR("Key in execution context is not a string");
            continue;
        }
        std::string varName(PyUnicode_AsUTF8(key));
        PyObject *var = PyDict_GetItemString(dict, varName.c_str());
        if (!var) {
            ERROR("Variable '{}' not found in execution context", varName);
            continue;
        }
        std::string typeStr(Py_TYPE(var)->tp_name);
        if (typeStr != "object") {
            if (typeStr == "builtin_function_or_method") {
                // callable
                continue;
            }
            TypeID typeID = resolveType(typeStr, ctx, ast.ast, 0);
            if (typeID == 0) {
                WARN("Failed to resolve type '{}' for variable '{}'", typeStr,
                     varName);
            }
            // update type
            for (VarID varID : ast.ast.scopes[0].variables) {
                auto &varInfo =
                    unfoldKey(ast.ast.variables.at(varID), ast.ast, ctx);
                if (varInfo.name == varName) {
                    varInfo.type = typeID;
                    continue;
                }
            }
        }
    }
}
