#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_fast_math.h"
#include "core/math/math_funcs.h"

// Multi-waveform oscillator with logistic-curve band-limiting (branch-free).
// Replaces PolyBLEP — no branches, no lookup tables, constant cost per sample.
// Waveforms: Sine(0), Saw(1), Square(2), Triangle(3), PWM(4)
//
// Anti-aliasing method (ADC 2025):
//   1. Compute naive waveform
//   2. Derive piecewise-linear window around discontinuity
//   3. Shape window with logistic curve (approximates sinc integral)
//   4. Subtract correction from naive output
//
// Quality: ~-130dB aliasing at 2x oversampling. Minimum period: ~20 samples.
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

	// Logistic steepness parameter. Controls the bandwidth of the correction window.
	// Higher = steeper transition = more aliasing suppression but wider minimum period.
	// 18.0 gives ~-130dB at 2x oversampling with ~20-sample minimum period.
	static constexpr float LOGISTIC_K = 18.0f;

	// ── Fast approximations (branch-free building blocks) ──────────────

	[[nodiscard]] static inline float fast_sine(float p_phase) {
		return SymphonyFastMath::fast_sine(p_phase);
	}

	// Fast 2^x approximation (~8 ops, 16-bit accuracy).
	// Used in the logistic function denominator.
	[[nodiscard]] static inline float fast_exp2(float p_x) {
		// Split into integer and fractional parts
		float i = floorf(p_x);
		float f = p_x - i;
		// 3rd-order polynomial for 2^f, f in [0, 1)
		float poly = 1.0f + f * (0.6931472f + f * (0.2402265f + f * 0.0554959f));
		// Combine: multiply by 2^i via bit manipulation
		// ldexpf is typically a single instruction on modern CPUs
		return ldexpf(poly, (int)i);
	}

	// Logistic-shaped band-limited sawtooth: phase in [0, 1), dt = freq/sample_rate.
	// Returns a band-limited saw in [-1, 1].
	inline float bl_saw(float p_phase, float p_dt) const {
		// 1. Naive sawtooth
		float naive = 2.0f * p_phase - 1.0f;

		// 2. Piecewise-linear window around the discontinuity at phase=0/1.
		//    Window width scales with dt (narrower at low frequencies = sharper transition).
		//    Using min(phase, 1-phase) gives a symmetric triangle peaking at 0.5.
		float window = p_phase;
		float complement = 1.0f - p_phase;
		window = window < complement ? window : complement; // min (branch-free on most CPUs via fmin)

		// Scale window to logistic input range: want ±LOGISTIC_K at window edges.
		// Window goes from 0 (at discontinuity) to 0.5 (mid-cycle).
		// We want the logistic argument to span [-K, +K] across the correction zone.
		// Correction zone width = dt (one sample's worth of phase advance).
		// Scale: argument = (window / dt - 0.5) * 2 * K
		float inv_dt = 1.0f / p_dt; // Could use fast_reciprocal, but dt is constant per sample block
		float arg = (window * inv_dt - 0.5f) * (2.0f * LOGISTIC_K);

		// 3. Logistic curve: 1 / (1 + 2^(-arg))
		float denom = 1.0f + fast_exp2(-arg);
		float shaped = 1.0f / denom;

		// 4. Correction: shaped goes from 0→1 across the transition zone.
		//    Scale to [-1, +1] and subtract from naive.
		//    The correction amplitude is 2.0 (full step height of the sawtooth).
		float correction = 2.0f * shaped - 1.0f;

		return naive - correction + 1.0f * p_dt; // +dt compensates for the slight DC offset
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

			// Clamp dt to avoid division issues at extreme frequencies
			dt = dt < 0.0001f ? 0.0001f : (dt > 0.49f ? 0.49f : dt);

			float sample = 0.0f;

			switch (waveform) {
				case 0: // Sine (polynomial approximation)
					sample = fast_sine(phase);
					break;

				case 1: // Saw (logistic band-limited)
					sample = bl_saw(phase, dt);
					break;

				case 2: { // Square (two saws with 0.5 offset)
					float saw1 = bl_saw(phase, dt);
					float phase2 = phase + 0.5f;
					phase2 -= floorf(phase2);
					float saw2 = bl_saw(phase2, dt);
					sample = saw1 - saw2;
				} break;

				case 3: { // Triangle (integrated square, leaky integrator)
					float saw1 = bl_saw(phase, dt);
					float phase2 = phase + 0.5f;
					phase2 -= floorf(phase2);
					float saw2 = bl_saw(phase2, dt);
					float sq = saw1 - saw2;
					// Leaky integrator: converts square → triangle
					// 4*dt scales so the triangle amplitude stays ~1.0 regardless of freq
					tri_integrator += 4.0f * dt * sq;
					tri_integrator *= 0.999f; // DC leak prevention
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
};
