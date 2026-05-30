#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "servers/audio/audio_stream.h"

// Final output node: copies mono input to stereo AudioFrame buffer (L = R = mono).
// pin_type param controls the input type (only AUDIO functional in Slice 1).
// sort_order + display_name control how this appears as a pin on a SubGraph node.
class SymphonyGraphOutput : public SymphonyOperator {
private:
	const float *__restrict__ input = nullptr;
	AudioFrame *output_frames = nullptr;
	int32_t output_offset = 0;

public:
	SymphonyGraphOutput() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		input = (const float *)p_input_ptrs[0];
	}

	void set_output(AudioFrame *p_frames, int32_t p_offset) {
		output_frames = p_frames;
		output_offset = p_offset;
	}

	virtual void execute(int32_t p_num_frames) override {
		if (!output_frames || !input) {
			return;
		}
		SYMPHONY_ASSUME_FRAMES(p_num_frames);
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
		desc.params.push_back({ "pin_type", 0.0f, 0.0f, 4.0f, 1.0f }); // 0=AUDIO,1=FLOAT,2=INT,3=BOOL,4=TRIGGER
		desc.params.push_back({ "sort_order", 0.0f, -1000.0f, 1000.0f, 1.0f });
		desc.params.push_back({ "display_name", 0.0f, 0.0f, 0.0f, 0.0f }); // String stored via node params
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
