#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Multi-waveform oscillator with PolyBLEP anti-aliasing.
// Waveforms: Sine(0), Saw(1), Square(2), Triangle(3)
class SymphonyOscillator : public SymphonyOperator {
private:
	const float *__restrict__ freq_input = nullptr;
	float *__restrict__ output = nullptr;
	float phase = 0.0f;
	float mix_rate = 44100.0f;
	float default_freq = 440.0f;
	int32_t waveform = 0; // 0=Sine, 1=Saw, 2=Square, 3=Triangle

	// State for integrated PolyBLEP triangle
	float tri_integrator = 0.0f;

	static inline float polyblep(float t, float dt) {
		if (t < dt) {
			float n = t / dt;
			return n + n - n * n - 1.0f;
		} else if (t > 1.0f - dt) {
			float n = (t - 1.0f) / dt;
			return n * n + n + n + 1.0f;
		}
		return 0.0f;
	}

public:
	SymphonyOscillator(float p_mix_rate, float p_default_freq, int32_t p_waveform)
			: mix_rate(p_mix_rate), default_freq(p_default_freq), waveform(p_waveform) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		freq_input = (const float *)p_input_ptrs[0];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);
		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			float freq = freq_input ? freq_input[i] : default_freq;
			float dt = freq / mix_rate;
			float sample = 0.0f;

			switch (waveform) {
				case 0: // Sine
					sample = Math::sin(phase * Math::TAU);
					break;
				case 1: { // Saw (PolyBLEP)
					sample = 2.0f * phase - 1.0f;
					sample -= polyblep(phase, dt);
				} break;
				case 2: { // Square (PolyBLEP)
					sample = (phase < 0.5f) ? 1.0f : -1.0f;
					sample += polyblep(phase, dt);
					sample -= polyblep(Math::fmod(phase + 0.5f, 1.0f), dt);
				} break;
				case 3: { // Triangle (integrated PolyBLEP square)
					float sq = (phase < 0.5f) ? 1.0f : -1.0f;
					sq += polyblep(phase, dt);
					sq -= polyblep(Math::fmod(phase + 0.5f, 1.0f), dt);
					// Leaky integrator to form triangle from square
					tri_integrator += 4.0f * dt * sq;
					tri_integrator *= 0.999f; // DC leak prevention
					sample = tri_integrator;
				} break;
			}

			output[i] = sample;

			phase += dt;
			if (phase >= 1.0f) {
				phase -= 1.0f;
			}
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
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "frequency", 440.0f, 0.0f, 20000.0f, 1.0f });
		desc.params.push_back({ "waveform", 0.0f, 0.0f, 3.0f, 1.0f }); // 0=Sine,1=Saw,2=Square,3=Triangle
		desc.state_size = sizeof(SymphonyOscillator);
		desc.state_align = alignof(SymphonyOscillator);
		desc.create_fn = &SymphonyOscillator::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float freq = p_params.has("frequency") ? (float)p_params["frequency"] : 440.0f;
		int32_t wf = p_params.has("waveform") ? (int32_t)(float)p_params["waveform"] : 0;
		void *mem = p_arena.alloc(sizeof(SymphonyOscillator), alignof(SymphonyOscillator));
		return new (mem) SymphonyOscillator(p_mix_rate, freq, wf);
	}
};
