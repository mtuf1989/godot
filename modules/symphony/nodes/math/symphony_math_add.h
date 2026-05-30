#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// Adds two audio-rate signals sample-by-sample.
class SymphonyMathAdd : public SymphonyOperator {
private:
	const float *__restrict__ input_a = nullptr;
	const float *__restrict__ input_b = nullptr;
	float *__restrict__ output = nullptr;

public:
	SymphonyMathAdd() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input_a = (const float *)p_input_ptrs[0];
		input_b = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);
		for (int32_t i = 0; i < p_num_frames; i++) {
			output[i] = input_a[i] + input_b[i];
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "MathAdd";
		desc.category = "Math";
		desc.inputs.push_back({ "a", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "b", SymphonyPinType::AUDIO, true });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.state_size = sizeof(SymphonyMathAdd);
		desc.state_align = alignof(SymphonyMathAdd);
		desc.create_fn = &SymphonyMathAdd::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyMathAdd), alignof(SymphonyMathAdd));
		return new (mem) SymphonyMathAdd();
	}
};
