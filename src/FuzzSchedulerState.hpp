#ifndef FUZZSCHEDULERSTATE_HPP
#define FUZZSCHEDULERSTATE_HPP

#include "ast.hpp"
#include <memory>
#include <stddef.h>

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
    // initial mutation
	MutationPhase phase = MutationPhase::DeclarationMutation;
	// corpus index
	uint idx = 0;
	std::vector<std::shared_ptr<ASTData>> corpus;

	// if failed to find new edge for 5 mutated declaration in a row, fallback
	size_t maxDeclFailures = 5;
	// max variables / type declarations in all scopes
	size_t maxNumDeclarations = 20;

	BuiltinContext ctx;
	FuzzSchedulerState() = default;

	void update(bool gotNewEdge, size_t maxNumDeclarations);

	size_t execFailureThreshold() const;
};
} // namespace FuzzingAST

#endif // FUZZSCHEDULERSTATE_HPP