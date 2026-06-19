#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// MapRange: remaps a float value from [in_min, in_max] to [out_min, out_max].
class SymphonyMapRange : public SymphonyOperator {
private:
	const float *input = nullptr;
	float *output = nullptr;
	float in_min, in_max, out_min, out_max;
	bool do_clamp;

public:
	SymphonyMapRange(float p_in_min, float p_in_max, float p_out_min, float p_out_max, bool p_clamp)
			: in_min(p_in_min), in_max(p_in_max), out_min(p_out_min), out_max(p_out_max), do_clamp(p_clamp) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		float val = input ? input[0] : 0.0f;
		float range_in = in_max - in_min;
		float t = (range_in != 0.0f) ? (val - in_min) / range_in : 0.0f;
		if (do_clamp) t = CLAMP(t, 0.0f, 1.0f);
		output[0] = out_min + t * (out_max - out_min);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "MapRange";
		desc.category = "Math";
		desc.inputs.push_back({ "input", SymphonyPinType::FLOAT, true });
		desc.outputs.push_back({ "output", SymphonyPinType::FLOAT, false });
		desc.params.push_back({ "in_min", 0.0f, -10000.0f, 10000.0f, 0.01f });
		desc.params.push_back({ "in_max", 1.0f, -10000.0f, 10000.0f, 0.01f });
		desc.params.push_back({ "out_min", 0.0f, -10000.0f, 10000.0f, 0.01f });
		desc.params.push_back({ "out_max", 1.0f, -10000.0f, 10000.0f, 0.01f });
		desc.params.push_back({ "clamp", 1.0f, 0.0f, 1.0f, 1.0f });
		desc.state_size = sizeof(SymphonyMapRange);
		desc.state_align = alignof(SymphonyMapRange);
		desc.create_fn = &SymphonyMapRange::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float imin = 0, imax = 1, omin = 0, omax = 1;
		bool cl = true;
		if (p_params.has("in_min")) imin = p_params["in_min"];
		if (p_params.has("in_max")) imax = p_params["in_max"];
		if (p_params.has("out_min")) omin = p_params["out_min"];
		if (p_params.has("out_max")) omax = p_params["out_max"];
		if (p_params.has("clamp")) cl = (float)p_params["clamp"] > 0.5f;
		void *mem = p_arena.alloc(sizeof(SymphonyMapRange), alignof(SymphonyMapRange));
		return new (mem) SymphonyMapRange(imin, imax, omin, omax, cl);
	}
};
