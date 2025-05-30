#ifndef FUZZER_HPP
#define FUZZER_HPP
#include "ast.hpp"
#include <memory>
#include <random>

namespace FuzzingAST {
void init_cov();
bool has_new_edge();
// size_t mutateEntry(ASTData **, size_t, size_t, unsigned int);
void fuzzerDriver(std::shared_ptr<ASTData> &);
} // namespace FuzzingAST

#endif // FUZZER_HPP