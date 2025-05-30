#include "log.hpp"
#include <dlfcn.h>
#include <unordered_set>

extern uint32_t newEdgeCnt;

extern "C" void __sanitizer_cov_trace_pc_guard_init(uint32_t *start,
													uint32_t *stop) {
	if (start == stop || *start)
		return; // already initialized
	static uint32_t N;
	for (uint32_t *x = start; x < stop; x++)
		*x = ++N;
	newEdgeCnt = 0;
	INFO("Sanitizer coverage trace PC guard initialized.");
}

extern "C" void __sanitizer_cov_trace_pc_guard(uint32_t *guard) {
	if (!*guard)
		return;

	void *pc = __builtin_return_address(0);
	Dl_info info;
	if (dladdr((void *)pc, &info) && info.dli_sname)
		INFO("NEW FUNC: {} {}", (void *)pc, info.dli_sname);
	else
		INFO("NEW FUNC: {}", (void *)pc);
	newEdgeCnt++;

	*guard = 0;
}
