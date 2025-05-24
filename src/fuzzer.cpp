#include "ast.hpp"
#include <cstdlib>

using namespace FuzzingAST;

size_t mutateEntry(ASTData **data, size_t size, size_t maxSize,
				   unsigned int seed) {
	srand(seed);
	return sizeof(ASTData *);
}
