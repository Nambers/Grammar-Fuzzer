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

constexpr size_t switch_threshold = 1000; // Threshold to switch strategies

extern "C" {
__attribute__((used, section("__sancov_cntrs"))) static const uint8_t
	__sancov_cntrs_marker_start = 0;

__attribute__((
	used,
	section("__sancov_cntrs"))) static const uint8_t __sancov_cntrs_marker_end =
	0;
}

extern "C" {
extern uint8_t __start___sancov_cntrs[] __attribute__((visibility("default")));
extern uint8_t __stop___sancov_cntrs[] __attribute__((visibility("default")));
}

static size_t sancov_num_edges;
static uint8_t *snapshot = nullptr;
static thread_local int no_edge_cnt = 0;
static nlohmann::json data_backup;

static thread_local std::mt19937 rng(std::random_device{}());

std::shared_ptr<ASTData>
mutateEntry(FuzzSchedulerState &scheduler,
			std::vector<std::shared_ptr<ASTData>> &corpus) {
	std::shared_ptr<ASTData> newData;
	switch (scheduler.phase) {
	case MutationPhase::ExecutionGeneration:
		newData = std::make_shared<ASTData>(*corpus.back());
		generate_execution(newData);
		break;

	case MutationPhase::DeclarationMutation:
		newData = std::make_shared<ASTData>(*corpus.back());
		mutate_declaration(newData);
		break;

	case MutationPhase::FallbackOldCorpus:
		size_t idx = std::uniform_int_distribution<>(0, corpus.size() / 3)(rng);
		newData = corpus[idx];
		corpus.erase(corpus.begin() + idx);
		mutate_declaration(newData);
		break;
	}
	return newData;
}

// extern "C" int LLVMFuzzerTestOneInput(const ASTData **data, size_t size) {
int testOneInput(const std::shared_ptr<ASTData> &data) {
	data_backup = data->ast;
	return runAST(data->ast);
}

extern "C" void __sanitizer_set_death_callback(void (*)(void));

void __attribute__((visibility("default"))) crash_handler() {
	ERROR("crash! last saved states\n");
	std::string dump = data_backup.dump();
	INFO("AST={}\n", dump);
	_exit(1);
}

void FuzzingAST::fuzzerDriver(std::shared_ptr<ASTData> &data) {
	__sanitizer_set_death_callback(crash_handler);
	std::vector<std::shared_ptr<ASTData>> corpus;
	corpus.emplace_back(data);
	FuzzSchedulerState scheduler;
	while (true) {
		scheduler.corpusSize = corpus.size();
		std::shared_ptr<ASTData> input = mutateEntry(scheduler, corpus);

		// if fail to run, drop the result and do it again.
		if (testOneInput(input) == 0) {
			bool newEdge = has_new_edge();

			scheduler.update(newEdge, input->ast.declarations.size());

			if (newEdge || corpus.empty()) {
				corpus.push_back(input);
			}

			// log_json.push_back({{"phase", to_string(scheduler.phase)},
			// 					{"no_edge_count", scheduler.noEdgeCount},
			// 					{"execStallCount", scheduler.execStallCount},
			// 					{"ast_size", input->astSize()}});

			// if (log_json.size() % 100 == 0) {
			// 	std::ofstream out("fuzz_log.json");
			// 	out << log_json.dump(2);
			// 	log_json.clear();
			// }
		}
	}
}

void FuzzingAST::init_cov() {
	sancov_num_edges = __stop___sancov_cntrs - __start___sancov_cntrs;
	snapshot = new uint8_t[sancov_num_edges];
	std::memset(snapshot, 0, sancov_num_edges);
}

bool FuzzingAST::has_new_edge() {
	bool found = false;
	for (size_t i = 0; i < sancov_num_edges; ++i) {
		if (__start___sancov_cntrs[i] != snapshot[i]) {
			snapshot[i] = __start___sancov_cntrs[i];
			found = true;
		}
	}
	return found;
}
