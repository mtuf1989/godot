#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Saturator with 1st-order Anti-Derivative Anti-Aliasing (ADAA).
// Modes: 0=soft(tanh), 1=hard clip.
// Drive parameter controls input gain before clipping.
// Drive can be modulated at runtime via the "drive" input pin (FLOAT).
//
// ADAA runs in the driven domain u = x·drive:
//   y[n] = (F(u[n]) - F(u[n-1])) / (u[n] - u[n-1])
// with midpoint fallback when |Δu| is below epsilon. This keeps drive
// modulation free of ADAA spikes.
//
// Optional 2x oversampling (compile-time parameter "oversample"):
// When enabled, input is upsampled 2x via an HIIR polyphase half-band pair,
// processed, then downsampled. Each polyphase path is a two-section allpass
// cascade (4th-order elliptic half-band).
class SymphonySaturator : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT input = nullptr;
	const float *SYMPHONY_RESTRICT drive_input = nullptr;
	float *SYMPHONY_RESTRICT output = nullptr;
	float default_drive = 2.0f;
	int32_t mode = 0; // 0=soft(tanh), 1=hard
	bool oversample = false;

	// ADAA state in the u = x·drive domain (double keeps the quotient accurate).
	double prev_u = 0.0;
	double prev_F = 0.0;

	// HIIR polyphase half-band: path0 coeffs {a0,a1}, path1 coeffs {b0,b1}.
	static constexpr float HB_P0_C0 = 0.0798664262f;
	static constexpr float HB_P0_C1 = 0.5453236511f;
	static constexpr float HB_P1_C0 = 0.2838293449f;
	static constexpr float HB_P1_C1 = 0.8344118915f;

	// Ill-conditioned ADAA step threshold (u-domain).
	static constexpr float ADAA_EPS = 1e-3f;

	// Interpolation filter state (path0 cascade + path1 cascade).
	float interp_p0_x0 = 0.0f, interp_p0_y0 = 0.0f, interp_p0_x1 = 0.0f, interp_p0_y1 = 0.0f;
	float interp_p1_x0 = 0.0f, interp_p1_y0 = 0.0f, interp_p1_x1 = 0.0f, interp_p1_y1 = 0.0f;

	// Decimation filter state.
	float decim_p0_x0 = 0.0f, decim_p0_y0 = 0.0f, decim_p0_x1 = 0.0f, decim_p0_y1 = 0.0f;
	float decim_p1_x0 = 0.0f, decim_p1_y0 = 0.0f, decim_p1_x1 = 0.0f, decim_p1_y1 = 0.0f;

	// Internal buffer for oversampled processing (2x input size)
	float *os_buffer = nullptr;

	[[nodiscard]] static inline float allpass_tick(float p_input, float p_coeff, float &r_x_state, float &r_y_state) {
		float out = p_coeff * (p_input - r_y_state) + r_x_state;
		r_x_state = p_input;
		r_y_state = out;
		return out;
	}

	[[nodiscard]] static inline float cascade_tick(float p_input, float p_c0, float p_c1,
			float &r_x0, float &r_y0, float &r_x1, float &r_y1) {
		float mid = allpass_tick(p_input, p_c0, r_x0, r_y0);
		return allpass_tick(mid, p_c1, r_x1, r_y1);
	}

	// Upsample one input sample → two output samples via polyphase half-band.
	inline void upsample_one(float p_in, float &r_out0, float &r_out1) {
		r_out0 = cascade_tick(p_in, HB_P0_C0, HB_P0_C1, interp_p0_x0, interp_p0_y0, interp_p0_x1, interp_p0_y1);
		r_out1 = cascade_tick(p_in, HB_P1_C0, HB_P1_C1, interp_p1_x0, interp_p1_y0, interp_p1_x1, interp_p1_y1);
	}

	// Downsample two input samples → one output sample via polyphase half-band.
	inline float downsample_pair(float p_in0, float p_in1) {
		float a0 = cascade_tick(p_in1, HB_P0_C0, HB_P0_C1, decim_p0_x0, decim_p0_y0, decim_p0_x1, decim_p0_y1);
		float a1 = cascade_tick(p_in0, HB_P1_C0, HB_P1_C1, decim_p1_x0, decim_p1_y0, decim_p1_x1, decim_p1_y1);
		return 0.5f * (a0 + a1);
	}

	// Antiderivative of tanh(u): ln(cosh(u)), evaluated in double for ADAA accuracy.
	[[nodiscard]] static inline double tanh_antideriv(double p_u) {
		double abs_u = fabs(p_u);
		return abs_u + log(1.0 + exp(-2.0 * abs_u)) - 0.693147180559945309417;
	}

	[[nodiscard]] static inline double hardclip_antideriv(double p_u) {
		if (p_u < -1.0) {
			return -p_u - 0.5;
		} else if (p_u > 1.0) {
			return p_u - 0.5;
		}
		return 0.5 * p_u * p_u;
	}

	[[nodiscard]] inline float compute_direct(float p_u) const {
		if (mode == 0) {
			return tanhf(p_u);
		}
		return CLAMP(p_u, -1.0f, 1.0f);
	}

	inline double compute_F(double p_u) const {
		if (mode == 0) {
			return tanh_antideriv(p_u);
		}
		return hardclip_antideriv(p_u);
	}

	// Process a single sample through u-domain ADAA. Returns f(u).
	inline float process_adaa(float p_x, float p_drive) {
		float drive = MAX(p_drive, 1e-6f);
		double u1 = (double)p_x * (double)drive;
		double F1 = compute_F(u1);
		float out;

		// Hard-clip is exactly linear on (-1, 1); ADAA would average consecutive
		// samples and smear an identity path. Skip ADAA while both endpoints are
		// inside the linear region (and when crossing into saturation use ADAA).
		if (mode == 1 && fabs(prev_u) <= 1.0 && fabs(u1) <= 1.0) {
			out = (float)u1;
		} else {
			double diff = u1 - prev_u;
			if (fabs(diff) > (double)ADAA_EPS) {
				out = (float)((F1 - prev_F) / diff);
			} else {
				out = compute_direct((float)(0.5 * (prev_u + u1)));
			}
		}

		prev_u = u1;
		prev_F = F1;
		return out;
	}

public:
	SymphonySaturator(float p_drive, int32_t p_mode, bool p_oversample, float *p_os_buffer)
			: default_drive(p_drive), mode(p_mode), oversample(p_oversample), os_buffer(p_os_buffer) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		drive_input = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		float drive = drive_input ? *drive_input : default_drive;

		if (!oversample) {
			for (int32_t i = 0; i < p_num_frames; i++) {
				output[i] = process_adaa(input[i], drive);
			}
		} else {
			int32_t os_frames = p_num_frames * 2;

			for (int32_t i = 0; i < p_num_frames; i++) {
				upsample_one(input[i], os_buffer[i * 2], os_buffer[i * 2 + 1]);
			}

			for (int32_t i = 0; i < os_frames; i++) {
				os_buffer[i] = process_adaa(os_buffer[i], drive);
			}

			for (int32_t i = 0; i < p_num_frames; i++) {
				output[i] = downsample_pair(os_buffer[i * 2], os_buffer[i * 2 + 1]);
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		size_t needed = sizeof(double) * 2; // prev_u, prev_F
		if (oversample) {
			needed += sizeof(float) * 16; // 8 interp + 8 decim
		}
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		size_t offset = 0;
		memcpy(p_buffer + offset, &prev_u, sizeof(double));
		offset += sizeof(double);
		memcpy(p_buffer + offset, &prev_F, sizeof(double));
		offset += sizeof(double);
		if (oversample) {
			memcpy(p_buffer + offset, &interp_p0_x0, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_p0_y0, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_p0_x1, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_p0_y1, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_p1_x0, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_p1_y0, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_p1_x1, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_p1_y1, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_p0_x0, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_p0_y0, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_p0_x1, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_p0_y1, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_p1_x0, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_p1_y0, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_p1_x1, sizeof(float));
			offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_p1_y1, sizeof(float));
			offset += sizeof(float);
		}
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(double) * 2;
		if (oversample) {
			needed += sizeof(float) * 16;
		}
		if (p_size < needed) {
			return;
		}
		size_t offset = 0;
		memcpy(&prev_u, p_buffer + offset, sizeof(double));
		offset += sizeof(double);
		memcpy(&prev_F, p_buffer + offset, sizeof(double));
		offset += sizeof(double);
		if (oversample) {
			memcpy(&interp_p0_x0, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&interp_p0_y0, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&interp_p0_x1, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&interp_p0_y1, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&interp_p1_x0, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&interp_p1_y0, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&interp_p1_x1, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&interp_p1_y1, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&decim_p0_x0, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&decim_p0_y0, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&decim_p0_x1, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&decim_p0_y1, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&decim_p1_x0, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&decim_p1_y0, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&decim_p1_x1, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
			memcpy(&decim_p1_y1, p_buffer + offset, sizeof(float));
			offset += sizeof(float);
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Saturator";
		desc.category = "Filters";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "drive", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "drive", 2.0f, 0.1f, 20.0f, 0.1f });
		desc.params.push_back({ "mode", 0.0f, 0.0f, 1.0f, 1.0f }); // 0=soft, 1=hard
		desc.params.push_back({ "oversample", 0.0f, 0.0f, 1.0f, 1.0f }); // 0=off, 1=2x
		desc.nonlinear = true;
		desc.state_size = sizeof(SymphonySaturator);
		desc.state_align = alignof(SymphonySaturator);
		desc.extra_arena_bytes = sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE * 2 + 32;
		desc.create_fn = &SymphonySaturator::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_mix_rate;
		float d = 2.0f;
		int32_t m = 0;
		bool os = false;
		if (p_params.has("drive")) {
			d = p_params["drive"];
		}
		if (p_params.has("mode")) {
			m = (int32_t)(float)p_params["mode"];
		}
		if (p_params.has("oversample")) {
			os = (int32_t)(float)p_params["oversample"] != 0;
		}

		float *os_buf = (float *)p_arena.alloc(sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE * 2, 32);

		void *mem = p_arena.alloc(sizeof(SymphonySaturator), alignof(SymphonySaturator));
		return new (mem) SymphonySaturator(d, m, os, os_buf);
	}
};
