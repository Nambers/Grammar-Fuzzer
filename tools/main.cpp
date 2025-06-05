#include "ast.hpp"
#include "driver.hpp"
#include "serialization.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace FuzzingAST;

extern void scopeToPython(std::ostringstream &out, ScopeID sid, const AST &ast,
                          const BuiltinContext &ctx, int indentLevel);

int main(int argc, char **argv) {
    BuiltinContext ctx;
    loadBuiltinsFuncs(ctx);
    initPrimitiveTypes(ctx);
    for (const auto &entry : fs::directory_iterator("corpus/saved")) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::ifstream in(entry.path());
            if (!in) {
                std::cerr << "Failed to open file: " << entry.path()
                          << std::endl;
                continue;
            }
            nlohmann::json j;
            in >> j;
            AST ast = j.get<AST>();

            std::ostringstream script;
            scopeToPython(script, 0, ast, ctx, 0);

            std::ofstream out("corpus/saved/" +
                              entry.path().filename().string() + ".py");
            if (!out) {
                std::cerr << "Failed to open output file: "
                          << "corpus/saved/" +
                                 entry.path().filename().string() + ".py"
                          << std::endl;
                continue;
            }
            out << script.str();
        }
    }
    return 0;
}