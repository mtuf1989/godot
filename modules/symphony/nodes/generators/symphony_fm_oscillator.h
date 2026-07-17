#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Phase Modulation (PM) oscillator with output anti-alias filter.
// PM is preferred over true FM for real-time parameter changes because the
// modulation depth is independent of modulator frequency — no spectral blowup
// when sweeping mod_freq.
//
// Algorithm:
//   mod_phase += mod_freq / sample_rate
//   carrier_phase += carrier_freq / sample_rate
//   output = sin(2π * carrier_phase + mod_index * sin(2π * mod_phase))
//
// If mod_input is connected, uses the external audio signal as modulator
// instead of the internal sine oscillator.
//
// Anti-alias: A one-pole lowpass at 18 kHz is applied to the output.
// At moderate mod_index this has negligible audible effect, but attenuates
// the above-Nyquist energy that causes aliased inharmonic artifacts at
// high modulation indices (mod_index > 3).
class SymphonyFMOscillator : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT carrier_freq_input = nullptr;
	const float *SYMPHONY_RESTRICT mod_freq_input = nullptr;
	const float *SYMPHONY_RESTRICT mod_index_input = nullptr;
	const float *SYMPHONY_RESTRICT mod_input = nullptr; // External audio modulator
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	float carrier_phase = 0.0f;
	float mod_phase = 0.0f;
	float mix_rate = 44100.0f;
	float default_carrier_freq = 440.0f;
	float default_mod_freq = 440.0f;
	float default_mod_index = 1.0f;

	// Output anti-alias one-pole filter state and coefficient.
	// y[n] = (1-a)*x[n] + a*y[n-1], a = exp(-2π * 18000 / sample_rate)
	float lp_coeff = 0.0f;
	float lp_prev = 0.0f;

public:
	SymphonyFMOscillator(float p_mix_rate, float p_carrier_freq, float p_mod_freq, float p_mod_index)
			: mix_rate(p_mix_rate),
			  default_carrier_freq(p_carrier_freq),
			  default_mod_freq(p_mod_freq),
			  default_mod_index(p_mod_index) {
		// Pre-compute one-pole coefficient for 18 kHz cutoff.
		// At 48 kHz: coeff ≈ 0.095, giving -3dB at 18 kHz, -6dB/oct slope above.
		lp_coeff = expf(-Math::TAU * 18000.0f / mix_rate);
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		carrier_freq_input = (const float *)p_input_ptrs[0];
		mod_freq_input = (const float *)p_input_ptrs[1];
		mod_index_input = (const float *)p_input_ptrs[2];
		mod_input = (const float *)p_input_ptrs[3];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		// FLOAT pins are single values (control-rate), read once per micro-block
		float carrier_freq = carrier_freq_input ? *carrier_freq_input : default_carrier_freq;
		float mod_freq = mod_freq_input ? *mod_freq_input : default_mod_freq;
		float mod_index = mod_index_input ? *mod_index_input : default_mod_index;

		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {

			// Advance modulator phase
			mod_phase += mod_freq / mix_rate;
			if (mod_phase >= 1.0f) {
				mod_phase -= 1.0f;
			}

			// Compute modulator signal
			float modulator;
			if (mod_input) {
				// External modulator: already in [-1, 1] range
				modulator = mod_input[i];
			} else {
				// Internal sine modulator
				modulator = Math::sin(mod_phase * Math::TAU);
			}

			// Advance carrier phase
			carrier_phase += carrier_freq / mix_rate;
			if (carrier_phase >= 1.0f) {
				carrier_phase -= 1.0f;
			}

			// Phase modulation: offset carrier phase by scaled modulator
			float pm_offset = mod_index * modulator;
			float raw = Math::sin(carrier_phase * Math::TAU + pm_offset);

			// One-pole anti-alias lowpass (18 kHz)
			lp_prev = (1.0f - lp_coeff) * raw + lp_coeff * lp_prev;
			audio_out[i] = lp_prev;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(float) * 3; // carrier_phase + mod_phase + lp_prev
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		memcpy(p_buffer, &carrier_phase, sizeof(float));
		memcpy(p_buffer + sizeof(float), &mod_phase, sizeof(float));
		memcpy(p_buffer + sizeof(float) * 2, &lp_prev, sizeof(float));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float) * 3;
		if (p_size >= needed) {
			memcpy(&carrier_phase, p_buffer, sizeof(float));
			memcpy(&mod_phase, p_buffer + sizeof(float), sizeof(float));
			memcpy(&lp_prev, p_buffer + sizeof(float) * 2, sizeof(float));
		} else if (p_size >= sizeof(float) * 2) {
			// Backward-compatible: old state without lp_prev
			memcpy(&carrier_phase, p_buffer, sizeof(float));
			memcpy(&mod_phase, p_buffer + sizeof(float), sizeof(float));
			lp_prev = 0.0f;
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "FMOscillator";
		desc.category = "Generators";
		desc.inputs.push_back({ "carrier_freq", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "mod_freq", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "mod_index", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "mod_input", SymphonyPinType::AUDIO, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "carrier_freq", 440.0f, 20.0f, 20000.0f, 1.0f });
		desc.params.push_back({ "mod_freq", 440.0f, 0.1f, 20000.0f, 0.1f });
		desc.params.push_back({ "mod_index", 1.0f, 0.0f, 10.0f, 0.01f });
		desc.state_size = sizeof(SymphonyFMOscillator);
		desc.state_align = alignof(SymphonyFMOscillator);
		desc.create_fn = &SymphonyFMOscillator::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float carrier_freq = p_params.has("carrier_freq") ? (float)p_params["carrier_freq"] : 440.0f;
		float mod_freq = p_params.has("mod_freq") ? (float)p_params["mod_freq"] : 440.0f;
		float mod_index = p_params.has("mod_index") ? (float)p_params["mod_index"] : 1.0f;
		void *mem = p_arena.alloc(sizeof(SymphonyFMOscillator), alignof(SymphonyFMOscillator));
		return new (mem) SymphonyFMOscillator(p_mix_rate, carrier_freq, mod_freq, mod_index);
	}
};
