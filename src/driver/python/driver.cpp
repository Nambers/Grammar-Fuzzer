#include "Python.h"
#include "ast.hpp"
#include "driver.hpp"
#include "pythonrun.h"
#include <iostream>
#include <sstream>

using namespace FuzzingAST;

std::string nodeToPython(const ASTNode &node, const AST &ast,
						 int indentLevel = 0);

std::string valueToPython(const ASTNodeValue &val, const AST &ast,
						  int indentLevel = 0) {
	if (std::holds_alternative<std::string>(val.val)) {
		std::string s = std::get<std::string>(val.val);
		return val.isIdentifier ? s : "\"" + s + "\"";
	} else if (std::holds_alternative<int64_t>(val.val)) {
		return std::to_string(std::get<int64_t>(val.val));
	} else if (std::holds_alternative<bool>(val.val)) {
		return std::get<bool>(val.val) ? "True" : "False";
	} else if (std::holds_alternative<double>(val.val)) {
		return std::to_string(std::get<double>(val.val));
	} else if (std::holds_alternative<NodeID>(val.val)) {
		NodeID id = std::get<NodeID>(val.val);
		return nodeToPython(ast.expressions.at(id), ast, indentLevel);
	}
	return "None";
}

std::string scopeToPython(ScopeID sid, const AST &ast, int indentLevel);

std::string nodeToPython(const ASTNode &node, const AST &ast, int indentLevel) {
	std::ostringstream out;
	std::string ind(indentLevel * 4, ' ');

	switch (node.kind) {
	case ASTNodeKind::DeclareVar:
	case ASTNodeKind::Assign: {
		std::string lhs = valueToPython(node.fields[0], ast);
		std::string rhs = valueToPython(node.fields[1], ast);
		out << ind << lhs << " = " << rhs;
		break;
	}
	case ASTNodeKind::Return: {
		std::string val = valueToPython(node.fields[0], ast);
		out << ind << "return " << val;
		break;
	}
	case ASTNodeKind::Call: {
		std::string fn = valueToPython(node.fields[0], ast);
		out << ind << fn << "(";
		for (size_t i = 1; i < node.fields.size(); ++i) {
			if (i > 1)
				out << ", ";
			out << valueToPython(node.fields[i], ast);
		}
		out << ")";
		break;
	}
	case ASTNodeKind::BinaryOp: {
		std::string lhs = valueToPython(node.fields[0], ast);
		std::string op = std::get<std::string>(node.fields[1].val);
		std::string rhs = valueToPython(node.fields[2], ast);
		out << ind << lhs << " " << op << " " << rhs;
		break;
	}
	case ASTNodeKind::UnaryOp: {
		std::string op = std::get<std::string>(node.fields[0].val);
		std::string expr = valueToPython(node.fields[1], ast);
		out << ind << op << expr;
		break;
	}
	case ASTNodeKind::Literal:
	case ASTNodeKind::Variable: {
		out << ind << valueToPython(node.fields[0], ast);
		break;
	}
	default:
		out << ind << "# unsupported kind " << static_cast<int>(node.kind)
			<< "\n";
		break;
	}
	return out.str();
}

std::string scopeToPython(ScopeID sid, const AST &ast, int indentLevel) {
	if (sid == -1) {
		return "";
	}
	std::ostringstream out;
	const ASTScope &scope = ast.scopes[sid];
	for (NodeID id : scope.declarations) {
		out << nodeToPython(ast.declarations[id], ast, indentLevel) << "\n";
	}
	for (NodeID id : scope.expressions) {
		out << nodeToPython(ast.expressions[id], ast, indentLevel) << "\n";
	}
	if (out.str().empty()) {
		std::string ind(indentLevel * 4, ' ');
		out << ind << "pass\n";
	}
	return out.str();
}

static PyObject *globalModule = nullptr;

int FuzzingAST::runAST(const AST &ast, bool echo) {
	std::ostringstream script;
	script << scopeToPython(0, ast, 0);

	std::string re = script.str();
	if (echo) {
		std::cout << "[Generated Python]:\n" << re << "\n";
	}

	PyRun_String(re.c_str(), Py_file_input, globalModule, globalModule);
	if (PyErr_Occurred()) {
#ifndef QUIET
		PyErr_Print();
#endif
		return -1;
	}
	return 0;
}

int FuzzingAST::initialize(int *argc, char ***argv) {
	Py_Initialize();
	globalModule = PyModule_GetDict(PyImport_AddModule("__main__"));
	return 0;
}

int FuzzingAST::finalize() {
	if (globalModule) {
		Py_DECREF(globalModule);
		globalModule = nullptr;
	}
	Py_Finalize();
	return 0;
}
