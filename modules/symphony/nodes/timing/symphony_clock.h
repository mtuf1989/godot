#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"

// Clock: generates periodic triggers at a given BPM with configurable subdivision.
//
// Subdivision controls how many triggers fire per beat:
//   1.0 = quarter notes (one trigger per beat)
//   2.0 = 8th notes (two triggers per beat)
//   4.0 = 16th notes (four triggers per beat)
//   3.0 = triplets (three triggers per beat)
//   0.5 = half notes (one trigger every two beats)
//
// The trigger value encodes the beat position:
//   value = 1.0 on downbeats (subdivision index 0)
//   value = subdivision_index / subdivision for off-beats (e.g., 0.25, 0.5, 0.75 for sub=4)
//   This lets downstream nodes (ADSR, GrainCloud) differentiate strong vs weak beats.
class SymphonyClock : public SymphonyOperator {
private:
	TriggerBuffer *output = nullptr;
	float samples_per_tick = 0.0f;  // Interval between subdivided triggers
	float subdivision = 1.0f;       // Ticks per beat
	float counter = 0.0f;
	int32_t tick_index = 0;         // Current subdivision index within the beat (0..subdivision-1)

public:
	SymphonyClock(float p_bpm, float p_subdivision, float p_mix_rate) {
		subdivision = (p_subdivision > 0.0f) ? p_subdivision : 1.0f;
		samples_per_tick = (60.0f / (p_bpm * subdivision)) * p_mix_rate;
		counter = 0.0f;
		tick_index = 0;
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		output = (TriggerBuffer *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		int32_t sub_int = (int32_t)subdivision;
		for (int32_t i = 0; i < p_num_frames; i++) {
			counter += 1.0f;
			if (counter >= samples_per_tick) {
				counter -= samples_per_tick;
				// Encode beat strength: 1.0 on downbeat, fractional on off-beats.
				float value;
				if (sub_int <= 1 || tick_index == 0) {
					value = 1.0f;
				} else {
					value = (float)tick_index / subdivision;
				}
				output->push(i, value);
				tick_index++;
				if (tick_index >= sub_int) {
					tick_index = 0;
				}
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(float) + sizeof(int32_t);
		if (!p_buffer) return needed;
		if (p_max_size >= needed) {
			memcpy(p_buffer, &counter, sizeof(float));
			memcpy(p_buffer + sizeof(float), &tick_index, sizeof(int32_t));
		}
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float) + sizeof(int32_t);
		if (p_size >= needed) {
			memcpy(&counter, p_buffer, sizeof(float));
			memcpy(&tick_index, p_buffer + sizeof(float), sizeof(int32_t));
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Clock";
		desc.category = "Timing";
		desc.outputs.push_back({ "trigger", SymphonyPinType::TRIGGER, false });
		desc.params.push_back({ "bpm", 120.0f, 1.0f, 999.0f, 0.1f });
		desc.params.push_back({ "subdivision", 1.0f, 0.25f, 16.0f, 0.25f });
		desc.state_size = sizeof(SymphonyClock);
		desc.state_align = alignof(SymphonyClock);
		desc.create_fn = &SymphonyClock::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float bpm = 120.0f;
		float sub = 1.0f;
		if (p_params.has("bpm")) bpm = p_params["bpm"];
		if (p_params.has("subdivision")) sub = p_params["subdivision"];
		void *mem = p_arena.alloc(sizeof(SymphonyClock), alignof(SymphonyClock));
		return new (mem) SymphonyClock(bpm, sub, p_mix_rate);
	}
};
