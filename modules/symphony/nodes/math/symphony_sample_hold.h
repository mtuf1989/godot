#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"

// Sample & Hold: captures input value on trigger, holds until next trigger.
class SymphonySampleHold : public SymphonyOperator {
private:
	const float *input = nullptr;
	const TriggerBuffer *trigger_in = nullptr;
	float *output = nullptr;
	float held_value = 0.0f;

public:
	SymphonySampleHold() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		trigger_in = (const TriggerBuffer *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		if (trigger_in) {
			for (int32_t i = 0; i < trigger_in->count; i++) {
				// Sample the input at trigger time (use first sample of block for Float input)
				held_value = input ? input[0] : 0.0f;
			}
		}
		output[0] = held_value;
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "SampleHold";
		desc.category = "Math";
		desc.inputs.push_back({ "input", SymphonyPinType::FLOAT, true });
		desc.inputs.push_back({ "trigger", SymphonyPinType::TRIGGER, false });
		desc.outputs.push_back({ "output", SymphonyPinType::FLOAT, false });
		desc.state_size = sizeof(SymphonySampleHold);
		desc.state_align = alignof(SymphonySampleHold);
		desc.create_fn = &SymphonySampleHold::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonySampleHold), alignof(SymphonySampleHold));
		return new (mem) SymphonySampleHold();
	}
};
