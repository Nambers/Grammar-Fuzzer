#ifndef FUZZER_EMIT_HPP
#define FUZZER_EMIT_HPP
#include "ast.hpp"
#include <string>
#include <vector>
#include <deque>

namespace FuzzingAST {
constexpr size_t MAX_CACHE_SIZE = 100;
extern std::vector<std::string> cacheCorpus;
void fuzzerEmitCacheCorpus();
void fuzzerLoadCorpus(const std::string &savedPath,
                      std::deque<ASTData> &corpus);
std::string make_unique_filename(int counter);
} // namespace FuzzingAST

#endif // FUZZER_EMIT_HPP