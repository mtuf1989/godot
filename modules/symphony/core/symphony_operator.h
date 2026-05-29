#pragma once

#include "symphony_pin_types.h"
#include <cstdint>
#include <cstring>

// Base interface for all DSP operators in the Symphony graph.
// Execute() is called on the audio thread — must be RT-safe (zero allocations).
class SymphonyOperator {
public:
	virtual ~SymphonyOperator() = default;

	// Called by the compiler after construction to wire input/output pin pointers.
	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) = 0;

	// Called every micro-block on the audio thread.
	virtual void execute(int32_t p_num_frames) = 0;

	// State migration for hot-swap. Override in stateful operators.
	// Returns the number of bytes written to p_buffer. If p_buffer is nullptr, returns required size.
	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const { return 0; }
	virtual void import_state(const uint8_t *p_buffer, size_t p_size) {}
};
