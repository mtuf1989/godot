#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// Ring modulation: multiplies two audio signals sample-by-sample.
// Creates sum and difference frequencies for metallic/inharmonic tones.
class SymphonyRingMod : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT input_a = nullptr;
	const float *SYMPHONY_RESTRICT input_b = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

public:
	SymphonyRingMod() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input_a = (const float *)p_input_ptrs[0];
		input_b = (const float *)p_input_ptrs[1];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);
		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			float a = input_a ? input_a[i] : 0.0f;
			float b = input_b ? input_b[i] : 0.0f;
			audio_out[i] = a * b;
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "RingMod";
		desc.category = "Math";
		desc.inputs.push_back({ "audio_a", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "audio_b", SymphonyPinType::AUDIO, true });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.state_size = sizeof(SymphonyRingMod);
		desc.state_align = alignof(SymphonyRingMod);
		desc.create_fn = &SymphonyRingMod::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyRingMod), alignof(SymphonyRingMod));
		return new (mem) SymphonyRingMod();
	}
};
