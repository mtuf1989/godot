#include "symphony_seed.h"

#include <atomic>

SymphonyCompileContext &symphony_compile_context() {
	static thread_local SymphonyCompileContext ctx;
	return ctx;
}

uint64_t symphony_draw_voice_seed() {
	static std::atomic<uint64_t> counter{ 1 };
	return splitmix64(counter.fetch_add(1, std::memory_order_relaxed));
}
