#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_denormal_guard.h"
#include "core/math/math_funcs.h"

// DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
// R = exp(−2π·15/sr) so the −3 dB point tracks ~15 Hz across sample rates.
class SymphonyDCBlocker : public SymphonyOperator {
private:
	const float *input = nullptr;
	float *output = nullptr;
	float x_prev = 0.0f;
	float y_prev = 0.0f;
	float R = 0.995f;

public:
	explicit SymphonyDCBlocker(float p_mix_rate) {
		float sr = MAX(p_mix_rate, 1.0f);
		R = expf(-Math::TAU * 15.0f / sr);
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		float r = R;
		float xp = x_prev;
		float yp = y_prev;
		for (int32_t i = 0; i < p_num_frames; i++) {
			float x = input[i];
			float y = x - xp + r * yp;
			xp = x;
			yp = y;
			output[i] = y;
		}
		x_prev = xp;
		y_prev = yp;
		if (Math::abs(x_prev) + Math::abs(y_prev) < 1e-9f) {
			x_prev = 0.0f;
			y_prev = 0.0f;
		} else {
			symphony_flush_denormal(x_prev);
			symphony_flush_denormal(y_prev);
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(float) * 2;
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		memcpy(p_buffer, &x_prev, sizeof(float));
		memcpy(p_buffer + sizeof(float), &y_prev, sizeof(float));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float) * 2;
		if (p_size >= needed) {
			memcpy(&x_prev, p_buffer, sizeof(float));
			memcpy(&y_prev, p_buffer + sizeof(float), sizeof(float));
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "DCBlocker";
		desc.category = "Filters";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.state_size = sizeof(SymphonyDCBlocker);
		desc.state_align = alignof(SymphonyDCBlocker);
		desc.create_fn = &SymphonyDCBlocker::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_params;
		void *mem = p_arena.alloc(sizeof(SymphonyDCBlocker), alignof(SymphonyDCBlocker));
		return new (mem) SymphonyDCBlocker(p_mix_rate);
	}
};
