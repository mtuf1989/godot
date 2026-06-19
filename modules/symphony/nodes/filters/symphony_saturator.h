#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Saturator: soft clip (tanh) or hard clip. Drive parameter controls input gain before clipping.
class SymphonySaturator : public SymphonyOperator {
private:
	const float *input = nullptr;
	float *output = nullptr;
	float drive = 1.0f;
	int32_t mode = 0; // 0=soft(tanh), 1=hard

public:
	SymphonySaturator(float p_drive, int32_t p_mode) : drive(p_drive), mode(p_mode) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		if (mode == 0) {
			for (int32_t i = 0; i < p_num_frames; i++) {
				output[i] = tanhf(input[i] * drive);
			}
		} else {
			for (int32_t i = 0; i < p_num_frames; i++) {
				output[i] = CLAMP(input[i] * drive, -1.0f, 1.0f);
			}
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Saturator";
		desc.category = "Filters";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "drive", 2.0f, 0.1f, 20.0f, 0.1f });
		desc.params.push_back({ "mode", 0.0f, 0.0f, 1.0f, 1.0f }); // 0=soft, 1=hard
		desc.state_size = sizeof(SymphonySaturator);
		desc.state_align = alignof(SymphonySaturator);
		desc.create_fn = &SymphonySaturator::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float d = 2.0f;
		int32_t m = 0;
		if (p_params.has("drive")) d = p_params["drive"];
		if (p_params.has("mode")) m = (int32_t)(float)p_params["mode"];
		void *mem = p_arena.alloc(sizeof(SymphonySaturator), alignof(SymphonySaturator));
		return new (mem) SymphonySaturator(d, m);
	}
};
