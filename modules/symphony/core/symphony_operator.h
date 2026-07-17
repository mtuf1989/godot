#pragma once

#include "symphony_pin_types.h"
#include <cstdint>
#include <cstring>

// Base interface for all DSP operators in the Symphony graph.
// Execute() is called on the audio thread — must be RT-safe (zero allocations).
class SymphonyOperator {
public:
	// Activity flag for silence propagation (set by execute(), read by CompiledGraph).
	// 0 = output is silent/zero this block (downstream may skip).
	// 1 = output is active (default, safe conservative value).
	// Operators that can produce silence (ADSR in IDLE, Gain at 0, gated signals)
	// should set this to 0 when their output is all zeros. If unsure, leave at 1.
	uint8_t activity = 1;

	// If true, this operator can be skipped when all its audio inputs are inactive.
	// Set by the compiler based on operator category. Generators (oscillators, noise,
	// wave players) and IO nodes always execute regardless of input activity.
	// Default: true (most operators are passthrough/processors).
	uint8_t skippable = 1;

	virtual ~SymphonyOperator() = default;

	// Called by the compiler after construction to wire input/output pin pointers.
	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) = 0;

	// Called every micro-block on the audio thread.
	virtual void execute(int32_t p_num_frames) = 0;

	// Called before arena is freed to release non-arena resources (e.g., heap-allocated
	// objects that couldn't be arena-allocated). Default is no-op.
	// This is NOT called on the audio thread — it runs during graph destruction on main thread.
	virtual void cleanup() {}

	// State migration for hot-swap. Override in stateful operators.
	// Returns the number of bytes written to p_buffer. If p_buffer is nullptr, returns required size.
	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const { return 0; }
	virtual void import_state(const uint8_t *p_buffer, size_t p_size) {}
};
