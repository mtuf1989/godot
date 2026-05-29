#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// Outputs a constant value to an audio-rate buffer (fills every sample with the same value).
class SymphonyConstant : public SymphonyOperator {
private:
	float *output = nullptr;
	float value = 0.0f;

public:
	SymphonyConstant(float p_value) : value(p_value) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		for (int32_t i = 0; i < p_num_frames; i++) {
			output[i] = value;
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Constant";
		// No inputs
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.state_size = sizeof(SymphonyConstant);
		desc.state_align = alignof(SymphonyConstant);
		desc.create_fn = &SymphonyConstant::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float v = 0.0f;
		if (p_params.has("value")) {
			v = p_params["value"];
		}
		void *mem = p_arena.alloc(sizeof(SymphonyConstant), alignof(SymphonyConstant));
		return new (mem) SymphonyConstant(v);
	}
};
