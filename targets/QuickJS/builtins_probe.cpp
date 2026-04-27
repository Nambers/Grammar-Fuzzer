// builtins_probe.cpp
// Runs inside a native QuickJS context to enumerate available globals and
// prototype methods, then writes builtins.json.  This ensures only APIs that
// QuickJS actually provides are included — unlike running builtins_gen.js under
// Node.js, which picks up browser/Node-only Web APIs.

#include <fstream>
#include <iostream>
#include <string>

extern "C" {
#include <quickjs.h>
}

static bool readFile(const std::string& path, std::string* out) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    *out = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

int main(int argc, char *argv[]) {
    const char *outPath = argc > 1 ? argv[1] : "builtins.json";
    const char *scriptPath = argc > 2 ? argv[2] : "builtins_gen.js";

    std::string script;
    if (!readFile(scriptPath, &script)) {
        std::cerr << "Failed to read probe script: " << scriptPath << "\n";
        return 1;
    }

    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        std::cerr << "Failed to create QuickJS runtime\n";
        return 1;
    }
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        std::cerr << "Failed to create QuickJS context\n";
        return 1;
    }

    JSValue result = JS_Eval(ctx, script.c_str(), script.size(),
                             scriptPath, JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exc);
        std::cerr << "Probe script error: " << (msg ? msg : "(unknown)") << "\n";
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, result);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    const char *json = JS_ToCString(ctx, result);
    if (!json) {
        std::cerr << "Failed to convert result to string\n";
        JS_FreeValue(ctx, result);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }

    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "Failed to open output file: " << outPath << "\n";
        JS_FreeCString(ctx, json);
        JS_FreeValue(ctx, result);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    out << json << "\n";
    out.close();

    JS_FreeCString(ctx, json);
    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    std::cout << "[builtins_probe] generated " << outPath << "\n";
    return 0;
}
