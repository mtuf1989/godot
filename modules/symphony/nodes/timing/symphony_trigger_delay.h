#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"

// TriggerDelay: delays incoming triggers by a configurable number of milliseconds.
// Uses a simple ring buffer of pending trigger times.
class SymphonyTriggerDelay : public SymphonyOperator {
private:
	static constexpr int32_t MAX_PENDING = 32;

	const TriggerBuffer *input = nullptr;
	TriggerBuffer *output = nullptr;

	int32_t delay_samples = 0;

	struct PendingTrigger {
		int32_t remaining_samples;
		float value;
	};
	PendingTrigger pending[MAX_PENDING] = {};
	int32_t pending_count = 0;

public:
	SymphonyTriggerDelay(float p_delay_ms, float p_mix_rate) {
		delay_samples = (int32_t)(p_delay_ms * 0.001f * p_mix_rate);
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const TriggerBuffer *)p_input_ptrs[0];
		output = (TriggerBuffer *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		// Enqueue new triggers
		if (input) {
			for (int32_t i = 0; i < input->count && pending_count < MAX_PENDING; i++) {
				pending[pending_count].remaining_samples = delay_samples + input->events[i].sample_offset;
				pending[pending_count].value = input->events[i].value;
				pending_count++;
			}
		}

		// Process pending triggers
		for (int32_t i = pending_count - 1; i >= 0; i--) {
			pending[i].remaining_samples -= p_num_frames;
			if (pending[i].remaining_samples <= 0) {
				int32_t offset = p_num_frames + pending[i].remaining_samples;
				if (offset < 0) offset = 0;
				output->push(offset, pending[i].value);
				// Remove by swapping with last
				pending[i] = pending[pending_count - 1];
				pending_count--;
			}
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "TriggerDelay";
		desc.category = "Timing";
		desc.inputs.push_back({ "trigger", SymphonyPinType::TRIGGER, true });
		desc.outputs.push_back({ "trigger", SymphonyPinType::TRIGGER, false });
		desc.params.push_back({ "delay_ms", 100.0f, 0.0f, 5000.0f, 1.0f });
		desc.state_size = sizeof(SymphonyTriggerDelay);
		desc.state_align = alignof(SymphonyTriggerDelay);
		desc.create_fn = &SymphonyTriggerDelay::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float d = 100.0f;
		if (p_params.has("delay_ms")) d = p_params["delay_ms"];
		void *mem = p_arena.alloc(sizeof(SymphonyTriggerDelay), alignof(SymphonyTriggerDelay));
		return new (mem) SymphonyTriggerDelay(d, p_mix_rate);
	}
};
