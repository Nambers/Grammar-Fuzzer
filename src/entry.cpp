#include "ast.hpp"
#include "serialization.hpp"

#include "driver.hpp"
#include "log.hpp"
#include <Python.h>
#include <cstdlib>
#include <fstream>
#include <stddef.h>
#include <stdint.h>

using namespace FuzzingAST;
using json = nlohmann::json;

extern "C" void __sanitizer_set_death_callback(void (*)(void));
size_t mutateEntry(ASTData **, size_t, size_t, unsigned int);

std::ifstream last_case;
json data_backup;

void __attribute__((visibility("default"))) crash_handler() {
	ERROR("crash! last saved states\n");
	std::string dump = data_backup.dump();
	INFO("AST={}\n", dump);
	_exit(1);
}

extern "C" int LLVMFuzzerTestOneInput(const ASTData **data, size_t size) {
	if (size != sizeof(ASTData *) || *data == nullptr) {
		// let's cook
		return -1;
	}
	data_backup = (*data)->ast;
	return runAST((*data)->ast);
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
	if (argc != NULL && argv != NULL) {
		for (int i = 0; i < *argc; i++) {
			if (std::strncmp((*argv)[i],
							 "-last-case=", strlen("-last-case=")) == 0) {
				std::string_view filename = (*argv)[i] + strlen("-last-case=");
				INFO("Using last-case file: {}\n", filename);
				last_case.open(filename.data(), std::ios::in);
				if (!last_case.is_open()) {
					PANIC("Failed to open last-case file: {}\n", filename);
				}
				break;
			}
		}
	}
	return initialize(argc, argv);
}

extern "C" size_t __attribute__((visibility("default")))
LLVMFuzzerCustomMutator(ASTData **data, size_t size, size_t maxSize,
						unsigned int seed) {
	__sanitizer_set_death_callback(crash_handler);
	if (size != sizeof(ASTData *) || *data == nullptr) {
		INFO("Initializing first ASTData");
		*data = new ASTData();
		if (last_case.is_open()) {
			json j;
			last_case >> j;
			last_case.close();
			(*data)->ast = j.get<AST>();
		}
		return sizeof(ASTData *);
	}
	return mutateEntry(data, size, maxSize, seed);
}
