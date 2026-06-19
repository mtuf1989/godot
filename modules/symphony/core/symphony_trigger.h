#pragma once

#include <cstdint>

static constexpr int32_t SYMPHONY_MAX_TRIGGERS_PER_BLOCK = 8;

struct TriggerEvent {
	int32_t sample_offset; // Exact sample index within the micro-block (0-63)
	float value;           // Optional payload (e.g., velocity, note number)
};

struct TriggerBuffer {
	TriggerEvent events[SYMPHONY_MAX_TRIGGERS_PER_BLOCK];
	int32_t count = 0;

	void clear() { count = 0; }

	void push(int32_t p_sample_offset, float p_value = 1.0f) {
		if (count < SYMPHONY_MAX_TRIGGERS_PER_BLOCK) {
			events[count].sample_offset = p_sample_offset;
			events[count].value = p_value;
			count++;
		}
	}
};
