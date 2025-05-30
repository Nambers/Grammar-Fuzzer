#ifndef FUZZSCHEDULERSTATE_HPP
#define FUZZSCHEDULERSTATE_HPP

#include <stddef.h>
#include <memory>
#include "ast.hpp"

namespace FuzzingAST {
enum class MutationPhase {
	ExecutionGeneration,
	DeclarationMutation,
	FallbackOldCorpus
};
class FuzzSchedulerState {
  public:
	size_t noEdgeCount = 0;
	size_t execStallCount = 0;
	MutationPhase phase = MutationPhase::ExecutionGeneration;
	// corpus index
	uint idx = 0;
	std::vector<std::shared_ptr<ASTData>> corpus;

	// if failed to find new edge for 5 mutated declaration in a row, fallback
	size_t maxDeclFailures = 5;
	// max 5 variables / type declarations
	size_t maxNumDeclarations = 5;

	std::unordered_map<std::string, FunctionSignature> builtinsFuncs;
	FuzzSchedulerState() = default;

	void update(bool gotNewEdge, size_t maxNumDeclarations);

	size_t execFailureThreshold() const;
};
} // namespace FuzzingAST

#endif // FUZZSCHEDULERSTATE_HPP