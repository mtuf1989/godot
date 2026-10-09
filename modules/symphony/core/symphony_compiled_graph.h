#pragma once

#include "symphony_arena_allocator.h"
#include "symphony_operator.h"
#include "symphony_trigger.h"
#include "symphony_pin_types.h"
#include "symphony_memory_budget.h"
#include "symphony_operator_registry.h"
#include "symphony_realtime_scope.h"
#include "core/string/string_name.h"

// The output of the GraphCompiler: a ready-to-execute graph.
// Owns the arena memory. Freed when this struct is destroyed.
struct CompiledGraph {
	// Operators in topological execution order (pointers into the arena).
	SymphonyOperator **operators = nullptr;
	int32_t operator_count = 0;

	// Parallel arrays: routing names, original node IDs, and operator type names.
	StringName *node_names = nullptr;
	int32_t *node_ids = nullptr;
	StringName *operator_types = nullptr; // Descriptor type_name per exec index (migrate match).

	// Trigger buffers (one per trigger-type output pin, stored in arena).
	TriggerBuffer **trigger_buffers = nullptr;
	int32_t trigger_buffer_count = 0;

	// Float→Audio promotion: pairs of (source float*, dest audio buffer*).
	// Layout: [0, promotion_head_count) are filled at the top of execute()
	// (graph-input producers). The rest are grouped by producer:
	// promotions[promotions_after_offsets[i] .. promotions_after_offsets[i+1])
	// are filled immediately after operator i runs.
	struct Promotion {
		const float *src;
		float *dst;
	};
	Promotion *promotions = nullptr;
	int32_t promotion_count = 0;
	int32_t promotion_head_count = 0;
	// Exec indices of graph-input producers. They run once, before the head
	// promotions, and are not run again in the operator loop.
	int32_t *head_publish_ops = nullptr;
	int32_t head_publish_count = 0;
	int32_t *promotions_after_offsets = nullptr; // Size: operator_count + 1. Heap.

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
		SymphonyRealtimeScope rt_scope;

		auto fill_promotions = [&](int32_t begin, int32_t end) {
			for (int32_t i = begin; i < end; i++) {
				const float val = promotions[i].src ? promotions[i].src[0] : 0.0f;
				SYMPHONY_ASSERT_FINITE_SCALAR(val);
				float *SYMPHONY_RESTRICT dst = promotions[i].dst;
				for (int32_t s = 0; s < p_num_frames; s++) {
					dst[s] = val;
				}
			}
		};

		auto was_published = [&](int32_t op_index) -> bool {
			for (int32_t h = 0; h < head_publish_count; h++) {
				if (head_publish_ops[h] == op_index) {
					return true;
				}
			}
			return false;
		};

		// Graph-input values are published here, then their Float→Audio promotions
		// are filled before any consumer runs. One execute per input: the main
		// loop skips these operators so a concurrent set_value is sampled once.
		for (int32_t h = 0; h < head_publish_count; h++) {
			const int32_t op_index = head_publish_ops[h];
			if (op_index < 0 || op_index >= operator_count || !operators[op_index]) {
				continue;
			}
			operators[op_index]->activity = 1;
			operators[op_index]->execute(p_num_frames);
		}
		if (promotion_head_count > 0) {
			fill_promotions(0, promotion_head_count);
		}

		// Clear all trigger buffers at the start of each micro-block
		for (int32_t i = 0; i < trigger_buffer_count; i++) {
			trigger_buffers[i]->clear();
		}
		// Execute operators in topological order with silence propagation.
		for (int32_t i = 0; i < operator_count; i++) {
			if (was_published(i)) {
				continue;
			}
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
							if (promotions_after_offsets) {
								fill_promotions(promotions_after_offsets[i], promotions_after_offsets[i + 1]);
							}
							continue;
						}
						// STATEFUL_TAIL with activity still set: fall through and execute.
					}
				}
			}

			// A skip above leaves activity at 0. An operator that runs is active
			// unless its execute() detects silence and clears the flag again;
			// stateless math operators never touch it, so without this reset a
			// single skip would mute them (and everything downstream) for good.
			op->activity = 1;
			op->execute(p_num_frames);

#ifdef DEV_ENABLED
			if (output_buffer_offsets) {
				int32_t buf_start = output_buffer_offsets[i];
				int32_t buf_end = output_buffer_offsets[i + 1];
				for (int32_t b = buf_start; b < buf_end; b++) {
					SYMPHONY_ASSERT_FINITE(output_audio_buffers[b], p_num_frames);
				}
			}
#endif
			if (promotions_after_offsets) {
				fill_promotions(promotions_after_offsets[i], promotions_after_offsets[i + 1]);
			}
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
		if (operator_types) {
			memdelete_arr(operator_types);
			operator_types = nullptr;
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
		if (promotions_after_offsets) {
			memdelete_arr(promotions_after_offsets);
			promotions_after_offsets = nullptr;
		}
		if (head_publish_ops) {
			memdelete_arr(head_publish_ops);
			head_publish_ops = nullptr;
		}
		head_publish_count = 0;
		promotion_head_count = 0;
		operators = nullptr;
		operator_count = 0;
		trigger_buffers = nullptr;
		trigger_buffer_count = 0;
	}

	~CompiledGraph() {
		destroy();
	}
};
