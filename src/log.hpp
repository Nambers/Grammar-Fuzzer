#ifndef LOGH
#define LOGH

#include <format>
#include <iostream>
#include <signal.h>

#define INFO Log::info
#define ERROR Log::error
#define PANIC Log::panic

namespace Log {

inline constexpr const char *RED = "\033[0;31m";
inline constexpr const char *RESET = "\033[0m";

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args &&...args) {
#ifndef DSIABLE_DEBUG_OUTPUT
    std::cout << std::format(fmt, std::forward<Args>(args)...) << std::endl;
#endif
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args &&...args) {
#ifndef DISABLE_INFO_OUTPUT
    std::cout << std::format(fmt, std::forward<Args>(args)...) << std::endl;
#endif
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args &&...args) {
    std::cerr << RED << std::format(fmt, std::forward<Args>(args)...) << RESET
              << std::endl;
}

template <typename... Args>
[[noreturn]] void __attribute__((noreturn))
panic(std::format_string<Args...> fmt, Args &&...args) {
    std::cerr << RED << std::format(fmt, std::forward<Args>(args)...) << RESET
              << std::endl;
    raise(SIGINT);
    abort();
}
} // namespace Log

#endif // LOGH
