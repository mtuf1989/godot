#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_fast_math.h"
#include "core/math/math_funcs.h"

// Phase Modulation (PM) oscillator.
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
// instead of the internal sine oscillator. External mod_input is not clamped.
//
// Internal-modulator index is Carson-clamped so sidebands stay below ~0.45·sr:
//   β ≤ max(0, (0.45·sr − fc) / fm − 3)
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

	static inline float fast_sine(float p_phase) {
		return SymphonyFastMath::fast_sine(p_phase);
	}

	static inline float fast_sine_rad(float p_radians) {
		float normalized = p_radians * (1.0f / (float)Math::TAU);
		normalized -= floorf(normalized);
		return fast_sine(normalized);
	}

public:
	SymphonyFMOscillator(float p_mix_rate, float p_carrier_freq, float p_mod_freq, float p_mod_index)
			: mix_rate(p_mix_rate),
			  default_carrier_freq(p_carrier_freq),
			  default_mod_freq(p_mod_freq),
			  default_mod_index(p_mod_index) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		carrier_freq_input = (const float *)p_input_ptrs[0];
		mod_freq_input = (const float *)p_input_ptrs[1];
		mod_index_input = (const float *)p_input_ptrs[2];
		mod_input = (const float *)p_input_ptrs[3];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float carrier_freq = carrier_freq_input ? *carrier_freq_input : default_carrier_freq;
		float mod_freq = mod_freq_input ? *mod_freq_input : default_mod_freq;
		float mod_index = mod_index_input ? *mod_index_input : default_mod_index;

		const bool external_mod = mod_input != nullptr;
		if (!external_mod) {
			// Carson bandwidth ≈ 2(β+1)fm; keep upper sideband under 0.45·sr with margin 2.
			float fm = MAX(fabsf(mod_freq), 1e-3f);
			// Carson BW ≈ 2(β+1)fm; keep the upper edge under 0.45·sr.
			// Margin 3 matches the plan (Carson + 2 extra sidebands beyond β+1).
			float beta_max = MAX(0.0f, (0.45f * mix_rate - carrier_freq) / fm - 3.0f);
			mod_index = MIN(mod_index, beta_max);
		}

		// P9 state-in-locals
		float l_mod_phase = mod_phase, l_carrier_phase = carrier_phase;

		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			l_mod_phase += mod_freq / mix_rate;
			l_mod_phase -= floorf(l_mod_phase);

			float modulator;
			if (external_mod) {
				modulator = mod_input[i];
			} else {
				modulator = fast_sine(l_mod_phase);
			}

			l_carrier_phase += carrier_freq / mix_rate;
			l_carrier_phase -= floorf(l_carrier_phase);

			float pm_offset = mod_index * modulator;
			audio_out[i] = fast_sine_rad(l_carrier_phase * (float)Math::TAU + pm_offset);
		}
		mod_phase = l_mod_phase;
		carrier_phase = l_carrier_phase;
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(float) * 2; // carrier_phase + mod_phase
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		memcpy(p_buffer, &carrier_phase, sizeof(float));
		memcpy(p_buffer + sizeof(float), &mod_phase, sizeof(float));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float) * 2;
		if (p_size >= needed) {
			memcpy(&carrier_phase, p_buffer, sizeof(float));
			memcpy(&mod_phase, p_buffer + sizeof(float), sizeof(float));
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
