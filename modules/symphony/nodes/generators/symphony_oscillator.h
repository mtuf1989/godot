#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_fast_math.h"
#include "core/math/math_funcs.h"

// Multi-waveform oscillator with PolyBLEP band-limiting.
// Waveforms: Sine(0), Saw(1), Square(2), Triangle(3), PWM(4)
//
// Saw subtracts a two-sample polynomial residual (PolyBLEP) at its reset, so
// the output stays in [-1, 1] with no DC. Square and PWM are the difference of
// two band-limited saws; PWM is a zero-mean pulse, so narrow widths swing
// asymmetrically (e.g. -0.2..+1.8 at width 0.1). Triangle integrates the
// band-limited square and is pulled gently toward the ideal triangle so it
// starts centred and cannot drift.
class SymphonyOscillator : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT freq_input = nullptr;
	const float *SYMPHONY_RESTRICT pw_input = nullptr; // Pulse width for PWM (waveform 4)
	float *SYMPHONY_RESTRICT output = nullptr;
	float phase = 0.0f;
	float mix_rate = 44100.0f;
	float default_freq = 440.0f;
	int32_t waveform = 0; // 0=Sine, 1=Saw, 2=Square, 3=Triangle, 4=PWM
	float default_pulse_width = 0.5f;

	[[nodiscard]] static inline float fast_sine(float p_phase) {
		return SymphonyFastMath::fast_sine(p_phase);
	}

	// Two-sample PolyBLEP residual for a unit downward step at phase 0/1.
	[[nodiscard]] static inline float poly_blep(float p_phase, float p_dt) {
		if (p_phase < p_dt) {
			const float t = p_phase / p_dt;
			return t + t - t * t - 1.0f;
		}
		if (p_phase > 1.0f - p_dt) {
			const float t = (p_phase - 1.0f) / p_dt;
			return t * t + t + t + 1.0f;
		}
		return 0.0f;
	}

	// Band-limited saw in [-1, 1]: phase in [0, 1), dt = freq / sample_rate.
	[[nodiscard]] static inline float bl_saw(float p_phase, float p_dt) {
		return (2.0f * p_phase - 1.0f) - poly_blep(p_phase, p_dt);
	}

	// Ideal triangle matching the integrated square: +1 at phase 0, -1 at 0.5.
	[[nodiscard]] static inline float naive_triangle(float p_phase) {
		return 4.0f * Math::abs(p_phase - 0.5f) - 1.0f;
	}

public:
	SymphonyOscillator(float p_mix_rate, float p_default_freq, int32_t p_waveform, float p_pulse_width)
			: mix_rate(p_mix_rate), default_freq(p_default_freq), waveform(p_waveform), default_pulse_width(p_pulse_width) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		freq_input = (const float *)p_input_ptrs[0];
		pw_input = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		// Read pulse width once per micro-block (control-rate parameter)
		float pulse_width = pw_input ? *pw_input : default_pulse_width;
		pulse_width = pulse_width < 0.01f ? 0.01f : (pulse_width > 0.99f ? 0.99f : pulse_width);

		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			float freq = freq_input ? freq_input[i] : default_freq;
			float dt = freq / mix_rate;
			// Clamp to [0, 0.49]; dt == 0 is DC hold (no BLEP, phase frozen).
			if (dt < 0.0f) {
				dt = 0.0f;
			} else if (dt > 0.49f) {
				dt = 0.49f;
			}

			float sample = 0.0f;

			if (dt == 0.0f) {
				// Frequency floor: hold the current waveform value as DC.
				switch (waveform) {
					case 0:
						sample = fast_sine(phase);
						break;
					case 1:
						sample = 2.0f * phase - 1.0f;
						break;
					case 3:
						sample = triangle_primed ? tri_integrator : naive_triangle(phase);
						break;
					default: {
						float phase2 = phase + ((waveform == 4) ? pulse_width : 0.5f);
						phase2 -= floorf(phase2);
						sample = (2.0f * phase - 1.0f) - (2.0f * phase2 - 1.0f);
					} break;
				}
				output[i] = sample;
				continue;
			}

			switch (waveform) {
				case 0: // Sine (polynomial approximation)
					sample = fast_sine(phase);
					break;

				case 1: // Saw (PolyBLEP)
					sample = bl_saw(phase, dt);
					break;

				case 2: { // Square (two saws with 0.5 offset)
					float saw1 = bl_saw(phase, dt);
					float phase2 = phase + 0.5f;
					phase2 -= floorf(phase2);
					float saw2 = bl_saw(phase2, dt);
					sample = saw1 - saw2;
				} break;

				case 3: { // Triangle (integrated band-limited square)
					float saw1 = bl_saw(phase, dt);
					float phase2 = phase + 0.5f;
					phase2 -= floorf(phase2);
					float saw2 = bl_saw(phase2, dt);
					float sq = saw1 - saw2;
					// Integrate square → triangle; 4*dt keeps the amplitude near 1.0.
					if (!triangle_primed) {
						// Start on the ideal curve for this phase and frequency so the
						// first cycle does not overshoot.
						float start_phase = phase + 0.5f * dt;
						start_phase -= floorf(start_phase);
						tri_integrator = naive_triangle(start_phase);
						triangle_primed = true;
					}
					tri_integrator += 4.0f * dt * sq;
					// Pull toward the ideal shape instead of leaking toward zero,
					// so low notes keep full amplitude and float error cannot drift.
					// The integral of this step is centred half a sample ahead of phase.
					float mid_phase = phase + 0.5f * dt;
					mid_phase -= floorf(mid_phase);
					tri_integrator += (naive_triangle(mid_phase) - tri_integrator) * TRIANGLE_PULL;
					sample = tri_integrator;
				} break;

				case 4: { // PWM (two saws with variable offset)
					float saw1 = bl_saw(phase, dt);
					float phase2 = phase + pulse_width;
					phase2 -= floorf(phase2);
					float saw2 = bl_saw(phase2, dt);
					sample = saw1 - saw2;
				} break;
			}

			output[i] = sample;

			phase += dt;
			phase -= floorf(phase); // Branch-free wrap to [0, 1)
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(float) * 2;
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		memcpy(p_buffer, &phase, sizeof(float));
		memcpy(p_buffer + sizeof(float), &tri_integrator, sizeof(float));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float) * 2;
		if (p_size >= needed) {
			memcpy(&phase, p_buffer, sizeof(float));
			memcpy(&tri_integrator, p_buffer + sizeof(float), sizeof(float));
			triangle_primed = true;
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Oscillator";
		desc.category = "Generators";
		desc.inputs.push_back({ "frequency", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "pulse_width", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "frequency", 440.0f, 0.0f, 20000.0f, 1.0f });
		desc.params.push_back({ "waveform", 0.0f, 0.0f, 4.0f, 1.0f }); // 0=Sine,1=Saw,2=Square,3=Triangle,4=PWM
		desc.params.push_back({ "pulse_width", 0.5f, 0.01f, 0.99f, 0.01f });
		desc.state_size = sizeof(SymphonyOscillator);
		desc.state_align = alignof(SymphonyOscillator);
		desc.create_fn = &SymphonyOscillator::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float freq = p_params.has("frequency") ? (float)p_params["frequency"] : 440.0f;
		int32_t wf = p_params.has("waveform") ? (int32_t)(float)p_params["waveform"] : 0;
		float pw = p_params.has("pulse_width") ? (float)p_params["pulse_width"] : 0.5f;
		void *mem = p_arena.alloc(sizeof(SymphonyOscillator), alignof(SymphonyOscillator));
		return new (mem) SymphonyOscillator(p_mix_rate, freq, wf, pw);
	}

private:
	// State for integrated triangle (leaky integrator of square wave)
	float tri_integrator = 0.0f;
	bool triangle_primed = false;
	static constexpr float TRIANGLE_PULL = 0.002f;
};
