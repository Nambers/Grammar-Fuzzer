#include "FuzzSchedulerState.hpp"
#include "log.hpp"
#include <cmath>

using namespace FuzzingAST;

void FuzzingAST::FuzzSchedulerState::update(bool gotNewEdge,
											size_t currentAstSize) {
	if (gotNewEdge) {
		noEdgeCount = 0;
		execStallCount = 0;
		phase = MutationPhase::ExecutionGeneration;
		return;
	}

	++noEdgeCount;

	// update strategy based on the current phase
	switch (phase) {
	case MutationPhase::ExecutionGeneration:
		if (noEdgeCount > execFailureThreshold()) {
			if (++execStallCount == maxDeclFailures) {
				phase = MutationPhase::FallbackOldCorpus;
				INFO("switching to fallback phase due to execution generation "
					 "failures\n");
				noEdgeCount = 0;
				execStallCount = 0;
			} else {
				INFO("switching to declaration mutation phase due to no new "
					 "edge "
					 "found\n");
				phase = MutationPhase::DeclarationMutation;
				noEdgeCount = 0;
			}
		}
		break;

	case MutationPhase::DeclarationMutation:
		phase = MutationPhase::ExecutionGeneration;
		break;
	case MutationPhase::FallbackOldCorpus:
		phase = MutationPhase::ExecutionGeneration;
		break;
	}
}

size_t FuzzingAST::FuzzSchedulerState::execFailureThreshold() const {
	return static_cast<size_t>(std::log2(corpusSize + 4) * 10);
}
