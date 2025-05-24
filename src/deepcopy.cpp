#include "ast.hpp"

using namespace FuzzingAST;

AST deepCopyAST(const AST &ast) {
	AST newAST;
	newAST.scopes = ast.scopes;
	newAST.declarations = ast.declarations;
	newAST.expressions = ast.expressions;
	return newAST;
}
