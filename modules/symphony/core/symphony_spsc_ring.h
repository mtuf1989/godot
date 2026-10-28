/**************************************************************************/
/*  symphony_spsc_ring.h                                                  */
/**************************************************************************/
/* Single-producer single-consumer ring for trivially copyable commands.  */
/**************************************************************************/

#pragma once

#include "core/error/error_macros.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

template <typename T, int N>
class SymphonySpscRing {
	static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");
	static_assert(std::is_trivially_copyable_v<T>, "SymphonySpscRing requires a trivially copyable T");

	// Godot memnew aligns to max_align_t. Pad instead of alignas(64): the slot
	// buffer, the producer index, and the consumer index stay at least 64
	// bytes apart, so they cannot share a cache line at any base address.
	static constexpr size_t kCachePad = 64;

	char pad_before_[kCachePad] = {};
	T slots[N];
	char pad_slots_[kCachePad] = {};
	std::atomic<uint32_t> write_pos{ 0 };
	char pad_indices_[kCachePad] = {};
	std::atomic<uint32_t> read_pos{ 0 };
	char pad_after_[kCachePad] = {};

public:
	static constexpr int CAPACITY = N;

	[[nodiscard]] bool try_push(const T &p_value) {
		static_assert(alignof(SymphonySpscRing) <= alignof(std::max_align_t));
		static_assert(alignof(SymphonySpscRing) == (alignof(T) > alignof(std::atomic<uint32_t>) ? alignof(T) : alignof(std::atomic<uint32_t>)));
		static_assert(sizeof(pad_before_) == kCachePad);
		static_assert(sizeof(pad_slots_) == kCachePad);
		static_assert(sizeof(pad_indices_) == kCachePad);
		static_assert(sizeof(pad_after_) == kCachePad);
		static_assert(offsetof(SymphonySpscRing, slots) >= sizeof(pad_before_));
		static_assert(offsetof(SymphonySpscRing, write_pos) >= offsetof(SymphonySpscRing, slots) + sizeof(slots) + sizeof(pad_slots_));
		static_assert(offsetof(SymphonySpscRing, read_pos) >= offsetof(SymphonySpscRing, write_pos) + sizeof(std::atomic<uint32_t>) + sizeof(pad_indices_));
		static_assert(sizeof(SymphonySpscRing) >= offsetof(SymphonySpscRing, read_pos) + sizeof(std::atomic<uint32_t>) + sizeof(pad_after_));

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
