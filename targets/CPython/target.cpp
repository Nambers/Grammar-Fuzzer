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
static PyObject *driverPyCodeObj;

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
        raise(SIGINT);
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

static int runInternal(PyObject *code, bool readResult = false,
                       std::string *outStr = nullptr,
                       uint32_t timeoutMs = 1000) {
    PyObject *dict = PyDict_New();
    PyObject *name = PyUnicode_FromString("__main__");
    PyDict_SetItemString(dict, "__name__", name);
    PyDict_SetItemString(dict, "__builtins__", PyEval_GetBuiltins());
    if (readResult) {
        PyObject *result = PyEval_EvalCode(code, dict, dict);
        if (!result) {
            Py_DECREF(name);
            if (dict && PyDict_Check(dict)) {
                PyDict_Clear(dict);
                Py_DECREF(dict);
            }
            if (PyErr_Occurred()) {
#ifndef DISABLE_DEBUG_OUTPUT
                PyErr_Print();
#endif
                PyErr_Clear();
                return -1;
            }
        } else
            Py_DECREF(result);
        result = PyEval_EvalCode(driverPyCodeObj, dict, dict);
        if (!result) {
            if (PyErr_Occurred()) {
#ifndef DISABLE_DEBUG_OUTPUT
                PyErr_Print();
#endif
                PyErr_Clear();
                PANIC("Failed to run driver.py");
            }
        } else
            Py_DECREF(result);
        PyObject *rawJson = PyDict_GetItemString(dict, "result");
        if (!rawJson) {
            PyErr_Print();
            PANIC("Failed to get 'result' from driver.py");
        }
        if (!PyUnicode_Check(rawJson)) {
            PyErr_Print();
            PANIC("'result' from driver.py is not a string");
        }
        std::string re(PyUnicode_AsUTF8(rawJson));
        Py_DECREF(rawJson);
        Py_DECREF(name);
        Py_DECREF(dict);
        outStr->assign(re);
        return 0;

    } else {
        set_timeout_ms(timeoutMs);
        PyObject *result =
            PyEval_EvalCode(code, dict, dict); // <<< in this line
        clear_timeout();                       // cancel timeout

        if (!result) {
            Py_DECREF(name);
            if (dict && PyDict_Check(dict)) {
                PyDict_Clear(dict);
                Py_DECREF(dict);
            }
            if (PyErr_Occurred()) {
#ifndef DISABLE_DEBUG_OUTPUT
                PyErr_Print();
#endif
                PyErr_Clear();
                return -1;
            }
        } else
            Py_DECREF(result);
        Py_DECREF(dict);
        Py_DECREF(name);
        return 0;
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

    Py_Initialize();
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
    PyObject *py_err_file = PyFile_FromFd(py_fd, "py_stderr", "w", -1, nullptr,
                                          nullptr, nullptr, 1);
    if (!py_err_file) {
        PyErr_Print();
        std::abort();
    }
    PySys_SetObject("stderr", py_err_file);
    Py_DECREF(py_err_file);

    close(devnull);
    return 0;
}

int FuzzingAST::finalize() {
    Py_DECREF(driverPyCodeObj);
    Py_Finalize();
    return 0;
}

void FuzzingAST::loadBuiltinsFuncs(BuiltinContext &ctx) {
    auto &funcSignatures = ctx.builtinsFuncs;
    auto &types = ctx.types;
    FILE *file = fopen("./targets/CPython/builtins.json", "r");
    if (!file) {
        PANIC("Failed to open builtins.json, run build.sh to generate it.");
    }
    nlohmann::json j = nlohmann::json::parse(file);
    auto tmp =
        j["funcs"].get<std::unordered_map<std::string, FunctionSignature>>();
    funcSignatures.swap(tmp);
    auto tmp2 = j["types"].get<std::vector<std::string>>();
    types.swap(tmp2);
}

void FuzzingAST::dummyAST(const std::shared_ptr<ASTData> &data,
                          const BuiltinContext &ctx) {
    // dummy corpus
    // decl:
    // + str_a = ""
    // + str_b = ""
    // + byte_a = b"""
    // + int_a = 0
    TypeID bytesType = std::find(ctx.types.begin(), ctx.types.end(), "bytes") -
                       ctx.types.begin();
    data->ast.declarations.push_back(
        {ASTNodeKind::DeclareVar, ctx.strID, {{"str_a"}, {"\"\""}}});
    data->ast.declarations.push_back(
        {ASTNodeKind::DeclareVar, ctx.strID, {{"str_b"}, {"\"\""}}});
    data->ast.declarations.push_back(
        {ASTNodeKind::DeclareVar, bytesType, {{"byte_a"}, {"b\"\""}}});
    data->ast.declarations.push_back(
        {ASTNodeKind::DeclareVar, ctx.intID, {{"int_a"}, {0}}});
    data->ast.scopes[0].declarations.resize(4);
    data->ast.scopes[0].variables.resize(4);
    data->ast.scopes[0].declarations[0] = 0;
    data->ast.scopes[0].declarations[1] = 1;
    data->ast.scopes[0].declarations[2] = 2;
    data->ast.scopes[0].declarations[3] = 3;
    data->ast.scopes[0].variables[0] = 0;
    data->ast.scopes[0].variables[1] = 1;
    data->ast.scopes[0].variables[2] = 2;
    data->ast.scopes[0].variables[3] = 3;
}

static TypeID resolveType(const std::string &name, const BuiltinContext &ctx,
                          const ASTScope &scope) {
    auto it = std::find(ctx.types.begin(), ctx.types.end(), name);
    if (it != ctx.types.end())
        return static_cast<TypeID>(std::distance(ctx.types.begin(), it));

    auto it2 = std::find(scope.types.begin(), scope.types.end(), name);
    if (it2 != scope.types.end())
        return static_cast<TypeID>(ctx.types.size() +
                                   std::distance(scope.types.begin(), it2));

    return 0; // fallback to "object"
}

int FuzzingAST::runAST(const AST &ast, const BuiltinContext &ctx, bool echo) {
    std::ostringstream script;
    scopeToPython(script, 0, ast, ctx, 0);
    std::string re = script.str();

    if (echo) {
        std::cout << "[Generated Python]:\n" << re << "\n";
    }

    PyObject *code = Py_CompileString(re.c_str(), "<ast>", Py_file_input);
    if (PyErr_Occurred()) {
#ifndef DISABLE_DEBUG_OUTPUT
        PyErr_Print();
#endif
        PyErr_Clear();
        return -1;
    }

    auto ret = runInternal(code);
    Py_DECREF(code);
    return ret;
}

int FuzzingAST::reflectObject(const AST &ast, ASTScope &scope,
                              const BuiltinContext &ctx) {
    std::ostringstream script;

    bool empty = true;

    for (NodeID id : scope.declarations) {
        const auto &node = ast.declarations.at(id);
        if (node.kind != ASTNodeKind::Function) {
            nodeToPython(script, ast.declarations.at(id), ast, ctx, 0);
            empty = false;
        }
    }

    if (empty)
        return 0;

    std::string re = script.str();

    PyObject *code = Py_CompileString(re.c_str(), "<ast>", Py_file_input);
    if (PyErr_Occurred()) {
#ifndef DISABLE_DEBUG_OUTPUT
        PyErr_Print();
#endif
        PyErr_Clear();
        ERROR("Failed to compile decl code block:\n{}", re);
        return -1;
    }

    std::string jsonStr;
    int ret = runInternal(code, true, &jsonStr);
    Py_DECREF(code);
    if (ret != 0)
        return ret;
    nlohmann::json j = nlohmann::json::parse(jsonStr);
    auto &funcs = j["funcs"];

    for (auto &[_name, sig] : funcs.items()) {
        for (auto &typeName : sig["paramTypes"]) {
            typeName = resolveType(typeName.get<std::string>(), ctx, scope);
        }

        if (sig.contains("returnType"))
            sig["returnType"] =
                resolveType(sig["returnType"].get<std::string>(), ctx, scope);

        if (sig.contains("selfType") && !sig["selfType"].is_null())
            sig["selfType"] =
                resolveType(sig["selfType"].get<std::string>(), ctx, scope);
        else
            sig["selfType"] = -1;
    }

    auto tmp = funcs.get<std::unordered_map<std::string, FunctionSignature>>();

    scope.funcSignatures.swap(tmp);
    // maintain types by ourself
    // auto tmp2 = j["types"].get<std::vector<std::string>>();
    // scope.types.swap(tmp2);
    return 0;
}
