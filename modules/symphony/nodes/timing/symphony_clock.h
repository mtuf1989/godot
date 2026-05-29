#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"

// Clock: generates periodic triggers at a given BPM.
class SymphonyClock : public SymphonyOperator {
private:
	TriggerBuffer *output = nullptr;
	float samples_per_beat = 0.0f;
	float counter = 0.0f;

public:
	SymphonyClock(float p_bpm, float p_mix_rate) {
		samples_per_beat = (60.0f / p_bpm) * p_mix_rate;
		counter = 0.0f;
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		output = (TriggerBuffer *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		for (int32_t i = 0; i < p_num_frames; i++) {
			counter += 1.0f;
			if (counter >= samples_per_beat) {
				counter -= samples_per_beat;
				output->push(i, 1.0f);
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		if (!p_buffer) return sizeof(float);
		if (p_max_size >= sizeof(float)) memcpy(p_buffer, &counter, sizeof(float));
		return sizeof(float);
	}
	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		if (p_size >= sizeof(float)) memcpy(&counter, p_buffer, sizeof(float));
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Clock";
		desc.category = "Timing";
		desc.outputs.push_back({ "trigger", SymphonyPinType::TRIGGER, false });
		desc.params.push_back({ "bpm", 120.0f, 1.0f, 999.0f, 0.1f });
		desc.state_size = sizeof(SymphonyClock);
		desc.state_align = alignof(SymphonyClock);
		desc.create_fn = &SymphonyClock::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float bpm = 120.0f;
		if (p_params.has("bpm")) bpm = p_params["bpm"];
		void *mem = p_arena.alloc(sizeof(SymphonyClock), alignof(SymphonyClock));
		return new (mem) SymphonyClock(bpm, p_mix_rate);
	}
};
