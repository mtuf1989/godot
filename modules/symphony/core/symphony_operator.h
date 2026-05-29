#pragma once

#include "symphony_pin_types.h"
#include <cstdint>

// Base interface for all DSP operators in the Symphony graph.
// Execute() is called on the audio thread — must be RT-safe (zero allocations).
class SymphonyOperator {
public:
	virtual ~SymphonyOperator() = default;

	// Called every micro-block on the audio thread.
	virtual void execute(int32_t p_num_frames) = 0;
};
