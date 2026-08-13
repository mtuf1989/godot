#pragma once

#include "symphony_arena_allocator.h"
#include "symphony_operator.h"
#include "symphony_trigger.h"
#include "symphony_pin_types.h"
#include "symphony_memory_budget.h"
#include "symphony_operator_registry.h"
#include "core/string/string_name.h"

// The output of the GraphCompiler: a ready-to-execute graph.
// Owns the arena memory. Freed when this struct is destroyed.
struct CompiledGraph {
	// Operators in topological execution order (pointers into the arena).
	SymphonyOperator **operators = nullptr;
	int32_t operator_count = 0;

	// Parallel arrays: routing names and original node IDs for each operator.
	StringName *node_names = nullptr;
	int32_t *node_ids = nullptr;

	// Trigger buffers (one per trigger-type output pin, stored in arena).
	TriggerBuffer **trigger_buffers = nullptr;
	int32_t trigger_buffer_count = 0;

	// Float→Audio promotion: pairs of (source float*, dest audio buffer*).
	struct Promotion {
		const float *src;
		float *dst;
	};
	Promotion *promotions = nullptr;
	int32_t promotion_count = 0;

	// --- Silence propagation support ---
	// For each operator [i], audio_input_ops[audio_input_offsets[i]..audio_input_offsets[i+1])
	// lists the indices of operators that feed AUDIO pins into operator i.
	// This allows checking if all audio-producing predecessors are inactive (silent).
	int32_t *audio_input_ops = nullptr;      // Flat array of operator indices
	int32_t *audio_input_offsets = nullptr;   // Size: operator_count + 1
	int32_t audio_input_ops_count = 0;        // Total entries in audio_input_ops

	// Per-operator output buffer pointers for silence zeroing when skipped.
	// output_buffers[output_buffer_offsets[i]..output_buffer_offsets[i+1])
	// lists all AUDIO output buffers for operator i.
	float **output_audio_buffers = nullptr;   // Flat array of buffer pointers
	int32_t *output_buffer_offsets = nullptr;  // Size: operator_count + 1
	int32_t output_audio_buffers_count = 0;

	// The single contiguous memory block for all operator states + buffers.
	ArenaAllocator arena;

	// Bytes reserved in SymphonyMemoryBudget for this package (released on destroy).
	size_t budgeted_bytes = 0;
	// Conservative relative CPU estimate (copied onto PreparedGraphPackage).
	float estimated_cost_units = 0.0f;

	// Execute all operators for one micro-block with silence propagation.
	void execute(int32_t p_num_frames) {
		// Fill promotion buffers (Float→Audio).
		for (int32_t i = 0; i < promotion_count; i++) {
			float val = promotions[i].src[0];
			SYMPHONY_ASSERT_FINITE_SCALAR(val);
			float *SYMPHONY_RESTRICT dst = promotions[i].dst;
			for (int32_t s = 0; s < p_num_frames; s++) {
				dst[s] = val;
			}
		}
		// Clear all trigger buffers at the start of each micro-block
		for (int32_t i = 0; i < trigger_buffer_count; i++) {
			trigger_buffers[i]->clear();
		}
		// Execute operators in topological order with silence propagation.
		for (int32_t i = 0; i < operator_count; i++) {
			SymphonyOperator *op = operators[i];

			if (op->skippable && audio_input_offsets) {
				int32_t dep_start = audio_input_offsets[i];
				int32_t dep_end = audio_input_offsets[i + 1];
				if (dep_start < dep_end) {
					bool all_silent = true;
					for (int32_t d = dep_start; d < dep_end; d++) {
						if (operators[audio_input_ops[d]]->activity) {
							all_silent = false;
							break;
						}
					}
					if (all_silent) {
						const bool is_stateless = op->silence_behavior == (uint8_t)SilenceBehavior::STATELESS;
						const bool is_tail = op->silence_behavior == (uint8_t)SilenceBehavior::STATEFUL_TAIL;
						// STATELESS: skip immediately. STATEFUL_TAIL: skip only after inactive.
						if (is_stateless || (is_tail && op->activity == 0)) {
							int32_t buf_start = output_buffer_offsets[i];
							int32_t buf_end = output_buffer_offsets[i + 1];
							for (int32_t b = buf_start; b < buf_end; b++) {
								memset(output_audio_buffers[b], 0, sizeof(float) * p_num_frames);
							}
							op->activity = 0;
							continue;
						}
						// STATEFUL_TAIL with activity still set: fall through and execute.
					}
				}
			}

			op->execute(p_num_frames);
			// Note: operators that detect silence set activity = 0 in execute().

#ifdef DEV_ENABLED
			if (output_buffer_offsets) {
				int32_t buf_start = output_buffer_offsets[i];
				int32_t buf_end = output_buffer_offsets[i + 1];
				for (int32_t b = buf_start; b < buf_end; b++) {
					SYMPHONY_ASSERT_FINITE(output_audio_buffers[b], p_num_frames);
				}
			}
#endif
		}
	}

	void destroy() {
		if (budgeted_bytes > 0 && SymphonyMemoryBudget::get_singleton()) {
			SymphonyMemoryBudget::get_singleton()->release(budgeted_bytes);
			budgeted_bytes = 0;
		}
		// Call cleanup() first to release non-arena heap resources (e.g., PFFFT_Setup).
		for (int32_t i = 0; i < operator_count; i++) {
			if (operators[i]) {
				operators[i]->cleanup();
			}
		}
		// Operators were placement-new'd in the arena; call destructors manually.
		for (int32_t i = 0; i < operator_count; i++) {
			if (operators[i]) {
				operators[i]->~SymphonyOperator();
			}
		}
		arena.free();
		if (node_names) {
			memdelete_arr(node_names);
			node_names = nullptr;
		}
		if (node_ids) {
			memdelete_arr(node_ids);
			node_ids = nullptr;
		}
		// Silence propagation arrays are heap-allocated (not arena).
		if (audio_input_ops) {
			memdelete_arr(audio_input_ops);
			audio_input_ops = nullptr;
		}
		if (audio_input_offsets) {
			memdelete_arr(audio_input_offsets);
			audio_input_offsets = nullptr;
		}
		if (output_audio_buffers) {
			memdelete_arr(output_audio_buffers);
			output_audio_buffers = nullptr;
		}
		if (output_buffer_offsets) {
			memdelete_arr(output_buffer_offsets);
			output_buffer_offsets = nullptr;
		}
		operators = nullptr;
		operator_count = 0;
		trigger_buffers = nullptr;
		trigger_buffer_count = 0;
	}

	~CompiledGraph() {
		destroy();
	}
};
