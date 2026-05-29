#pragma once

#include "../../core/symphony_operator.h"
#include "servers/audio/audio_stream.h"

// Copies mono input buffer to stereo AudioFrame output (L = R = mono).
class SymphonyGraphOutput : public SymphonyOperator {
private:
	const float *input_buffer = nullptr;
	AudioFrame *output_frames = nullptr;
	int32_t output_offset = 0;

public:
	void initialize(const float *p_input_buffer) {
		input_buffer = p_input_buffer;
	}

	// Set the destination for the current mix() call.
	void set_output(AudioFrame *p_output_frames, int32_t p_offset) {
		output_frames = p_output_frames;
		output_offset = p_offset;
	}

	virtual void execute(int32_t p_num_frames) override {
		for (int32_t i = 0; i < p_num_frames; i++) {
			float sample = input_buffer[i];
			output_frames[output_offset + i] = AudioFrame(sample, sample);
		}
	}
};
