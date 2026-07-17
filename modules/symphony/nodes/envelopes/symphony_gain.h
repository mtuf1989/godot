#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// Multiplies input audio by a gain value. Output: audio.
// Gain can be modulated at runtime via the "gain" input pin (FLOAT).
class SymphonyGain : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT input = nullptr;
	const float *SYMPHONY_RESTRICT gain_input = nullptr;
	float *SYMPHONY_RESTRICT output = nullptr;
	float default_gain = 0.5f;

public:
	SymphonyGain(float p_gain) : default_gain(p_gain) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		gain_input = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);
		float g = gain_input ? *gain_input : default_gain;
		// Report silence when gain is negligible (< -80dB equivalent).
		if (g < 0.0001f && g > -0.0001f) {
			memset(output, 0, sizeof(float) * p_num_frames);
			activity = 0;
			return;
		}
		activity = 1;
		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			output[i] = input[i] * g;
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Gain";
		desc.category = "Envelopes";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "gain", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "gain", 0.5f, 0.0f, 10.0f, 0.01f });
		desc.state_size = sizeof(SymphonyGain);
		desc.state_align = alignof(SymphonyGain);
		desc.create_fn = &SymphonyGain::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float g = 0.5f;
		if (p_params.has("gain")) {
			g = p_params["gain"];
		}
		void *mem = p_arena.alloc(sizeof(SymphonyGain), alignof(SymphonyGain));
		return new (mem) SymphonyGain(g);
	}
};
