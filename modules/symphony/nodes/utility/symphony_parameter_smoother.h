#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// One-pole smoothing filter on a Float input.
// Eliminates clicks when RTPC parameters change at 60Hz game rate.
// Operates at control rate (once per micro-block), not per sample.
class SymphonyParameterSmoother : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT value_input = nullptr;
	const float *SYMPHONY_RESTRICT time_input = nullptr; // optional smooth_time_ms override
	float *SYMPHONY_RESTRICT smoothed_output = nullptr;

	float smoothed = 0.0f;
	float default_smooth_ms = 5.0f;
	float mix_rate = 44100.0f;
	bool initialized = false;

public:
	SymphonyParameterSmoother(float p_mix_rate, float p_smooth_ms)
			: default_smooth_ms(p_smooth_ms), mix_rate(p_mix_rate) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		value_input = (const float *)p_input_ptrs[0];
		time_input = (const float *)p_input_ptrs[1];
		smoothed_output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		float target = value_input ? *value_input : 0.0f;

		if (!initialized) {
			smoothed = target;
			initialized = true;
		} else {
			float smooth_ms = time_input ? *time_input : default_smooth_ms;
			if (smooth_ms < 0.01f) {
				smoothed = target;
			} else {
				float block_duration = (float)p_num_frames / mix_rate;
				float tau = smooth_ms * 0.001f;
				float coeff = 1.0f - Math::exp(-block_duration / tau);
				smoothed += coeff * (target - smoothed);
			}
		}

		*smoothed_output = smoothed;
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(float) + sizeof(bool);
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		memcpy(p_buffer, &smoothed, sizeof(float));
		memcpy(p_buffer + sizeof(float), &initialized, sizeof(bool));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float) + sizeof(bool);
		if (p_size < needed) return;
		memcpy(&smoothed, p_buffer, sizeof(float));
		memcpy(&initialized, p_buffer + sizeof(float), sizeof(bool));
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "ParameterSmoother";
		desc.category = "Utility";
		desc.inputs.push_back({ "value", SymphonyPinType::FLOAT, true });
		desc.inputs.push_back({ "smooth_time_ms", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "smoothed", SymphonyPinType::FLOAT, false });
		desc.params.push_back({ "smooth_time_ms", 5.0f, 0.0f, 1000.0f, 0.1f });
		desc.state_size = sizeof(SymphonyParameterSmoother);
		desc.state_align = alignof(SymphonyParameterSmoother);
		desc.create_fn = &SymphonyParameterSmoother::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float ms = p_params.has("smooth_time_ms") ? (float)p_params["smooth_time_ms"] : 5.0f;
		void *mem = p_arena.alloc(sizeof(SymphonyParameterSmoother), alignof(SymphonyParameterSmoother));
		return new (mem) SymphonyParameterSmoother(p_mix_rate, ms);
	}
};
