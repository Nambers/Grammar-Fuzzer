#include "ast.hpp"
#include "driver.hpp"
#include "serialization.hpp"
#include <Python.h>
#include <fstream>
#include <iostream>

using json = nlohmann::json;
using namespace FuzzingAST;

int main() {
	Py_Initialize();
	std::ifstream in("test/driver/python/ast_test.json");
	if (!in) {
		std::cerr << "Failed to open input\n";
		return 1;
	}
	json j;
	in >> j;

	AST ast = j.get<AST>();
	runAST(ast, true);
	return 0;
}
