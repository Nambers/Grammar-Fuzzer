#ifndef FUZZER_EMIT_HPP
#define FUZZER_EMIT_HPP
#include <string>
#include <vector>

namespace FuzzingAST {
constexpr size_t MAX_CACHE_SIZE = 500;
extern std::vector<std::string> cacheCorpus;
void fuzzerEmitCacheCorpus(const std::vector<std::string> &jsonCorpus);
} // namespace FuzzingAST

#endif // FUZZER_EMIT_HPP