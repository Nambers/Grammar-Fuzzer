#include "driver.hpp"
#include "fuzzer.hpp"

using namespace FuzzingAST;

int main(int argc, char **argv) {
	FuzzerInitialize(&argc, &argv);
	fuzzerDriver();
	finalize();
	return 0;
}
