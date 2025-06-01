#include "FuzzSchedulerState.hpp"
#include "ast.hpp"
#include "driver.hpp"
#include "fuzzer.hpp"
#include "log.hpp"
#include "mutators.hpp"
#include "serialization.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace FuzzingAST;

static nlohmann::json data_backup;
uint32_t newEdgeCnt = 0;

static thread_local std::mt19937 rng(std::random_device{}());

int testOneInput(const std::shared_ptr<ASTData> &data) {
	data_backup = data->ast;
	return runAST(data->ast);
}

void crash_handler() {
	ERROR("crash! last saved states");
	std::string dump = data_backup.dump();
	INFO("AST={}", dump);
	_exit(1);
}

void FuzzingAST::fuzzerDriver(std::shared_ptr<ASTData> &data) {
	FuzzSchedulerState scheduler;
	scheduler.corpus.emplace_back(data);
	loadBuiltinsFuncs(scheduler.builtinsFuncs, scheduler.types);
	while (true) {
		if (scheduler.corpus.empty()) {
			scheduler.corpus.emplace_back(std::make_shared<ASTData>());
		}
		std::shared_ptr<ASTData> newData;
		switch (scheduler.phase) {
		case MutationPhase::ExecutionGeneration:
			// continue generation on current
			newData =
				std::make_shared<ASTData>(*scheduler.corpus[scheduler.idx]);
			generate_execution(newData);
			break;

		case MutationPhase::FallbackOldCorpus:
			if (scheduler.corpus.size() > 1) {
				// randomly fallback to one of first half of the corpus
				scheduler.corpus.erase(scheduler.corpus.begin() +
									   scheduler.idx);
				scheduler.idx =
					std::uniform_int_distribution<>(0, scheduler.idx / 2)(rng);
			}
			[[fallthrough]];
		case MutationPhase::DeclarationMutation:
			newEdgeCnt = 0; // reset edge count for declaration change
			// continue mutating on current
			newData =
				std::make_shared<ASTData>(*scheduler.corpus[scheduler.idx]);
			mutate_declaration(newData);
			scheduler.corpus.emplace_back(newData);
			scheduler.idx = scheduler.corpus.size() - 1;
			break;
		}

		if (scheduler.phase == MutationPhase::ExecutionGeneration) {
			// if fail to run, drop the result and do it again.
			if (testOneInput(newData) == 0) {
				scheduler.update(newEdgeCnt > 0,
								 newData->ast.declarations.size());
			}
		}
	}
}
