#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"
#include "core/math/math_funcs.h"

// Trigger-driven float envelope generator. Outputs a FLOAT control value
// that sweeps from start_value to end_value over duration_ms when triggered.
// Operates at control rate (once per micro-block) — negligible CPU cost.
//
// Curves:
//   0 = linear
//   1 = exponential rise  (starts slow, accelerates)
//   2 = exponential decay (starts fast, decelerates)
//   3 = smooth (cosine interpolation)
//
// Use cases: pitch glide (bubble), filter cutoff sweep (acid bass),
// power-up ramp, laser zap frequency chirp.
class SymphonyEnvelopeFloat : public SymphonyOperator {
public:
	enum State { IDLE, ACTIVE };

private:
	const TriggerBuffer *SYMPHONY_RESTRICT trigger_in = nullptr;
	const float *SYMPHONY_RESTRICT target_input = nullptr;
	float *SYMPHONY_RESTRICT output = nullptr;

	float start_value = 0.0f;
	float end_value = 1.0f;
	float duration_samples = 4800.0f; // duration in samples
	int32_t curve = 0;

	State state = IDLE;
	float phase = 0.0f;       // 0..1 progress through envelope
	float phase_inc = 0.0f;   // per micro-block increment
	float current_value = 0.0f;
	bool hold_end = true;     // if true, holds end_value after completion; if false, returns to start

	float compute_curve(float p_phase) const {
		switch (curve) {
			case 0: // Linear
				return p_phase;
			case 1: // Exponential rise (slow start, fast end)
				return p_phase * p_phase;
			case 2: // Exponential decay (fast start, slow end)
				return 1.0f - (1.0f - p_phase) * (1.0f - p_phase);
			case 3: // Smooth (cosine)
				return 0.5f * (1.0f - Math::cos(p_phase * Math::PI));
			default:
				return p_phase;
		}
	}

public:
	SymphonyEnvelopeFloat(float p_start, float p_end, float p_duration_ms, int32_t p_curve, bool p_hold, float p_mix_rate)
			: start_value(p_start), end_value(p_end), curve(p_curve), hold_end(p_hold) {
		duration_samples = p_duration_ms * 0.001f * p_mix_rate;
		if (duration_samples < 1.0f) duration_samples = 1.0f;
		phase_inc = (float)SYMPHONY_MICRO_BLOCK_SIZE / duration_samples;
		current_value = p_start;
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		trigger_in = (const TriggerBuffer *)p_input_ptrs[0];
		target_input = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		// Check for trigger events this block.
		if (trigger_in && trigger_in->count > 0) {
			for (int32_t i = 0; i < trigger_in->count; i++) {
				if (trigger_in->events[i].value > 0.0f) {
					// Positive trigger: start envelope.
					state = ACTIVE;
					phase = 0.0f;
					// If target input is connected, override end_value.
					if (target_input) {
						end_value = *target_input;
					}
					// Start from current output value for smooth retriggering.
					start_value = current_value;
				}
			}
		}

		if (state == ACTIVE) {
			phase += phase_inc;
			if (phase >= 1.0f) {
				phase = 1.0f;
				state = IDLE;
				current_value = end_value;
				if (!hold_end) {
					current_value = start_value;
				}
			} else {
				float shaped = compute_curve(phase);
				current_value = start_value + (end_value - start_value) * shaped;
			}
		}
		// In IDLE: current_value holds (either end_value or start_value depending on hold_end).

		*output = current_value;
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(State) + sizeof(float) * 3; // state + phase + current_value + start_value
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		size_t offset = 0;
		memcpy(p_buffer + offset, &state, sizeof(State)); offset += sizeof(State);
		memcpy(p_buffer + offset, &phase, sizeof(float)); offset += sizeof(float);
		memcpy(p_buffer + offset, &current_value, sizeof(float)); offset += sizeof(float);
		memcpy(p_buffer + offset, &start_value, sizeof(float)); offset += sizeof(float);
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(State) + sizeof(float) * 3;
		if (p_size < needed) return;
		size_t offset = 0;
		memcpy(&state, p_buffer + offset, sizeof(State)); offset += sizeof(State);
		memcpy(&phase, p_buffer + offset, sizeof(float)); offset += sizeof(float);
		memcpy(&current_value, p_buffer + offset, sizeof(float)); offset += sizeof(float);
		memcpy(&start_value, p_buffer + offset, sizeof(float)); offset += sizeof(float);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "EnvelopeFloat";
		desc.category = "Envelopes";
		desc.inputs.push_back({ "trigger", SymphonyPinType::TRIGGER, true });
		desc.inputs.push_back({ "target", SymphonyPinType::FLOAT, false }); // Optional: override end_value at runtime
		desc.outputs.push_back({ "value", SymphonyPinType::FLOAT, false });
		desc.params.push_back({ "start_value", 0.0f, -100000.0f, 100000.0f, 0.01f });
		desc.params.push_back({ "end_value", 1.0f, -100000.0f, 100000.0f, 0.01f });
		desc.params.push_back({ "duration_ms", 100.0f, 0.1f, 30000.0f, 0.1f });
		desc.params.push_back({ "curve", 0.0f, 0.0f, 3.0f, 1.0f }); // 0=linear, 1=exp_rise, 2=exp_decay, 3=smooth
		desc.params.push_back({ "hold_end", 1.0f, 0.0f, 1.0f, 1.0f }); // 1=hold end_value, 0=return to start
		desc.state_size = sizeof(SymphonyEnvelopeFloat);
		desc.state_align = alignof(SymphonyEnvelopeFloat);
		desc.create_fn = &SymphonyEnvelopeFloat::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float sv = p_params.has("start_value") ? (float)p_params["start_value"] : 0.0f;
		float ev = p_params.has("end_value") ? (float)p_params["end_value"] : 1.0f;
		float dur = p_params.has("duration_ms") ? (float)p_params["duration_ms"] : 100.0f;
		int32_t crv = p_params.has("curve") ? (int32_t)(float)p_params["curve"] : 0;
		bool hold = p_params.has("hold_end") ? ((float)p_params["hold_end"] > 0.5f) : true;
		void *mem = p_arena.alloc(sizeof(SymphonyEnvelopeFloat), alignof(SymphonyEnvelopeFloat));
		return new (mem) SymphonyEnvelopeFloat(sv, ev, dur, crv, hold, p_mix_rate);
	}
};
