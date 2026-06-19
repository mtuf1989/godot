#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// Mix/Crossfade: blends two audio inputs. mix=0 → A, mix=1 → B.
class SymphonyMix : public SymphonyOperator {
private:
	const float *input_a = nullptr;
	const float *input_b = nullptr;
	const float *mix_input = nullptr; // Float input (control-rate)
	float *output = nullptr;
	float mix = 0.5f;

public:
	SymphonyMix(float p_mix) : mix(p_mix) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input_a = (const float *)p_input_ptrs[0];
		input_b = (const float *)p_input_ptrs[1];
		mix_input = (const float *)p_input_ptrs[2];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		float m = mix_input ? mix_input[0] : mix;
		m = CLAMP(m, 0.0f, 1.0f);
		float inv_m = 1.0f - m;
		for (int32_t i = 0; i < p_num_frames; i++) {
			float a = input_a ? input_a[i] : 0.0f;
			float b = input_b ? input_b[i] : 0.0f;
			output[i] = a * inv_m + b * m;
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Mix";
		desc.category = "Math";
		desc.inputs.push_back({ "a", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "b", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "mix", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "mix", 0.5f, 0.0f, 1.0f, 0.01f });
		desc.state_size = sizeof(SymphonyMix);
		desc.state_align = alignof(SymphonyMix);
		desc.create_fn = &SymphonyMix::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float m = 0.5f;
		if (p_params.has("mix")) m = p_params["mix"];
		void *mem = p_arena.alloc(sizeof(SymphonyMix), alignof(SymphonyMix));
		return new (mem) SymphonyMix(m);
	}
};
