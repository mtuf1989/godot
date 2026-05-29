#pragma once

#include "symphony_pin_types.h"
#include <cstdint>

// Base interface for all DSP operators in the Symphony graph.
// Execute() is called on the audio thread — must be RT-safe (zero allocations).
class SymphonyOperator {
public:
	virtual ~SymphonyOperator() = default;

	// Called by the compiler after construction to wire input/output pin pointers.
	// p_input_ptrs: array of void* pointers to upstream output buffers (one per input pin).
	// p_output_ptrs: array of void* pointers to this operator's output buffers (allocated by compiler).
	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) = 0;

	// Called every micro-block on the audio thread.
	virtual void execute(int32_t p_num_frames) = 0;
};
