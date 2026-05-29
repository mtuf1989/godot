#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Sine wave oscillator. Input: frequency (audio-rate). Output: audio.
class SymphonyOscillator : public SymphonyOperator {
private:
	const float *freq_input = nullptr; // Audio-rate frequency input
	float *output = nullptr;
	float phase = 0.0f;
	float mix_rate = 44100.0f;
	float default_freq = 440.0f;

public:
	SymphonyOscillator(float p_mix_rate, float p_default_freq)
			: mix_rate(p_mix_rate), default_freq(p_default_freq) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		freq_input = (const float *)p_input_ptrs[0]; // May be nullptr if unconnected
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		for (int32_t i = 0; i < p_num_frames; i++) {
			output[i] = Math::sin(phase * Math::TAU);
			float freq = freq_input ? freq_input[i] : default_freq;
			phase += freq / mix_rate;
			if (phase >= 1.0f) {
				phase -= 1.0f;
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		if (!p_buffer) {
			return sizeof(float);
		}
		if (p_max_size >= sizeof(float)) {
			memcpy(p_buffer, &phase, sizeof(float));
		}
		return sizeof(float);
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		if (p_size >= sizeof(float)) {
			memcpy(&phase, p_buffer, sizeof(float));
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Oscillator";
		desc.inputs.push_back({ "frequency", SymphonyPinType::AUDIO, false }); // Optional: uses default_freq if unconnected
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "frequency", 440.0f, 0.0f, 20000.0f, 1.0f });
		desc.state_size = sizeof(SymphonyOscillator);
		desc.state_align = alignof(SymphonyOscillator);
		desc.create_fn = &SymphonyOscillator::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float freq = 440.0f;
		if (p_params.has("frequency")) {
			freq = p_params["frequency"];
		}
		void *mem = p_arena.alloc(sizeof(SymphonyOscillator), alignof(SymphonyOscillator));
		return new (mem) SymphonyOscillator(p_mix_rate, freq);
	}
};
