#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Low-frequency oscillator. Outputs a Float pin (control-rate, one value per micro-block).
// Waveforms: 0=sine, 1=triangle, 2=saw, 3=square.
class SymphonyLFO : public SymphonyOperator {
private:
	float *output = nullptr;
	float phase = 0.0f;
	float rate = 1.0f; // Hz
	int32_t waveform = 0;
	float phase_inc = 0.0f; // per micro-block

public:
	SymphonyLFO(float p_rate, int32_t p_waveform, float p_mix_rate)
			: rate(p_rate), waveform(p_waveform) {
		// Phase increment per micro-block (rate / blocks_per_second)
		phase_inc = rate * SYMPHONY_MICRO_BLOCK_SIZE / p_mix_rate;
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		float val = 0.0f;
		switch (waveform) {
			case 0: // Sine
				val = Math::sin(phase * Math::TAU);
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
		output[0] = val;

		phase += phase_inc;
		if (phase >= 1.0f) {
			phase -= 1.0f;
		}
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
		desc.outputs.push_back({ "output", SymphonyPinType::FLOAT, false });
		desc.params.push_back({ "rate", 1.0f, 0.01f, 100.0f, 0.01f });
		desc.params.push_back({ "waveform", 0.0f, 0.0f, 3.0f, 1.0f }); // 0=sin,1=tri,2=saw,3=sq
		desc.state_size = sizeof(SymphonyLFO);
		desc.state_align = alignof(SymphonyLFO);
		desc.create_fn = &SymphonyLFO::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float r = 1.0f;
		int32_t w = 0;
		if (p_params.has("rate")) r = p_params["rate"];
		if (p_params.has("waveform")) w = (int32_t)(float)p_params["waveform"];
		void *mem = p_arena.alloc(sizeof(SymphonyLFO), alignof(SymphonyLFO));
		return new (mem) SymphonyLFO(r, w, p_mix_rate);
	}
};
