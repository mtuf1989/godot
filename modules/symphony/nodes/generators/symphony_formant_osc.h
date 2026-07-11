#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Phase-Aligned Formant (PAF) oscillator.
// Generates a single formant peak by modulating a carrier oscillator at the
// fundamental frequency with a Gaussian spectral envelope centered at the
// formant frequency.
//
// The PAF technique produces a band of harmonics (of the fundamental) whose
// amplitudes follow a Gaussian bell curve centered at formant_freq.
// Multiple FormantOsc instances (2-5) create vowel/creature vocalizations.
//
// Algorithm (per sample):
//   1. Advance carrier phase at fundamental_freq
//   2. Compute the "formant ratio" k = formant_freq / fundamental_freq
//   3. Compute Gaussian envelope: env = exp(-0.5 * ((harmonic_phase - formant_center) / sigma)^2)
//      where sigma derives from bandwidth
//   4. Output = carrier_waveform * envelope_modulation
//
// Simplified PAF approach (Music V style, Perry Cook):
//   carrier_phase advances at fundamental_freq
//   modulator = sinc-like window applied to carrier harmonics at formant center
//   Implemented as: out = sin(2π * carrier_phase) * exp(-bw * |wrapped_phase_offset|)
//   where wrapped_phase_offset = distance from current harmonic phase to formant peak
//
// Practical implementation (efficient real-time PAF):
//   carrier advances at fundamental_freq
//   formant_phase advances at formant_freq
//   Output = cos(2π * formant_phase) * gaussian_window(carrier_phase, bandwidth)
//   The gaussian window repeats at fundamental rate, placing energy at formant_freq.
//
class SymphonyFormantOsc : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT fundamental_freq_input = nullptr;
	const float *SYMPHONY_RESTRICT formant_freq_input = nullptr;
	const float *SYMPHONY_RESTRICT bandwidth_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	float carrier_phase = 0.0f; // Phase of the fundamental (0-1), resets each period
	float mix_rate = 48000.0f;
	float default_fundamental_freq = 110.0f;
	float default_formant_freq = 800.0f;
	float default_bandwidth = 80.0f; // Hz — width of the formant bell curve

public:
	SymphonyFormantOsc(float p_mix_rate, float p_fundamental_freq, float p_formant_freq, float p_bandwidth)
			: mix_rate(p_mix_rate),
			  default_fundamental_freq(p_fundamental_freq),
			  default_formant_freq(p_formant_freq),
			  default_bandwidth(p_bandwidth) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		fundamental_freq_input = (const float *)p_input_ptrs[0];
		formant_freq_input = (const float *)p_input_ptrs[1];
		bandwidth_input = (const float *)p_input_ptrs[2];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float fundamental_freq = fundamental_freq_input ? *fundamental_freq_input : default_fundamental_freq;
		float formant_freq = formant_freq_input ? *formant_freq_input : default_formant_freq;
		float bandwidth = bandwidth_input ? *bandwidth_input : default_bandwidth;

		// Clamp to sane values
		fundamental_freq = CLAMP(fundamental_freq, 20.0f, 5000.0f);
		formant_freq = CLAMP(formant_freq, 50.0f, 16000.0f);
		bandwidth = CLAMP(bandwidth, 10.0f, 2000.0f);

		// Compute the Gaussian envelope width parameter.
		// sigma controls how many harmonics around the formant center are audible.
		// Higher bandwidth = wider bell = more harmonics excited.
		// We express bandwidth in terms of phase-space width:
		//   sigma_phase = (bandwidth / fundamental_freq) / (2 * sqrt(2 * ln(2)))
		// This ensures the -3dB point of the Gaussian is at ± bandwidth/2 Hz.
		//
		// Simplified: use a "sharpness" parameter that controls the Gaussian tightness.
		// sharpness = π * bandwidth / (fundamental_freq * sample_rate_independent_factor)
		// We use: beta = π * bandwidth / fundamental_freq
		// The envelope shape is: exp(-beta² * x²) where x is normalized carrier phase [-0.5, 0.5]
		float beta = (float)Math::PI * bandwidth / fundamental_freq;
		// Precompute -beta² for the Gaussian exponent
		float neg_beta_sq = -beta * beta;

		// Phase increment per sample for the fundamental
		float phase_inc = fundamental_freq / mix_rate;

		// Formant ratio: how many fundamental cycles fit in one formant cycle
		// This determines the "center frequency" of the spectral envelope
		float formant_ratio = formant_freq / fundamental_freq;

		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			// Advance carrier phase [0, 1)
			carrier_phase += phase_inc;
			if (carrier_phase >= 1.0f) {
				carrier_phase -= 1.0f;
			}

			// Map carrier_phase to [-0.5, 0.5] centered window
			// This makes the Gaussian peak at phase = 0 (center of each fundamental period)
			float centered_phase = carrier_phase - 0.5f;

			// Gaussian envelope: peaks at center of each fundamental period
			// exp(-beta² * phase²) where phase is in [-0.5, 0.5]
			float envelope = Math::exp(neg_beta_sq * centered_phase * centered_phase);

			// Cosine carrier at formant frequency, phase-aligned to fundamental
			// This places spectral energy at formant_freq (as harmonics of fundamental)
			float formant_carrier = Math::cos(carrier_phase * formant_ratio * (float)Math::TAU);

			// Output: windowed cosine at formant frequency
			audio_out[i] = formant_carrier * envelope;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(float); // carrier_phase
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		memcpy(p_buffer, &carrier_phase, sizeof(float));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float);
		if (p_size >= needed) {
			memcpy(&carrier_phase, p_buffer, sizeof(float));
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "FormantOsc";
		desc.category = "Generators";
		desc.inputs.push_back({ "fundamental_freq", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "formant_freq", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "bandwidth", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "fundamental_freq", 110.0f, 20.0f, 5000.0f, 1.0f });
		desc.params.push_back({ "formant_freq", 800.0f, 50.0f, 16000.0f, 1.0f });
		desc.params.push_back({ "bandwidth", 80.0f, 10.0f, 2000.0f, 1.0f });
		desc.state_size = sizeof(SymphonyFormantOsc);
		desc.state_align = alignof(SymphonyFormantOsc);
		desc.create_fn = &SymphonyFormantOsc::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float fundamental_freq = p_params.has("fundamental_freq") ? (float)p_params["fundamental_freq"] : 110.0f;
		float formant_freq = p_params.has("formant_freq") ? (float)p_params["formant_freq"] : 800.0f;
		float bandwidth = p_params.has("bandwidth") ? (float)p_params["bandwidth"] : 80.0f;
		void *mem = p_arena.alloc(sizeof(SymphonyFormantOsc), alignof(SymphonyFormantOsc));
		return new (mem) SymphonyFormantOsc(p_mix_rate, fundamental_freq, formant_freq, bandwidth);
	}
};
