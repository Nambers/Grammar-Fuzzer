#ifndef FUZZSCHEDULERSTATE_HPP
#define FUZZSCHEDULERSTATE_HPP

#include <stddef.h>

namespace FuzzingAST {
enum class MutationPhase { ExecutionGeneration, DeclarationMutation, FallbackOldCorpus };
class FuzzSchedulerState {
  public:
	size_t noEdgeCount = 0;
	size_t execStallCount = 0;
	MutationPhase phase = MutationPhase::ExecutionGeneration;

	// if failed to find new edge 3 times in a row, fallback
	size_t maxDeclFailures = 3;
	// max 5 variables / type declarations
	size_t maxNumDeclarations = 5;
	size_t corpusSize = 1;

	FuzzSchedulerState() = default;

	void update(bool gotNewEdge, size_t maxNumDeclarations);

	size_t execFailureThreshold() const;
};
} // namespace FuzzingAST

#endif // FUZZSCHEDULERSTATE_HPP