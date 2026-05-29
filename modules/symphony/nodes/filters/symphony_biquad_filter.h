#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Biquad filter (Robert Bristow-Johnson cookbook).
// Modes: 0=LP, 1=HP, 2=BP, 3=Notch.
// Cutoff input is Audio-rate (can be modulated), but coefficients recalculated per micro-block.
class SymphonyBiquadFilter : public SymphonyOperator {
private:
	const float *input = nullptr;
	const float *cutoff_input = nullptr; // Audio-rate cutoff (uses first sample of block)
	float *output = nullptr;

	int32_t mode = 0;
	float cutoff = 1000.0f;
	float resonance = 0.707f;
	float mix_rate = 44100.0f;

	// Filter state
	float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	// Coefficients
	float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

	void calc_coefficients(float p_cutoff, float p_q) {
		float w0 = Math::TAU * CLAMP(p_cutoff, 20.0f, mix_rate * 0.49f) / mix_rate;
		float sin_w0 = Math::sin(w0);
		float cos_w0 = Math::cos(w0);
		float alpha = sin_w0 / (2.0f * p_q);

		float a0_inv;
		switch (mode) {
			case 0: // LP
				b0 = (1.0f - cos_w0) * 0.5f;
				b1 = 1.0f - cos_w0;
				b2 = b0;
				break;
			case 1: // HP
				b0 = (1.0f + cos_w0) * 0.5f;
				b1 = -(1.0f + cos_w0);
				b2 = b0;
				break;
			case 2: // BP
				b0 = alpha;
				b1 = 0.0f;
				b2 = -alpha;
				break;
			case 3: // Notch
				b0 = 1.0f;
				b1 = -2.0f * cos_w0;
				b2 = 1.0f;
				break;
		}
		float a0 = 1.0f + alpha;
		a1 = -2.0f * cos_w0;
		a2 = 1.0f - alpha;

		a0_inv = 1.0f / a0;
		b0 *= a0_inv;
		b1 *= a0_inv;
		b2 *= a0_inv;
		a1 *= a0_inv;
		a2 *= a0_inv;
	}

public:
	SymphonyBiquadFilter(float p_mix_rate, int32_t p_mode, float p_cutoff, float p_resonance)
			: mode(p_mode), cutoff(p_cutoff), resonance(p_resonance), mix_rate(p_mix_rate) {
		calc_coefficients(cutoff, resonance);
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		cutoff_input = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		// Recalculate coefficients once per micro-block from cutoff input.
		float fc = cutoff_input ? cutoff_input[0] : cutoff;
		calc_coefficients(fc, resonance);

		for (int32_t i = 0; i < p_num_frames; i++) {
			float x0 = input[i];
			float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
			x2 = x1; x1 = x0;
			y2 = y1; y1 = y0;
			output[i] = y0;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = 4 * sizeof(float);
		if (!p_buffer) return needed;
		if (p_max_size >= needed) {
			float state[4] = { x1, x2, y1, y2 };
			memcpy(p_buffer, state, needed);
		}
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = 4 * sizeof(float);
		if (p_size >= needed) {
			float state[4];
			memcpy(state, p_buffer, needed);
			x1 = state[0]; x2 = state[1]; y1 = state[2]; y2 = state[3];
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "BiquadFilter";
		desc.category = "Filters";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "cutoff", SymphonyPinType::AUDIO, false }); // Optional modulation
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "mode", 0.0f, 0.0f, 3.0f, 1.0f }); // 0=LP,1=HP,2=BP,3=Notch
		desc.params.push_back({ "cutoff", 1000.0f, 20.0f, 20000.0f, 1.0f });
		desc.params.push_back({ "resonance", 0.707f, 0.1f, 20.0f, 0.01f });
		desc.state_size = sizeof(SymphonyBiquadFilter);
		desc.state_align = alignof(SymphonyBiquadFilter);
		desc.create_fn = &SymphonyBiquadFilter::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		int32_t m = 0;
		float c = 1000.0f, r = 0.707f;
		if (p_params.has("mode")) m = (int32_t)(float)p_params["mode"];
		if (p_params.has("cutoff")) c = p_params["cutoff"];
		if (p_params.has("resonance")) r = p_params["resonance"];
		void *mem = p_arena.alloc(sizeof(SymphonyBiquadFilter), alignof(SymphonyBiquadFilter));
		return new (mem) SymphonyBiquadFilter(p_mix_rate, m, c, r);
	}
};
