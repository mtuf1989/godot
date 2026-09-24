#include "symphony_render_seed.h"

#include <atomic>

namespace SymphonyRenderSeed {
static std::atomic<uint32_t> value{ 0 };

void set(uint32_t p_seed) {
	value.store(p_seed, std::memory_order_release);
}

uint32_t get() {
	return value.load(std::memory_order_acquire);
}
}
