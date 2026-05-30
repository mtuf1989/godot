#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/templates/safe_refcount.h"

// Exposes a named parameter to the game thread (GDScript API).
// Game thread writes via SafeNumeric; audio thread reads each micro-block.
// pin_type param controls the output type (only FLOAT functional in Slice 1).
// sort_order + display_name control how this appears as a pin on a SubGraph node.
class SymphonyGraphInput : public SymphonyOperator {
private:
	float *output = nullptr;
	SafeNumeric<float> value;
	float default_value = 0.0f;

public:
	SymphonyGraphInput(float p_default) : default_value(p_default) {
		value.set(p_default);
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		float v = value.get();
		output[0] = v;
	}

	void set_value(float p_value) {
		value.set(p_value);
	}

	float get_value() const {
		return value.get();
	}

	float get_default_value() const {
		return default_value;
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "GraphInput";
		desc.category = "I/O";
		desc.outputs.push_back({ "output", SymphonyPinType::FLOAT, false });
		desc.params.push_back({ "parameter_name", 0.0f, 0.0f, 0.0f, 0.0f }); // String stored via node params
		desc.params.push_back({ "default_value", 0.0f, -10000.0f, 10000.0f, 0.01f });
		desc.params.push_back({ "pin_type", 0.0f, 0.0f, 4.0f, 1.0f }); // 0=AUDIO,1=FLOAT,2=INT,3=BOOL,4=TRIGGER
		desc.params.push_back({ "sort_order", 0.0f, -1000.0f, 1000.0f, 1.0f });
		desc.params.push_back({ "display_name", 0.0f, 0.0f, 0.0f, 0.0f }); // String stored via node params
		desc.state_size = sizeof(SymphonyGraphInput);
		desc.state_align = alignof(SymphonyGraphInput);
		desc.create_fn = &SymphonyGraphInput::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float def = 0.0f;
		if (p_params.has("default_value")) {
			def = p_params["default_value"];
		}
		void *mem = p_arena.alloc(sizeof(SymphonyGraphInput), alignof(SymphonyGraphInput));
		return new (mem) SymphonyGraphInput(def);
	}
};
