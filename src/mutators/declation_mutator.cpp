#include "mutators.hpp"
#include <random>

using namespace FuzzingAST;

extern std::mt19937 rng;

int FuzzingAST::mutate_expression(const std::shared_ptr<ASTData> &ast,
                      const std::vector<NodeID> &nodes) {
    // TODO mutate
    return 0;
}