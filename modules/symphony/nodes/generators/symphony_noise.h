#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// White and pink noise generator.
// White: linear congruential generator.
// Pink: Voss-McCartney algorithm (sum of octave-band white noise).
class SymphonyNoise : public SymphonyOperator {
private:
	float *output = nullptr;
	uint32_t seed = 1;
	int32_t mode = 0; // 0=white, 1=pink

	// Pink noise state (Voss-McCartney, 8 octaves)
	float pink_rows[8] = {};
	float pink_running_sum = 0.0f;
	int32_t pink_index = 0;

	float white_sample() {
		seed = seed * 1664525u + 1013904223u;
		return (float)(int32_t)seed / 2147483648.0f;
	}

public:
	SymphonyNoise(int32_t p_mode) : mode(p_mode) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		if (mode == 0) {
			for (int32_t i = 0; i < p_num_frames; i++) {
				output[i] = white_sample();
			}
		} else {
			for (int32_t i = 0; i < p_num_frames; i++) {
				// Voss-McCartney: update one row per sample based on trailing zeros
				int32_t changed = __builtin_ctz(pink_index + 1) & 7;
				pink_running_sum -= pink_rows[changed];
				pink_rows[changed] = white_sample();
				pink_running_sum += pink_rows[changed];
				pink_index++;
				output[i] = (pink_running_sum + white_sample()) * 0.125f;
			}
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Noise";
		desc.category = "Generators";
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "mode", 0.0f, 0.0f, 1.0f, 1.0f }); // 0=white, 1=pink
		desc.state_size = sizeof(SymphonyNoise);
		desc.state_align = alignof(SymphonyNoise);
		desc.create_fn = &SymphonyNoise::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		int32_t m = 0;
		if (p_params.has("mode")) {
			m = (int32_t)(float)p_params["mode"];
		}
		void *mem = p_arena.alloc(sizeof(SymphonyNoise), alignof(SymphonyNoise));
		return new (mem) SymphonyNoise(m);
	}
};
