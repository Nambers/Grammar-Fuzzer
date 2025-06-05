#ifndef FUZZER_EMIT_HPP
#define FUZZER_EMIT_HPP
#include "ast.hpp"
#include <memory>
#include <string>
#include <vector>

namespace FuzzingAST {
constexpr size_t MAX_CACHE_SIZE = 500;
extern std::vector<std::string> cacheCorpus;
void fuzzerEmitCacheCorpus();
void fuzzerLoadCorpus(const std::string &savedPath,
                      std::vector<std::shared_ptr<ASTData>> &corpus);
std::string make_unique_filename(int counter);
} // namespace FuzzingAST

#endif // FUZZER_EMIT_HPP