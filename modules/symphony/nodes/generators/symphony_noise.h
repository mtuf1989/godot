#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_seed.h"

#include <cstring>

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

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		struct State {
			uint32_t lcg;
			float rows[8];
			float sum;
			int32_t index;
		};
		static_assert(sizeof(State) == 44, "Noise state layout must stay tightly packed");
		if (!p_buffer) {
			return sizeof(State);
		}
		if (p_max_size < sizeof(State)) {
			return 0;
		}
		State state;
		state.lcg = seed;
		for (int i = 0; i < 8; i++) {
			state.rows[i] = pink_rows[i];
		}
		state.sum = pink_running_sum;
		state.index = pink_index;
		memcpy(p_buffer, &state, sizeof(State));
		return sizeof(State);
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		struct State {
			uint32_t lcg;
			float rows[8];
			float sum;
			int32_t index;
		};
		if (p_size < sizeof(State)) {
			return;
		}
		State state;
		memcpy(&state, p_buffer, sizeof(State));
		seed = state.lcg;
		for (int i = 0; i < 8; i++) {
			pink_rows[i] = state.rows[i];
		}
		pink_running_sum = state.sum;
		pink_index = state.index;
	}

	virtual void execute(int32_t p_num_frames) override {
		if (mode == 0) {
			for (int32_t i = 0; i < p_num_frames; i++) {
				output[i] = white_sample();
			}
		} else {
			for (int32_t i = 0; i < p_num_frames; i++) {
				// Voss-McCartney: update one row per sample based on trailing zeros
				int32_t changed = symphony_ctz(pink_index + 1) & 7;
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
		desc.params.push_back({ "seed", 0.0f, 0.0f, (float)SYMPHONY_SEED_PARAM_MAX, 1.0f });
		{
			ParamDescriptor deterministic;
			deterministic.name = "deterministic_seed";
			deterministic.default_value = 0.0f;
			deterministic.min_value = 0.0f;
			deterministic.max_value = 1.0f;
			deterministic.step = 1.0f;
			deterministic.value_type = ParamValueType::BOOL;
			desc.params.push_back(deterministic);
		}
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
		SymphonyNoise *noise = new (mem) SymphonyNoise(m);
		const SymphonyCompileContext &ctx = symphony_compile_context();
		const uint32_t authored = symphony_authored_seed(p_params, 0);
		uint32_t mixed = symphony_node_seed(ctx.voice_seed, ctx.node_id, authored, symphony_deterministic_seed(p_params));
		if (mixed == 0u) {
			mixed = 1u;
		}
		noise->seed = mixed;
		return noise;
	}
};
