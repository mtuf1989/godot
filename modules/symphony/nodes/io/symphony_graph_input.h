#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/templates/safe_refcount.h"

// Exposes a named parameter to the game thread (GDScript API).
// Game thread writes via SafeNumeric; audio thread reads each micro-block.
// Outputs a FLOAT pin (control-rate single value).
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

	// Called from game thread via set_parameter routing.
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
		// No inputs — this is a source node fed from the game thread.
		desc.outputs.push_back({ "output", SymphonyPinType::FLOAT, false });
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
