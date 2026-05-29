#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"

// ADSR envelope that multiplies input signal by envelope value.
// Uses block-splitting pattern for sample-accurate trigger response.
class SymphonyADSR : public SymphonyOperator {
public:
	enum State { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };

private:
	const float *input = nullptr;
	float *output = nullptr;
	const TriggerBuffer *trigger_in = nullptr;

	float attack_inc = 0.0f;
	float decay_inc = 0.0f;
	float release_inc = 0.0f;
	float sustain_level = 0.7f;

	State state = IDLE;
	float envelope_value = 0.0f;

	void process_audio(int32_t p_from, int32_t p_to) {
		for (int32_t i = p_from; i < p_to; i++) {
			switch (state) {
				case IDLE:
					envelope_value = 0.0f;
					break;
				case ATTACK:
					envelope_value += attack_inc;
					if (envelope_value >= 1.0f) {
						envelope_value = 1.0f;
						state = DECAY;
					}
					break;
				case DECAY:
					envelope_value -= decay_inc;
					if (envelope_value <= sustain_level) {
						envelope_value = sustain_level;
						state = SUSTAIN;
					}
					break;
				case SUSTAIN:
					break;
				case RELEASE:
					envelope_value -= release_inc;
					if (envelope_value <= 0.0f) {
						envelope_value = 0.0f;
						state = IDLE;
					}
					break;
			}
			output[i] = input[i] * envelope_value;
		}
	}

	void apply_trigger(const TriggerEvent &p_event) {
		if (p_event.value > 0.0f) {
			state = ATTACK;
		} else {
			state = RELEASE;
		}
	}

public:
	SymphonyADSR(float p_mix_rate, float p_attack, float p_decay, float p_sustain, float p_release) {
		sustain_level = p_sustain;
		attack_inc = (p_attack > 0.0f) ? (1.0f / (p_attack * p_mix_rate)) : 1.0f;
		decay_inc = (p_decay > 0.0f) ? ((1.0f - sustain_level) / (p_decay * p_mix_rate)) : 1.0f;
		release_inc = (p_release > 0.0f) ? (sustain_level / (p_release * p_mix_rate)) : 1.0f;
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
		trigger_in = (const TriggerBuffer *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		int32_t current_frame = 0;
		if (trigger_in) {
			for (int32_t i = 0; i < trigger_in->count; i++) {
				int32_t trigger_frame = trigger_in->events[i].sample_offset;
				if (trigger_frame > current_frame) {
					process_audio(current_frame, trigger_frame);
					current_frame = trigger_frame;
				}
				apply_trigger(trigger_in->events[i]);
			}
		}
		if (current_frame < p_num_frames) {
			process_audio(current_frame, p_num_frames);
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(State) + sizeof(float);
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size >= needed) {
			memcpy(p_buffer, &state, sizeof(State));
			memcpy(p_buffer + sizeof(State), &envelope_value, sizeof(float));
		}
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(State) + sizeof(float);
		if (p_size >= needed) {
			memcpy(&state, p_buffer, sizeof(State));
			memcpy(&envelope_value, p_buffer + sizeof(State), sizeof(float));
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "ADSR";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "trigger", SymphonyPinType::TRIGGER, false }); // Optional
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "attack", 0.005f, 0.0f, 10.0f, 0.001f });
		desc.params.push_back({ "decay", 0.01f, 0.0f, 10.0f, 0.001f });
		desc.params.push_back({ "sustain", 0.7f, 0.0f, 1.0f, 0.01f });
		desc.params.push_back({ "release", 0.02f, 0.0f, 10.0f, 0.001f });
		desc.state_size = sizeof(SymphonyADSR);
		desc.state_align = alignof(SymphonyADSR);
		desc.create_fn = &SymphonyADSR::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float a = 0.005f, d = 0.01f, s = 0.7f, r = 0.02f;
		if (p_params.has("attack")) a = p_params["attack"];
		if (p_params.has("decay")) d = p_params["decay"];
		if (p_params.has("sustain")) s = p_params["sustain"];
		if (p_params.has("release")) r = p_params["release"];
		void *mem = p_arena.alloc(sizeof(SymphonyADSR), alignof(SymphonyADSR));
		return new (mem) SymphonyADSR(p_mix_rate, a, d, s, r);
	}
};
