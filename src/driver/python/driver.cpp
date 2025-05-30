#include "Python.h"
#include "ast.hpp"
#include "driver.hpp"
#include "log.hpp"
#include "pythonrun.h"
#include <iostream>
#include <serialization.hpp>
#include <sstream>

using namespace FuzzingAST;

void valueToPython(std::ostringstream &out, const ASTNodeValue &val,
				   const AST &ast, int indentLevel);

void nodeToPython(std::ostringstream &out, const ASTNode &node, const AST &ast,
				  int indentLevel);

void scopeToPython(std::ostringstream &out, ScopeID sid, const AST &ast,
				   int indentLevel);

void valueToPython(std::ostringstream &out, const ASTNodeValue &val,
				   const AST &ast, int indentLevel) {
	if (std::holds_alternative<std::string>(val.val)) {
		const std::string &s = std::get<std::string>(val.val);
		if (val.isIdentifier)
			out << s;
		else
			out << "\"" << s << "\"";
	} else if (std::holds_alternative<int64_t>(val.val)) {
		out << std::get<int64_t>(val.val);
	} else if (std::holds_alternative<bool>(val.val)) {
		out << (std::get<bool>(val.val) ? "True" : "False");
	} else if (std::holds_alternative<double>(val.val)) {
		out << std::get<double>(val.val);
	} else if (std::holds_alternative<NodeID>(val.val)) {
		const ASTNode &child = ast.expressions.at(std::get<NodeID>(val.val));
		nodeToPython(out, child, ast, indentLevel);
	} else {
		out << "None";
	}
}

void nodeToPython(std::ostringstream &out, const ASTNode &node, const AST &ast,
				  int indentLevel) {
	std::string ind(indentLevel * 4, ' ');
	out << ind;

	switch (node.kind) {
	case ASTNodeKind::DeclareVar:
	case ASTNodeKind::Assign:
		valueToPython(out, node.fields[0], ast, indentLevel);
		out << " = ";
		valueToPython(out, node.fields[1], ast, indentLevel);
		break;

	case ASTNodeKind::Return:
		out << "return ";
		valueToPython(out, node.fields[0], ast, indentLevel);
		break;

	case ASTNodeKind::Call:
		valueToPython(out, node.fields[0], ast, indentLevel);
		out << "(";
		for (size_t i = 1; i < node.fields.size(); ++i) {
			if (i > 1)
				out << ", ";
			valueToPython(out, node.fields[i], ast, indentLevel);
		}
		out << ")";
		break;

	case ASTNodeKind::BinaryOp:
		valueToPython(out, node.fields[0], ast, indentLevel);
		out << " " << std::get<std::string>(node.fields[1].val) << " ";
		valueToPython(out, node.fields[2], ast, indentLevel);
		break;

	case ASTNodeKind::UnaryOp:
		out << std::get<std::string>(node.fields[0].val);
		valueToPython(out, node.fields[1], ast, indentLevel);
		break;

	case ASTNodeKind::Literal:
	case ASTNodeKind::Variable:
		valueToPython(out, node.fields[0], ast, indentLevel);
		break;

	default:
		out << "# unsupported kind " << static_cast<int>(node.kind);
		break;
	}
	out << "\n";
}

void scopeToPython(std::ostringstream &out, ScopeID sid, const AST &ast,
				   int indentLevel) {
	if (sid == -1)
		return;

	const ASTScope &scope = ast.scopes[sid];
	bool empty = true;

	for (NodeID id : scope.declarations) {
		nodeToPython(out, ast.declarations.at(id), ast, indentLevel);
		empty = false;
	}
	for (NodeID id : scope.expressions) {
		nodeToPython(out, ast.expressions.at(id), ast, indentLevel);
		empty = false;
	}

	if (empty) {
		out << std::string(indentLevel * 4, ' ') << "pass\n";
	}
}

int FuzzingAST::runAST(const AST &ast, bool echo) {
	std::ostringstream script;
	scopeToPython(script, 0, ast, 0);
	std::string re = script.str();

	if (echo) {
		std::cout << "[Generated Python]:\n" << re << "\n";
	}

	PyObject *ctx = PyModule_GetDict(PyImport_AddModule("__main__"));
	PyRun_String(re.c_str(), Py_file_input, ctx, ctx);
	Py_DECREF(ctx);
	if (PyErr_Occurred()) {
#ifndef QUIET
		PyErr_Print();
#endif
		return -1;
	}
	return 0;
}

static FILE *driverPyFile = nullptr;

int FuzzingAST::initialize(int *argc, char ***argv) {
	driverPyFile = fopen("driver.py", "r");
	Py_Initialize();
	return 0;
}

int FuzzingAST::finalize() {
	if (driverPyFile) {
		fclose(driverPyFile);
		driverPyFile = nullptr;
	}
	Py_Finalize();
	return 0;
}

void FuzzingAST::loadBuiltinsFuncs(
	std::unordered_map<std::string, FunctionSignature> &funcSignatures) {
	FILE *file = fopen("builtins.json", "r");
	if (!file) {
		PANIC("Failed to open builtins.json, run build.sh to generate it.");
	}
	nlohmann::json j = nlohmann::json::parse(file);
	fclose(file);
	funcSignatures =
		j.get<std::unordered_map<std::string, FunctionSignature>>();
}

void FuzzingAST::reflectObject(ASTData &data, ScopeID sid) {
	std::ostringstream script;

	const ASTScope &scope = data.ast.scopes[sid];
	bool empty = true;

	for (NodeID id : scope.declarations) {
		nodeToPython(script, data.ast.declarations.at(id), data.ast, 0);
		empty = false;
	}

	std::string re = script.str();

	PyObject *ctx = PyModule_GetDict(PyImport_AddModule("__main__"));
	PyRun_String(re.c_str(), Py_file_input, ctx, ctx);
	PyRun_File(driverPyFile, "driver.py", Py_file_input, ctx, ctx);
	PyObject *rawJson = PyObject_GetAttrString(ctx, "result");
}
