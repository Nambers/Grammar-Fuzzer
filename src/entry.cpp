#include "ast.hpp"
#include "serialization.hpp"

#include "driver.hpp"
#include "fuzzer.hpp"
#include "log.hpp"
#include <Python.h>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <stddef.h>
#include <stdint.h>

using namespace FuzzingAST;
using json = nlohmann::json;

extern "C" void __attribute__((visibility("default"))) LLVMFuzzerFinalize() {
	finalize();
	INFO("Fuzzer finalized.");
}

int main(int argc, char **argv) {
	// init_cov();
	auto ret = FuzzerInitialize(&argc, &argv);
	fuzzerDriver(std::move(ret));
	LLVMFuzzerFinalize();
	return 0;
}
