#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1], R ≈ 0.995
class SymphonyDCBlocker : public SymphonyOperator {
private:
	const float *input = nullptr;
	float *output = nullptr;
	float x_prev = 0.0f;
	float y_prev = 0.0f;
	static constexpr float R = 0.995f;

public:
	SymphonyDCBlocker() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		for (int32_t i = 0; i < p_num_frames; i++) {
			float x = input[i];
			float y = x - x_prev + R * y_prev;
			x_prev = x;
			y_prev = y;
			output[i] = y;
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "DCBlocker";
		desc.category = "Filters";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.state_size = sizeof(SymphonyDCBlocker);
		desc.state_align = alignof(SymphonyDCBlocker);
		desc.create_fn = &SymphonyDCBlocker::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyDCBlocker), alignof(SymphonyDCBlocker));
		return new (mem) SymphonyDCBlocker();
	}
};
