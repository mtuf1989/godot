#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_denormal_guard.h"
#include "core/math/math_funcs.h"

// Biquad filter (Robert Bristow-Johnson cookbook).
// Modes: 0=LP, 1=HP, 2=BP, 3=Notch.
// AUDIO cutoff is smoothed per sample (τ = 5 ms); coefficients recompute every
// 16 samples when the smoothed value moved more than 0.1%.
class SymphonyBiquadFilter : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT input = nullptr;
	const float *SYMPHONY_RESTRICT cutoff_input = nullptr;
	float *SYMPHONY_RESTRICT output = nullptr;

	int32_t mode = 0;
	float cutoff = 1000.0f;
	float resonance = 0.707f;
	float mix_rate = 44100.0f;

	float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

	float smoothed_cutoff = 1000.0f;
	float smoothed_q = 0.707f;
	float smooth_coeff = 0.0f; // a = exp(-1/(τ·sr)), τ = 5 ms
	float last_coeff_cutoff = -1.0f;
	float last_coeff_q = -1.0f;
	int32_t samples_since_recalc = 16;

	void calc_coefficients(float p_cutoff, float p_q) {
		float w0 = Math::TAU * CLAMP(p_cutoff, 20.0f, mix_rate * 0.49f) / mix_rate;
		float sin_w0 = Math::sin(w0);
		float cos_w0 = Math::cos(w0);
		float alpha = sin_w0 / (2.0f * MAX(p_q, 1e-4f));

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

		float a0_inv = 1.0f / a0;
		b0 *= a0_inv;
		b1 *= a0_inv;
		b2 *= a0_inv;
		a1 *= a0_inv;
		a2 *= a0_inv;

		last_coeff_cutoff = p_cutoff;
		last_coeff_q = p_q;
	}

public:
	SymphonyBiquadFilter(float p_mix_rate, int32_t p_mode, float p_cutoff, float p_resonance)
			: mode(p_mode), cutoff(p_cutoff), resonance(p_resonance), mix_rate(p_mix_rate),
			  smoothed_cutoff(p_cutoff), smoothed_q(p_resonance) {
		smooth_coeff = expf(-1.0f / (0.005f * MAX(mix_rate, 1.0f)));
		calc_coefficients(smoothed_cutoff, smoothed_q);
		samples_since_recalc = 0;
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		cutoff_input = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		// Locals for the hot loop (P9 state-in-locals convention).
		float lx1 = x1, lx2 = x2, ly1 = y1, ly2 = y2;
		float lb0 = b0, lb1 = b1, lb2 = b2, la1 = a1, la2 = a2;
		float s_fc = smoothed_cutoff;
		float s_q = smoothed_q;
		float a = smooth_coeff;
		float one_m_a = 1.0f - a;
		float target_q = resonance;
		int32_t since = samples_since_recalc;
		float last_fc = last_coeff_cutoff;
		float last_q = last_coeff_q;

		for (int32_t i = 0; i < p_num_frames; i++) {
			float target_fc = cutoff_input ? cutoff_input[i] : cutoff;
			s_fc += one_m_a * (target_fc - s_fc);
			s_q += one_m_a * (target_q - s_q);

			since++;
			if (since >= 16 || last_fc < 0.0f) {
				float rel_fc = fabsf(s_fc - last_fc) / MAX(fabsf(last_fc), 1.0f);
				float rel_q = fabsf(s_q - last_q) / MAX(fabsf(last_q), 1e-4f);
				if (rel_fc > 0.001f || rel_q > 0.001f || last_fc < 0.0f) {
					float w0 = Math::TAU * CLAMP(s_fc, 20.0f, mix_rate * 0.49f) / mix_rate;
					float sin_w0 = Math::sin(w0);
					float cos_w0 = Math::cos(w0);
					float alpha = sin_w0 / (2.0f * MAX(s_q, 1e-4f));
					float nb0 = 0, nb1 = 0, nb2 = 0;
					switch (mode) {
						case 0:
							nb0 = (1.0f - cos_w0) * 0.5f;
							nb1 = 1.0f - cos_w0;
							nb2 = nb0;
							break;
						case 1:
							nb0 = (1.0f + cos_w0) * 0.5f;
							nb1 = -(1.0f + cos_w0);
							nb2 = nb0;
							break;
						case 2:
							nb0 = alpha;
							nb1 = 0.0f;
							nb2 = -alpha;
							break;
						case 3:
							nb0 = 1.0f;
							nb1 = -2.0f * cos_w0;
							nb2 = 1.0f;
							break;
					}
					float a0 = 1.0f + alpha;
					float na1 = -2.0f * cos_w0;
					float na2 = 1.0f - alpha;
					float a0_inv = 1.0f / a0;
					lb0 = nb0 * a0_inv;
					lb1 = nb1 * a0_inv;
					lb2 = nb2 * a0_inv;
					la1 = na1 * a0_inv;
					la2 = na2 * a0_inv;
					last_fc = s_fc;
					last_q = s_q;
				}
				since = 0;
			}

			float x0 = input[i];
			float y0 = lb0 * x0 + lb1 * lx1 + lb2 * lx2 - la1 * ly1 - la2 * ly2;
			lx2 = lx1;
			lx1 = x0;
			ly2 = ly1;
			ly1 = y0;
			output[i] = y0;
		}

		x1 = lx1;
		x2 = lx2;
		y1 = ly1;
		y2 = ly2;
		b0 = lb0;
		b1 = lb1;
		b2 = lb2;
		a1 = la1;
		a2 = la2;
		smoothed_cutoff = s_fc;
		smoothed_q = s_q;
		samples_since_recalc = since;
		last_coeff_cutoff = last_fc;
		last_coeff_q = last_q;

		if (Math::abs(x1) + Math::abs(x2) + Math::abs(y1) + Math::abs(y2) < 1e-9f) {
			x1 = x2 = y1 = y2 = 0.0f;
		} else {
			symphony_flush_denormal(x1);
			symphony_flush_denormal(x2);
			symphony_flush_denormal(y1);
			symphony_flush_denormal(y2);
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = 6 * sizeof(float); // delays + smoothed fc/q
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size >= needed) {
			float state[6] = { x1, x2, y1, y2, smoothed_cutoff, smoothed_q };
			memcpy(p_buffer, state, needed);
		}
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		if (p_size >= 6 * sizeof(float)) {
			float state[6];
			memcpy(state, p_buffer, 6 * sizeof(float));
			x1 = state[0];
			x2 = state[1];
			y1 = state[2];
			y2 = state[3];
			smoothed_cutoff = state[4];
			smoothed_q = state[5];
			last_coeff_cutoff = -1.0f; // force recalc
			samples_since_recalc = 16;
		} else if (p_size >= 4 * sizeof(float)) {
			float state[4];
			memcpy(state, p_buffer, 4 * sizeof(float));
			x1 = state[0];
			x2 = state[1];
			y1 = state[2];
			y2 = state[3];
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "BiquadFilter";
		desc.category = "Filters";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "cutoff", SymphonyPinType::AUDIO, false });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "mode", 0.0f, 0.0f, 3.0f, 1.0f });
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
		if (p_params.has("mode")) {
			m = (int32_t)(float)p_params["mode"];
		}
		if (p_params.has("cutoff")) {
			c = p_params["cutoff"];
		}
		if (p_params.has("resonance")) {
			r = p_params["resonance"];
		}
		void *mem = p_arena.alloc(sizeof(SymphonyBiquadFilter), alignof(SymphonyBiquadFilter));
		return new (mem) SymphonyBiquadFilter(p_mix_rate, m, c, r);
	}
};
