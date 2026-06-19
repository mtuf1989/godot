#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Single-allocation bump-pointer arena for operator states and audio buffers.
// Allocated once during graph compilation (main thread). Never grows.
// All memory freed in one shot when the compiled graph is destroyed.
struct ArenaAllocator {
	uint8_t *base = nullptr;
	size_t capacity = 0;
	size_t offset = 0;

	// Allocate the arena with a given total size.
	bool init(size_t p_capacity) {
		base = (uint8_t *)memalloc(p_capacity);
		if (!base) {
			return false;
		}
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
		if (base) {
			memfree(base);
			base = nullptr;
			capacity = 0;
			offset = 0;
		}
	}
};
