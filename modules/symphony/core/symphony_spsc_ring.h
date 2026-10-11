/**************************************************************************/
/*  symphony_spsc_ring.h                                                  */
/**************************************************************************/
/* Single-producer single-consumer ring for trivially copyable commands.  */
/**************************************************************************/

#pragma once

#include "core/error/error_macros.h"

#include <atomic>
#include <cstdint>
#include <type_traits>

template <typename T, int N>
class SymphonySpscRing {
	static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");
	static_assert(std::is_trivially_copyable_v<T>, "SymphonySpscRing requires a trivially copyable T");

	T slots[N];
	alignas(64) std::atomic<uint32_t> write_pos{ 0 };
	alignas(64) std::atomic<uint32_t> read_pos{ 0 };

public:
	static constexpr int CAPACITY = N;

	[[nodiscard]] bool try_push(const T &p_value) {
		const uint32_t w = write_pos.load(std::memory_order_relaxed);
		const uint32_t r = read_pos.load(std::memory_order_acquire);
		if ((w - r) >= (uint32_t)N) {
			return false;
		}
		slots[w & (N - 1)] = p_value;
		write_pos.store(w + 1, std::memory_order_release);
		return true;
	}

	[[nodiscard]] bool try_pop(T &r_out) {
		const uint32_t r = read_pos.load(std::memory_order_relaxed);
		const uint32_t w = write_pos.load(std::memory_order_acquire);
		if (r == w) {
			return false;
		}
		r_out = slots[r & (N - 1)];
		read_pos.store(r + 1, std::memory_order_release);
		return true;
	}

	[[nodiscard]] int size() const {
		const uint32_t w = write_pos.load(std::memory_order_acquire);
		const uint32_t r = read_pos.load(std::memory_order_acquire);
		return (int)(w - r);
	}

	[[nodiscard]] bool empty() const { return size() == 0; }
	[[nodiscard]] int remaining() const { return N - size(); }

	void clear() {
		const uint32_t w = write_pos.load(std::memory_order_relaxed);
		read_pos.store(w, std::memory_order_release);
	}
};
