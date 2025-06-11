#ifndef FUZZER_HPP
#define FUZZER_HPP
#include "ast.hpp"
#include <random>

namespace FuzzingAST {
void FuzzerInitialize(int *argc, char ***argv);
void fuzzerDriver();
} // namespace FuzzingAST

#endif // FUZZER_HPP