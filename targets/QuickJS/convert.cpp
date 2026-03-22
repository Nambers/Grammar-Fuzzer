#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json;
using namespace FuzzingAST;

int main(int argc, char *argv[]) {
    BuiltinContext ctx;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_json_files>\n";
        return 1;
    }
    std::string path = argv[1];
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::ifstream in(entry.path());
            if (!in) {
                std::cerr << "Failed to open file: " << entry.path() << "\n";
                continue;
            }
            json j;
            in >> j;
            AST ast = j.get<AST>();

            std::ostringstream script;
            scopeToJS(script, 0, ast, 0);

            auto p = entry.path();
            std::ofstream out(p.replace_extension(".js"));
            if (!out) {
                std::cerr << "Failed to open output file: " << p << "\n";
                continue;
            }
            out << script.str();
        }
    }
    return 0;
}
