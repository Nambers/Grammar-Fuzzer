#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <fstream>
#include <iostream>
#include <sys/time.h>

extern "C" {
#include <quickjs.h>
}

using json = nlohmann::json;
using namespace FuzzingAST;

static void runJSStr(const std::string &code) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);

    struct timeval start{};
    gettimeofday(&start, nullptr);

    JSValue ret = JS_Eval(ctx, code.c_str(), code.size(), "<test>",
                          JS_EVAL_TYPE_GLOBAL);

    struct timeval now{};
    gettimeofday(&now, nullptr);
    int elapsed_ms = static_cast<int>((now.tv_sec - start.tv_sec) * 1000 +
                                      (now.tv_usec - start.tv_usec) / 1000);
    std::cout << "Execution time: " << elapsed_ms << " ms\n";

    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exc);
        if (msg) {
            std::cerr << "QuickJS error: " << msg << "\n";
            JS_FreeCString(ctx, msg);
        }
        JS_FreeValue(ctx, exc);
    }
    JS_FreeValue(ctx, ret);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

int main(int argc, char *argv[]) {
    std::ifstream in("ast_test.json");
    if (!in) {
        std::cerr << "Failed to open ast_test.json\n";
        return 1;
    }
    json j;
    in >> j;

    AST ast = j.get<AST>();
    BuiltinContext ctx;
    loadBuiltinsFuncs(ctx);
    initPrimitiveTypes(ctx);

    std::string result;
    if (argc == 2 && std::string(argv[1]) == "-d") {
        std::cout << "Generated JS declarations:\n";
        for (const auto &declID : ast.scopes[0].declarations) {
            const auto &node = ast.declarations[declID];
            if (node.kind != ASTNodeKind::Function) {
                std::ostringstream script;
                nodeToJS(script, node, ast, ctx, 0);
                std::cout << script.str();
                result += script.str();
            }
        }
    } else {
        std::ostringstream script;
        scopeToJS(script, 0, ast, ctx, 0);
        std::cout << "Generated JS script:\n" << script.str() << "\n";
        result = script.str();
    }
    runJSStr(result);
    return 0;
}
