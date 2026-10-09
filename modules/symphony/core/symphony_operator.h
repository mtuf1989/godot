#pragma once

#include "symphony_pin_types.h"
#include "symphony_seed.h"
#include "core/string/ustring.h"
#include <cstdint>
#include <cstring>

// Base interface for all DSP operators in the Symphony graph.
// Execute() is called on the audio thread — must be RT-safe (zero allocations).
class SymphonyOperator {
public:
	// Activity flag for silence propagation (set by execute(), read by CompiledGraph).
	// 0 = output is silent/zero this block (downstream may skip).
	// 1 = output is active (default, safe conservative value).
	uint8_t activity = 1;

	// If true, this operator can be skipped when silence rules allow.
	// Set by the compiler from SilenceBehavior. ALWAYS_PROCESS forces 0.
	uint8_t skippable = 1;

	// Copied from OperatorDescriptor at compile time for audio-thread decisions.
	uint8_t silence_behavior = 1; // SilenceBehavior::STATEFUL_TAIL

	// Consecutive inactive blocks for stateful-tail hysteresis (improve_plan §7).
	uint8_t inactive_blocks = 0;

	// P10: pristine export_state snapshot (arena) for note-voice reset.
	const uint8_t *pristine_state = nullptr;
	uint32_t pristine_size = 0;
	int32_t graph_node_id = -1;
	uint32_t seed_authored = 0;
	bool seed_deterministic = false;

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

	// P10 note-voice reset: restore pristine state, clear delay/capture rings, reseed RNG.
	virtual void clear_large_buffers() {}
	virtual void reseed(uint32_t p_seed) { (void)p_seed; }

	void reset_for_note(uint64_t p_voice_seed) {
		if (pristine_state != nullptr && pristine_size > 0) {
			import_state(pristine_state, pristine_size);
		}
		clear_large_buffers();
		const StringName node_name(String::num_int64(graph_node_id));
		reseed(symphony_node_seed(p_voice_seed, node_name, seed_authored, seed_deterministic));
		activity = 1;
		inactive_blocks = 0;
	}
};
