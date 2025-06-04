#ifndef FUZZER_HPP
#define FUZZER_HPP
#include "ast.hpp"
#include <memory>
#include <random>

namespace FuzzingAST {
AST FuzzerInitialize(int *argc, char ***argv);
void fuzzerDriver(AST initAST);
} // namespace FuzzingAST

#endif // FUZZER_HPP