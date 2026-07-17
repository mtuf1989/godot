#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// FrequencyEnvelopeFollower: Extracts amplitude envelope at a specific frequency.
//
// Unlike the broadband EnvelopeFollower (which tracks total signal level), this
// operator answers: "how loud is the signal at frequency F right now?"
//
// Uses a single resonator (complex phasor + EWMA) to isolate the target frequency,
// then outputs the smoothed power as a control-rate Float value.
//
// Use cases:
//   - "How loud is the bass?" → drive bass-reactive procedural audio
//   - "Is there a sustained pitched tone?" → detect musical content vs. noise
//   - Track specific harmonic of procedural oscillator output for feedback control
//
// Cost: ~14 FLOPs/sample. State: 32 bytes. No buffers, no allocation.
class SymphonyFrequencyEnvelopeFollower : public SymphonyOperator {
private:
	// --- Pin pointers ---
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT frequency_input = nullptr; // Optional: modulate center frequency
	float *SYMPHONY_RESTRICT envelope_out = nullptr; // Single float output (power at target freq)

	// --- Resonator state ---
	float phasor_real = 1.0f;
	float phasor_imag = 0.0f;
	float rot_real = 1.0f;       // Precomputed rotation (cos)
	float rot_imag = 0.0f;       // Precomputed rotation (sin)
	float smoothed_real = 0.0f;
	float smoothed_imag = 0.0f;
	float power_smoothed = 0.0f;
	float alpha = 0.01f;
	float one_minus_alpha = 0.99f;

	// --- Parameters ---
	float center_frequency = 200.0f;
	float mix_rate = 44100.0f;
	float responsiveness = 0.5f;
	int32_t sample_counter = 0;

	static constexpr int32_t RENORM_INTERVAL = 64;

	// Recompute rotation factors when frequency changes.
	void update_rotation(float p_frequency) {
		p_frequency = CLAMP(p_frequency, 20.0f, mix_rate * 0.45f);
		float angle = Math::TAU * p_frequency / mix_rate;
		rot_real = cosf(angle);
		rot_imag = sinf(angle);

		// Recompute alpha for the new frequency
		float K = 5.0f - 4.5f * responsiveness;
		float tc = (K * mix_rate) / (p_frequency * logf(1.0f + p_frequency));
		tc = MAX(tc, 1.0f);
		alpha = 1.0f / tc;
		one_minus_alpha = 1.0f - alpha;
	}

public:
	SymphonyFrequencyEnvelopeFollower() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		frequency_input = (const float *)p_input_ptrs[1];
		envelope_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		// Check for frequency modulation input (control-rate: read once per block)
		if (frequency_input) {
			float new_freq = *frequency_input;
			if (fabsf(new_freq - center_frequency) > 0.5f) {
				center_frequency = new_freq;
				update_rotation(center_frequency);
			}
		}

		if (!audio_in) {
			*envelope_out = 0.0f;
			return;
		}

		for (int32_t i = 0; i < p_num_frames; i++) {
			float input = audio_in[i];

			// 1. Correlate with phasor
			float resp_r = input * phasor_real;
			float resp_i = input * phasor_imag;

			// 2. EWMA smooth
			smoothed_real = alpha * resp_r + one_minus_alpha * smoothed_real;
			smoothed_imag = alpha * resp_i + one_minus_alpha * smoothed_imag;

			// 3. Power (magnitude²)
			float pwr = smoothed_real * smoothed_real + smoothed_imag * smoothed_imag;

			// 4. Second EWMA on power
			power_smoothed = alpha * pwr + one_minus_alpha * power_smoothed;

			// 5. Advance phasor
			float nr = phasor_real * rot_real - phasor_imag * rot_imag;
			float ni = phasor_real * rot_imag + phasor_imag * rot_real;
			phasor_real = nr;
			phasor_imag = ni;
		}

		// 6. Periodic renormalization
		sample_counter += p_num_frames;
		if (sample_counter >= RENORM_INTERVAL) {
			sample_counter = 0;
			float mag_sq = phasor_real * phasor_real + phasor_imag * phasor_imag;
			float inv_mag = 1.0f / sqrtf(mag_sq);
			phasor_real *= inv_mag;
			phasor_imag *= inv_mag;
		}

		// Output final power value
		*envelope_out = power_smoothed;
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		struct State {
			float phasor_real, phasor_imag;
			float smoothed_real, smoothed_imag;
			float power_smoothed;
			float center_frequency;
			int32_t sample_counter;
		};
		size_t needed = sizeof(State);
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		State s;
		s.phasor_real = phasor_real;
		s.phasor_imag = phasor_imag;
		s.smoothed_real = smoothed_real;
		s.smoothed_imag = smoothed_imag;
		s.power_smoothed = power_smoothed;
		s.center_frequency = center_frequency;
		s.sample_counter = sample_counter;
		memcpy(p_buffer, &s, sizeof(State));
		return sizeof(State);
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		struct State {
			float phasor_real, phasor_imag;
			float smoothed_real, smoothed_imag;
			float power_smoothed;
			float center_frequency;
			int32_t sample_counter;
		};
		if (p_size < sizeof(State)) {
			return;
		}
		State s;
		memcpy(&s, p_buffer, sizeof(State));
		phasor_real = s.phasor_real;
		phasor_imag = s.phasor_imag;
		smoothed_real = s.smoothed_real;
		smoothed_imag = s.smoothed_imag;
		power_smoothed = s.power_smoothed;
		center_frequency = s.center_frequency;
		sample_counter = s.sample_counter;
		// Recompute rotation from imported frequency
		update_rotation(center_frequency);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "FrequencyEnvelopeFollower";
		desc.category = "Utility";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "frequency", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "envelope_out", SymphonyPinType::FLOAT, false });
		desc.params.push_back({ "center_frequency", 200.0f, 20.0f, 20000.0f, 1.0f });
		desc.params.push_back({ "responsiveness", 0.5f, 0.0f, 1.0f, 0.01f });
		desc.state_size = sizeof(SymphonyFrequencyEnvelopeFollower);
		desc.state_align = alignof(SymphonyFrequencyEnvelopeFollower);
		desc.extra_arena_bytes = 0; // All state is inline (no arena buffers needed)
		desc.create_fn = &SymphonyFrequencyEnvelopeFollower::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyFrequencyEnvelopeFollower), alignof(SymphonyFrequencyEnvelopeFollower));
		if (!mem) {
			return nullptr;
		}
		SymphonyFrequencyEnvelopeFollower *fef = new (mem) SymphonyFrequencyEnvelopeFollower();

		fef->mix_rate = p_mix_rate;
		fef->center_frequency = p_params.has("center_frequency") ? (float)p_params["center_frequency"] : 200.0f;
		fef->center_frequency = CLAMP(fef->center_frequency, 20.0f, p_mix_rate * 0.45f);

		fef->responsiveness = p_params.has("responsiveness") ? (float)p_params["responsiveness"] : 0.5f;
		fef->responsiveness = CLAMP(fef->responsiveness, 0.0f, 1.0f);

		// Initialize resonator
		fef->update_rotation(fef->center_frequency);
		fef->phasor_real = 1.0f;
		fef->phasor_imag = 0.0f;
		fef->smoothed_real = 0.0f;
		fef->smoothed_imag = 0.0f;
		fef->power_smoothed = 0.0f;
		fef->sample_counter = 0;

		return fef;
	}
};
