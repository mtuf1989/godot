#pragma once

#include "symphony_pin_types.h"

#include <atomic>
#include <cstdint>

// Capacity matches the configured micro-block (32 web / 64 native) — plan §9.
static constexpr int32_t SYMPHONY_MAX_TRIGGERS_PER_BLOCK = SYMPHONY_MICRO_BLOCK_SIZE;

struct TriggerEvent {
	int32_t sample_offset; // Exact sample index within the micro-block
	float value; // Optional payload (e.g., velocity, note number)
};

struct TriggerBuffer {
	TriggerEvent events[SYMPHONY_MAX_TRIGGERS_PER_BLOCK];
	int32_t count = 0;

	void clear() { count = 0; }

	// Returns false when full (never silently overwrites).
	bool push(int32_t p_sample_offset, float p_value = 1.0f) {
		if (count >= SYMPHONY_MAX_TRIGGERS_PER_BLOCK) {
			return false;
		}
		events[count].sample_offset = p_sample_offset;
		events[count].value = p_value;
		count++;
		return true;
	}
};

// Process-wide dropped-trigger counter (game-thread fire / buffer push failures).
inline std::atomic<uint64_t> &symphony_dropped_trigger_count() {
	static std::atomic<uint64_t> counter{ 0 };
	return counter;
}

inline void symphony_note_dropped_trigger() {
	symphony_dropped_trigger_count().fetch_add(1, std::memory_order_relaxed);
}
