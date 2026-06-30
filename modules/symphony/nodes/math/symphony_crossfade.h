#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Equal-power crossfade: out = a * cos(mix * π/2) + b * sin(mix * π/2)
// mix=0 → pure A, mix=1 → pure B, mix=0.5 → no volume dip.
class SymphonyCrossFade : public SymphonyOperator {
private:
	const float *__restrict__ input_a = nullptr;
	const float *__restrict__ input_b = nullptr;
	const float *__restrict__ mix_input = nullptr;
	float *__restrict__ audio_out = nullptr;
	float default_mix = 0.5f;

public:
	SymphonyCrossFade(float p_mix) : default_mix(p_mix) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input_a = (const float *)p_input_ptrs[0];
		input_b = (const float *)p_input_ptrs[1];
		mix_input = (const float *)p_input_ptrs[2];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);
		float m = mix_input ? mix_input[0] : default_mix;
		m = CLAMP(m, 0.0f, 1.0f);
		float gain_a = Math::cos(m * (float)Math::PI * 0.5f);
		float gain_b = Math::sin(m * (float)Math::PI * 0.5f);
		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			float a = input_a ? input_a[i] : 0.0f;
			float b = input_b ? input_b[i] : 0.0f;
			audio_out[i] = a * gain_a + b * gain_b;
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "CrossFade";
		desc.category = "Math";
		desc.inputs.push_back({ "audio_a", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "audio_b", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "mix", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "mix", 0.5f, 0.0f, 1.0f, 0.01f });
		desc.state_size = sizeof(SymphonyCrossFade);
		desc.state_align = alignof(SymphonyCrossFade);
		desc.create_fn = &SymphonyCrossFade::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float m = 0.5f;
		if (p_params.has("mix")) {
			m = p_params["mix"];
		}
		void *mem = p_arena.alloc(sizeof(SymphonyCrossFade), alignof(SymphonyCrossFade));
		return new (mem) SymphonyCrossFade(m);
	}
};
