#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Simple one-pole low-pass filter (exponential smoothing).
// y[n] = (1-a)*x[n] + a*y[n-1], where a = exp(-TAU * cutoff / sample_rate)
class SymphonyOnePole : public SymphonyOperator {
private:
	const float *input = nullptr;
	float *output = nullptr;
	float prev = 0.0f;
	float coeff = 0.0f; // 'a' coefficient

public:
	SymphonyOnePole(float p_cutoff, float p_mix_rate) {
		coeff = expf(-Math::TAU * p_cutoff / p_mix_rate);
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		for (int32_t i = 0; i < p_num_frames; i++) {
			prev = (1.0f - coeff) * input[i] + coeff * prev;
			output[i] = prev;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		if (!p_buffer) return sizeof(float);
		if (p_max_size >= sizeof(float)) memcpy(p_buffer, &prev, sizeof(float));
		return sizeof(float);
	}
	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		if (p_size >= sizeof(float)) memcpy(&prev, p_buffer, sizeof(float));
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "OnePole";
		desc.category = "Filters";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "cutoff", 100.0f, 1.0f, 20000.0f, 1.0f });
		desc.state_size = sizeof(SymphonyOnePole);
		desc.state_align = alignof(SymphonyOnePole);
		desc.create_fn = &SymphonyOnePole::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float c = 100.0f;
		if (p_params.has("cutoff")) c = p_params["cutoff"];
		void *mem = p_arena.alloc(sizeof(SymphonyOnePole), alignof(SymphonyOnePole));
		return new (mem) SymphonyOnePole(c, p_mix_rate);
	}
};
