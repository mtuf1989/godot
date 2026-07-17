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
// ADAA computes y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
// where F(x) is the antiderivative of the nonlinear function.
// This eliminates aliasing from the nonlinear waveshaping with zero added latency.
//
// Optional 2x oversampling (compile-time parameter "oversample"):
// When enabled, input is upsampled 2x via IIR half-band interpolation, processed,
// then downsampled via IIR half-band decimation. This provides additional alias
// rejection beyond ADAA alone — useful for extreme drive values or cinematic quality.
class SymphonySaturator : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT input = nullptr;
	const float *SYMPHONY_RESTRICT drive_input = nullptr;
	float *SYMPHONY_RESTRICT output = nullptr;
	float default_drive = 2.0f;
	int32_t mode = 0; // 0=soft(tanh), 1=hard
	bool oversample = false;

	// ADAA state (1st-order: needs previous sample and its antiderivative)
	float prev_x = 0.0f;
	float prev_F = 0.0f;

	// --- Half-band IIR filter for 2x oversampling ---
	// 3rd-order elliptic half-band coefficients (allpass decomposition).
	// Two allpass branches: A0 with coeff a0, A1 with coeff a1.
	// These give ~-70dB stopband attenuation with minimal passband ripple.
	static constexpr float HB_A0 = 0.07986642623635751f;
	static constexpr float HB_A1 = 0.5453536510711322f;

	// Interpolation filter state (2 allpass branches)
	float interp_a0_x = 0.0f, interp_a0_y = 0.0f;
	float interp_a1_x = 0.0f, interp_a1_y = 0.0f;

	// Decimation filter state (2 allpass branches)
	float decim_a0_x = 0.0f, decim_a0_y = 0.0f;
	float decim_a1_x = 0.0f, decim_a1_y = 0.0f;

	// Internal buffer for oversampled processing (2x input size)
	float *os_buffer = nullptr;

	// --- Half-band allpass section ---
	static inline float allpass_tick(float input, float coeff, float &x_state, float &y_state) {
		float out = coeff * (input - y_state) + x_state;
		x_state = input;
		y_state = out;
		return out;
	}

	// Upsample one input sample → two output samples via half-band interpolation.
	inline void upsample_one(float in, float &out0, float &out1) {
		// Branch A0 processes every other sample
		float a0 = allpass_tick(in, HB_A0, interp_a0_x, interp_a0_y);
		// Branch A1 processes every other sample
		float a1 = allpass_tick(in, HB_A1, interp_a1_x, interp_a1_y);
		// Interleave: one output from each branch
		out0 = (a0 + a1) * 0.5f;
		out1 = (a0 - a1) * 0.5f;
	}

	// Downsample two input samples → one output sample via half-band decimation.
	inline float downsample_pair(float in0, float in1) {
		// Process alternating samples through allpass branches
		float a0 = allpass_tick(in0, HB_A0, decim_a0_x, decim_a0_y);
		float a1 = allpass_tick(in1, HB_A1, decim_a1_x, decim_a1_y);
		return (a0 + a1) * 0.5f;
	}

	// --- Antiderivative functions ---

	// Antiderivative of tanh(x): ln(cosh(x))
	// Numerically stable form: |x| + ln(1 + exp(-2|x|)) - ln(2)
	static inline float tanh_antideriv(float x) {
		float abs_x = fabsf(x);
		return abs_x + logf(1.0f + expf(-2.0f * abs_x)) - 0.6931471805599453f;
	}

	// Antiderivative of hard_clip(x) = clamp(x, -1, 1)
	// Piecewise: F(x) = -x - 0.5  for x < -1
	//            F(x) = 0.5 * x^2  for -1 <= x <= 1
	//            F(x) = x - 0.5    for x > 1
	static inline float hardclip_antideriv(float x) {
		if (x < -1.0f) {
			return -x - 0.5f;
		} else if (x > 1.0f) {
			return x - 0.5f;
		} else {
			return 0.5f * x * x;
		}
	}

	static inline float tanh_direct(float x) {
		return tanhf(x);
	}

	static inline float hardclip_direct(float x) {
		return CLAMP(x, -1.0f, 1.0f);
	}

	inline float compute_F(float x, float drive) const {
		float xd = x * drive;
		if (mode == 0) {
			return tanh_antideriv(xd) / drive;
		} else {
			return hardclip_antideriv(xd) / drive;
		}
	}

	inline float compute_direct(float x, float drive) const {
		float xd = x * drive;
		if (mode == 0) {
			return tanh_direct(xd);
		} else {
			return hardclip_direct(xd);
		}
	}

	// Process a single sample through ADAA
	inline float process_adaa(float x1, float drive) {
		float F1 = compute_F(x1, drive);
		float diff = x1 - prev_x;
		float out;

		if (fabsf(diff) > 1e-7f) {
			out = (F1 - prev_F) / diff;
		} else {
			out = compute_direct(x1, drive);
		}

		prev_x = x1;
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
			// Standard ADAA path (no oversampling)
			for (int32_t i = 0; i < p_num_frames; i++) {
				output[i] = process_adaa(input[i], drive);
			}
		} else {
			// 2x oversampled path: upsample → ADAA process → downsample
			int32_t os_frames = p_num_frames * 2;

			// Upsample input into os_buffer
			for (int32_t i = 0; i < p_num_frames; i++) {
				upsample_one(input[i], os_buffer[i * 2], os_buffer[i * 2 + 1]);
			}

			// Process at 2x rate through ADAA
			for (int32_t i = 0; i < os_frames; i++) {
				os_buffer[i] = process_adaa(os_buffer[i], drive);
			}

			// Downsample back to original rate
			for (int32_t i = 0; i < p_num_frames; i++) {
				output[i] = downsample_pair(os_buffer[i * 2], os_buffer[i * 2 + 1]);
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// ADAA state + oversampling filter states
		size_t needed = sizeof(float) * 2; // prev_x, prev_F
		if (oversample) {
			needed += sizeof(float) * 8; // 4 interp + 4 decim filter state floats
		}
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		size_t offset = 0;
		memcpy(p_buffer + offset, &prev_x, sizeof(float)); offset += sizeof(float);
		memcpy(p_buffer + offset, &prev_F, sizeof(float)); offset += sizeof(float);
		if (oversample) {
			memcpy(p_buffer + offset, &interp_a0_x, sizeof(float)); offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_a0_y, sizeof(float)); offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_a1_x, sizeof(float)); offset += sizeof(float);
			memcpy(p_buffer + offset, &interp_a1_y, sizeof(float)); offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_a0_x, sizeof(float)); offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_a0_y, sizeof(float)); offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_a1_x, sizeof(float)); offset += sizeof(float);
			memcpy(p_buffer + offset, &decim_a1_y, sizeof(float)); offset += sizeof(float);
		}
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(float) * 2;
		if (oversample) needed += sizeof(float) * 8;
		if (p_size < needed) return;
		size_t offset = 0;
		memcpy(&prev_x, p_buffer + offset, sizeof(float)); offset += sizeof(float);
		memcpy(&prev_F, p_buffer + offset, sizeof(float)); offset += sizeof(float);
		if (oversample) {
			memcpy(&interp_a0_x, p_buffer + offset, sizeof(float)); offset += sizeof(float);
			memcpy(&interp_a0_y, p_buffer + offset, sizeof(float)); offset += sizeof(float);
			memcpy(&interp_a1_x, p_buffer + offset, sizeof(float)); offset += sizeof(float);
			memcpy(&interp_a1_y, p_buffer + offset, sizeof(float)); offset += sizeof(float);
			memcpy(&decim_a0_x, p_buffer + offset, sizeof(float)); offset += sizeof(float);
			memcpy(&decim_a0_y, p_buffer + offset, sizeof(float)); offset += sizeof(float);
			memcpy(&decim_a1_x, p_buffer + offset, sizeof(float)); offset += sizeof(float);
			memcpy(&decim_a1_y, p_buffer + offset, sizeof(float)); offset += sizeof(float);
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
		// Extra arena: oversampling buffer (2 * MICRO_BLOCK_SIZE floats) — always reserved for worst case
		desc.extra_arena_bytes = sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE * 2 + 32;
		desc.create_fn = &SymphonySaturator::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float d = 2.0f;
		int32_t m = 0;
		bool os = false;
		if (p_params.has("drive")) d = p_params["drive"];
		if (p_params.has("mode")) m = (int32_t)(float)p_params["mode"];
		if (p_params.has("oversample")) os = (int32_t)(float)p_params["oversample"] != 0;

		// Allocate oversampling buffer (only used when oversample=true, but pre-allocated always
		// to avoid conditional arena sizing — keeps extra_arena_bytes deterministic).
		float *os_buf = (float *)p_arena.alloc(sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE * 2, 32);

		void *mem = p_arena.alloc(sizeof(SymphonySaturator), alignof(SymphonySaturator));
		return new (mem) SymphonySaturator(d, m, os, os_buf);
	}
};
