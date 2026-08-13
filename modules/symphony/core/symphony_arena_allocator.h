#pragma once

#include "core/os/memory.h"
#include "symphony_realtime_scope.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Single-allocation bump-pointer arena for operator states and audio buffers.
// Allocated once during graph compilation (main thread). Never grows.
// All memory freed in one shot when the compiled graph is destroyed.
struct ArenaAllocator {
	uint8_t *raw_base = nullptr; // Original memalloc pointer (may be unaligned)
	uint8_t *base = nullptr; // 32-byte-aligned usable region
	size_t capacity = 0;
	size_t offset = 0;
	size_t mark_offset = 0;

	// Record current bump offset for transactional operator construction.
	void mark() { mark_offset = offset; }
	void rewind_to_mark() { offset = mark_offset; }

	// Allocate the arena with a given total size.
	// Base pointer is 32-byte aligned (C12 / improve_plan §3).
	bool init(size_t p_capacity) {
		symphony_rt_note(SymphonyRTViolation::Alloc, "ArenaAllocator::init");
		constexpr size_t k_align = 32;
		// Over-allocate so we can align the usable base without a separate aligned API.
		size_t alloc_size = p_capacity + k_align;
		raw_base = (uint8_t *)memalloc(alloc_size);
		if (!raw_base) {
			return false;
		}
		uintptr_t addr = (uintptr_t)raw_base;
		uintptr_t aligned = (addr + (k_align - 1)) & ~(uintptr_t)(k_align - 1);
		base = (uint8_t *)aligned;
		capacity = p_capacity;
		offset = 0;
		memset(base, 0, p_capacity);
		return true;
	}

	// Bump-allocate with alignment. Returns nullptr if out of space.
	void *alloc(size_t p_size, size_t p_align = 32) {
		// Align the current offset
		size_t aligned_offset = (offset + (p_align - 1)) & ~(p_align - 1);
		if (aligned_offset + p_size > capacity) {
			return nullptr;
		}
		void *ptr = base + aligned_offset;
		offset = aligned_offset + p_size;
		return ptr;
	}

	// Allocate a typed array of floats (for audio buffers).
	float *alloc_audio_buffer(int32_t p_num_samples) {
		return (float *)alloc(sizeof(float) * p_num_samples, 32);
	}

	size_t get_used() const { return offset; }
	size_t get_remaining() const { return capacity - offset; }

	void free() {
		symphony_rt_note(SymphonyRTViolation::Free, "ArenaAllocator::free");
		if (raw_base) {
			memfree(raw_base);
		} else if (base) {
			memfree(base);
		}
		raw_base = nullptr;
		base = nullptr;
		capacity = 0;
		offset = 0;
		mark_offset = 0;
	}
};
