#ifndef AST_HPP
#define AST_HPP

#include <array>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace FuzzingAST {

using NodeID = int;
using ScopeID = int;
using TypeID = int;

enum class ASTNodeKind {
	Function,	// def f(x):
	Class,		// class A:
	DeclareVar, // var a = ...
	Import,		// import x
				// ---
	Assign,		// x = y
	Call,		// f(x)
	Return,		// return x
	Literal,	// 42, "abc"
	Variable,	// x
	BinaryOp,	// x + y
	UnaryOp,	// -x
	Reflect		// reflectObject(x)
};

constexpr ASTNodeKind DECL_NODE_END = ASTNodeKind::Import;
constexpr ASTNodeKind EXEC_NODE_START = ASTNodeKind::Assign;

class ASTNodeValue {
  public:
	bool isIdentifier = false;
	std::variant<std::string, int64_t, bool, double, NodeID> val;
};

class ASTNode {
  public:
	ASTNodeKind kind;
	// if it's declareVar, the type is the type of the variable, lvar
	TypeID type = -1;
	std::vector<ASTNodeValue> fields;
	ScopeID scope = -1;
};

class ASTScope {
  public:
	std::vector<NodeID> declarations;
	std::vector<NodeID> expressions;
};

class FunctionSignature {
  public:
	std::vector<TypeID> paramTypes;
	TypeID selfType = -1; // for methods, the type of the class
	TypeID returnType = -1;
};

class AST {
  public:
	std::vector<ASTScope> scopes;
	std::vector<ASTNode> declarations;
	std::vector<ASTNode> expressions;
	std::vector<std::string> types;
	std::unordered_map<std::string, FunctionSignature> funcSignatures;

	// generate main block
	AST() : scopes({ASTScope()}) {}
};

class ASTData {
  public:
	AST ast;

	ASTData() = default;
	ASTData(const ASTData &other) = default;
	ASTData &operator=(const ASTData &other) = default;
};
}; // namespace FuzzingAST

#endif // AST_HPP
