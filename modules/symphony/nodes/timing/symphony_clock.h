#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"
#include "core/math/math_funcs.h"

#include <cmath>
#include <cstring>

// Clock: periodic triggers at a BPM with optional subdivision.
//
// P7/N11: first tick at sample 0; FLOAT bpm input (block-smoothed, phase-continuous);
// TRIGGER reset; FLOAT phase output in [0, 1) within the current tick period.
class SymphonyClock : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT bpm_input = nullptr;
	const TriggerBuffer *SYMPHONY_RESTRICT reset_input = nullptr;
	TriggerBuffer *SYMPHONY_RESTRICT trigger_out = nullptr;
	float *SYMPHONY_RESTRICT phase_out = nullptr;

	float mix_rate = 48000.0f;
	float default_bpm = 120.0f;
	float subdivision = 1.0f;
	float smoothed_bpm = 120.0f;
	float samples_per_tick = 0.0f;
	float counter = 0.0f; // countdown; <= 0 → fire
	int32_t tick_index = 0;
	float bpm_smooth = 1.0f; // per-block one-pole toward bpm input

	void _set_interval_preserving_phase(float p_new_spt) {
		if (!(p_new_spt > 1.0f)) {
			p_new_spt = 1.0f;
		}
		float phase = 0.0f;
		if (samples_per_tick > 1.0f) {
			phase = 1.0f - (counter / samples_per_tick);
			phase = CLAMP(phase, 0.0f, 1.0f);
		}
		samples_per_tick = p_new_spt;
		counter = (1.0f - phase) * samples_per_tick;
	}

	void _apply_bpm(float p_bpm, bool p_snap) {
		p_bpm = CLAMP(p_bpm, 1.0f, 999.0f);
		if (p_snap) {
			smoothed_bpm = p_bpm;
		} else {
			smoothed_bpm += (p_bpm - smoothed_bpm) * bpm_smooth;
		}
		const float sub = (subdivision > 0.0f) ? subdivision : 1.0f;
		const float new_spt = (60.0f / (smoothed_bpm * sub)) * mix_rate;
		if (p_snap) {
			samples_per_tick = new_spt > 1.0f ? new_spt : 1.0f;
		} else {
			_set_interval_preserving_phase(new_spt);
		}
	}

public:
	SymphonyClock(float p_bpm, float p_subdivision, float p_mix_rate)
			: mix_rate(p_mix_rate > 1.0f ? p_mix_rate : 48000.0f),
			  default_bpm(p_bpm),
			  subdivision(p_subdivision > 0.0f ? p_subdivision : 1.0f) {
		// ~5 ms block smoothing for bpm changes.
		const float smooth_n = 0.005f * mix_rate / (float)SYMPHONY_MICRO_BLOCK_SIZE;
		bpm_smooth = (smooth_n > 1.0f) ? (1.0f - exp2f(-1.0f / smooth_n)) : 1.0f;
		_apply_bpm(p_bpm, true);
		// First tick at sample 0 (counter already 0).
		counter = 0.0f;
		tick_index = 0;
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		bpm_input = (const float *)p_input_ptrs[0];
		reset_input = (const TriggerBuffer *)p_input_ptrs[1];
		trigger_out = (TriggerBuffer *)p_output_ptrs[0];
		phase_out = (float *)p_output_ptrs[1];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);
		if (trigger_out) {
			trigger_out->clear();
		}

		if (reset_input && reset_input->count > 0) {
			counter = 0.0f;
			tick_index = 0;
		}

		const float target_bpm = bpm_input ? *bpm_input : default_bpm;
		_apply_bpm(target_bpm, false);

		const int32_t sub_int = MAX(1, (int32_t)subdivision);
		// P9 state-in-locals
		float l_counter = counter;
		int32_t l_tick = tick_index;
		const float spt = samples_per_tick;
		for (int32_t i = 0; i < p_num_frames; i++) {
			if (l_counter <= 0.0f) {
				float value = 1.0f;
				if (sub_int > 1 && l_tick != 0) {
					value = (float)l_tick / subdivision;
				}
				if (trigger_out && !trigger_out->push(i, value)) {
					symphony_note_dropped_trigger();
				}
				l_tick++;
				if (l_tick >= sub_int) {
					l_tick = 0;
				}
				l_counter += spt;
			}
			if (phase_out) {
				const float phase = (spt > 1.0f)
						? CLAMP(1.0f - (l_counter / spt), 0.0f, 1.0f)
						: 0.0f;
				phase_out[i] = phase;
			}
			l_counter -= 1.0f;
		}
		counter = l_counter;
		tick_index = l_tick;
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(float) * 3 + sizeof(int32_t); // counter, smoothed_bpm, samples_per_tick, tick_index
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		size_t o = 0;
		memcpy(p_buffer + o, &counter, sizeof(float));
		o += sizeof(float);
		memcpy(p_buffer + o, &smoothed_bpm, sizeof(float));
		o += sizeof(float);
		memcpy(p_buffer + o, &samples_per_tick, sizeof(float));
		o += sizeof(float);
		memcpy(p_buffer + o, &tick_index, sizeof(int32_t));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(float) * 3 + sizeof(int32_t);
		if (p_size < needed) {
			return;
		}
		size_t o = 0;
		memcpy(&counter, p_buffer + o, sizeof(float));
		o += sizeof(float);
		memcpy(&smoothed_bpm, p_buffer + o, sizeof(float));
		o += sizeof(float);
		memcpy(&samples_per_tick, p_buffer + o, sizeof(float));
		o += sizeof(float);
		memcpy(&tick_index, p_buffer + o, sizeof(int32_t));
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Clock";
		desc.category = "Timing";
		desc.inputs.push_back({ "bpm", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "reset", SymphonyPinType::TRIGGER, false });
		desc.outputs.push_back({ "trigger", SymphonyPinType::TRIGGER, false });
		desc.outputs.push_back({ "phase", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "bpm", 120.0f, 1.0f, 999.0f, 0.1f });
		desc.params.push_back({ "subdivision", 1.0f, 0.25f, 16.0f, 0.25f });
		desc.state_size = sizeof(SymphonyClock);
		desc.state_align = alignof(SymphonyClock);
		desc.cost_per_sample = 2.0f;
		desc.create_fn = &SymphonyClock::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float bpm = 120.0f;
		float sub = 1.0f;
		if (p_params.has("bpm")) {
			bpm = p_params["bpm"];
		}
		if (p_params.has("subdivision")) {
			sub = p_params["subdivision"];
		}
		void *mem = p_arena.alloc(sizeof(SymphonyClock), alignof(SymphonyClock));
		if (!mem) {
			return nullptr;
		}
		return new (mem) SymphonyClock(bpm, sub, p_mix_rate);
	}
};
