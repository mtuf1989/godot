#pragma once

#include "../../core/symphony_operator.h"

class SymphonyGain : public SymphonyOperator {
private:
	const float *input_buffer = nullptr;
	float *output_buffer = nullptr;
	float gain = 0.5f;

public:
	void initialize(const float *p_input_buffer, float *p_output_buffer, float p_gain) {
		input_buffer = p_input_buffer;
		output_buffer = p_output_buffer;
		gain = p_gain;
	}

	virtual void execute(int32_t p_num_frames) override {
		for (int32_t i = 0; i < p_num_frames; i++) {
			output_buffer[i] = input_buffer[i] * gain;
		}
	}
};
