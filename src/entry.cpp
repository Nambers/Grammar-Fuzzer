#include "ast.hpp"
#include "serialization.hpp"

#include "driver.hpp"
#include "fuzzer.hpp"
#include "log.hpp"
#include <Python.h>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

using namespace FuzzingAST;
using json = nlohmann::json;

extern "C" void __sanitizer_set_death_callback(void (*)(void));
int testOneInput(const std::shared_ptr<ASTData> &data);
void crash_handler();

void sigint_handler(int signo) {
	crash_handler();
	std::_Exit(130);
}

std::shared_ptr<ASTData> FuzzerInitialize(int *argc, char ***argv) {
	std::shared_ptr<ASTData> ret = std::make_shared<ASTData>();
	if (argc != NULL && argv != NULL) {
		std::ifstream last_case;
		for (int i = 0; i < *argc; i++) {
			if (std::strncmp((*argv)[i],
							 "-last-case=", strlen("-last-case=")) == 0) {
				std::string_view filename = (*argv)[i] + strlen("-last-case=");
				INFO("Using last-case file: {}", filename);
				last_case.open(filename.data(), std::ios::in);
				if (!last_case.is_open()) {
					PANIC("Failed to open last-case file: {}", filename);
				}
				ret->ast = json::parse(last_case).get<AST>();
				last_case.close();
				break;
			}
		}
	}
	initialize(argc, argv);
	// override potential SIGINT handler in language interpreter
	signal(SIGINT, sigint_handler);
	__sanitizer_set_death_callback(crash_handler);
	return ret;
}

extern uint8_t *snapshot;
extern "C" void __attribute__((visibility("default"))) LLVMFuzzerFinalize() {
	if (snapshot) {
		delete[] snapshot;
		snapshot = nullptr;
	}
	finalize();
	INFO("Fuzzer finalized.");
}

int main(int argc, char **argv) {
	// init_cov();
	auto ret = FuzzerInitialize(&argc, &argv);
	fuzzerDriver(ret);
	LLVMFuzzerFinalize();
	return 0;
}
