#pragma once

#include "../../core/symphony_operator.h"
#include "core/math/math_funcs.h"

class SymphonyOscillator : public SymphonyOperator {
private:
	float *output_buffer = nullptr;
	float frequency = 440.0f;
	float phase = 0.0f;
	float phase_increment = 0.0f;
	float mix_rate = 44100.0f;

public:
	void initialize(float *p_output_buffer, float p_frequency, float p_mix_rate) {
		output_buffer = p_output_buffer;
		frequency = p_frequency;
		mix_rate = p_mix_rate;
		phase_increment = frequency / mix_rate;
	}

	virtual void execute(int32_t p_num_frames) override {
		for (int32_t i = 0; i < p_num_frames; i++) {
			output_buffer[i] = Math::sin(phase * Math::TAU);
			phase += phase_increment;
			if (phase >= 1.0f) {
				phase -= 1.0f;
			}
		}
	}
};
