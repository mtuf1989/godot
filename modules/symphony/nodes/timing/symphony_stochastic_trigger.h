#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"

// Poisson-like random trigger emitter. Fires trigger impulses at a rate
// controlled by density (events/sec). Essential for rain, fire crackle, insects.
class SymphonyStochasticTrigger : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT density_input = nullptr;
	TriggerBuffer *SYMPHONY_RESTRICT trigger_out = nullptr;

	float mix_rate = 44100.0f;
	float default_density = 5.0f;
	uint32_t rng_state = 1;

	// xorshift32 PRNG — fast, good enough for audio randomness
	inline uint32_t xorshift32() {
		rng_state ^= rng_state << 13;
		rng_state ^= rng_state >> 17;
		rng_state ^= rng_state << 5;
		return rng_state;
	}

	inline float rand_uniform() {
		return (float)(xorshift32() & 0x7FFFFFFF) / (float)0x7FFFFFFF;
	}

public:
	SymphonyStochasticTrigger(float p_mix_rate, float p_density, uint32_t p_seed)
			: mix_rate(p_mix_rate), default_density(p_density), rng_state(p_seed ? p_seed : 1) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		density_input = (const float *)p_input_ptrs[0];
		trigger_out = (TriggerBuffer *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);
		trigger_out->clear();

		float density = density_input ? *density_input : default_density;
		if (density <= 0.0f) return;

		float threshold = density / mix_rate;

		for (int32_t i = 0; i < p_num_frames; i++) {
			if (rand_uniform() < threshold) {
				if (!trigger_out->push(i, 1.0f)) {
					symphony_note_dropped_trigger();
				}
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		if (!p_buffer) return sizeof(uint32_t);
		if (p_max_size < sizeof(uint32_t)) return 0;
		memcpy(p_buffer, &rng_state, sizeof(uint32_t));
		return sizeof(uint32_t);
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		if (p_size >= sizeof(uint32_t)) {
			memcpy(&rng_state, p_buffer, sizeof(uint32_t));
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "StochasticTrigger";
		desc.category = "Timing";
		desc.inputs.push_back({ "density", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "trigger_out", SymphonyPinType::TRIGGER, false });
		desc.params.push_back({ "density", 5.0f, 0.0f, 1000.0f, 0.1f });
		desc.params.push_back({ "seed", 0.0f, 0.0f, 4294967295.0f, 1.0f });
		desc.state_size = sizeof(SymphonyStochasticTrigger);
		desc.state_align = alignof(SymphonyStochasticTrigger);
		desc.create_fn = &SymphonyStochasticTrigger::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float density = p_params.has("density") ? (float)p_params["density"] : 5.0f;
		uint32_t seed = p_params.has("seed") ? (uint32_t)(float)p_params["seed"] : 0;
		// If seed is 0, use a pseudo-random seed from the arena offset (unique per voice)
		if (seed == 0) {
			seed = (uint32_t)(uintptr_t)&p_arena ^ 0xDEADBEEF;
			seed = seed * 1664525u + 1013904223u; // LCG step for variety
		}
		void *mem = p_arena.alloc(sizeof(SymphonyStochasticTrigger), alignof(SymphonyStochasticTrigger));
		return new (mem) SymphonyStochasticTrigger(p_mix_rate, density, seed);
	}
};
