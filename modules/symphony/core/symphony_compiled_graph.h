#pragma once

#include "symphony_arena_allocator.h"
#include "symphony_operator.h"
#include "symphony_trigger.h"
#include "symphony_pin_types.h"
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

	// The single contiguous memory block for all operator states + buffers.
	ArenaAllocator arena;

	// Execute all operators for one micro-block.
	void execute(int32_t p_num_frames) {
		// Fill promotion buffers (Float→Audio).
		for (int32_t i = 0; i < promotion_count; i++) {
			float val = promotions[i].src[0];
			for (int32_t s = 0; s < p_num_frames; s++) {
				promotions[i].dst[s] = val;
			}
		}
		// Clear all trigger buffers at the start of each micro-block
		for (int32_t i = 0; i < trigger_buffer_count; i++) {
			trigger_buffers[i]->clear();
		}
		// Execute operators in topological order
		for (int32_t i = 0; i < operator_count; i++) {
			operators[i]->execute(p_num_frames);
		}
	}

	void destroy() {
		// Operators were placement-new'd in the arena; call destructors manually.
		for (int32_t i = 0; i < operator_count; i++) {
			operators[i]->~SymphonyOperator();
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
		operators = nullptr;
		operator_count = 0;
		trigger_buffers = nullptr;
		trigger_buffer_count = 0;
	}

	~CompiledGraph() {
		destroy();
	}
};
