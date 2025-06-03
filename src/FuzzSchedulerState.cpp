#include "FuzzSchedulerState.hpp"
#include "log.hpp"
#include <cmath>

using namespace FuzzingAST;

extern uint32_t newEdgeCnt;

void FuzzingAST::FuzzSchedulerState::update(bool gotNewEdge,
                                            size_t currentAstSize) {
    std::cout << "current phrase : " << static_cast<int>(phase)
              << ", new edge count: " << newEdgeCnt
              << ", no edge count: " << noEdgeCount
              << ", fail threshold: " << execFailureThreshold()
              << ", exec stall count: " << execStallCount
              << ", current AST size: " << currentAstSize << "\n";
    if (gotNewEdge) {
        noEdgeCount = 0;
        execStallCount = 0;
        phase = MutationPhase::ExecutionGeneration;
        return;
    }

    // update strategy based on the current phase
    switch (phase) {
    case MutationPhase::ExecutionGeneration:
        ++noEdgeCount;
        if (noEdgeCount > execFailureThreshold()) {
            noEdgeCount = 0;
            if (++execStallCount == maxDeclFailures) {
                phase = MutationPhase::FallbackOldCorpus;
                INFO("switching to fallback phase due to execution generation "
                     "failures\n");
                execStallCount = 0;
            } else {
                INFO("switching to declaration mutation phase due to no new "
                     "edge "
                     "found\n");
                phase = MutationPhase::DeclarationMutation;
            }
        }
        break;

    case MutationPhase::DeclarationMutation:
        [[fallthrough]];
    case MutationPhase::FallbackOldCorpus:
        phase = MutationPhase::ExecutionGeneration;
        break;
    }
}

size_t FuzzingAST::FuzzSchedulerState::execFailureThreshold() const {
    double base = std::log2(static_cast<double>(newEdgeCnt) + 4.0);
    double adjusted = std::sqrt(base); // optional: log2(base + 1.0)
    return static_cast<size_t>(std::clamp(adjusted * 20.0, 100.0, 2000.0));
}
