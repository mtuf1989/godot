#ifndef SYMPHONY_PUBLISHED_U64_H
#define SYMPHONY_PUBLISHED_U64_H

#include <atomic>
#include <cstdint>

// Single-writer 64-bit value published with 32-bit atomics.
//
// std::atomic<uint64_t> is not lock-free on wasm32 without the threads/atomics
// feature: libatomic then takes a hidden lock inside the audio callback.
// 32-bit atomics stay lock-free in both threaded and single-threaded Web builds.
//
// Only the writer may call publish() or fetch_add(). Readers call load().
// A second writer racing publish() is a data race.

class SymphonyPublishedU64 {
	static_assert(std::atomic<uint32_t>::is_always_lock_free, "Symphony publication requires lock-free 32-bit atomics");

	alignas(64) std::atomic<uint32_t> seq_{ 0 };
	std::atomic<uint32_t> lo_{ 0 };
	std::atomic<uint32_t> hi_{ 0 };
	uint64_t shadow_ = 0;

public:
	void publish(uint64_t p_value) {
		shadow_ = p_value;
		const uint32_t seq = seq_.load(std::memory_order_relaxed);
		seq_.store(seq + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		lo_.store(static_cast<uint32_t>(p_value), std::memory_order_relaxed);
		hi_.store(static_cast<uint32_t>(p_value >> 32), std::memory_order_relaxed);
		seq_.store(seq + 2, std::memory_order_release);
	}

	uint64_t fetch_add(uint64_t p_delta) {
		const uint64_t previous = shadow_;
		publish(previous + p_delta);
		return previous;
	}

	[[nodiscard]] uint64_t writer_value() const { return shadow_; }

	[[nodiscard]] uint64_t load() const {
		uint64_t result = 0;
		while (true) {
			const uint32_t s1 = seq_.load(std::memory_order_acquire);
			if (s1 & 1u) {
				continue;
			}
			const uint32_t lo = lo_.load(std::memory_order_relaxed);
			const uint32_t hi = hi_.load(std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_acquire);
			const uint32_t s2 = seq_.load(std::memory_order_relaxed);
			if (s1 == s2) {
				result = (uint64_t)lo | ((uint64_t)hi << 32);
				return result;
			}
		}
	}
};

#endif // SYMPHONY_PUBLISHED_U64_H
