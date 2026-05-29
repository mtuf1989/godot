#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_trigger.h"
#include "../../core/symphony_pin_types.h"

class SymphonyADSR : public SymphonyOperator {
public:
	enum State {
		IDLE,
		ATTACK,
		DECAY,
		SUSTAIN,
		RELEASE,
	};

private:
	const float *input_buffer = nullptr;
	float *output_buffer = nullptr;
	const TriggerBuffer *trigger_in = nullptr;

	float mix_rate = 44100.0f;

	// Parameters (in seconds)
	float attack_time = 0.005f;
	float decay_time = 0.01f;
	float sustain_level = 0.7f;
	float release_time = 0.02f;

	// State
	State state = IDLE;
	float envelope_value = 0.0f;

	// Per-sample increments (computed from parameters)
	float attack_inc = 0.0f;
	float decay_inc = 0.0f;
	float release_inc = 0.0f;

	void compute_rates() {
		attack_inc = (attack_time > 0.0f) ? (1.0f / (attack_time * mix_rate)) : 1.0f;
		decay_inc = (decay_time > 0.0f) ? ((1.0f - sustain_level) / (decay_time * mix_rate)) : 1.0f;
		release_inc = (release_time > 0.0f) ? (sustain_level / (release_time * mix_rate)) : 1.0f;
	}

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
			output_buffer[i] = input_buffer[i] * envelope_value;
		}
	}

	void apply_trigger(const TriggerEvent &p_event) {
		if (p_event.value > 0.0f) {
			// Note-on: start attack
			state = ATTACK;
		} else {
			// Note-off: start release
			state = RELEASE;
		}
	}

public:
	void initialize(const float *p_input, float *p_output, const TriggerBuffer *p_trigger, float p_mix_rate) {
		input_buffer = p_input;
		output_buffer = p_output;
		trigger_in = p_trigger;
		mix_rate = p_mix_rate;
		compute_rates();
	}

	virtual void execute(int32_t p_num_frames) override {
		// Block-splitting pattern: process audio in segments between triggers
		int32_t current_frame = 0;

		for (int32_t i = 0; i < trigger_in->count; i++) {
			int32_t trigger_frame = trigger_in->events[i].sample_offset;
			if (trigger_frame > current_frame) {
				process_audio(current_frame, trigger_frame);
				current_frame = trigger_frame;
			}
			apply_trigger(trigger_in->events[i]);
		}

		if (current_frame < p_num_frames) {
			process_audio(current_frame, p_num_frames);
		}
	}

	State get_state() const { return state; }
	float get_envelope_value() const { return envelope_value; }
};
