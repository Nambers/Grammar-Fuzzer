#ifndef LOGH
#define LOGH

#include <format>
#include <iostream>

#define INFO Log::info
#define ERROR Log::error
#define PANIC Log::panic

namespace Log {

inline constexpr const char *RED = "\033[0;31m";
inline constexpr const char *RESET = "\033[0m";

template <typename... Args> void info(const std::string_view &fmt, Args &&...args) {
#ifndef QUIET
	std::cout << std::vformat(
					 fmt, std::make_format_args(std::forward<Args>(args)...))
			  << std::endl;
#endif
}

template <typename... Args> void error(const std::string_view &fmt, Args &&...args) {
	std::cerr << RED
			  << std::vformat(
					 fmt, std::make_format_args(std::forward<Args>(args)...))
			  << RESET << std::endl;
}

template <typename... Args>
[[noreturn]] void __attribute__((noreturn)) panic(const std::string_view &fmt,
												  Args &&...args) {
	std::cerr << RED
			  << std::vformat(
					 fmt, std::make_format_args(std::forward<Args>(args)...))
			  << RESET << std::endl;
	abort();
}
} // namespace Log

#endif // LOGH
