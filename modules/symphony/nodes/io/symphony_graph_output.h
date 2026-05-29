#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "servers/audio/audio_stream.h"

// Final output node: copies mono input to stereo AudioFrame buffer (L = R = mono).
// The output AudioFrame* is set externally each mix() call via set_output().
class SymphonyGraphOutput : public SymphonyOperator {
private:
	const float *input = nullptr;
	AudioFrame *output_frames = nullptr;
	int32_t output_offset = 0;

public:
	SymphonyGraphOutput() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
	}

	// Called externally before each micro-block to set the destination.
	void set_output(AudioFrame *p_frames, int32_t p_offset) {
		output_frames = p_frames;
		output_offset = p_offset;
	}

	virtual void execute(int32_t p_num_frames) override {
		if (!output_frames || !input) {
			return;
		}
		for (int32_t i = 0; i < p_num_frames; i++) {
			float sample = input[i];
			output_frames[output_offset + i] = AudioFrame(sample, sample);
		}
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "GraphOutput";
		desc.category = "I/O";
		desc.inputs.push_back({ "input", SymphonyPinType::AUDIO, true });
		// No outputs — this is the terminal node
		desc.state_size = sizeof(SymphonyGraphOutput);
		desc.state_align = alignof(SymphonyGraphOutput);
		desc.create_fn = &SymphonyGraphOutput::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyGraphOutput), alignof(SymphonyGraphOutput));
		return new (mem) SymphonyGraphOutput();
	}
};
