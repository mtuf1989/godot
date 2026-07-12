#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// ModalBank: N parallel resonant bandpass biquad filters driven by data arrays.
// Each mode = (frequency, t60 decay time, gain).
// Struck with an excitation signal, produces material-specific impact tones.
//
// Modal data (frequencies, decay times, gains) is provided via the graph's
// parameter interface. Preset data lives in the .tres resource, not in C++.
//
// Biquad bandpass coefficients per mode:
//   bandwidth = -log(0.001) / (PI * t60)
//   R = exp(-PI * bandwidth / sample_rate)
//   theta = 2 * PI * frequency / sample_rate
//   b0 = 1 - R
//   a1 = -2 * R * cos(theta)
//   a2 = R * R
//
// Processing: all modes summed per sample.
class SymphonyModalBank : public SymphonyOperator {
private:
	static constexpr int32_t MAX_MODES_LIMIT = 64; // Hard cap for arena allocation

	const float *SYMPHONY_RESTRICT excitation = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	// Per-mode biquad state (y[n-1], y[n-2])
	float *state_y1 = nullptr; // [max_modes]
	float *state_y2 = nullptr; // [max_modes]

	// Per-mode coefficients (precomputed)
	float *coeff_b0 = nullptr; // [max_modes] - input gain (1 - R)
	float *coeff_a1 = nullptr; // [max_modes] - feedback coeff 1 (-2*R*cos(theta))
	float *coeff_a2 = nullptr; // [max_modes] - feedback coeff 2 (R*R)
	float *mode_gain = nullptr; // [max_modes] - per-mode output gain

	int32_t num_modes = 0;
	int32_t max_modes = 0;
	float mix_rate = 44100.0f;

public:
	SymphonyModalBank() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		excitation = (const float *)p_input_ptrs[0];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		if (!excitation || num_modes == 0) {
			for (int32_t i = 0; i < p_num_frames; i++) {
				audio_out[i] = 0.0f;
			}
			return;
		}

		for (int32_t i = 0; i < p_num_frames; i++) {
			float input = excitation[i];
			float sum = 0.0f;

			// Process all modes for this sample
			for (int32_t m = 0; m < num_modes; m++) {
				// 2nd-order IIR (biquad resonator):
				// y[n] = b0 * x[n] - a1 * y[n-1] - a2 * y[n-2]
				float y = coeff_b0[m] * input - coeff_a1[m] * state_y1[m] - coeff_a2[m] * state_y2[m];

				state_y2[m] = state_y1[m];
				state_y1[m] = y;

				sum += y * mode_gain[m];
			}

			audio_out[i] = sum;
		}
	}

	// Recompute biquad coefficients from frequency/t60/gain arrays.
	// Called during create() and can be called for hot-swap data updates.
	void compute_coefficients(const float *frequencies, const float *t60s, const float *gains, int32_t p_num_modes) {
		num_modes = (p_num_modes > max_modes) ? max_modes : p_num_modes;

		for (int32_t m = 0; m < num_modes; m++) {
			float freq = frequencies[m];
			float t60 = t60s[m];
			float gain = gains[m];

			// Bandwidth from T60 decay time:
			// bw = -log(0.001) / (PI * t60) = 6.908 / (PI * t60)
			float bandwidth = 6.9078f / ((float)Math::PI * t60);

			// Resonator pole radius from bandwidth
			float R = Math::exp(-(float)Math::PI * bandwidth / mix_rate);

			// Pole angle from center frequency
			float theta = 2.0f * (float)Math::PI * freq / mix_rate;

			// Biquad bandpass coefficients
			coeff_b0[m] = 1.0f - R;
			coeff_a1[m] = -2.0f * R * Math::cos(theta);
			coeff_a2[m] = R * R;
			mode_gain[m] = gain;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// Export y1 and y2 state arrays
		size_t needed = sizeof(int32_t) + sizeof(float) * max_modes * 2;
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;

		size_t offset = 0;
		memcpy(p_buffer + offset, &num_modes, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(p_buffer + offset, state_y1, sizeof(float) * max_modes);
		offset += sizeof(float) * max_modes;
		memcpy(p_buffer + offset, state_y2, sizeof(float) * max_modes);
		offset += sizeof(float) * max_modes;
		return offset;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(int32_t) + sizeof(float) * max_modes * 2;
		if (p_size < needed) return;

		size_t offset = 0;
		int32_t imported_modes = 0;
		memcpy(&imported_modes, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);

		if (imported_modes <= max_modes) {
			num_modes = imported_modes;
		}
		memcpy(state_y1, p_buffer + offset, sizeof(float) * max_modes);
		offset += sizeof(float) * max_modes;
		memcpy(state_y2, p_buffer + offset, sizeof(float) * max_modes);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "ModalBank";
		desc.category = "Synthesis";
		desc.inputs.push_back({ "excitation", SymphonyPinType::AUDIO, true });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "max_modes", 20.0f, 1.0f, 64.0f, 1.0f });
		desc.params.push_back({ "num_modes", 20.0f, 1.0f, 64.0f, 1.0f });
		desc.state_size = sizeof(SymphonyModalBank);
		desc.state_align = alignof(SymphonyModalBank);
		desc.extra_arena_bytes = sizeof(float) * 64 * 5 + 64; // State + coeff arrays for max 64 modes
		desc.create_fn = &SymphonyModalBank::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		int32_t max_modes = p_params.has("max_modes") ? (int)(float)p_params["max_modes"] : 20;
		int32_t num_modes_param = p_params.has("num_modes") ? (int)(float)p_params["num_modes"] : max_modes;
		if (max_modes < 1) max_modes = 1;
		if (max_modes > MAX_MODES_LIMIT) max_modes = MAX_MODES_LIMIT;
		if (num_modes_param > max_modes) num_modes_param = max_modes;

		// Allocate the operator
		void *mem = p_arena.alloc(sizeof(SymphonyModalBank), alignof(SymphonyModalBank));
		if (!mem) return nullptr;
		SymphonyModalBank *bank = new (mem) SymphonyModalBank();

		bank->max_modes = max_modes;
		bank->mix_rate = p_mix_rate;

		// Allocate per-mode arrays in arena
		bank->state_y1 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->state_y2 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->coeff_b0 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->coeff_a1 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->coeff_a2 = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));
		bank->mode_gain = (float *)p_arena.alloc(sizeof(float) * max_modes, alignof(float));

		// Zero-init state
		memset(bank->state_y1, 0, sizeof(float) * max_modes);
		memset(bank->state_y2, 0, sizeof(float) * max_modes);

		// Load mode data from params (PackedFloat32Arrays)
		if (p_params.has("frequencies") && p_params.has("decay_times") && p_params.has("gains")) {
			PackedFloat32Array freqs = p_params["frequencies"];
			PackedFloat32Array decays = p_params["decay_times"];
			PackedFloat32Array gains = p_params["gains"];

			int32_t actual_modes = MIN(num_modes_param, (int32_t)freqs.size());
			actual_modes = MIN(actual_modes, (int32_t)decays.size());
			actual_modes = MIN(actual_modes, (int32_t)gains.size());

			bank->compute_coefficients(freqs.ptr(), decays.ptr(), gains.ptr(), actual_modes);
		} else {
			// No data provided — zero modes (silent until data is set)
			bank->num_modes = 0;
			memset(bank->coeff_b0, 0, sizeof(float) * max_modes);
			memset(bank->coeff_a1, 0, sizeof(float) * max_modes);
			memset(bank->coeff_a2, 0, sizeof(float) * max_modes);
			memset(bank->mode_gain, 0, sizeof(float) * max_modes);
		}

		return bank;
	}
};
