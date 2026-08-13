#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_fast_math.h"
#include "core/math/math_funcs.h"

// Low-frequency oscillator. Outputs a Float pin (control-rate, one value per micro-block).
// Waveforms: 0=sine, 1=triangle, 2=saw, 3=square.
// Input pins allow runtime modulation of rate and amplitude from GraphInput or other nodes.
class SymphonyLFO : public SymphonyOperator {
private:
	const float *rate_input = nullptr;
	const float *amplitude_input = nullptr;
	float *output = nullptr;
	float phase = 0.0f;
	float default_rate = 1.0f; // Hz
	float default_amplitude = 1.0f;
	int32_t waveform = 0;
	float mix_rate = 48000.0f;

	// Fast polynomial sine approximation via shared helper.
	static inline float fast_sine(float p_phase) {
		return SymphonyFastMath::fast_sine(p_phase);
	}

public:
	SymphonyLFO(float p_rate, float p_amplitude, int32_t p_waveform, float p_mix_rate)
			: default_rate(p_rate), default_amplitude(p_amplitude), waveform(p_waveform), mix_rate(p_mix_rate) {
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		rate_input = (const float *)p_input_ptrs[0];
		amplitude_input = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		float rate = rate_input ? *rate_input : default_rate;
		float amplitude = amplitude_input ? *amplitude_input : default_amplitude;

		// Recompute phase increment from current rate each micro-block.
		float phase_inc = rate * (float)SYMPHONY_MICRO_BLOCK_SIZE / mix_rate;

		float val = 0.0f;
		switch (waveform) {
			case 0: // Sine (polynomial approximation, ~6 ops)
				val = fast_sine(phase);
				break;
			case 1: // Triangle
				val = 4.0f * fabsf(phase - 0.5f) - 1.0f;
				break;
			case 2: // Saw
				val = 2.0f * phase - 1.0f;
				break;
			case 3: // Square
				val = phase < 0.5f ? 1.0f : -1.0f;
				break;
		}
		output[0] = val * amplitude;

		phase += phase_inc;
		phase -= floorf(phase); // Branch-free wrap to [0, 1)
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		if (!p_buffer) return sizeof(float);
		if (p_max_size >= sizeof(float)) memcpy(p_buffer, &phase, sizeof(float));
		return sizeof(float);
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		if (p_size >= sizeof(float)) memcpy(&phase, p_buffer, sizeof(float));
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "LFO";
		desc.category = "Generators";
		desc.inputs.push_back({ "rate", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "amplitude", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "output", SymphonyPinType::FLOAT, false });
		desc.params.push_back({ "rate", 1.0f, 0.01f, 100.0f, 0.01f });
		desc.params.push_back({ "amplitude", 1.0f, 0.0f, 10.0f, 0.01f });
		desc.params.push_back({ "waveform", 0.0f, 0.0f, 3.0f, 1.0f }); // 0=sin,1=tri,2=saw,3=sq
		desc.state_size = sizeof(SymphonyLFO);
		desc.state_align = alignof(SymphonyLFO);
		desc.create_fn = &SymphonyLFO::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float r = 1.0f;
		float a = 1.0f;
		int32_t w = 0;
		if (p_params.has("rate")) r = p_params["rate"];
		if (p_params.has("amplitude")) a = p_params["amplitude"];
		if (p_params.has("waveform")) w = (int32_t)(float)p_params["waveform"];
		void *mem = p_arena.alloc(sizeof(SymphonyLFO), alignof(SymphonyLFO));
		return new (mem) SymphonyLFO(r, a, w, p_mix_rate);
	}
};
