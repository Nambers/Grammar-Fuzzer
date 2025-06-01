#ifndef FUZZER_HPP
#define FUZZER_HPP
#include "ast.hpp"
#include <memory>
#include <random>

namespace FuzzingAST {
void fuzzerDriver(std::shared_ptr<ASTData> &);
} // namespace FuzzingAST

#endif // FUZZER_HPP