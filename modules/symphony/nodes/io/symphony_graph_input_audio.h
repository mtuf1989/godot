#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// Audio-rate graph input: exposes an audio buffer pin on a sub-graph.
// When inlined by the flattener, the parent's source buffer is aliased directly
// to this node's output — zero copy. When standalone (no parent connection),
// the output buffer stays zeroed (silence).
// execute() is a no-op: the buffer is written externally via aliasing.
class SymphonyGraphInputAudio : public SymphonyOperator {
private:
	float *output = nullptr;

public:
	SymphonyGraphInputAudio() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		// No-op: buffer is written by parent via flattener aliasing.
		// If unconnected, buffer remains zeroed from arena allocation.
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "GraphInputAudio";
		desc.category = "I/O";
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "parameter_name", 0.0f, 0.0f, 0.0f, 0.0f });
		desc.params.push_back({ "sort_order", 0.0f, -1000.0f, 1000.0f, 1.0f });
		desc.params.push_back({ "display_name", 0.0f, 0.0f, 0.0f, 0.0f });
		desc.state_size = sizeof(SymphonyGraphInputAudio);
		desc.state_align = alignof(SymphonyGraphInputAudio);
		desc.create_fn = &SymphonyGraphInputAudio::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyGraphInputAudio), alignof(SymphonyGraphInputAudio));
		return new (mem) SymphonyGraphInputAudio();
	}
};
